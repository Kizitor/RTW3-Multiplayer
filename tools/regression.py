"""RTW3 Multiplayer regression suite: one launch of a host and a client copy of the game, the selected scenarios
in order, then both copies are closed and the save slots restored.

  python regression.py                       run every scenario
  python regression.py --only merge,diplomacy  run some (session is always added: the others build on it)
  python regression.py --list                list the scenarios

Requires Steam running, the mod installed in the game folder (env RTW3_DIR, see rtw3ctl.py) and a campaign in
save slot 1 that is past its setup steps (the host plays nation 0). Refuses to start while RTW3.exe runs.
Host bridge 47701, client bridge 47702 (client save slot 77); nation 2 is reserved for a stand-in player "Bot".
The client runs with RTW3MP_EVENT_CHANCE=0 (no random events unless a test asks for one); the client's event
cooldown files (events_*.ini in the RTW3MP data folder) are deleted before and after the run.
The game is driven only through the test bridge and posted window messages: the user's mouse and keyboard are
never used and the screen is never captured. Exit code: 0 = every check passed, 1 = a check failed or the saves
could not be restored, 2 = not started (precondition).
"""
import argparse, ctypes, ctypes.wintypes as wt, hashlib, os, re, shutil, subprocess, sys, tempfile, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import e2e_test  # noqa: E402
import rtw3ctl as ctl  # noqa: E402
from e2e_test import dismiss_dialogs  # noqa: E402
from ui_mp import IDS, mp_window, text, windows_of  # noqa: E402

H, C = 47701, 47702
CLIENT_SLOT = 77
HOST_SLOT = 1
MP_PORT = 47624
STANDIN = 2  # nation reserved for the stand-in player "Bot" (diplomacy between two joined nations)
AI_NATION = 3  # stays with the AI for the whole run
MONTH_TIMEOUT = 240
MAXINT = 2147483647
SAVE = ctl.SAVE_DIR
LOG_DIR = os.path.join(os.path.dirname(SAVE), 'RTW3MP')
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BACKUP = os.path.join(tempfile.gettempdir(), 'rtw3mp_regression_backup_Game1')

u = ctypes.windll.user32
u.GetDlgItem.restype = wt.HWND
u.GetDlgItem.argtypes = [wt.HWND, ctypes.c_int]
u.GetParent.restype = wt.HWND
u.GetParent.argtypes = [wt.HWND]
u.GetDlgCtrlID.argtypes = [wt.HWND]
u.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
IDC = dict(label=1080, list=1081, war=1082, ally=1083, peace=1084, leave=1085, cancel=1086, chatlog=IDS['chatlog'])

results = []  # (scenario, check, ok, detail)
_scenario = ['-']
PROCS = []  # the game copies this run started (Popen)


class Ctx:
    def __init__(self):
        self.hp = self.cp = None
        self.selected = []
        self.months = []  # (label, from date, to date, seconds)
        self.intel_hist = []  # snapshots: session start, then one per processed month
        self.intel_prev = None  # the latest snapshot (baseline of the per-month checks)
        self.fixture = None  # [IntelReports] of the slot 1 save on disk before the run
        self.month_hooks = []  # callables (ctx, label) run after each processed month; scenarios that share months
        self.pre_month_hooks = []  # callables (ctx, label, n) run before month n (1-based) of the run is submitted
        self.months_planned = 0  # months the selected scenarios process
        self.event_chance = 0  # the client's random-event chance in % (RTW3MP_EVENT_CHANCE=0 at launch)
        self.month_marks = (0, 0)  # host and client log sizes when the latest month was submitted
        self.roll = None  # the client's event roll of the latest month (wait_event_roll)


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    ok = bool(ok)
    results.append((_scenario[0], name, ok, '' if ok else str(detail)))
    log(('PASS ' if ok else 'FAIL ') + name, detail if detail not in ('', None) else '')
    return ok


# ---------------------------------------------------------------- bridge, logs, windows

def cmd(port, line, timeout=130):
    r = ctl.cmd(port, line, timeout)
    if not r.startswith('ok'):
        log('  !!', port, line, '->', r)
    return r


def value(reply):
    """'ok 13' -> '13'; None for an error reply."""
    return reply[2:].strip() if reply.startswith('ok') else None


def items(reply):
    """The entries of a list reply 'ok a; b; c; ' (the reply arrives stripped, so the last one ends in ';')."""
    return [p.strip().rstrip(';') for p in (value(reply) or '').split('; ') if p.strip().rstrip(';')]


def rel(a, b):
    r = cmd(H, f'rel {a} {b}')
    return dict(kv.split('=') for kv in r[3:].split())


def row(port, nation):
    r = cmd(port, 'diploview')
    for part in r.split('rows=', 1)[-1].split('; '):
        f = part.split('/')
        if len(f) >= 7 and f[0] == str(nation):
            return dict(nation=f[0], name=f[1], player=f[2], relation=f[3], tension=f[4], pending=f[5].strip('[]'),
                        flags=f[6].rstrip(';'))
    return None


def designs(port, nation):
    """[(index, class name, ReadyForBuild)] of a nation's designs (ReadyForBuild -1 = not a TDesignShip)."""
    r = cmd(port, f'designs {nation}')
    out = []
    for part in items(r):
        if ':' not in part:
            continue
        idx, rest = part.split(':', 1)
        name, ready = rest.rsplit(':', 1)
        out.append((int(idx), name, int(ready)))
    return out


def ready_of(lst, idx, name):
    for i, n, r in lst:
        if i == idx and n == name:
            return r
    return next((r for i, n, r in lst if n == name), None)  # the list was reordered


def pick_design(lst):
    """A buildable design (ReadyForBuild 0) whose class name is unique in the list; the newest one."""
    names = [n for _, n, _ in lst]
    ok = [(i, n) for i, n, r in lst if r == 0 and n and names.count(n) == 1]
    return ok[-1] if ok else None


def shipinfo(port, nation, ship):
    m = re.match(r'^ok (.*) loc=(.*) ordered=(.*) status=(-?\d+)$', cmd(port, f'shipinfo {nation} {ship}'))
    return dict(name=m.group(1), loc=m.group(2), ordered=m.group(3), status=m.group(4)) if m else None


def find_ship(port, nation, name, first=0, limit=400):
    for i in range(first, limit):
        s = shipinfo(port, nation, i)
        if s is None:
            return None
        if s['name'] == name:
            return s
    return None


def log_path(port):
    return os.path.join(LOG_DIR, f'rtw3mp_{port}.log')


def log_mark(port):
    try:
        return os.path.getsize(log_path(port))
    except OSError:
        return 0


def log_since(port, mark=0):
    try:
        with open(log_path(port), 'rb') as f:
            f.seek(mark)
            return f.read().decode('utf-8', errors='replace')
    except OSError:
        return ''


def log_lines(txt, pattern):
    return [ln.strip() for ln in txt.splitlines() if re.search(pattern, ln)]


def chat(pid):
    """The Multiplayer window's chat log of a copy (the window must have been opened once: `mpwindow`)."""
    w = mp_window(pid)
    return text(u.GetDlgItem(w, IDS['chatlog'])) if w else ''


def top_windows(pid):
    out = []

    def cb(h, l):
        p = wt.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u.IsWindowVisible(h):
            out.append(h)
        return True

    u.EnumWindows(e2e_test.EnumProc(cb), 0)
    return out


def describe_windows(pid):
    return '; '.join(f'{e2e_test.class_name(h)}:"{e2e_test.window_text(h)[:50]}"' for h in top_windows(pid))


def assert_alive():
    for p in PROCS:
        if p.poll() is not None:
            raise RuntimeError(f'game copy PID {p.pid} exited (code {p.returncode})')


def answer_dialogs(pid, who):
    """Answer the game's dialogs of one copy. The game reuses its dialog windows, so the per-window click guard of
    dismiss_dialogs is reset on every pass; the callers' timeouts bound their loops."""
    e2e_test._clicks.clear()
    for c in dismiss_dialogs(pid):
        log(f'  {who} dialog answered:', c)
    for top in top_windows(pid):  # other forms with a "Not now" button (e.g. a design ready for construction)
        k = e2e_test.class_name(top)
        if k == 'TMessageForm' or k.startswith('Tdlg') or k in ('TfrmBuildCamp', 'RTW3MP_Window'):
            continue
        found = []

        def ccb(h, l):
            if u.IsWindowVisible(h) and u.IsWindowEnabled(h) and e2e_test.class_name(h) in ('TButton', 'TBitBtn') \
                    and e2e_test.window_text(h).replace('&', '').strip().lower() == 'not now':
                found.append(h)
            return True

        u.EnumChildWindows(top, e2e_test.EnumProc(ccb), 0)
        if found:
            u.PostMessageW(found[0], 0x00F5, 0, 0)  # BM_CLICK
            log(f'  {who} dialog answered: {k}:"{e2e_test.window_text(top)[:40]}"->Not now')


def answer_host_box(pid):
    """Fallback: the host's own 'Still waiting for ...' box (Yes = advance now)."""
    for d in windows_of(pid, '#32770'):
        if text(d) == 'Multiplayer':
            u.PostMessageW(u.GetDlgItem(d, 6), 0x00F5, 0, 0)  # IDYES
            log('  answered the host\'s waiting box')


def drain(pid, seconds, who='host'):
    for _ in range(int(seconds)):
        answer_dialogs(pid, who)
        time.sleep(1)


def wait(port, pred, timeout, what, pids=()):
    end = time.time() + timeout
    st = {}
    while time.time() < end:
        assert_alive()
        for p in pids:
            answer_dialogs(p, 'host' if p == pids[0] else 'client')
        try:
            st = ctl.status(port)
            if pred(st):
                return st
        except OSError:
            pass
        time.sleep(1.5)
    raise RuntimeError(f'timeout waiting for {what}: {st}')


def mp_ctl(pid, key):
    w = windows_of(pid, 'RTW3MP_Window')
    return u.GetDlgItem(w[0], IDC[key]) if w else None


# ---------------------------------------------------------------- intel reports, saves, month package, memory

INTEL_GROWTH_MAX = 30  # new intel reports per month
PACKAGE_GROWTH_MAX = 0.25  # month package for the client, month over month


def intel(port):
    """(entries, distinct non-empty entries) of the in-memory intel report list, or (None, None)."""
    m = re.match(r'^ok count=(-?\d+) unique=(-?\d+)', cmd(port, 'intel'))
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)


def intel_clean(c):
    return c[0] is not None and c[0] == c[1] and c[1] > 0


def save_path(slot):
    return os.path.join(SAVE, f'Game{slot}', f'RTWGame{slot}.bcs')


def saved_intel(path):
    """[IntelReports] of a saved .bcs: ReportNo, IntelN keys, empty / repeated entries, distinct non-empty ones."""
    try:
        with open(path, 'rb') as f:
            txt = f.read().decode('utf-8-sig', errors='replace')
    except OSError:
        return None
    m = re.search(r'^\[IntelReports\][^\n]*\n(.*?)(?=^\[|\Z)', txt, re.S | re.M)
    if not m:
        return None
    report_no, vals = None, []
    for ln in m.group(1).splitlines():
        k, sep, v = ln.partition('=')
        if not sep:
            continue
        if k == 'ReportNo':
            report_no = int(v) if v.strip().lstrip('-').isdigit() else None
        elif re.fullmatch(r'Intel\d+', k):
            vals.append(v)
    nonempty = [v for v in vals if v]
    distinct = len(set(nonempty))
    return dict(ReportNo=report_no, keys=len(vals), empty=len(vals) - len(nonempty), repeated=len(nonempty) - distinct,
                distinct=distinct)


def saved_clean(s):
    """No empty or repeated entries: ReportNo == the number of distinct non-empty entries."""
    return bool(s) and s['ReportNo'] == s['distinct'] == s['keys'] and s['empty'] == 0 and s['repeated'] == 0


class _PMC(ctypes.Structure):
    _fields_ = [('cb', wt.DWORD), ('PageFaultCount', wt.DWORD)] + \
               [(n, ctypes.c_size_t) for n in ('PeakWorkingSetSize', 'WorkingSetSize', 'QuotaPeakPagedPoolUsage',
                                               'QuotaPagedPoolUsage', 'QuotaPeakNonPagedPoolUsage',
                                               'QuotaNonPagedPoolUsage', 'PagefileUsage', 'PeakPagefileUsage',
                                               'PrivateUsage')]


def private_mb(pid):
    """Private memory (GetProcessMemoryInfo PrivateUsage) of a process in MB, or None."""
    k = ctypes.windll.kernel32
    k.OpenProcess.restype = wt.HANDLE
    k.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
    k.K32GetProcessMemoryInfo.argtypes = [wt.HANDLE, ctypes.POINTER(_PMC), wt.DWORD]
    k.CloseHandle.argtypes = [wt.HANDLE]
    if not pid:
        return None
    h = k.OpenProcess(0x1000 | 0x0010, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ
    if not h:
        return None
    try:
        pmc = _PMC()
        pmc.cb = ctypes.sizeof(pmc)
        return round(pmc.PrivateUsage / 2 ** 20, 1) if k.K32GetProcessMemoryInfo(h, ctypes.byref(pmc), pmc.cb) else None
    finally:
        k.CloseHandle(h)


def intel_snapshot(ctx, mark, label):
    """Intel lists (host, client), saved [IntelReports] (Game1, Game77), the month package the host logged for the
    client since `mark` (`state seq N -> name (nation 1, B bytes)`) and both copies' private memory."""
    txt = log_since(H, mark)
    sent = re.findall(r'state seq (\d+) -> .+? \(nation 1, (\d+) bytes\)', txt)
    s = dict(label=label, host=intel(H), client=intel(C), host_file=saved_intel(save_path(HOST_SLOT)),
             client_file=saved_intel(save_path(CLIENT_SLOT)), pkg=int(sent[-1][1]) if sent else None,
             not_sent=log_lines(txt, r'NOT sent|too large'), cmem=private_mb(ctx.cp), hmem=private_mb(ctx.hp))
    log(f'  intel [{label}]: host {s["host"]}, client {s["client"]} (count, unique); saved host {s["host_file"]}, '
        f'client {s["client_file"]}; package {s["pkg"]} bytes; private memory client {s["cmem"]} MB, '
        f'host {s["hmem"]} MB')
    ctx.intel_hist.append(s)
    return s


def month_checks(ctx, label, mark, cmark=None):
    """After every processed month: no duplicated intel reports in memory or in the saves, few new reports, and a
    month package that does not grow (it doubled every month before 0.2.2); the client rolled the month's random
    event once (0.2.4; at chance 0 the roll finds no event)."""
    prev, s = ctx.intel_prev, intel_snapshot(ctx, mark, label)
    check(f'{label}: host intel list has no empty or repeated entries', intel_clean(s['host']), s['host'])
    check(f'{label}: client intel list has no empty or repeated entries', intel_clean(s['client']), s['client'])
    gh = s['host'][0] - prev['host'][0] if prev and None not in (s['host'][0], prev['host'][0]) else None
    gc = s['client'][0] - prev['client'][0] if prev and None not in (s['client'][0], prev['client'][0]) else None
    check(f'{label}: intel reports grow by at most {INTEL_GROWTH_MAX} a month',
          gh is not None and gc is not None and gh <= INTEL_GROWTH_MAX and gc <= INTEL_GROWTH_MAX,
          f'host {prev and prev["host"][0]} -> {s["host"][0]}, client {prev and prev["client"][0]} -> {s["client"][0]}')
    check(f'{label}: host save [IntelReports] has no empty or repeated entries', saved_clean(s['host_file']),
          s['host_file'])
    check(f'{label}: client save [IntelReports] has no empty or repeated entries', saved_clean(s['client_file']),
          s['client_file'])
    pp, pn = prev and prev['pkg'], s['pkg']
    grow = f'{(pn / pp - 1) * 100:+.1f} %' if pp and pn else '?'
    check(f'{label}: the month package for the client grows by at most {PACKAGE_GROWTH_MAX:.0%}',
          pp and pn and pn <= pp * (1 + PACKAGE_GROWTH_MAX) and not s['not_sent'],
          f'{pp} -> {pn} bytes ({grow}) {s["not_sent"]}')
    hlog = log_since(H, mark)
    bad = log_lines(hlog, r'NOT applied|rejected|write .* FAILED')
    check(f'{label}: no rejected turn or failed write in the host log', not bad, bad[:3])
    retried = log_lines(hlog, r'write .*: (succeeded after \d+ retries|replaced in place)')
    if retried:
        log(f'  {label}: transient file locks handled:', retried[:3])
    if cmark is not None:
        rolls = roll_lines(log_since(C, cmark))
        if ctx.event_chance == 0:
            check(f'{label}: the client rolled the new month\'s event once, no event at chance 0',
                  rolls == ['event: no event this month'], rolls)
        else:
            check(f'{label}: the client rolled the new month\'s event once (chance {ctx.event_chance} %)',
                  len(rolls) == 1 and 'already rolled' not in rolls[0], rolls)
    ctx.intel_prev = s


def click(pid, key):
    u.PostMessageW(mp_ctl(pid, key), 0x00F5, 0, 0)  # BM_CLICK
    time.sleep(1.0)


def confirm(pid, title, timeout=5):
    end = time.time() + timeout
    while time.time() < end:
        for d in windows_of(pid, '#32770'):
            if text(d) == title:
                u.PostMessageW(u.GetDlgItem(d, 6), 0x00F5, 0, 0)  # IDYES
                time.sleep(1.0)
                return True
        time.sleep(0.3)
    return False


def run_month(ctx, label):
    """Process one month: the client submits, the host waits for everyone, presses Turn and its dialogs (and the
    client's) are answered until the client plans the new month. Never polls the host's bridge meanwhile: its
    calls run on the game's main thread, which is busy with the month. Then the client's event roll of the new
    month is awaited (its dialog answered: an open event dialog refuses most bridge commands), and the per-month
    checks (intel reports, saved files, month package size, event roll) run."""
    n_month = len(ctx.months) + 1
    for hook in list(ctx.pre_month_hooks):
        hook(ctx, label, n_month)
    date0 = ctl.status(C).get('date')
    mark, cmark = log_mark(H), log_mark(C)
    ctx.month_marks = (mark, cmark)
    war = rel(0, 1).get('warCounter', '0')
    if war.lstrip('-').isdigit() and int(war) > 0:
        log(f'  WARNING: the host nation is at war (warCounter={war}); the month may start a tactical battle')
    t0 = time.time()
    cmd(C, 'submit')
    wait(C, lambda s: s.get('submitted') == '1', 30, 'client submitted', pids=(ctx.hp, ctx.cp))
    wait(H, lambda s: 'All players are ready' in s.get('status', ''), 30, 'host sees everyone ready',
         pids=(ctx.hp, ctx.cp))
    cmd(H, 'turn')
    end = time.time() + MONTH_TIMEOUT
    ev_seen = []  # the client's event dialogs (the new month's roll may show one before the month is detected)
    while time.time() < end:
        assert_alive()
        answer_host_box(ctx.hp)
        answer_dialogs(ctx.hp, 'host')
        ev_seen += [i for i in select_first_answer(ctx.cp) if i not in ev_seen]
        answer_dialogs(ctx.cp, 'client')
        try:
            st = ctl.status(C)
            if st.get('phase') == 'planning' and st.get('date') != date0 and st.get('slot') == str(CLIENT_SLOT):
                time.sleep(2)
                dt = time.time() - t0
                ctx.months.append((label, date0, st.get('date'), dt))
                log(f'month processed ({label}): {date0} -> {st.get("date")} in {dt:.0f} s')
                wait_event_roll(ctx, cmark, ev_seen)
                month_checks(ctx, label, mark, cmark)
                for hook in list(ctx.month_hooks):
                    hook(ctx, label)
                return st.get('date')
        except OSError:
            pass
        time.sleep(1.5)
    raise RuntimeError(f'the month did not advance within {MONTH_TIMEOUT} s ({label}); host windows: '
                       f'{describe_windows(ctx.hp)}')


def wait_month_chat(pid, before, settle=3, timeout=20):
    """The client's chat after the host's '* New month' line arrived (the host sends each player's private lines
    right after it), plus a moment for those lines."""
    n0 = before.count('* New month: ')
    end = time.time() + timeout
    txt = chat(pid)
    while time.time() < end and txt.count('* New month: ') <= n0:
        time.sleep(0.5)
        txt = chat(pid)
    time.sleep(settle)
    return chat(pid)


# ---------------------------------------------------------------- scenarios

def scenario_session(ctx):
    st = ctl.status(H)
    mark, loaded = log_mark(H), False
    if st.get('slot') != str(HOST_SLOT) or st.get('playerIdx') != '0' or st.get('startBuild') != '2':
        loaded = cmd(H, f'load {HOST_SLOT} 0') == 'ok'
        st = wait(H, lambda s: s.get('playerIdx') == '0' and s.get('startBuild') == '2', 90, 'host campaign',
                  pids=(ctx.hp,))
    check('host has the slot 1 campaign open as nation 0',
          st.get('slot') == str(HOST_SLOT) and st.get('playerIdx') == '0' and st.get('startBuild') == '2', st)
    if 'intel_reports' in ctx.selected:  # before hosting: the session start saves slot 1
        intel_load_checks(ctx, mark, loaded)
    r = cmd(H, f'host Admiral {MP_PORT} 0')
    check('host opens the session', r == 'ok' and ctl.status(H).get('role') == 'host', r)
    rj = cmd(C, f'join Captain 127.0.0.1 {MP_PORT}')
    time.sleep(3)
    rc = cmd(C, 'claim 1')
    time.sleep(1.5)
    check('client joins and claims nation 1', rj == 'ok' and rc == 'ok' and ctl.status(C).get('role') == 'client',
          f'join {rj}, claim {rc}')
    mark = log_mark(H)
    cmd(H, 'start')
    st = wait(C, lambda s: s.get('phase') == 'planning' and s.get('playerIdx') == '1' and
              s.get('slot') == str(CLIENT_SLOT), 120, 'client planning as nation 1', pids=(ctx.hp, ctx.cp))
    check('client plans nation 1 in slot 77', True, st.get('date'))
    time.sleep(1)
    ctx.intel_prev = intel_snapshot(ctx, mark, 'session start')  # baseline of the per-month checks
    r = cmd(H, f'reserve {STANDIN} Bot')
    time.sleep(1.5)
    r2 = row(H, STANDIN)
    check('nation 2 reserved for the stand-in player "Bot"', r == 'ok' and r2 is not None, r2)
    cmd(C, 'mpwindow')
    time.sleep(1.5)
    check('client Multiplayer window open (chat log readable)', mp_window(ctx.cp) is not None)
    log('month:', st.get('date'), '| nations:', cmd(H, 'nations')[3:160])


def intel_load_checks(ctx, mark, loaded):
    """`intel_reports`, part 1 (called by `session` before hosting, so slot 1 is not saved yet): the bloated slot 1
    fixture is repaired on load, and loading it twice more into the same form does not grow the list."""
    outer, _scenario[0] = _scenario[0], 'intel_reports'
    try:
        fx = ctx.fixture
        log(f'slot 1 fixture on disk: {fx}' + ('' if fx and not saved_clean(fx) else ' (NOT bloated: the repair is '
                                                                                  'not exercised)'))
        c = intel(H)
        check('repair on load: the host\'s intel list has no empty or repeated entries', intel_clean(c),
              f'(count, unique) {c}')
        rep = log_lines(log_since(H, mark), r'intel reports: \d+ entries -> \d+')
        check('repair on load: host log has the "intel reports: N entries -> M" line', loaded and rep,
              rep or ('no line' if loaded else 'the campaign was already open, no load'))
        log('  ', rep[:1])
        for n in (2, 3):
            m2 = log_mark(H)
            r = cmd(H, f'load {HOST_SLOT} 0')
            wait(H, lambda s: s.get('playerIdx') == '0' and s.get('startBuild') == '2', 90, 'host campaign',
                 pids=(ctx.hp,))
            c2 = intel(H)
            check(f'load {n} of slot 1: the intel list keeps its size', r == 'ok' and c2 == c and intel_clean(c2),
                  f'{c} -> {c2}; {log_lines(log_since(H, m2), r"intel reports:")[:1]}')
    finally:
        _scenario[0] = outer


def scenario_intel_reports(ctx):
    """Part 2 (part 1 runs inside `session`): the state the session started with. The host saved slot 1 at the
    start and sent it to the client; both saves and both lists must be free of empty and repeated entries. Every
    processed month then repeats these checks (`month_checks`)."""
    s = ctx.intel_hist[0] if ctx.intel_hist else None
    if not check('session start: intel snapshot taken', s, s['label'] if s else 'no snapshot (the session did not start?)'):
        return
    check('session start: the client\'s intel list has no empty or repeated entries', intel_clean(s['client']),
          s['client'])
    check('session start: host save [IntelReports] has no empty or repeated entries (saved at the start)',
          saved_clean(s['host_file']), s['host_file'])
    check('session start: client save [IntelReports] has no empty or repeated entries', saved_clean(s['client_file']),
          s['client_file'])
    check('session start: month package for the client sent and logged', s['pkg'] and not s['not_sent'],
          f'{s["pkg"]} bytes {s["not_sent"]}')
    quiet = log_lines(log_since(C), r'intel reports:')  # the game's own trailing empty entry is not worth a line
    check('session start: the client\'s load of the clean save logs no repair line', not quiet, quiet[:2])
    log(f'  session start: package {s["pkg"]} bytes; private memory client {s["cmem"]} MB, host {s["hmem"]} MB')


LOCK_FILE = 'rtw3mp_locktest.tmp'  # throwaway file in the client slot folder; never a real save file


def lock_file(path):
    """Open `path` with share mode 0 (as a scanner holding a fresh save would): a handle, or None."""
    k = ctypes.windll.kernel32
    k.CreateFileW.restype = wt.HANDLE
    k.CreateFileW.argtypes = [wt.LPCWSTR, wt.DWORD, wt.DWORD, wt.LPVOID, wt.DWORD, wt.DWORD, wt.HANDLE]
    k.CloseHandle.argtypes = [wt.HANDLE]
    h = k.CreateFileW(path, 0x80000000, 0, None, 3, 0, None)  # GENERIC_READ, no sharing, OPEN_EXISTING
    return None if h in (None, ctypes.c_void_p(-1).value) else h


def writetest_while_locked(path, hold_s, nbytes):
    """`writetest` on the client while Python holds `path` locked: released after `hold_s` seconds, or only after the
    call returned (hold_s None). Returns (reply, seconds the lock was held)."""
    import threading
    h = lock_file(path)
    if not h:
        return f'cannot lock {os.path.basename(path)}', 0
    out, t0 = {}, time.time()
    th = threading.Thread(target=lambda: out.update(r=ctl.cmd(C, f'writetest {CLIENT_SLOT} {LOCK_FILE} {nbytes}', 60)),
                          daemon=True)
    try:
        th.start()
        th.join(hold_s if hold_s is not None else 30)
    finally:
        ctypes.windll.kernel32.CloseHandle(h)
        held = time.time() - t0
    th.join(30)
    return out.get('r', 'no reply'), held


def parse_writetest(r):
    m = re.match(r'^ok ms=(\d+)$|^error write failed \((\d+)\) ms=(\d+)$', r or '')
    if not m:
        return None, None
    return (None, int(m[1])) if m[1] else (int(m[2]), int(m[3]))


def scenario_file_lock(ctx):
    """WriteFileBytes (every save write of the mod) survives a short lock by another process and reports a long one,
    with the client's `writetest` into its own slot folder and a throwaway file locked from Python."""
    folder = os.path.join(SAVE, f'Game{CLIENT_SLOT}')
    path = os.path.join(folder, LOCK_FILE)
    n = 4096
    try:
        mark = log_mark(C)
        r = cmd(C, f'writetest {CLIENT_SLOT} {LOCK_FILE} 100')
        err, ms = parse_writetest(r)
        quiet = not log_lines(log_since(C, mark), re.escape(LOCK_FILE))
        check('writetest without a lock: ok at once, no retry line', err is None and ms is not None and ms < 1000
              and quiet and os.path.exists(path) and os.path.getsize(path) == 100, f'{r}; log lines: {not quiet}')
        mark = log_mark(C)
        r, held = writetest_while_locked(path, 1.2, n)
        err, ms = parse_writetest(r)
        size = os.path.getsize(path) if os.path.exists(path) else None
        check('lock released after ~1.2 s: the write succeeds after retries (~1-3 s)',
              err is None and ms is not None and 800 <= ms < 3000 and size == n,
              f'{r} (lock held {held:.1f} s, file {size} bytes)')
        ln = log_lines(log_since(C, mark), re.escape(LOCK_FILE) + r': succeeded after \d+ retries')
        check('client log: "write ...: succeeded after N retries"', ln, ln or 'no line')
        log('  ', [x.split('] ', 1)[-1].replace(SAVE, '<Save>') for x in ln[:1]])
        mark = log_mark(C)
        r, held = writetest_while_locked(path, None, 2 * n)
        err, ms = parse_writetest(r)
        check('lock held for the whole call: the write fails with error 32 or 5 after >= 3 s',
              err in (32, 5) and ms is not None and ms >= 2900, f'{r} (lock held {held:.1f} s)')
        ln = log_lines(log_since(C, mark), re.escape(LOCK_FILE) + r' FAILED: error \d+ \(in place: \d+\)')
        check('client log: "write ... FAILED: error E (in place: E2)"', ln, ln or 'no line')
        log('  ', [x.split('] ', 1)[-1].replace(SAVE, '<Save>') for x in ln[:1]])
        left = [f for f in os.listdir(folder) if f.endswith('.mptmp')]
        size = os.path.getsize(path) if os.path.exists(path) else None
        check('after the failed write the file is unchanged and no .mptmp file is left', size == n and not left,
              f'file {size} bytes (want {n}), .mptmp files {left}')
    finally:
        for f in (path, path + '.mptmp'):
            for _ in range(10):
                try:
                    if os.path.exists(f):
                        os.remove(f)
                    break
                except OSError:
                    time.sleep(0.3)
    check('test file deleted', not os.path.exists(path) and not os.path.exists(path + '.mptmp'), path)


# ---------------------------------------------------------------- random events of joined players (0.2.4)

EVENT_ROLL = (r'\] (event \d+ for nation \d+: |event: (no event this month|this month was already rolled|'
              r'cannot read the date|events unavailable|no such event|event needs the host))')
EVENT_ROLL_TIMEOUT = 15  # s after the new month: the client's roll and the answer of its dialog
EVENT_BUSY = 'error an event dialog is open'
REARM = 'XX appears to be considering a naval rearm'  # first answer: budget +2, prestige +1, tension +2
RISKY_EVENT_WORDS = ('ship', 'accident', 'explosion', 'collision', 'aground', 'design', 'fleet', 'sabotage', 'fire',
                     'mutiny', 'sunk', 'damage', 'yard', 'cruiser', 'battleship')  # 2nd forced event: leave ships alone
EVENT_PAIRS = [(1, j) for j in [0] + list(range(2, 9))] + [(0, i) for i in range(2, 9)]


def roll_lines(txt):
    """The client's event roll results in a log slice ('event N for nation K: ...', 'event: no event this month', ...)."""
    return [ln.split('] ', 1)[-1] for ln in log_lines(txt, EVENT_ROLL)]


def event_files():
    try:
        return sorted(f for f in os.listdir(LOG_DIR) if f.startswith('events_') and f.endswith('.ini'))
    except OSError:
        return []


def remove_event_files(when):
    """Delete the client's event cooldown files (events_<hash>.ini): the fixture campaign is always at the same month,
    so a leftover file would mark that month as already rolled. Returns the files still there."""
    gone = []
    for f in event_files():
        for _ in range(10):
            try:
                os.remove(os.path.join(LOG_DIR, f))
                gone.append(f)
                break
            except FileNotFoundError:
                break
            except OSError:
                time.sleep(0.5)
    if gone:
        log(f'removed event cooldown files ({when}):', gone)
    return event_files()


class _RECT(ctypes.Structure):
    _fields_ = [('left', ctypes.c_long), ('top', ctypes.c_long), ('right', ctypes.c_long), ('bottom', ctypes.c_long)]


def screen_top(h):
    r = _RECT()
    u.GetWindowRect(h, ctypes.byref(r))
    return r.top


def event_dialogs(pid):
    return [h for h in windows_of(pid, 'TdlgEventAnswer') if u.IsWindowVisible(h)]


def select_first_answer(pid):
    """Event dialogs (TdlgEventAnswer: RadioButton1-3 in Panel1, a hidden nation combo, OK) of one copy: select the
    first answer (the top-most radio) if none is selected yet, and the first nation if the combo is shown and empty.
    OK is left to dismiss_dialogs (answer_dialogs), which keeps a selected answer. Returns [(title, answers, chosen)]."""
    out = []
    for dlg in event_dialogs(pid):
        kids = [h for h in children(dlg) if u.IsWindowVisible(h)]
        radios = sorted((h for h in kids if e2e_test.class_name(h) == 'TRadioButton' and u.IsWindowEnabled(h)),
                        key=screen_top)
        if radios and not any(u.SendMessageW(h, 0x00F0, 0, 0) for h in radios):  # BM_GETCHECK
            u.SendMessageW(radios[0], 0x00F5, 0, 0)  # BM_CLICK, as dismiss_dialogs selects an answer
            if not u.SendMessageW(radios[0], 0x00F0, 0, 0):  # else what a real click sends the parent (BN_CLICKED)
                u.PostMessageW(u.GetParent(radios[0]), 0x0111, u.GetDlgCtrlID(radios[0]) & 0xFFFF, radios[0])
                time.sleep(0.3)
        for cb in (h for h in kids if e2e_test.class_name(h) == 'TComboBox'):
            if u.SendMessageW(cb, 0x0147, 0, 0) == -1 and u.SendMessageW(cb, 0x0146, 0, 0) > 0:  # CB_GETCURSEL/COUNT
                u.SendMessageW(cb, 0x014E, 0, 0)  # CB_SETCURSEL 0, then CBN_SELCHANGE as a real pick
                u.PostMessageW(u.GetParent(cb), 0x0111, (1 << 16) | (u.GetDlgCtrlID(cb) & 0xFFFF), cb)
                time.sleep(0.3)
        chosen = next((caption(h) for h in radios if u.SendMessageW(h, 0x00F0, 0, 0)), '')
        out.append((e2e_test.window_text(dlg), [caption(h) for h in radios], chosen))
    return out


def answer_events(ctx, timeout, until=None):
    """Answer the client's event dialogs (first answer; dismiss_dialogs presses OK) until none is open and `until()`
    holds. Returns (dialogs seen [(title, answers, chosen)], seconds, or None after `timeout`)."""
    seen, t0 = [], time.time()
    while True:
        assert_alive()
        for info in select_first_answer(ctx.cp):
            if not any(s[0] == info[0] and s[1] == info[1] for s in seen):
                seen.append(info)
        answer_dialogs(ctx.cp, 'client')
        time.sleep(0.7)
        if not event_dialogs(ctx.cp) and (until is None or until()):
            return seen, time.time() - t0
        if time.time() - t0 > timeout:
            return seen, None


def wait_event_roll(ctx, cmark, earlier=(), timeout=EVENT_ROLL_TIMEOUT):
    """The client rolls the new month's event as soon as it is idle: answer its dialog (first answer) until the roll's
    log line is there, so no later bridge command meets an open event dialog (it would be refused). `earlier`: event
    dialogs already answered while the month was processed."""
    seen, dt = answer_events(ctx, timeout, until=lambda: roll_lines(log_since(C, cmark)))
    seen = list(earlier) + [i for i in seen if i not in earlier]
    ctx.roll = dict(rolls=roll_lines(log_since(C, cmark)), seen=seen, seconds=dt)
    log(f'  client event roll: {ctx.roll["rolls"]}' + (f'; dialogs {seen}' if seen else '') +
        (f' ({dt:.1f} s)' if dt is not None else
         f' (NO roll line within {timeout} s; windows: {describe_windows(ctx.cp)})'))
    return ctx.roll


def wait_log(port, mark, pattern, timeout):
    end = time.time() + timeout
    while True:
        ln = log_lines(log_since(port, mark), pattern)
        if ln or time.time() > end:
            return ln
        time.sleep(0.5)


def nint(port, nation, field):
    v = value(cmd(port, f'nfield {nation} {field}'))
    return int(v) if v and v.lstrip('-').isdigit() else None


def host_event_view():
    """Host values a joined player's event may change: nation 1's Prestige and BudgetModifier, (tension, war, alliance
    months) of nation 1 with everyone and of the host nation with 2-8, and the host's `eventlast`."""
    v = dict(P=nint(H, 1, 'Prestige'), B=nint(H, 1, 'BudgetModifier'), last=value(cmd(H, 'eventlast')), rel={})
    for a, b in EVENT_PAIRS:
        r = rel(a, b)
        v['rel'][(a, b)] = tuple(r.get(k) for k in ('tension', 'war', 'ally'))
    return v


def client_event_view():
    st = ctl.status(C)
    return dict(P=nint(C, 1, 'Prestige'), B=nint(C, 1, 'BudgetModifier'), playerIdx=st.get('playerIdx'),
                names=[p.split(':')[1] for p in items(cmd(C, 'nations')) if p.count(':') >= 2])


def fmt_rel(rels):
    return ' '.join(f'{a}-{b}:{t[0]}' + ('W' if t[1] == '1' else '') + (f'A{t[2]}' if t[2] not in ('0', None) else '')
                    for (a, b), t in rels.items())


def parse_client_event(s):
    """Client `eventlast`: 'event=N tension=j:d;... alliance=j:m;... delta=Field:d;... set=Field:v;...'."""
    m = re.match(r'^event=(-?\d+) tension=(\S*) alliance=(\S*) delta=(\S*) set=(\S*)$', s or '')
    if not m:
        return None
    pairs = lambda t: [(a, int(b)) for a, b in (e.split(':', 1) for e in t.split(';') if ':' in e)]
    return dict(event=int(m[1]), tension=[(int(a), d) for a, d in pairs(m[2])],
                alliance=[(int(a), d) for a, d in pairs(m[3])], delta=dict(pairs(m[4])), set=dict(pairs(m[5])))


def parse_host_event(s):
    """Host `eventlast`: 'nation=K event=N tension K-j a->b ... Prestige+1 ... alliance K-j=m ...'."""
    m = re.match(r'^nation=(\d+) event=(-?\d+)(.*)$', s or '')
    if not m:
        return None
    return dict(nation=int(m[1]), event=int(m[2]), rest=m[3].strip(),
                tension=[tuple(map(int, t)) for t in re.findall(r'tension (\d+)-(\d+) (-?\d+)->(-?\d+)', m[3])],
                delta={f: int(v) for f, v in re.findall(r' (Prestige|BudgetModifier|BaseResources|Funds|UnrestLevel|'
                                                        r'TechLeakRisk|Corruption)([+-]\d+)', m[3])})


def force_event(ctx, idx, probe=False, timeout=25):
    """`eventnow idx` on the client: the dialog's title, the bridge's replies while it is open (probe), the answer
    (the first; OK by dismiss_dialogs), the roll's log line and the client's `eventlast` afterwards."""
    cmark = log_mark(C)
    out = dict(idx=idx, reply=cmd(C, f'eventnow {idx}'), title=None, during={})
    end = time.time() + 10
    while time.time() < end and not event_dialogs(ctx.cp) and not roll_lines(log_since(C, cmark)):
        time.sleep(0.2)
    dlg = event_dialogs(ctx.cp)
    if dlg:
        out['title'] = e2e_test.window_text(dlg[0])
        if probe:
            st = ctl.status(C)
            out['during'] = dict(status='role' in st, playerIdx=st.get('playerIdx'), nations=ctl.cmd(C, 'nations'),
                                 eventlast=ctl.cmd(C, 'eventlast'))
    out['seen'], out['seconds'] = answer_events(ctx, timeout, until=lambda: roll_lines(log_since(C, cmark)))
    out['rolls'] = roll_lines(log_since(C, cmark))
    out['last'] = value(cmd(C, 'eventlast'))
    log(f'  eventnow {idx}: {out}')
    return out


def planned_months(names):
    """Months the selected scenarios process (doctrine and events process their own if no later scenario does)."""
    n = sum(s[1] for s in SCENARIOS if s[0] in names)
    if 'doctrine' in names and 'design_study' not in names:
        n += 2
    if 'events' in names and n == 0:
        n += 2
    return n


def scenario_events(ctx):
    """0.2.4: random events of a joined player's nation are rolled and answered on the player's PC (the game rolls them
    only for nation 0). A forced "naval rearm" event in the planning month (real dialog, first answer): its deltas
    reach the host for nation 1 and nation 1's relations only, the client's nation list is restored; a second forced
    event in the same month is ignored by the host; a host-only event is refused. After the next month: the effect
    is kept once through the merge (reported). The last processed month with nation 1 at peace (the month before
    diplomacy's war month) rolls a random event at chance 100; the war month's roll at chance 100 finds none (at war
    only war events qualify, and those need the host)."""
    cp = ctx.cp
    start = roll_lines(log_since(C))
    check('session start: the client rolled the month once, no event at chance 0 (RTW3MP_EVENT_CHANCE=0)',
          start == ['event: no event this month'], start)
    files, kv = event_files(), {}
    if len(files) == 1:
        with open(os.path.join(LOG_DIR, files[0]), encoding='utf-8', errors='replace') as f:
            kv = dict(ln.split('=', 1) for ln in f.read().splitlines() if '=' in ln)
    rolled = int(kv['rolled']) if kv.get('rolled', '').isdigit() else 0
    check('client cooldown file events_<hash>.ini: this campaign\'s nation 1, the month marked as rolled',
          len(files) == 1 and kv.get('campaign', '').endswith('|nation1') and rolled > 0 and kv.get('month') == str(rolled),
          f'{files} rolled={kv.get("rolled")} month={kv.get("month")} campaign=...{kv.get("campaign", "")[-12:]}')

    # a) the event table as the client sees it
    evs = []
    for p in items(cmd(C, 'eventlist')):
        f = p.split(':', 3)
        if len(f) == 4 and f[0].isdigit():
            evs.append(dict(idx=int(f[0]), cond=f[1], host=f[2] == 'H', cap=f[3]))
    hostonly = [e for e in evs if e['host']]
    log(f'  eventlist: {len(evs)} events, {len(hostonly)} need the host: '
        + '; '.join(f'{e["idx"]}:{e["cond"]}:{e["cap"][:28]}' for e in hostonly))
    check('eventlist (client): at least 50 events', len(evs) >= 50, f'{len(evs)} events')
    check('eventlist: some events are marked H (need the host: war/peace/treaties), the others are not',
          0 < len(hostonly) < len(evs), f'{len(hostonly)} of {len(evs)}')
    rearm = next((e for e in evs if e['cap'][:40] == REARM[:40]), None)
    if not check(f'eventlist: "{REARM}" is listed and can run for a joined player', rearm and not rearm['host'], rearm):
        return
    idx = rearm['idx']

    # b) before
    h0, c0 = host_event_view(), client_event_view()
    log(f'  before event #{idx}: host nation 1 Prestige {h0["P"]} BudgetModifier {h0["B"]} eventlast "{h0["last"]}"; '
        f'client Prestige {c0["P"]} BudgetModifier {c0["B"]} playerIdx {c0["playerIdx"]} nations {c0["names"]}')
    log(f'  host relations before: {fmt_rel(h0["rel"])}')
    check('before: host and client values recorded (nation 1 Prestige/BudgetModifier, 16 relations, client nations, '
          'playerIdx 1)', None not in (h0['P'], h0['B'], c0['P'], c0['B']) and len(c0['names']) >= 9
          and c0['playerIdx'] == '1' and all(None not in t for t in h0['rel'].values()),
          f'host {h0["P"]}/{h0["B"]}, client {c0["P"]}/{c0["B"]} idx {c0["playerIdx"]} {len(c0["names"])} nations')

    # c) the forced event: the game's dialog on the client, first answer
    hmark, cmark = log_mark(H), log_mark(C)
    ev = force_event(ctx, idx, probe=True)
    check(f'client: eventnow {idx} shows the game\'s event dialog (TdlgEventAnswer "Event - <month>")',
          ev['reply'] == 'ok' and (ev['title'] or '').startswith('Event - '), f'reply {ev["reply"]}, title {ev["title"]}')
    d = ev['during']
    check('while the dialog is open the client bridge answers status but refuses `nations` ("an event dialog is open")',
          d.get('status') and d.get('nations') == EVENT_BUSY, d)
    first = ev['seen'][0] if ev['seen'] else None
    check('the dialog is answered with its first answer and closes',
          ev['seconds'] is not None and first and first[1] and first[2] == first[1][0],
          f'{ev["seen"]} ({ev["seconds"]} s)')
    cl = parse_client_event(ev['last'])
    check(f'client eventlast: event={idx}', cl and cl['event'] == idx, ev['last'])

    # d) what the client sent and what the host applied
    ten = [(j, dd) for j, dd in (cl['tension'] if cl else []) if dd]
    j, dt = ten[0] if len(ten) == 1 else (None, None)
    check('client eventlast: exactly one relation change, with another nation (normally +2)',
          len(ten) == 1 and j != 1, ev['last'])
    delta = cl['delta'] if cl else {}
    db = delta.get('BudgetModifier', 0)
    check('client eventlast: delta Prestige:1', delta.get('Prestige') == 1, ev['last'])
    hl = wait_log(H, hmark, rf'event of Captain \(nation 1, #{idx}\b', 10)
    h1, c1 = host_event_view(), client_event_view()
    he = parse_host_event(h1['last'])
    a = int(h0['rel'][(1, j)][0]) if (1, j) in h0['rel'] else None
    b = None if a is None else a if a > 40 else max(0, min(40, a + max(-20, min(20, dt))))
    want = [(1, j, a, b)] if a is not None and a <= 40 else []
    check(f'host eventlast: nation=1 event={idx} tension 1-{j} {a}->{b} (b = clamp(a{dt:+d}, 0, 40))' if dt else
          f'host eventlast: nation=1 event={idx} with the relation change',
          he and he['nation'] == 1 and he['event'] == idx and a is not None and he['tension'] == want, h1['last'])
    t1 = h1['rel'].get((1, j))
    check(f'host rel 1 {j}: tension {b}, war and alliance unchanged',
          t1 and b is not None and int(t1[0]) == b and t1[1:] == h0['rel'][(1, j)][1:], f'{h0["rel"].get((1, j))} -> {t1}')
    changed = {f'{k[0]}-{k[1]}': (h0['rel'][k], h1['rel'][k]) for k in h0['rel'] if k != (1, j) and
               h0['rel'][k] != h1['rel'][k]}
    check('host: every other recorded pair unchanged (nation 1 with the others, the host nation with 2-8)',
          not changed, changed)
    check(f'host nation 1: Prestige +1 and BudgetModifier {db:+d} (as reported)',
          None not in (h0['P'], h1['P'], h0['B'], h1['B']) and h1['P'] == h0['P'] + 1 and h1['B'] == h0['B'] + db
          and he and he['delta'].get('Prestige') == 1,
          f'Prestige {h0["P"]} -> {h1["P"]}, BudgetModifier {h0["B"]} -> {h1["B"]}; host eventlast {h1["last"]}')
    check('client nation 1 Prestige equals the host\'s', c1['P'] is not None and c1['P'] == h1['P'],
          f'client {c1["P"]} (BudgetModifier {c1["B"]}), host {h1["P"]} ({h1["B"]})')
    check('client nation list order and player index restored', c1['names'] == c0['names'] and c1['playerIdx'] == '1',
          f'{c0["names"]} -> {c1["names"]}, playerIdx {c1["playerIdx"]}')
    cline = log_lines(log_since(C, cmark), rf'\] event {idx} for nation 1: ')
    check(f'client log: "event {idx} for nation 1: ..."', len(cline) == 1, cline)
    check(f'host log: "event of Captain (nation 1, #{idx} ..."', len(hl) == 1, hl)
    log('  ', [x.split('] ', 1)[-1] for x in cline + hl])
    log(f'  host after the event: Prestige {h1["P"]} BudgetModifier {h1["B"]}; relations {fmt_rel(h1["rel"])}')

    # e) a second forced event in the same month: shown on the client, ignored by the host
    risky = lambda e: any(w in e['cap'].lower() for w in RISKY_EVENT_WORDS)
    cands = sorted((e for e in evs if not e['host'] and e['idx'] != idx),
                   key=lambda e: (risky(e), e['cond'] != '0', e['idx']))
    hmark = log_mark(H)
    ev2, c2 = None, None
    for e2 in cands[:4]:
        ev2 = force_event(ctx, e2['idx'])
        c2 = parse_client_event(ev2['last'])
        if c2 and c2['event'] == e2['idx']:
            log(f'  second event #{e2["idx"]}: "{e2["cap"]}"')
            break
        log(f'  event #{e2["idx"]} was not shown when forced ({ev2["last"]}); trying another')
    check('second forced event (another index, same month): shown on the client and sent',
          c2 and c2['event'] != idx and ev2['title'] and ev2['seconds'] is not None,
          ev2 and f'{ev2["title"]} {ev2["last"]}')
    ln = wait_log(H, hmark, r'second event of Captain in .+ ignored \(one per month\)', 10)
    check('host log: "second event of Captain in <date> ignored (one per month)"', len(ln) == 1,
          ln or log_lines(log_since(H, hmark), r'event (of|from) '))
    h2, c2v = host_event_view(), client_event_view()
    diff = {k: (h1[k], h2[k]) for k in ('P', 'B', 'last') if h1[k] != h2[k]}
    diff.update({f'{k[0]}-{k[1]}': (h1['rel'][k], h2['rel'][k]) for k in h1['rel'] if h1['rel'][k] != h2['rel'][k]})
    check('host: the second event changed nothing (nation 1 Prestige/BudgetModifier, relations, eventlast)', not diff,
          diff)
    log(f'  after the second event: client nation 1 Prestige {c2v["P"]} BudgetModifier {c2v["B"]} (its own fields '
        f'reach the host with the submitted turn); host {h2["P"]}/{h2["B"]}; {[x.split("] ", 1)[-1] for x in ln]}')

    # f) a host-only event is refused on the client
    hev = hostonly[0]
    hmark, cmark = log_mark(H), log_mark(C)
    r = cmd(C, f'eventnow {hev["idx"]}')
    seen_dlg, last, end = False, None, time.time() + 10
    while time.time() < end:
        seen_dlg = seen_dlg or bool(event_dialogs(cp))
        last = value(ctl.cmd(C, 'eventlast'))
        if last == 'error event needs the host':
            break
        time.sleep(0.5)
    time.sleep(1)
    seen_dlg = seen_dlg or bool(event_dialogs(cp))
    if seen_dlg:
        answer_events(ctx, 15)
    sent = log_lines(log_since(H, hmark), r'event (of|from) ')
    check(f'host-only event #{hev["idx"]} ("{hev["cap"][:30]}"): client eventlast "error event needs the host"',
          r == 'ok' and last == 'error event needs the host', f'{r}; eventlast {last}')
    check('the refused event shows no dialog and sends nothing to the host',
          not seen_dlg and not sent and log_lines(log_since(C, cmark), r'\] event: event needs the host'),
          f'dialog {seen_dlg}, host lines {sent}')

    # g) after the next month: the effect is kept once through the merge (reported)
    base = dict(before=h0['P'], event=h1['P'], submit=c2v['P'], B_event=h1['B'], B_submit=c2v['B'])
    gst = dict(n=0, need=1)

    def g_hook(ctx, label):
        outer, _scenario[0] = _scenario[0], 'events'
        try:
            ctx.month_hooks.remove(g_hook)
            gst['n'] = 1
            hp_, cp_ = nint(H, 1, 'Prestige'), nint(C, 1, 'Prestige')
            hb_, cb_ = nint(H, 1, 'BudgetModifier'), nint(C, 1, 'BudgetModifier')
            mc = f'{hp_ - base["submit"]:+d}' if None not in (hp_, base['submit']) else '?'
            log(f'  events, after the next month ({label}): nation 1 Prestige host {hp_}, client {cp_} | before the '
                f'event {base["before"]}, right after it {base["event"]}, client at submit {base["submit"]} -> month '
                f'change {mc} (game-driven); BudgetModifier host {hb_}, client {cb_} (after the event '
                f'{base["B_event"]}, client at submit {base["B_submit"]})')
            check(f'after the next month ({label}): host and client agree on nation 1\'s Prestige and BudgetModifier',
                  hp_ is not None and hp_ == cp_ and hb_ == cb_, f'Prestige host {hp_} client {cp_}, BudgetModifier '
                                                                  f'host {hb_} client {cb_}')
        finally:
            _scenario[0] = outer

    g_hook.scenario, g_hook.state = 'events', gst
    ctx.month_hooks.append(g_hook)

    # h) the random path: chance 100 for the last processed month in which nation 1 stays at peace (diplomacy's second
    # month starts wars with the host and nation 2, and at war no event qualifies for a joined player, see i); the
    # client rolls by itself
    hst = dict(n=0, need=1)
    target = ctx.months_planned - (1 if 'diplomacy' in ctx.selected and ctx.months_planned > 1 else 0)

    def h_post(ctx, label):
        outer, _scenario[0] = _scenario[0], 'events'
        try:
            ctx.month_hooks.remove(h_post)
            hst['n'] = 1
            mark, _ = ctx.month_marks
            roll = ctx.roll or {}
            rolls = roll.get('rolls', [])
            line = [x for x in rolls if re.match(r'event \d+ for nation 1: ', x)]
            n = int(re.match(r'event (\d+)', line[0]).group(1)) if line else None
            check(f'random path ({label}): the client rolled an event by itself (`event N for nation 1`), its dialog '
                  f'answered within {EVENT_ROLL_TIMEOUT} s', len(line) == 1 and roll.get('seconds') is not None
                  and roll.get('seen'), f'{rolls}, dialogs {roll.get("seen")}, {roll.get("seconds")} s')
            check(f'random path ({label}): no "event: no event this month" for that month',
                  'event: no event this month' not in rolls, rolls)
            hl = wait_log(H, mark, rf'event of Captain \(nation 1, #{n}\b' if n is not None else r'event of Captain', 10)
            hv = value(cmd(H, 'eventlast'))
            he = parse_host_event(hv)
            check(f'random path ({label}): host log "event of Captain (nation 1, #{n} ...)", host eventlast nation=1 '
                  f'event={n}', len(hl) == 1 and n is not None and he and he['nation'] == 1 and he['event'] == n,
                  f'{hl} | {hv}')
            log(f'  random event: {line} | dialog {roll.get("seen")} | host {[x.split("] ", 1)[-1] for x in hl]} | '
                f'client eventlast {value(cmd(C, "eventlast"))}')
            r = cmd(C, 'eventchance 0')
            if r == 'ok':
                ctx.event_chance = 0
            check('random path: client eventchance 0 again', r == 'ok', r)
        finally:
            _scenario[0] = outer

    def h_pre(ctx, label, n):
        if n != target:
            return
        ctx.pre_month_hooks.remove(h_pre)
        outer, _scenario[0] = _scenario[0], 'events'
        try:
            r = cmd(C, 'eventchance 100')
            if r == 'ok':
                ctx.event_chance = 100
            check(f'random path: client eventchance 100 before month {n} of {ctx.months_planned} ({label}, nation 1 '
                  f'at peace)', r == 'ok', r)
            ctx.month_hooks.append(h_post)
        finally:
            _scenario[0] = outer

    h_pre.scenario, h_pre.state = 'events', hst
    h_post.scenario, h_post.state = 'events', hst
    ctx.pre_month_hooks.append(h_pre)

    # i) the war rule (EventLikelihoodCheck: while the war counter is > 0 only war events qualify, and every war event
    # needs the host): chance 100 for diplomacy's war month, so the roll when the month at war loads runs the random
    # path at war; it must find no event and show no dialog. `eventnow` without an index can't do this itself: it
    # goes through the once-a-month guard, and the month was rolled when it loaded (`this month was already rolled`).
    war_month = ctx.months_planned if 'diplomacy' in ctx.selected and ctx.months_planned >= 2 else None
    wst = dict(n=0, need=1, pre=None)

    def w_post(ctx, label):
        outer, _scenario[0] = _scenario[0], 'events'
        try:
            ctx.month_hooks.remove(w_post)
            wst['n'] = 1
            roll = ctx.roll or {}
            r01, r12 = rel(0, 1), rel(1, 2)
            at_war = r01.get('war') == '1' or r12.get('war') == '1'
            last = value(cmd(C, 'eventlast'))
            cmark = log_mark(C)
            rn = cmd(C, 'eventnow')
            seen_dlg, end = False, time.time() + 5
            while time.time() < end and not roll_lines(log_since(C, cmark)):
                seen_dlg = seen_dlg or bool(event_dialogs(ctx.cp))
                time.sleep(0.3)
            time.sleep(1)
            seen_dlg = seen_dlg or bool(event_dialogs(ctx.cp))
            if seen_dlg:
                answer_events(ctx, 15)
            now = roll_lines(log_since(C, cmark))
            check(f'war rule ({label}): nation 1 at war, the month\'s roll at chance 100 finds no event (`event: no '
                  f'event this month`, eventlast none, no dialog); a random `eventnow` adds none',
                  wst['pre'] == 'ok' and at_war and roll.get('rolls') == ['event: no event this month']
                  and not roll.get('seen') and last == 'none' and rn == 'ok' and not seen_dlg and len(now) == 1
                  and now[0] in ('event: this month was already rolled', 'event: no event this month'),
                  f'eventchance 100: {wst["pre"]}; war 0-1 {r01.get("war")}, 1-2 {r12.get("war")}; roll '
                  f'{roll.get("rolls")}, dialogs {roll.get("seen")}; eventlast {last}; eventnow {rn} -> {now}, '
                  f'dialog {seen_dlg}')
            r = cmd(C, 'eventchance 0')
            if r == 'ok':
                ctx.event_chance = 0
        finally:
            _scenario[0] = outer

    def w_pre(ctx, label, n):
        if n != war_month:
            return
        ctx.pre_month_hooks.remove(w_pre)
        wst['pre'] = cmd(C, 'eventchance 100')
        if wst['pre'] == 'ok':
            ctx.event_chance = 100
        log(f'  war rule: client eventchance 100 before month {n} ({label}, the war month): {wst["pre"]}')
        ctx.month_hooks.append(w_post)

    if war_month:
        w_pre.scenario, w_pre.state = 'events', wst
        w_post.scenario, w_post.state = 'events', wst
        ctx.pre_month_hooks.append(w_pre)
    else:
        log('  SKIP war rule check: no month at war in this selection (needs `diplomacy`)')
    if planned_months([n for n in ctx.selected if n != 'events']) == 0:  # no later months: process two here
        run_month(ctx, 'events 1')
        run_month(ctx, 'events 2')


def scenario_merge(ctx):
    rp0 = value(cmd(C, 'nfield 1 ResearchPct'))
    rp_new = 13 if rp0 != '13' else 14
    r = cmd(C, f'nfield 1 ResearchPct {rp_new}')
    check('client sets nation 1 research %', value(r) == str(rp_new), f'{rp0} -> {r}')
    areas = [a.split(':', 1) for a in items(cmd(C, 'areas')) if ':' in a]
    cs, hs = shipinfo(C, 1, 0), shipinfo(H, 1, 0)
    taken = {cs['loc'], cs['ordered'], hs['loc'], hs['ordered']} if cs and hs else set()
    target = next(((i, n) for i, n in areas if n not in taken), None) if cs and hs else None
    r = cmd(C, f'shiporder 1 0 {target[0]}') if target else 'no ship or no free area'
    so = re.match(r'^ok (.*) loc=(.*) ordered=(.*) status', r)
    check('client orders ship 0 of nation 1 to another area', target and so and so.group(3) == target[1],
          f'{cs} -> {target}: {r}')
    log('client ship before:', cs, '| host ship before:', hs, '| target:', target)
    mark = log_mark(H)
    run_month(ctx, 'merge')
    hlog = log_since(H, mark)
    hv, cv = value(cmd(H, 'nfield 1 ResearchPct')), value(cmd(C, 'nfield 1 ResearchPct'))
    check('host keeps the client\'s research % after the month', hv == str(rp_new), f'host {hv}, expected {rp_new}')
    check('client has its research % in the new month', cv == str(rp_new), f'client {cv}, expected {rp_new}')
    want = target[1] if target else '?'
    for port, who in ((H, 'host'), (C, 'client')):
        s = shipinfo(port, 1, 0)
        if s and cs and s['name'] != cs['name']:
            s = find_ship(port, 1, cs['name'])
        check(f'{who}: the ship follows the client\'s order (ordered or arrived)',
              s and want in (s['loc'], s['ordered']), f'{s}, target {want}')
    check('host log: nation 1\'s turn merged', log_lines(hlog, r'merge nation 1:'),
          log_lines(hlog, r'merge nation|Could not apply'))
    sup = log_lines(hlog, r'AI suppressed: .* for nation 1\b')
    check('host log: AI suppressed for nation 1', sup, f'{len(sup)} lines' if sup else 'no line')
    log('  e.g.', sup[:3])
    other = log_lines(hlog, rf'AI suppressed: .* for nation {AI_NATION}\b')
    check(f'host log: the AI still runs AI nation {AI_NATION}', not other, other[:3])


def doctrine(port, nation):
    """`doctrine <nation>` as a dict (training/pending as 4-tuples), or None."""
    m = re.match(r'^ok training=([\d,]+) pending=([\d,]+) months=(-?\d+) missiles=(-?\d+) pendingMissiles=(-?\d+) '
                 r'missileMonths=(-?\d+)', cmd(port, f'doctrine {nation}'))
    if not m:
        return None
    four = lambda s: tuple(int(x) for x in s.split(','))
    return dict(training=four(m[1]), pending=four(m[2]), months=int(m[3]), missiles=int(m[4]),
                pendingMissiles=int(m[5]), missileMonths=int(m[6]))


def other_flags(t):
    """Training flags that differ from `t` and are not all 0 (all 0 means "nothing pending")."""
    f = list(t)
    i = f.index(0) if 0 in f else len(f) - 1
    f[i] = 1 - f[i]
    return tuple(f)


def children(top):
    out = []
    u.EnumChildWindows(top, e2e_test.EnumProc(lambda h, l: out.append(h) or True), 0)
    return out


def caption(h):
    return e2e_test.window_text(h).replace('&', '').strip()


def wait_window(pid, klass, timeout, gone=False):
    end = time.time() + timeout
    while time.time() < end:
        w = [h for h in windows_of(pid, klass) if u.IsWindowVisible(h)]
        if gone and not w:
            return True
        if w and not gone:
            return w[0]
        time.sleep(0.3)
    return None


def answer_confirmations(pid, timeout, who='client'):
    """Message boxes (TMessageForm, #32770) that a dialog's button opened: Yes / OK."""
    seen, end = [], time.time() + timeout
    while time.time() < end:
        for top in [h for k in ('TMessageForm', '#32770') for h in windows_of(pid, k) if u.IsWindowVisible(h)]:
            btn = next((h for h in children(top) if caption(h).lower() in ('yes', 'ok')), None)
            if btn and top not in seen:
                seen.append(top)
                u.PostMessageW(btn, 0x00F5, 0, 0)  # BM_CLICK
                log(f'  {who} confirmation answered: {e2e_test.class_name(top)}:"{caption(top)}"->{caption(btn)}')
        time.sleep(0.4)
    return seen


def toggle_checkbox(box, parent, was):
    """Toggle a VCL TCheckBox in another process without the real mouse. A posted BM_CLICK is tried first; with the
    game's VCL styles the check box's style hook ignores it (it wants the real cursor over the box), so the parent
    then gets the notification a real click produces (WM_COMMAND BN_CLICKED + the box's handle), which VCL turns into
    CN_COMMAND -> Toggle -> OnClick. BM_SETCHECK would only change the picture, not the VCL state."""
    for how in ('BM_CLICK', 'WM_COMMAND BN_CLICKED to the parent'):
        if how == 'BM_CLICK':
            u.PostMessageW(box, 0x00F5, 0, 0)
        else:
            u.PostMessageW(parent, 0x0111, u.GetDlgCtrlID(box) & 0xFFFF, box)  # BN_CLICKED (0) << 16 | id
        end = time.time() + 1.5
        while time.time() < end:
            time.sleep(0.25)
            if int(u.SendMessageW(box, 0x00F0, 0, 0)) != was:  # BM_GETCHECK
                return how
    return 'neither BM_CLICK nor WM_COMMAND BN_CLICKED'


def doctrine_dialog_apply(ctx):
    """The player's path: the strategic screen's 'Doctrine' button (TBitBtn btbnTraining) -> TdlgDoctrine -> toggle
    one 'Training priorities' check box -> that group's Apply (bnApply; the other Apply is the missile policy's) ->
    Close. Posted clicks only; the client's dialogs are not auto-answered meanwhile (dismiss_dialogs would close
    TdlgDoctrine). Returns (dialog opened, what was done)."""
    cp = ctx.cp
    btn = next((h for top in windows_of(cp, 'TfrmBuildCamp') for h in children(top)
                if e2e_test.class_name(h) == 'TBitBtn' and caption(h) == 'Doctrine'), None)
    if not btn:
        return False, 'no "Doctrine" TBitBtn on the client\'s TfrmBuildCamp'
    state = f'button visible {bool(u.IsWindowVisible(btn))} enabled {bool(u.IsWindowEnabled(btn))}'
    u.PostMessageW(btn, 0x00F5, 0, 0)  # BM_CLICK
    dlg = wait_window(cp, 'TdlgDoctrine', 8)
    if not dlg:
        return False, f'{state}; no TdlgDoctrine; windows: {describe_windows(cp)}'
    try:
        kids = children(dlg)
        group = next((h for h in kids if e2e_test.class_name(h) == 'TGroupBox' and caption(h) == 'Training priorities'),
                     None)
        inner = children(group) if group else []
        boxes = [h for h in inner if e2e_test.class_name(h) == 'TCheckBox']
        apply = next((h for h in inner if e2e_test.class_name(h) in ('TButton', 'TBitBtn') and caption(h) == 'Apply'),
                     None)
        before = [(caption(h), int(u.SendMessageW(h, 0x00F0, 0, 0))) for h in boxes]  # BM_GETCHECK
        log('  doctrine dialog: training check boxes', before, '| Apply', 'found' if apply else 'MISSING')
        if not boxes or not apply:
            return True, f'training group {bool(group)}, check boxes {len(boxes)}, Apply {bool(apply)}'
        target = next((h for h in boxes if not u.SendMessageW(h, 0x00F0, 0, 0)), boxes[-1])
        was = int(u.SendMessageW(target, 0x00F0, 0, 0))
        how = toggle_checkbox(target, group, was)
        toggled = int(u.SendMessageW(target, 0x00F0, 0, 0))
        log(f'  doctrine dialog: "{caption(target)}" {was} -> {toggled} ({how}); Apply enabled '
            f'{bool(u.IsWindowEnabled(apply))}')
        if toggled == was:
            return True, f'"{caption(target)}" did not toggle ({how}); Apply not pressed'
        u.PostMessageW(apply, 0x00F5, 0, 0)
        answer_confirmations(cp, 3)
        return True, f'toggled "{caption(target)}" to {toggled} ({how}), applied'
    finally:
        close = next((h for h in children(dlg) if e2e_test.class_name(h) in ('TButton', 'TBitBtn')
                      and caption(h) == 'Close' and u.GetParent(h) == dlg), None)
        if close:
            u.PostMessageW(close, 0x00F5, 0, 0)
        if not wait_window(cp, 'TdlgDoctrine', 3, gone=True):
            answer_confirmations(cp, 2)
            if not wait_window(cp, 'TdlgDoctrine', 3, gone=True):
                log('  doctrine dialog did not close with "Close"; sending WM_CLOSE')
                u.PostMessageW(dlg, 0x0010, 0, 0)  # WM_CLOSE
                wait_window(cp, 'TdlgDoctrine', 5, gone=True)


def scenario_doctrine(ctx):
    """A joined player's training priorities and missile storage policy take effect on the host. Set up in the
    planning month before `design_study`, checked after design_study's two months (month hook), so no extra months
    are processed (the scenario processes its own two months if design_study is not selected)."""
    t0 = time.time()
    cd0, hd0, n2 = doctrine(C, 1), doctrine(H, 0), doctrine(H, STANDIN)
    log('  doctrine before: client nation 1', cd0, '| host nation 0', hd0, '| nation 2', n2)
    if not check('doctrine readable on host and client', cd0 and hd0, f'client {cd0}, host {hd0}'):
        return
    opened, what = doctrine_dialog_apply(ctx)
    check('client: the Doctrine button opens the doctrine dialog', opened, what)
    closed = not [h for h in windows_of(ctx.cp, 'TdlgDoctrine') if u.IsWindowVisible(h)]
    check('client: the doctrine dialog is closed again', closed, describe_windows(ctx.cp))
    ui = doctrine(C, 1)
    ui_ok = ui and ui['months'] == 12 and any(ui['pending']) and ui['pending'] != cd0['training']
    check('client: Apply in the dialog makes a pending training change (12 months)', ui_ok, f'{what}; {ui}')
    if ui_ok:
        changed = [i for i in range(4) if ui['pending'][i] != cd0['training'][i]]
        log(f'  dialog Apply: training {cd0["training"]} -> pending {ui["pending"]} (flag index {changed} changed)')
    flags = ui['pending'] if ui_ok else other_flags(cd0['training'])  # fall back to `settraining` (as the dialog)
    fs = ' '.join(map(str, flags))
    cmd(C, f'settraining 1 {fs} 2')
    d = doctrine(C, 1)
    check('client: the training change shortened to 2 months', d and d['pending'] == flags and d['months'] == 2, d)
    policy = 2 if cd0['missiles'] != 2 else 1
    cmd(C, f'setmissiles 1 {policy} 2')
    d = doctrine(C, 1)
    check(f'client: missile storage policy {policy} pending for 2 months',
          d and d['pendingMissiles'] == policy and d['missileMonths'] == 2, d)
    hflags = other_flags(hd0['training'])
    cmd(H, f'settraining 0 {" ".join(map(str, hflags))} 2')
    d = doctrine(H, 0)
    check('host: its own training change pending for 2 months', d and d['pending'] == hflags and d['months'] == 2, d)
    allowed = {'1'} | ({str(STANDIN)} if n2 and (n2['months'] > 0 or n2['missileMonths'] > 0) else set())
    st = dict(n=0, mark=log_mark(H), before=None, done=False)
    log(f'  doctrine set up in {time.time() - t0:.0f} s: nation 1 pending {flags} + missiles {policy}, nation 0 '
        f'pending {hflags}; checked after the next two months')

    def hook(ctx, label):
        outer, _scenario[0] = _scenario[0], 'doctrine'
        try:
            st['n'] += 1
            hd, cd = doctrine(H, 1), doctrine(C, 1)
            log(f'  doctrine after {label}: host nation 1 {hd} | client {cd} | host nation 0 {doctrine(H, 0)}')
            if st['n'] == 1:
                want = dict(training=cd0['training'], pending=flags, months=1)
                for who, dd in (('host', hd), ('client', cd)):
                    check(f'doctrine month 1 ({label}): {who} has nation 1\'s training change at 1 month, not applied',
                          dd and all(dd[k] == v for k, v in want.items()), f'{dd}, want {want}')
                check(f'doctrine month 1 ({label}): missile policy at 1 month (host and client)',
                      hd and cd and all(x['missileMonths'] == 1 and x['pendingMissiles'] == policy and
                                        x['missiles'] == cd0['missiles'] for x in (hd, cd)), f'host {hd}, client {cd}')
                ln = log_lines(log_since(H, st['mark']), r'doctrine nation 1: ')
                check(f'doctrine month 1 ({label}): host log "doctrine nation 1: training 2 -> 1 months, missile '
                      f'storage 2 -> 1 months"', len(ln) == 1 and ln[0].endswith(
                          'doctrine nation 1: training 2 -> 1 months, missile storage 2 -> 1 months'), ln)
                st['before'] = chat(ctx.cp)
                return
            ctx.month_hooks.remove(hook)
            st['done'] = True
            check(f'doctrine month 2 ({label}): nation 1\'s training applied (host and client)',
                  hd and cd and all(x['training'] == flags and x['pending'] == (0, 0, 0, 0) and x['months'] == 0
                                    for x in (hd, cd)), f'host {hd}, client {cd}, want training {flags}')
            check(f'doctrine month 2 ({label}): nation 1\'s missile policy applied (host and client)',
                  hd and cd and all(x['missiles'] == policy and x['pendingMissiles'] == 0 and x['missileMonths'] == 0
                                    for x in (hd, cd)), f'host {hd}, client {cd}, want missiles {policy}')
            lines = ['* Doctrine: new training applied. Crews are now deemed proficient in the new tactics!',
                     '* Doctrine: missile stocks are now adapted to the new missile storage policy.']
            end, txt = time.time() + 20, chat(ctx.cp)
            while time.time() < end and not all(txt.count(x) > st['before'].count(x) for x in lines):
                time.sleep(0.5)
                txt = chat(ctx.cp)
            got = [txt.count(x) - st['before'].count(x) for x in lines]
            check(f'doctrine month 2 ({label}): client chat has both Doctrine lines', got == [1, 1],
                  f'{got}; last lines {txt.strip().splitlines()[-4:]}')
            dlog = log_since(H, st['mark'])
            ln = log_lines(dlog, r'doctrine nation 1: ')
            want = 'doctrine nation 1: training 1 -> 0 months (applied), missile storage 1 -> 0 months (applied)'
            check(f'doctrine month 2 ({label}): host log has "(applied)" for training and missile storage',
                  len(ln) == 2 and ln[1].endswith(want), ln)
            h0 = doctrine(H, 0)
            check(f'doctrine month 2 ({label}): host\'s own training applied by the game ({hflags})',
                  h0 and h0['training'] == hflags and h0['months'] == 0, h0)
            other = [x for x in log_lines(dlog, r'doctrine nation \d+: ')
                     if re.search(r'doctrine nation (\d+): ', x).group(1) not in allowed]
            check('doctrine: no countdown lines for AI nations in the host log', not other, other[:3])
        finally:
            _scenario[0] = outer

    hook.scenario, hook.state = 'doctrine', st
    ctx.month_hooks.append(hook)
    if 'design_study' not in ctx.selected:  # no shared months: process two here
        run_month(ctx, 'doctrine 1')
        run_month(ctx, 'doctrine 2')


def scenario_design_study(ctx):
    cd = designs(C, 1)
    cpick = pick_design(cd)
    r = cmd(C, f'setready 1 {cpick[0]} 2') if cpick else 'no buildable design with a unique class name'
    check('client: a buildable nation 1 design is put into a 2-month study', value(r) == '2', f'{cpick} {r}')
    hd = designs(H, 0)
    hpick = pick_design(hd)
    r = cmd(H, f'setready 0 {hpick[0]} 2') if hpick else 'no buildable design with a unique class name'
    check('host: a buildable nation 0 design is put into a 2-month study', value(r) == '2', f'{hpick} {r}')
    ai_before = designs(H, AI_NATION)
    check(f'AI nation {AI_NATION}: designs recorded', ai_before, f'{len(ai_before)} designs')
    if not cpick or not hpick:
        return  # nothing to count down (the checks above failed); the session itself is fine
    ci, cname = cpick
    hi, hname = hpick
    log(f'client study: nation 1 #{ci} "{cname}"; host study: nation 0 #{hi} "{hname}"; '
        f'AI nation {AI_NATION}: {len(ai_before)} designs, studies '
        f'{[(n, r) for _, n, r in ai_before if 0 < r < MAXINT]}')
    ready_line = f'* Design study: the {cname} class is ready for construction.'
    delay_line = f'* Design study: technical issues have delayed the {cname} class by a month.'
    mark0 = log_mark(H)
    prev = 2
    for month in (1, 2):
        before = chat(ctx.cp)
        mark = log_mark(H)
        run_month(ctx, f'design_study {month}')
        after = wait_month_chat(ctx.cp, before)
        hlog = log_since(H, mark)
        v = ready_of(designs(C, 1), ci, cname)
        vh = ready_of(designs(H, 1), ci, cname)
        n_ready = after.count(ready_line) - before.count(ready_line)
        n_delay = after.count(delay_line) - before.count(delay_line)
        mine = log_lines(hlog, rf"design study nation 1 '{re.escape(cname)}':")
        log(f'  month {month}: client {v}, host {vh}, host 0 {ready_of(designs(H, 0), hi, hname)}, chat ready '
            f'{n_ready} delayed {n_delay}, log {mine}')
        delayed = v == prev
        check(f'month {month}: the client\'s design study counts down ({prev} -> {prev - 1}, or stays with the '
              f'technical-issues line)', v == prev - 1 or (delayed and n_delay >= 1), f'got {v}, delayed lines {n_delay}')
        check(f'month {month}: host and client agree on the study', vh == v, f'host {vh}, client {v}')
        want = f"design study nation 1 '{cname}': " + ('delayed (technical issues)' if delayed else f'{prev} -> {v}')
        check(f'month {month}: host log has one matching design-study line', len(mine) == 1 and mine[0].endswith(want),
              f'want "{want}", got {mine}')
        if v == 0:
            what, ok = 'the "ready for construction" line', n_ready == 1 and n_delay == 0
        elif delayed:
            what, ok = f'the "technical issues" line (the study stayed at {prev})', n_delay == 1 and n_ready == 0
        else:
            what, ok = 'no design-study line', n_ready == 0 and n_delay == 0
        check(f'month {month}: client chat shows {what}', ok, f'ready {n_ready}, delayed {n_delay}; last lines: '
              f'{after.strip().splitlines()[-4:]}')
        prev = v if isinstance(v, int) else prev
    dlog = log_since(H, mark0)
    h_after = ready_of(designs(H, 0), hi, hname)
    check('host\'s own study advanced in the game\'s routine (2 -> 0, or 1 after a random delay)', h_after in (0, 1),
          h_after)
    n0 = log_lines(dlog, r'design study nation 0 ')
    check('the mod leaves the host nation\'s studies to the game (no "design study nation 0" lines)', not n0, n0[:3])
    ai_after = designs(H, AI_NATION)
    names_b, names_a = [n for _, n, _ in ai_before], [n for _, n, _ in ai_after]
    common = [n for n in names_b if names_b.count(n) == 1 and names_a.count(n) == 1]
    rb, ra = {n: r for _, n, r in ai_before}, {n: r for _, n, r in ai_after}
    diff = [(n, rb[n], ra[n]) for n in common if rb[n] != ra[n]]
    check(f'AI nation {AI_NATION}: designs unchanged', common and not diff,
          f'{len(common)} compared, changed {diff}, before {len(ai_before)} after {len(ai_after)}')
    humans = {'1', str(STANDIN)}
    others = [ln for ln in log_lines(dlog, r'design study nation \d+ ')
              if re.search(r'design study nation (\d+) ', ln).group(1) not in humans | {'0'}]
    check('no design-study countdown for AI nations in the host log', not others, others[:3])


def scenario_diplomacy(ctx):
    hp, cp = ctx.hp, ctx.cp
    mark0 = log_mark(H)
    log('start relations 0-1', rel(0, 1), '| 0-2', rel(0, 2), '| 1-2', rel(1, 2))
    r = row(C, 0)
    check('client sees the host nation in its diplomacy list', r and r['relation'] == 'Peace', r)
    check('client sees the stand-in nation 2', row(C, 2) is not None, row(C, 2))
    check('host sees nations 1 and 2', row(H, 1) is not None and row(H, 2) is not None)

    # Month A: host<->2 alliance (host offers, 2 accepts); client<->2 alliance (client offers, 2 accepts).
    check('host offers an alliance to nation 2', cmd(H, 'diplo ally 2') == 'ok')
    check('nation 2 accepts', cmd(H, 'diploas 2 ally 0') == 'ok')
    check('client offers an alliance to nation 2', cmd(C, 'diplo ally 2') == 'ok')
    time.sleep(1.5)
    r = row(H, 2)
    check('host: alliance with 2 agreed (pending)', r and 'alliance agreed' in r['pending'], r)
    check('nation 2 accepts the client\'s offer', cmd(H, 'diploas 2 ally 1') == 'ok')
    time.sleep(1.5)
    check('a repeated offer is refused on the client', cmd(C, 'diplo ally 2').startswith('error'))
    r = row(C, 2)
    check('client: alliance with 2 agreed (pending)', r and 'alliance agreed' in r['pending'], r)
    check('nothing applied before the month ends', int(rel(0, 2)['ally']) <= 0 and int(rel(1, 2)['ally']) <= 0)
    date = run_month(ctx, 'diplomacy A')

    log('month B:', date)
    r01, r02, r12 = rel(0, 1), rel(0, 2), rel(1, 2)
    log('  0-1', r01, '| 0-2', r02, '| 1-2', r12)
    check('host and nation 2 allied after the month', r02['ally'] == '60' and r02['war'] == '0', r02)
    check('client and nation 2 allied after the month', r12['ally'] == '60' and r12['war'] == '0', r12)
    r = row(C, 2)
    check('client list shows the alliance', r and r['relation'] == 'Allied' and r['pending'] == '', r)
    host_at_peace_b = int(r01['warCounter']) <= 0

    # Month B UI (client window, host row preselected): propose -> host accepts -> call off; propose -> declined.
    cmd(C, 'mpwindow')
    time.sleep(1.5)
    check('client window has the diplomacy buttons', text(mp_ctl(cp, 'ally')) == 'Propose alliance',
          text(mp_ctl(cp, 'ally')))
    click(cp, 'ally')
    time.sleep(1.5)
    r = row(H, 1)
    check('host sees the offer from the button', r and 'offers an alliance' in r['pending'] and 'a' in r['flags'], r)
    check('host accepts', cmd(H, 'diplo ally 1') == 'ok')
    time.sleep(1.5)
    check('Leave alliance is enabled for an agreed alliance', u.IsWindowEnabled(mp_ctl(cp, 'leave')))
    click(cp, 'leave')
    check('leave asks for confirmation', confirm(cp, 'Leave alliance'))
    time.sleep(1.5)
    r = row(H, 1)
    check('the agreed alliance was called off', r and r['pending'] == '', r)
    check('host offers an alliance', cmd(H, 'diplo ally 1') == 'ok')
    time.sleep(2)
    check('client button reads "Decline"', text(mp_ctl(cp, 'cancel')) == 'Decline', text(mp_ctl(cp, 'cancel')))
    check('client button reads "Accept alliance"', text(mp_ctl(cp, 'ally')) == 'Accept alliance',
          text(mp_ctl(cp, 'ally')))
    click(cp, 'cancel')
    time.sleep(1.5)
    r = row(H, 1)
    check('decline removed the offer', r and r['pending'] == '', r)
    # Declare war via the button (host row), withdraw, declare again; and a war on nation 2 by command.
    click(cp, 'war')
    check('declare war asks for confirmation', confirm(cp, 'Declare war'))
    time.sleep(1.5)
    r = row(H, 1)
    check('host sees the declaration of war', r and 'declared war on you' in r['pending'], r)
    check('Withdraw is enabled', u.IsWindowEnabled(mp_ctl(cp, 'cancel')) and text(mp_ctl(cp, 'cancel')) == 'Withdraw')
    click(cp, 'cancel')
    time.sleep(1.5)
    r = row(H, 1)
    check('withdraw removed the declaration', r and r['pending'] == '', r)
    click(cp, 'war')
    check('declare war again', confirm(cp, 'Declare war'))
    check('client declares war on its ally nation 2', cmd(C, 'diplo war 2') == 'ok')
    time.sleep(1.5)
    check('a second war declaration is refused', cmd(C, 'diplo war 2').startswith('error'))
    date = run_month(ctx, 'diplomacy B')

    log('month C:', date)
    r01, r02, r12 = rel(0, 1), rel(0, 2), rel(1, 2)
    log('  0-1', r01, '| 0-2', r02, '| 1-2', r12)
    check('host and client at war after the month', r01['war'] == '1' and r01['warCounter'] == '1', r01)
    check('client and nation 2 at war, their alliance gone', r12['war'] == '1' and r12['ally'] == '0', r12)
    check('host and nation 2 still allied (kept at 60 months)', r02['ally'] == '60', r02)
    if host_at_peace_b:
        check('the alliance countdown was topped up', log_since(H, mark0).count('diplomacy: alliance 0-2 = 60') >= 2)
    r = row(C, 0)
    check('client list shows war with the host', r and r['relation'] == 'At war', r)
    r = row(C, 2)
    check('client list shows war with nation 2', r and r['relation'] == 'At war', r)

    # The game's AI peace must not end the players' war (nation 1's only AI war is the one with 2).
    cmd(H, 'aipeace 1 0')
    drain(hp, 3)
    check('the AI peace routine does not end the players\' war', rel(1, 2)['war'] == '1', rel(1, 2))
    cmd(H, 'settension 2 3 50')  # nation 2 also fights an AI nation
    cmd(H, 'aipeace 2 0')
    drain(hp, 6)
    r23, r12 = rel(2, 3), rel(1, 2)
    check('AI peace ends the AI war of nation 2', r23['war'] == '0', r23)
    check('...but keeps the players\' war', r12['war'] == '1', r12)
    cmd(H, 'aipeace 2 1')
    drain(hp, 3)
    check('a forced AI peace keeps the players\' war too', rel(1, 2)['war'] == '1', rel(1, 2))
    # The game ends the players' war some other way: the month-end step restores it.
    cmd(H, 'settension 1 2 5')
    cmd(H, 'applydiplo')
    drain(hp, 3)
    check('the players\' war is restored at month end', rel(1, 2)['war'] == '1' and 'goes on' in chat(cp), rel(1, 2))

    # Peace on both fronts, the host leaves nation 2; applied with the month-end step (a processed month at war
    # with the host would mean a tactical battle).
    check('host offers peace', cmd(H, 'diplo peace 1') == 'ok')
    time.sleep(2)
    check('client button reads "Accept peace"', text(mp_ctl(cp, 'peace')) == 'Accept peace', text(mp_ctl(cp, 'peace')))
    click(cp, 'peace')
    time.sleep(1.5)
    r = row(H, 1)
    check('host sees peace agreed', r and 'peace agreed' in r['pending'], r)
    check('client offers peace to nation 2', cmd(C, 'diplo peace 2') == 'ok')
    time.sleep(1.5)
    check('nation 2 accepts peace', cmd(H, 'diploas 2 peace 1') == 'ok')
    check('host leaves the alliance with nation 2', cmd(H, 'diplo leave 2') == 'ok')
    cmd(H, 'applydiplo')
    drain(hp, 10)
    r01, r02, r12 = rel(0, 1), rel(0, 2), rel(1, 2)
    log('  0-1', r01, '| 0-2', r02, '| 1-2', r12)
    check('host and client at peace (game end-of-war routine)', r01['war'] == '0' and r01['warCounter'] == '0', r01)
    check('client and nation 2 at peace', r12['war'] == '0' and int(r12['tension']) <= 40, r12)
    check('host left the alliance with nation 2', r02['ally'] == '0', r02)
    time.sleep(2)
    r = row(C, 0)
    check('client list shows peace with the host', r and r['relation'] == 'Peace' and r['pending'] == '', r)


SCENARIOS = [  # name, months processed, what it guards, function
    ('session', 0, 'host/join/claim/start; the client plans nation 1 in slot 77; nation 2 reserved as the stand-in '
                   'player "Bot"; client Multiplayer window open', scenario_session),
    ('intel_reports', 0, 'the bloated slot 1 fixture is repaired on load and two more loads keep the intel list size '
                         '(run inside session, before hosting); clean lists and saves at the session start. Every '
                         'processed month also checks intel lists, saves and month package size (run_month)',
     scenario_intel_reports),
    ('file_lock', 0, 'the mod\'s save writes (WriteFileBytes, via the client\'s `writetest` into slot 77) survive a '
                     '~1 s lock by another process (retries, log line) and report a lock held > 3 s (error 32/5, '
                     'FAILED log line, file unchanged, no .mptmp left); throwaway file, deleted afterwards',
     scenario_file_lock),
    ('events', 0, 'random events of a joined player (0.2.4): eventlist (H = needs the host); a forced "naval rearm" '
                  'event on the client (real dialog, first answer) -> deltas applied by the host to nation 1 and its '
                  'relations only, client nation list restored; a second forced event in the month is ignored by '
                  'the host; a host-only event is refused; after the next month the effect is kept once (reported); '
                  'the last processed month with nation 1 at peace (before diplomacy\'s war month) rolls a random '
                  'event at chance 100, the war month\'s roll at chance 100 finds none (own 2 months if no later '
                  'scenario processes any)', scenario_events),
    ('merge', 1, 'client research % and a ship order survive the host\'s month; AI suppressed for nation 1 only',
     scenario_merge),
    ('doctrine', 0, 'the player\'s Doctrine dialog (real button, posted clicks) makes a pending training change; a '
                    'joined nation\'s training and missile storage policy take effect on the host after their months '
                    '(chat + host log); the host\'s own change still applies; no countdown for AI nations. Checked '
                    'after design_study\'s two months (2 own months if design_study is not selected)',
     scenario_doctrine),
    ('design_study', 2, 'a joined player\'s design study counts down on the host and reaches "ready for '
                        'construction" (chat + host log); the host\'s own study still advances; AI designs untouched',
     scenario_design_study),
    ('diplomacy', 2, 'offers/accept/decline/withdraw, war/peace/alliance at month end, AI peace kept away from '
                     'player wars, Diplomacy buttons via posted clicks', scenario_diplomacy),
]


# ---------------------------------------------------------------- runner

def running_pids(image):
    out = subprocess.run(['tasklist', '/FI', f'IMAGENAME eq {image}', '/FO', 'CSV', '/NH'], capture_output=True,
                         text=True).stdout
    pids = []
    for line in out.splitlines():
        f = line.strip().strip('"').split('","')
        if len(f) > 1 and f[0].lower() == image.lower() and f[1].isdigit():
            pids.append(int(f[1]))
    return pids


def file_hash(p):
    with open(p, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def tree_digest(d):
    out = {}
    for root, _, files in os.walk(d):
        for n in files:
            p = os.path.join(root, n)
            out[os.path.relpath(p, d)] = file_hash(p)
    return out


def preflight(g1):
    problems = []
    if running_pids('RTW3.exe'):
        problems.append(f'RTW3.exe is already running (PIDs {running_pids("RTW3.exe")}); close it first')
    if not running_pids('steam.exe'):
        problems.append('Steam is not running')
    for f in ('RTW3.exe', 'version.dll', 'RTW3MP.dll'):
        if not os.path.exists(os.path.join(ctl.GAME, f)):
            problems.append(f'{f} not found in the game folder (set RTW3_DIR)')
    if not os.path.exists(os.path.join(g1, f'RTWGame{HOST_SLOT}.bcs')):
        problems.append(f'save slot {HOST_SLOT} has no campaign')
    if os.path.exists(BACKUP):
        if os.path.isdir(g1) and tree_digest(BACKUP) == tree_digest(g1):
            shutil.rmtree(BACKUP)
            log('removed a leftover backup identical to the save slot')
        else:
            problems.append(f'a backup from an interrupted run exists ({BACKUP}) and differs from save slot '
                            f'{HOST_SLOT}: restore it into the slot or delete it first')
    return problems


def launch(port, slot=None, extra=None):
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith('RTW3MP_')}
    env.update(SteamAppId='2008100', SteamGameId='2008100', RTW3MP_BRIDGE_PORT=str(port), RTW3MP_LOG_TAG=str(port))
    if slot:
        env['RTW3MP_CLIENT_SLOT'] = str(slot)
    env.update(extra or {})
    return subprocess.Popen([os.path.join(ctl.GAME, 'RTW3.exe')], cwd=ctl.GAME, env=env)


def wait_bridge(port, timeout=90):
    end = time.time() + timeout
    while time.time() < end:
        try:
            if ctl.cmd(port, 'ping', 10).startswith('ok'):
                return True
        except OSError:
            pass
        time.sleep(1)
    return False


def mod_info():
    inst = os.path.join(ctl.GAME, 'RTW3MP.dll')
    built = os.path.join(REPO, 'dist', 'RTW3MP.dll')
    if os.path.exists(built):
        same = file_hash(inst) == file_hash(built) and \
            file_hash(os.path.join(ctl.GAME, 'version.dll')) == file_hash(os.path.join(REPO, 'dist', 'version.dll'))
        log('installed mod', 'matches dist\\' if same else 'DIFFERS from dist\\ (install the build first?)')
    first = [ln for ln in log_since(H).splitlines()[:3] if 'starting' in ln]
    log('host mod:', first[0].split('] ', 1)[-1] if first else '?')


def close(procs):
    for p in procs:
        if p.poll() is None:
            subprocess.run(['taskkill', '/PID', str(p.pid), '/F'], capture_output=True)
    left = []
    for p in procs:
        try:
            p.wait(timeout=20)
        except subprocess.TimeoutExpired:
            left.append(p.pid)
    return left


def restore(g1):
    err = None
    for _ in range(10):
        try:
            if os.path.exists(g1):
                shutil.rmtree(g1)
            shutil.copytree(BACKUP, g1)
            if tree_digest(g1) == tree_digest(BACKUP):
                shutil.rmtree(BACKUP, ignore_errors=True)
                return True, ''
            err = 'restored files differ'
        except OSError as e:
            err = repr(e)
        time.sleep(1)
    return False, err


def remove_client_slot():
    g77 = os.path.join(SAVE, f'Game{CLIENT_SLOT}')
    for _ in range(10):
        shutil.rmtree(g77, ignore_errors=True)
        if not os.path.exists(g77):
            return True
        time.sleep(1)
    return False


def run_scenarios(ctx, names):
    ctx.selected = list(names)
    ctx.months_planned = planned_months(names)
    aborted = None
    for name in names:
        _scenario[0] = name
        if aborted:
            check(f'{name}: run', False, f'not run: {aborted} aborted')
            continue
        log(f'=== {name} ===')
        fn = next(s[3] for s in SCENARIOS if s[0] == name)
        try:
            fn(ctx)
        except (Exception, SystemExit) as e:
            check(f'{name}: completed', False, repr(e))
            aborted = name
            for pid, who in ((ctx.hp, 'host'), (ctx.cp, 'client')):
                log(f'  {who} windows:', describe_windows(pid))
            for port in (H, C):
                log(f'  log {port} tail:\n    ' + '\n    '.join(log_since(port).splitlines()[-12:]))
    for hook in ctx.month_hooks + ctx.pre_month_hooks:  # a scenario that shares later months never got them
        _scenario[0] = hook.scenario
        check(f'{hook.scenario}: its checks after the shared months ran', False,
              f'only {hook.state["n"]} of {hook.state.get("need", 2)} months processed after its setup')


def summary(names, open_s, ctx, saves_ok, left, ev_left=()):
    print()
    log('==== summary ====')
    for name in names:
        rs = [r for r in results if r[0] == name]
        n_ok = sum(1 for r in rs if r[2])
        print(f'  {name:<14} {n_ok:>3}/{len(rs):<3} {"PASS" if rs and n_ok == len(rs) else "FAIL"}')
    failed = [r for r in results if not r[2]]
    for sc, name, _, detail in failed:
        print(f'  FAIL [{sc}] {name}: {detail}')
    for label, d0, d1, dt in ctx.months:
        print(f'  month {label}: {d0} -> {d1}, {dt:.0f} s')
    if ctx.intel_hist:
        print(f'  {"after":<16}{"intel host":>12}{"intel client":>14}{"saved host":>12}{"saved client":>14}'
              f'{"package B":>11}{"client MB":>11}{"host MB":>9}')
        for s in ctx.intel_hist:
            print(f'  {s["label"]:<16}{str(s["host"][0]):>12}{str(s["client"][0]):>14}'
                  f'{str((s["host_file"] or {}).get("ReportNo")):>12}{str((s["client_file"] or {}).get("ReportNo")):>14}'
                  f'{str(s["pkg"]):>11}{str(s["cmem"]):>11}{str(s["hmem"]):>9}')
    print(f'  game open: {open_s:.0f} s; saves {"restored" if saves_ok else "NOT RESTORED (see above)"}; '
          + (f'still running: {left}' if left else 'no started copy left running') + '; '
          + (f'event cooldown files LEFT: {list(ev_left)}' if ev_left else 'no events_*.ini left'))
    n = len(results)
    print(f'{n - len(failed)}/{n} checks passed', flush=True)
    return not failed


def main(argv=None):
    ap = argparse.ArgumentParser(description='RTW3 Multiplayer regression suite (two live game copies).')
    ap.add_argument('--list', action='store_true', help='list the scenarios and exit')
    ap.add_argument('--only', help='comma-separated scenarios to run (session is always included)')
    a = ap.parse_args(argv)
    if a.list:
        print(f'{"scenario":<14}{"months":<8}what it guards')
        for name, months, what, _ in SCENARIOS:
            print(f'{name:<14}{months:<8}{what}')
        print(f'default: all, in this order ({sum(s[1] for s in SCENARIOS)} months processed)')
        return 0
    names = [s[0] for s in SCENARIOS]
    if a.only:
        want = [w.strip() for w in a.only.split(',') if w.strip()]
        bad = [w for w in want if w not in names]
        if bad:
            ap.error(f'unknown scenario(s) {bad}; known: {names}')
        names = [n for n in names if n in want or n == 'session']
    g1 = os.path.join(SAVE, f'Game{HOST_SLOT}')
    problems = preflight(g1)
    for p in problems:
        log('cannot start:', p)
    if problems:
        return 2
    stale = remove_event_files('before the run')
    if stale:
        log('cannot start: event cooldown files that cannot be deleted:', stale)
        return 2
    shutil.copytree(g1, BACKUP)
    log('backed up save slot', HOST_SLOT, '| scenarios:', ', '.join(names))
    ctx, procs, t_open = Ctx(), PROCS, time.time()
    ctx.fixture = saved_intel(save_path(HOST_SLOT))
    saves_ok, left, ev_left = False, [], []
    try:
        procs.append(launch(H))
        time.sleep(4)
        procs.append(launch(C, CLIENT_SLOT, {'RTW3MP_EVENT_CHANCE': str(ctx.event_chance)}))
        ctx.hp, ctx.cp = procs[0].pid, procs[1].pid
        log('started host PID', ctx.hp, 'client PID', ctx.cp)
        _scenario[0] = 'session'
        up = {port: wait_bridge(port) for port in (H, C)}
        if check('both copies answer on the test bridge', all(up.values()), up):
            mod_info()
            run_scenarios(ctx, names)
        else:
            for n in names[1:]:
                _scenario[0] = n
                check(f'{n}: run', False, 'not run: the game did not start')
    except KeyboardInterrupt:
        check('suite: completed', False, 'interrupted')
    finally:
        left = close(procs)
        open_s = time.time() - t_open
        time.sleep(2)
        saves_ok, err = restore(g1)
        if saves_ok:
            log('restored save slot', HOST_SLOT)
        else:
            log(f'!! could not restore save slot {HOST_SLOT} ({err}); the backup is kept at {BACKUP}')
        if not remove_client_slot():
            log(f'!! could not remove Game{CLIENT_SLOT}')
        ev_left = remove_event_files('after the run')
        if ev_left:
            log('!! could not delete the event cooldown files', ev_left)
    passed = summary(names, open_s, ctx, saves_ok, left, ev_left)
    return 0 if passed and saves_ok and not left and not ev_left else 1


if __name__ == '__main__':
    sys.exit(main())
