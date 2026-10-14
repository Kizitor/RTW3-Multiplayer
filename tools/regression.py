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
import argparse, ctypes, ctypes.wintypes as wt, hashlib, os, re, shutil, struct, subprocess, sys, tempfile, threading, time

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
CLAIM_IDC = dict(label=1090, list=1091, sum=1092, ok=1093, auto=1094, cancel=1095)  # RTW3MP_Claim window (0.2.5)

results = []  # (scenario, check, ok, detail)
_scenario = ['-']
PROCS = []  # the game copies this run started (Popen)
ANSWERED = []  # (time, who, 'Class:"title"->button') of every game dialog answer_dialogs answered


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
    """A `diploview` row: nation/name/player/relation/tension/[pending]/flags, since 0.2.5 followed by
    /vp=mine:theirs/claim=budget:nclaimable:nclaims/outlook."""
    r = cmd(port, 'diploview')
    for part in r.split('rows=', 1)[-1].split('; '):
        f = part.split('/')
        if len(f) >= 7 and f[0] == str(nation):
            d = dict(nation=f[0], name=f[1], player=f[2], relation=f[3], tension=f[4], pending=f[5].strip('[]'),
                     flags=f[6].rstrip(';'), vp=None, claim=None, outlook='')
            for x in f[7:]:
                x = x.rstrip(';')
                if x.startswith('vp='):
                    d['vp'] = x[3:]
                elif x.startswith('claim='):
                    d['claim'] = x[6:]
                else:
                    d['outlook'] = x
            return d
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
    dismiss_dialogs is reset on every pass; the callers' timeouts bound their loops. A "Peace treaty" dialog is
    recorded (TREATY_SEEN) before it is answered."""
    capture_treaty(pid, who)
    e2e_test._clicks.clear()
    for c in dismiss_dialogs(pid):
        ANSWERED.append((time.time(), who, c))
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


def months_through(names, last):
    """Months processed up to and including scenario `last` (run order), with planned_months' rules."""
    order = [s[0] for s in SCENARIOS]
    return planned_months([n for n in names if order.index(n) <= order.index(last)])


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
    # client rolls by itself. Later scenarios (treaty) may process months after diplomacy's, so the war month is
    # counted in run order.
    hst = dict(n=0, need=1)
    war_month = months_through(ctx.selected, 'diplomacy') if 'diplomacy' in ctx.selected else None
    target = war_month - 1 if war_month else ctx.months_planned

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


# ---------------------------------------------------------------- peace treaties between players (0.2.5)

TREATY_RE = (r'treaty (\d+) over (\d+) \(level (\d)\): (\d+) possession\(s\) \((claimed|automatic)\), (\d+) ship\(s\), '
             r'prestige \+(\d+), resources -(\d+)')
TREATY_CAPTION = 'Peace treaty'  # ShowPeaceTreaty uses the game's TdlgTotalLog (RichEdit + OK/Cancel) with this caption
TREATY_SEEN = []  # each "Peace treaty" dialog answer_dialogs met before answering it: dict(who, t, text)
_treaty_open = {}
SESSION_FILE = 'rtw3mp_session.ini'  # the host's session file in the slot 1 folder (restored with the slot)
LVM_GETITEMCOUNT, LVM_SETITEMSTATE, LVM_GETITEMSTATE, LVM_GETITEMW, LVM_GETITEMTEXTW = 0x1004, 0x102B, 0x102C, 0x104B, 0x1073

_k32 = ctypes.WinDLL('kernel32', use_last_error=True)  # own instances: the prototypes below don't touch `u`
_k32.OpenProcess.restype = wt.HANDLE
_k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
_k32.VirtualAllocEx.restype = ctypes.c_void_p
_k32.VirtualAllocEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wt.DWORD, wt.DWORD]
_k32.VirtualFreeEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wt.DWORD]
_k32.WriteProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                    ctypes.POINTER(ctypes.c_size_t)]
_k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                   ctypes.POINTER(ctypes.c_size_t)]
_k32.CloseHandle.argtypes = [wt.HANDLE]
_u32 = ctypes.WinDLL('user32')
_u32.SendMessageW.restype = ctypes.c_ssize_t
_u32.SendMessageW.argtypes = [wt.HWND, wt.UINT, ctypes.c_size_t, ctypes.c_size_t]
_u32.GetWindow.restype = wt.HWND
_u32.GetWindow.argtypes = [wt.HWND, wt.UINT]


class RemoteBuf:
    """A scratch buffer inside a game copy (32-bit) for list-view messages that carry a pointer (LVM_GETITEM,
    LVM_GETITEMTEXT, LVM_SETITEMSTATE). The messages themselves are sent to the window as usual; nothing else in the
    game's memory is read or written."""

    def __init__(self, pid, size=1024):
        self.h = _k32.OpenProcess(0x0008 | 0x0010 | 0x0020 | 0x0400, False, pid)  # VM_OPERATION/READ/WRITE, QUERY
        if not self.h:
            raise OSError(f'OpenProcess({pid}) failed: {ctypes.get_last_error()}')
        self.addr = _k32.VirtualAllocEx(self.h, None, size, 0x3000, 0x04)  # MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE
        if not self.addr:
            _k32.CloseHandle(self.h)
            raise OSError(f'VirtualAllocEx failed: {ctypes.get_last_error()}')

    def write(self, off, data):
        b = ctypes.create_string_buffer(data, len(data))
        _k32.WriteProcessMemory(self.h, self.addr + off, b, len(data), None)

    def read(self, off, n):
        b = ctypes.create_string_buffer(max(1, n))
        _k32.ReadProcessMemory(self.h, self.addr + off, b, n, None)
        return b.raw[:n]

    def __enter__(self):
        return self

    def __exit__(self, *a):
        _k32.VirtualFreeEx(self.h, self.addr, 0, 0x8000)  # MEM_RELEASE
        _k32.CloseHandle(self.h)


def _lvitem(mask=0, item=0, sub=0, state=0, state_mask=0, text_ptr=0, text_max=0):
    """LVITEMW as a 32-bit process sees it (60 bytes; lParam at offset 32)."""
    return struct.pack('<IiiIIIii', mask, item, sub, state, state_mask, text_ptr, text_max, 0) + bytes(28)


def lv_rows(pid, lv, cols=5):
    """[(lParam, selected, [column texts])] of a report list view in a game copy."""
    out = []
    n = _u32.SendMessageW(lv, LVM_GETITEMCOUNT, 0, 0)
    with RemoteBuf(pid) as rb:
        for i in range(max(0, n)):
            rb.write(0, _lvitem(mask=0x4, item=i))  # LVIF_PARAM
            _u32.SendMessageW(lv, LVM_GETITEMW, 0, rb.addr)
            param = struct.unpack('<i', rb.read(32, 4))[0]
            sel = bool(_u32.SendMessageW(lv, LVM_GETITEMSTATE, i, 0x2) & 0x2)  # LVIS_SELECTED
            texts = []
            for c in range(cols):
                rb.write(0, _lvitem(item=i, sub=c, text_ptr=rb.addr + 64, text_max=256))
                k = _u32.SendMessageW(lv, LVM_GETITEMTEXTW, i, rb.addr)
                texts.append(rb.read(64, 2 * max(0, k)).decode('utf-16-le', 'replace'))
            out.append((param, sel, texts))
    return out


def lv_select(pid, lv, i):
    """Select and focus row i (LVM_SETITEMSTATE: the list notifies its parent with LVN_ITEMCHANGED, as for a click)."""
    with RemoteBuf(pid) as rb:
        rb.write(0, _lvitem(state=0x3, state_mask=0x3))  # LVIS_SELECTED | LVIS_FOCUSED
        return _u32.SendMessageW(lv, LVM_SETITEMSTATE, i, rb.addr)


def capture_treaty(pid, who):
    """Record the game's peace treaty dialog (TdlgTotalLog captioned "Peace treaty") and its text before
    answer_dialogs presses OK. The game reuses the form: a new text in the window is a new treaty."""
    for h in windows_of(pid, 'TdlgTotalLog'):
        key = (pid, h)
        if not u.IsWindowVisible(h):
            _treaty_open.pop(key, None)
            continue
        if e2e_test.window_text(h) != TREATY_CAPTION:
            continue
        rich = next((c for c in children(h) if 'RichEdit' in e2e_test.class_name(c)), None)
        txt = text(rich).replace('\r\n', '\n').replace('\r', '\n').strip() if rich else ''
        if _treaty_open.get(key) != txt:
            _treaty_open[key] = txt
            TREATY_SEEN.append(dict(who=who, t=time.time(), text=txt))
            log(f'  {who} shows the game\'s "{TREATY_CAPTION}" dialog: {txt!r}')


def treaty_dialog_open(pid):
    return [h for h in windows_of(pid, 'TdlgTotalLog') if u.IsWindowVisible(h)]


def wait_until(fn, pred, timeout, step=0.4):
    """Poll fn() until pred(value) holds or the timeout passes; returns the last value."""
    end = time.time() + timeout
    while True:
        v = fn()
        if pred(v) or time.time() > end:
            return v
        time.sleep(step)


def wait_chat(pid, line, before, timeout=8):
    """True once the Multiplayer window's chat of a copy holds `line` more often than in `before`."""
    n0 = before.count(line)
    return wait_until(lambda: chat(pid), lambda t: t.count(line) > n0, timeout).count(line) > n0


def new_chat_missing(before, after, lines, pattern=None):
    """The expected lines (and regex) that did not appear again since `before` (counts, not offsets: the chat log
    drops its oldest lines when it grows long)."""
    miss = [x for x in lines if after.count(x) <= before.count(x)]
    if pattern and len(re.findall(pattern, after)) <= len(re.findall(pattern, before)):
        miss.append(pattern)
    return miss


def nation_table(port=H):
    """{index: dict(name, funds, ships)} from a copy's `nations` (the host's by default)."""
    out = {}
    for p in items(cmd(port, 'nations')):
        f = p.split(':')
        if len(f) >= 4 and f[0].isdigit():
            out[int(f[0])] = dict(name=':'.join(f[1:-2]), funds=int(f[-2].split('=')[1]), ships=int(f[-1].split('=')[1]))
    return out


def poss_list(port, query):
    """`possessions <n>` / `claimable <w> <l>`: 'ok N name~area~value[~oil]; ...' -> [dict(name, area, value, oil)]."""
    v = value(cmd(port, query))
    if v is None:
        return None
    cnt, _, rest = v.partition(' ')
    out = []
    for e in rest.split('; '):
        e = e.strip().rstrip(';').strip()
        if e:
            f = e.split('~')
            out.append(dict(name=f[0], area=f[1] if len(f) > 1 else '', oil=len(f) > 3 and f[3] == 'oil',
                            value=int(f[2]) if len(f) > 2 and f[2].lstrip('-').isdigit() else 0))
    return out if cnt.isdigit() and int(cnt) == len(out) else None


def pnames(lst):
    return sorted(p['name'] for p in lst or [])


def terms(a, b):
    """Host `terms a b`: 'agreed winner=W loser=L level=N budget=B claims=x|y|' or 'outlook winner=W level=N vp=a:b'."""
    v = value(cmd(H, f'terms {a} {b}')) or ''
    kind, _, rest = v.partition(' ')
    d = dict(kind=kind, raw=v)
    head, sep, claims = rest.partition('claims=')
    for k, x in re.findall(r'(\w+)=(\S*)', head):
        d[k] = x
    if sep:
        d['claims'] = [c for c in claims.split('|') if c]
    return d


def warvp(a, b, va=None, vb=None):
    """Host `warvp a b [va vb]` -> dict(a=points of a, b=points of b, scale=G) or None."""
    m = re.match(rf'^ok {a}:(-?\d+) {b}:(-?\d+) scale=(-?\d+)',
                 cmd(H, f'warvp {a} {b}' + (f' {va} {vb}' if va is not None else '')))
    return dict(a=int(m[1]), b=int(m[2]), scale=int(m[3])) if m else None


def session_file():
    try:
        with open(os.path.join(SAVE, f'Game{HOST_SLOT}', SESSION_FILE), 'rb') as f:
            txt = f.read().decode('utf-8', errors='replace')
    except OSError:
        return {}
    return dict(ln.split('=', 1) for ln in txt.splitlines() if '=' in ln)


def treaty_loss(p, level):
    """The loser's Prestige after a treaty (the game's PrestigeWarLoss applied to the loser): ProportionalPrestigeLoss(10)
    = P div 10 when P >= 20, otherwise 1 (not below 0); a major defeat 1 more, and 1 more again while above 20."""
    p = max(0, p - (p // 10 if p >= 20 else 1))
    if level >= 2:
        p -= 1
        if p > 20:
            p -= 1
    return p


def greedy(options, budget):
    """The automatic pick of the mod (and a sensible claim): most valuable first, every one that still fits."""
    pick, used = [], 0
    for p in sorted(options, key=lambda p: -p['value']):
        if used + p['value'] <= budget:
            pick.append(p)
            used += p['value']
    return pick, used


def over_budget(options, budget):
    """The most valuable possessions until their worth exceeds the budget, or None if all of them fit."""
    pick, used = [], 0
    for p in sorted(options, key=lambda p: -p['value']):
        pick.append(p)
        used += p['value']
        if used > budget:
            return pick
    return None


def client_request(line, action, target, timeout=8):
    """A diplomacy command on the client (`diplo ...`, `claimland ...`). The client's bridge answers as soon as the
    request is sent, so wait until the host handled it (`diplomacy request 1 <action> <target> accepted`) before
    anything that depends on it (applydiplo, host terms). 'ok' only if the host accepted it."""
    mark = log_mark(H)
    r = cmd(C, line)
    if r != 'ok':
        return r
    if wait_log(H, mark, rf'diplomacy request 1 {action} {target} accepted', timeout):
        return 'ok'
    return f'ok, but the host did not accept it within {timeout} s'


def apply_diplo(ctx, until, timeout=20):
    """The host's month-end diplomacy step now (`applydiplo`, async on the bridge): the host's dialogs are answered
    until until() holds. Returns the host log written meanwhile."""
    mark = log_mark(H)
    cmd(H, 'applydiplo')
    end = time.time() + timeout
    time.sleep(0.5)
    while time.time() < end:
        answer_dialogs(ctx.hp, 'host')
        if until():
            break
        time.sleep(0.5)
    drain(ctx.hp, 1)
    return log_since(H, mark)


def claim_state():
    """Client `claimui state` (or toggle reply) -> dict(open, target, budget, mp, checked, sum, rows) or None."""
    r = cmd(C, 'claimui state')
    return parse_claim(r)


def parse_claim(r):
    m = re.match(r'^ok open=(\d)(?: target=(-?\d+) budget=(\d+) mpEnabled=(\d) checked=([\d,]*) sum=(.*?) rows=(.*))?$',
                 r or '', re.S)
    if not m:
        return None
    if m[1] == '0':
        return dict(open=False)
    rows = []
    for e in m[7].split('; '):
        e = e.strip().rstrip(';')
        if e:
            i, rest = e.split(':', 1)
            name, v = rest.rsplit(':', 1)
            rows.append(dict(i=int(i), name=name, value=int(v)))
    return dict(open=True, target=int(m[2]), budget=int(m[3]), mp=m[4] == '1',
                checked=[int(x) for x in m[5].split(',') if x], sum=m[6].strip(), rows=rows)


def claim_windows(pid):
    return [h for h in windows_of(pid, 'RTW3MP_Claim') if u.IsWindowVisible(h)]


def open_claim_window(pid):
    """Post a click on the Multiplayer window's "Claim land..." button (IDC_DPEACE) and wait for the claim window."""
    u.PostMessageW(mp_ctl(pid, 'peace'), 0x00F5, 0, 0)  # BM_CLICK
    return wait_until(lambda: claim_windows(pid), bool, 5)[:1] and claim_windows(pid)[0]


def claim_button(cw, key):
    """What a click on one of the claim window's buttons sends it: WM_COMMAND, BN_CLICKED | id, the button."""
    u.PostMessageW(cw, 0x0111, CLAIM_IDC[key] & 0xFFFF, u.GetDlgItem(cw, CLAIM_IDC[key]))


def claim_closed(ctx):
    """The claim window is gone and the Multiplayer window enabled again (a refused claim would show a message box,
    which is closed and reported)."""
    gone = wait_until(lambda: claim_windows(ctx.cp), lambda w: not w, 5) == []
    boxes = [d for d in windows_of(ctx.cp, '#32770') if text(d) == 'Claim land' and u.IsWindowVisible(d)]
    for d in boxes:
        u.PostMessageW(d, 0x0010, 0, 0)  # WM_CLOSE
    return gone and bool(u.IsWindowEnabled(mp_window(ctx.cp))), boxes


def treaty_text(dlg_text, shown, head, ceded, ships, repar):
    """A "Peace treaty" dialog against the log: heading, ceded possessions (exact lines, or prefixes such as "X cedes
    no possessions"), the reparation ships, then the reparations line. A hand-over may give several lines (the game's
    text is split at its line break: "<ship> handed over to <winner> as war reparation." / "She is renamed ..."), so
    the line count comes from the dialog and must equal the log's `showing a peace treaty (n lines)`. Returns
    (ok, lines, detail)."""
    lines = [ln.strip() for ln in (dlg_text or '').split('\n') if ln.strip()]
    m = re.search(r'\((\d+) lines\)', shown[0]) if shown else None
    n_log = int(m.group(1)) if m else None
    is_ceded = lambda ln: any(ln.startswith(c.rstrip('.')) for c in ceded)
    ship_lines = [ln for ln in lines[1:] if not is_ceded(ln) and ln not in repar]
    handed = [ln for ln in ship_lines if ' handed over to ' in ln]
    bad = [ln for ln in lines if not ln.endswith('.') or ln.endswith('..') or 'reparation She' in ln]
    ok = (n_log == len(lines) and lines[:1] == [head] and
          all(any(ln.startswith(c.rstrip('.')) for ln in lines) for c in ceded) and
          all(c in lines for c in ceded if c.endswith('.')) and all(r in lines for r in repar) and
          len(handed) == ships and all(ln.endswith(' as war reparation.') for ln in handed) and not bad and
          len(ship_lines) >= ships)
    return ok, lines, (f'log {n_log} lines, dialog {len(lines)}; {len(handed)} hand-over(s) of {ships}, '
                       f'{len(ship_lines) - len(handed)} more ship line(s); bad endings {bad}; lines {lines}')


def treaty_lines(txt, winner, loser):
    return [m for m in re.findall(TREATY_RE, txt) if int(m[0]) == winner and int(m[1]) == loser]


def nation_view(nations):
    """Host values a treaty changes: possessions, ships, funds, Prestige, BaseResources of some nations."""
    nt = nation_table()
    return {n: dict(poss=pnames(poss_list(H, f'possessions {n}')), ships=nt[n]['ships'], funds=nt[n]['funds'],
                    P=nint(H, n, 'Prestige'), R=nint(H, n, 'BaseResources'), VP=nint(H, n, 'VP')) for n in nations}


def fmt_view(v):
    return ' | '.join(f'{n}: {len(x["poss"])} poss, {x["ships"]} ships, funds {x["funds"]}, Prestige {x["P"]}, '
                      f'BaseResources {x["R"]}, VP {x["VP"]}' for n, x in v.items())


def scenario_treaty(ctx):
    """0.2.5: peace terms of wars between players. War 1-2 (client vs the stand-in), a major victory for the client set
    with `warvp`; peace offered and accepted freezes the terms; claims are validated on the client and the host; the
    client's "Claim land..." window (real button, posted messages, `claimui`) claims land within the budget; calling
    the peace off erases the terms; the processed month applies the treaty (possessions, ships, prestige, reparations,
    chat, the client's treaty dialog) and the war score lines of that month are checked. Then, with `applydiplo` after
    the last processed month (no later merge can overwrite the host's values with the client's planning copy): an
    automatic minor victory of nation 2, a host-vs-client major victory (DoPeace, the host's treaty dialog) and a
    compromise peace."""
    hp, cp = ctx.hp, ctx.cp
    W, Lo = 1, STANDIN

    # a) startup
    avail = {}
    for port, who in ((H, 'host'), (C, 'client')):
        ln = log_lines(log_since(port), r'Peace treaties: ')
        ok = bool(ln) and re.search(r'Peace treaties: ok \(removeShip=(?!0+ )\w+ scale=(?!0+ )\w+ slCreate=(?!0+ )\w+ '
                                    r'battleHooks=1/1/1\)', ln[0])
        avail[who] = bool(ok)
        check(f'a) {who} log at startup: "Peace treaties: ok (removeShip=.. scale=.. slCreate=.. battleHooks=1/1/1)"',
              ok, [x.split('] ', 1)[-1] for x in ln[:1]] or 'no line')
    if not all(avail.values()):  # every further step needs the feature: don't keep the game open for timeouts
        check('treaty: peace treaties available on host and client (needed by every further check)', False,
              f'{avail}: the remaining checks (b-k) were not run')
        return
    cmd(H, 'mpwindow')  # the host's chat log is checked too
    cmd(C, 'mpwindow')
    time.sleep(1)
    nt = nation_table()
    n0, n1, n2 = nt[0]['name'], nt[W]['name'], nt[Lo]['name']
    r01, r12 = rel(0, 1), rel(1, 2)
    check('start: nations 1 and 2 at peace, the host at war with nobody',
          r01['war'] == '0' and r12['war'] == '0' and r01['hostWars'] == '0', f'0-1 {r01} | 1-2 {r12}')
    p1, p2 = poss_list(H, 'possessions 1'), poss_list(H, 'possessions 2')
    c12, c21, c01 = (poss_list(H, q) for q in ('claimable 1 2', 'claimable 2 1', 'claimable 0 1'))
    log(f'  nations: 0 {n0}, 1 {n1}, 2 {n2}')
    log(f'  possessions 1: {[(p["name"], p["value"]) for p in p1 or []]}')
    log(f'  possessions 2: {[(p["name"], p["value"]) for p in p2 or []]}')
    for q, lst in (('1 2', c12), ('2 1', c21), ('0 1', c01)):
        log(f'  claimable {q}: {[(p["name"], p["area"], p["value"]) for p in lst or []]} '
            f'(worth {sum(p["value"] for p in lst or [])})')
    check('claimable 1 2 / 2 1 / 0 1: only possessions of the loser, each worth less than 11',
          None not in (p1, p2, c12, c21, c01) and set(pnames(c12)) <= set(pnames(p2)) and
          set(pnames(c21)) <= set(pnames(p1)) and set(pnames(c01)) <= set(pnames(p1)) and
          all(p['value'] < 11 for p in (c12 or []) + (c21 or []) + (c01 or [])),
          f'{len(c12 or [])}/{len(c21 or [])}/{len(c01 or [])} claimable')
    c12, c21, c01, p1, p2 = c12 or [], c21 or [], c01 or [], p1 or [], p2 or []

    # b) war 1-2 and a major victory for 1 by its war score
    before = chat(cp)
    rc = client_request(f'diplo war {Lo}', 'war', Lo)
    hl = apply_diplo(ctx, lambda: rel(1, 2).get('war') == '1')
    r12 = rel(1, 2)
    check('b) client declares war on nation 2; applydiplo starts it (rel 1 2 war=1, "diplomacy: war 1-2")',
          rc == 'ok' and r12['war'] == '1' and log_lines(hl, r'diplomacy: war 1-2'), f'{rc}; {r12}')
    line = f'War: {n1} has declared war on {n2}!'
    check(f'b) chat (client): "{line}"', wait_chat(cp, line, before), chat(cp)[-300:])
    v0 = warvp(1, 2)
    check('b) the new war starts with a war score of 0:0 (warvp 1 2)', v0 and v0['a'] == 0 and v0['b'] == 0, v0)
    g = v0['scale'] if v0 else 0
    A, B = max(900, 100 * g + 500), 100
    v1, t = warvp(1, 2, A, B), terms(1, 2)
    check(f'b) warvp 1 2 {A} {B} (scale G={g}): terms outlook winner=1 level=2 vp={A}:{B}',
          v1 and (v1['a'], v1['b']) == (A, B) and t['kind'] == 'outlook' and t.get('winner') == '1' and
          t.get('level') == '2' and t.get('vp') == f'{A}:{B}', f'{v1} | {t["raw"]}')
    r = wait_until(lambda: row(C, Lo), lambda r: r and r['vp'] == f'{A}:{B}', 6)
    check(f'b) client diploview row 2: At war, vp={A}:{B}, outlook "you are winning: a peace now would be a major '
          f'victory"', r and r['relation'] == 'At war' and r['vp'] == f'{A}:{B}' and
          r['outlook'] == 'you are winning: a peace now would be a major victory', r)

    # c) peace offered by the client, accepted by nation 2: the terms are frozen
    before = chat(cp)
    rc = client_request(f'diplo peace {Lo}', 'peace', Lo)
    got = wait_chat(cp, f'{n1} offers peace to {n2}.', before)
    rh = cmd(H, f'diploas {Lo} peace {W}')
    line = (f'{n2} accepts the peace offered by {n1}: {n1} wins a major victory and may claim {n2}\'s land worth up '
            f'to 8. It takes effect when this month is processed.')
    okc = wait_chat(cp, line, before)
    t = terms(1, 2)
    check('c) client offers peace, nation 2 accepts: terms agreed winner=1 loser=2 level=2 budget=8, no claims',
          rc == 'ok' and got and rh == 'ok' and t['kind'] == 'agreed' and (t.get('winner'), t.get('loser'),
          t.get('level'), t.get('budget')) == ('1', '2', '2', '8') and t.get('claims') == [], f'{rc}; {rh}; {t["raw"]}')
    check(f'c) chat on both sides: "{line[:60]}... worth up to 8. ..."', okc and line in chat(hp),
          f'client {okc}, host {line in chat(hp)}')
    r = wait_until(lambda: row(C, Lo), lambda r: r and (r['claim'] or '').startswith('8:'), 6)
    check(f'c) client diploview row 2: claim=8:{len(c12)}:0, pending "peace agreed: you win (major), claim land '
          f'(worth up to 8)"', r and r['claim'] == f'8:{len(c12)}:0' and
          r['pending'] == 'peace agreed: you win (major), claim land (worth up to 8)', r)
    rc = cmd(C, f'diplo peace {Lo}')
    check('c) another peace offer is refused (client)', rc.startswith('error'), rc)
    sf = session_file()
    check(f'c) {SESSION_FILE} keeps the war score and the agreed terms (warVP 1-2:{A}:{B}, treaties 1-2 winner 1 '
          f'level 2, diplomacyDate)', f'1-2:{A}:{B},' in sf.get('warVP', '') and
          sf.get('treaties', '').startswith('1\x1f2\x1f1\x1f2\x1f2') and sf.get('diplomacyDate'),
          {k: sf.get(k) for k in ('warVP', 'treaties', 'diplomacyDate')})

    # d) claim validation (client first, then the host's own check with diploas)
    big = next((p for p in p2 if p['value'] >= 11), None)
    bad = [('an unknown name', 'Atlantis'), ('a possession of the winner', p1[0]['name'] if p1 else 'Atlantis')]
    if big:
        bad.append(('a possession of nation 2 worth 11 or more', big['name']))
    for what, name in bad:
        rc, rh = cmd(C, f'claimland {Lo} {name}'), cmd(H, f'diploas {W} claim {Lo} {name}')
        check(f'd) claiming {what} ("{name}") is refused on the client and on the host ("cannot be claimed")',
              rc.startswith('error') and 'cannot be claimed' in rc and rh.startswith('error') and
              'cannot be claimed' in rh, f'client {rc} | host {rh}')
    over = over_budget(c12, 8)
    if over:
        worth = sum(p['value'] for p in over)
        names = '|'.join(p['name'] for p in over)
        rc, rh = cmd(C, f'claimland {Lo} {names}'), cmd(H, f'diploas {W} claim {Lo} {names}')
        want = f'worth {worth}; you may claim up to 8'
        check(f'd) claiming land worth {worth} (> 8) is refused on the client and on the host',
              rc.startswith('error') and want in rc and rh.startswith('error') and want in rh,
              f'client {rc} | host {rh}')
    else:
        log(f'  SKIP d) over-budget claim: the claimable possessions are worth only '
            f'{sum(p["value"] for p in c12)} together')
    rh = cmd(H, f'diploas {Lo} claim {W} {c21[0]["name"] if c21 else "Atlantis"}')
    check('d) the loser cannot claim (host: "Only the winner of the war can claim possessions.")',
          rh == 'error Only the winner of the war can claim possessions.', rh)
    t = terms(1, 2)
    check('d) the refused claims changed nothing (terms claims empty)', t.get('claims') == [], t['raw'])
    first = next((p for p in sorted(c12, key=lambda p: -p['value']) if p['value'] <= 8), None)
    if first:
        before = chat(cp)
        line = f'{n1} claims {first["name"]} from {n2}.'
        rc = client_request(f'claimland {Lo} {first["name"]}', 'claim', Lo)
        okc = wait_chat(cp, line, before)
        t = terms(1, 2)
        r = wait_until(lambda: row(C, Lo), lambda r: r and (r['claim'] or '').endswith(':1'), 6)
        check(f'd) a valid claim ({first["name"]}, worth {first["value"]}) is accepted: terms claims, chat "{line}", '
              f'client diploview claim=8:{len(c12)}:1 "land claimed: {first["value"]} of 8"',
              rc == 'ok' and okc and t.get('claims') == [first['name']] and r and r['claim'] == f'8:{len(c12)}:1' and
              r['pending'] == f'peace agreed: you win (major), land claimed: {first["value"]} of 8',
              f'{rc}; chat {okc}; {t["raw"]}; {r}')
    else:
        before = chat(cp)
        line = f'{n1} leaves the choice of land to the peace conference.'
        rc = client_request(f'claimland {Lo}', 'claim', Lo)
        check('d) no claimable land next to nation 1: an empty claim is accepted ("leaves the choice of land to the '
              'peace conference")', rc == 'ok' and wait_chat(cp, line, before), rc)

    # f) calling the agreed peace off erases the terms; agreed again the other way round (2 offers, 1 accepts)
    before = chat(cp)
    rc = client_request(f'diplo cancel {Lo}', 'cancel', Lo)
    okc = wait_chat(cp, f'{n1} calls off the agreed peace with {n2}.', before)
    t = terms(1, 2)
    r = wait_until(lambda: row(C, Lo), lambda r: r and r['pending'] == '' and r['claim'] == '0:0:0', 6)
    sf = session_file()
    check('f) the client calls off the agreed peace: terms erased (terms 1 2 = the outlook, claims gone, no treaty in '
          'the session file), chat line, client row: nothing pending, claim=0:0:0, peace can be proposed again',
          rc == 'ok' and okc and t['kind'] == 'outlook' and t.get('vp') == f'{A}:{B}' and r and r['claim'] == '0:0:0'
          and 'P' in r['flags'] and sf.get('treaties', 'x') == '', f'{rc}; chat {okc}; {t["raw"]}; {r}; '
                                                                   f'treaties {sf.get("treaties")!r}')
    before = chat(cp)
    rh = cmd(H, f'diploas {Lo} peace {W}')
    r = wait_until(lambda: row(C, Lo), lambda r: r and 'p' in r['flags'], 6)
    rc = client_request(f'diplo peace {Lo}', 'peace', Lo)
    line = (f'{n1} accepts the peace offered by {n2}: {n1} wins a major victory and may claim {n2}\'s land worth up '
            f'to 8.')
    okc = wait_chat(cp, line, before)
    t = terms(1, 2)
    check('f) peace agreed again (nation 2 offers, the client accepts): winner 1, level 2, budget 8, the old claim is '
          'not back', rh == 'ok' and r and 'p' in r['flags'] and rc == 'ok' and okc and t['kind'] == 'agreed' and
          (t.get('winner'), t.get('level'), t.get('budget')) == ('1', '2', '8') and t.get('claims') == [],
          f'{rh}; {r}; {rc}; chat {okc}; {t["raw"]}')
    if first:
        client_request(f'claimland {Lo} {first["name"]}', 'claim', Lo)
        wait_until(lambda: row(C, Lo), lambda r: r and (r['claim'] or '').endswith(':1'), 6)

    # e) the client's "Claim land..." window (real button and list view, posted messages; `claimui` toggles a row
    # through the list view's own notification)
    cmd(C, 'mpwindow')
    time.sleep(0.5)
    mpw, lvh, btn = mp_window(cp), mp_ctl(cp, 'list'), mp_ctl(cp, 'peace')
    rows = lv_rows(cp, lvh)
    idx = next((i for i, (prm, _, _) in enumerate(rows) if prm == Lo), None)
    if idx is not None:
        lv_select(cp, lvh, idx)
    bt = wait_until(lambda: text(btn), lambda s: s == 'Claim land...', 4)
    rows = lv_rows(cp, lvh)
    sel = [prm for prm, s, _ in rows if s]
    check('e) client window: nation 2 selected in the Diplomacy list (LVM_SETITEMSTATE), the button (1084) reads '
          '"Claim land..." and is enabled', idx is not None and sel == [Lo] and bt == 'Claim land...' and
          u.IsWindowEnabled(btn), f'rows {rows}, selected {sel}, button "{bt}"')
    cols = rows[idx][2] if idx is not None else []
    label = text(mp_ctl(cp, 'label'))
    claimed = f'land claimed: {first["value"]} of 8' if first else 'claim land (worth up to 8)'
    check(f'e) Diplomacy list row: Tension column "VP {A}:{B}", Pending "peace agreed: you win (major), {claimed}"; '
          f'label "Diplomacy: you are winning: a peace now would be a major victory"',
          len(cols) == 5 and cols[0] == n2 and cols[3] == f'VP {A}:{B}' and
          cols[4] == f'peace agreed: you win (major), {claimed}' and
          label == 'Diplomacy: you are winning: a peace now would be a major victory', f'{cols} | label "{label}"')

    pre = [i for i, p in enumerate(c12) if first and p['name'] == first['name']]
    pre_sum = f'Selected: {first["value"] if first else 0} of 8'
    # open 1: the claimed row pre-checked, the Multiplayer window disabled; untick, then Cancel
    cw = open_claim_window(cp)
    st = claim_state()
    check('e) "Claim land..." opens the RTW3MP_Claim window, owned by the Multiplayer window, which is disabled '
          '(claimui mpEnabled=0)', cw and _u32.GetWindow(cw, 4) == mpw and not u.IsWindowEnabled(mpw) and st and
          st['open'] and not st['mp'], f'window {cw}, owner {cw and _u32.GetWindow(cw, 4)} (MP {mpw}), MP enabled '
                                       f'{u.IsWindowEnabled(mpw)}, {st}')
    lbl = text(u.GetDlgItem(cw, CLAIM_IDC['label'])) if cw else ''
    check(f'e) claim window: target 2, budget 8, one row per claimable possession (claimable 1 2), the claimed one '
          f'pre-checked, sum label "{pre_sum}"', st and st.get('open') and st['target'] == Lo and st['budget'] == 8
          and [x['name'] for x in st['rows']] == [p['name'] for p in c12] and st['checked'] == pre and
          st['sum'] == pre_sum and text(u.GetDlgItem(cw, CLAIM_IDC['sum'])) == pre_sum and
          (lbl.startswith(f'Peace with {n2}: you may claim possessions worth up to 8.') if c12 else
           lbl.startswith(f'None of {n2}\'s possessions lie next to yours')), f'{st} | label "{lbl}"')
    if pre:
        s2 = parse_claim(cmd(C, f'claimui toggle {pre[0]}'))
        check('e) unticking the claimed row: sum "Selected: 0 of 8"', s2 and s2['checked'] == [] and
              s2['sum'] == 'Selected: 0 of 8', s2)
    if cw:
        claim_button(cw, 'cancel')
    ok, boxes = claim_closed(ctx)
    t = terms(1, 2)
    check('e) Cancel (1095) closes the window, enables the Multiplayer window and sends nothing (claims unchanged)',
          cw and ok and not boxes and t.get('claims') == ([first['name']] if first else []), f'{ok} {boxes} {t["raw"]}')
    # open 2: WM_CLOSE
    cw = open_claim_window(cp)
    st = claim_state()
    if cw:
        u.PostMessageW(cw, 0x0010, 0, 0)  # WM_CLOSE (the caption's [x])
    ok, boxes = claim_closed(ctx)
    t = terms(1, 2)
    check('e) reopened: the claim still pre-checked; WM_CLOSE closes the window and enables the Multiplayer window, '
          'claims unchanged', cw and st and st.get('checked') == pre and ok and not boxes and
          t.get('claims') == ([first['name']] if first else []), f'{st} | {ok} {boxes} {t["raw"]}')
    # open 3: "Let the conference choose" withdraws the claims
    before = chat(cp)
    cw = open_claim_window(cp)
    if cw:
        claim_button(cw, 'auto')
    ok, boxes = claim_closed(ctx)
    line = f'{n1} leaves the choice of land to the peace conference.'
    okc = wait_chat(cp, line, before)
    t = terms(1, 2)
    check(f'e) "Let the conference choose" (1094): window closed, claims withdrawn (terms claims empty), chat "{line}"',
          cw and ok and not boxes and okc and t.get('claims') == [], f'{ok} {boxes} chat {okc} {t["raw"]}')
    # open 4: tick within the budget, a tick past it is refused, then "Claim selected"
    cw = open_claim_window(cp)
    st = claim_state()
    pick, used = greedy(c12, 8)
    ticks_ok, steps = True, []
    for p in pick:
        i = next(k for k, q in enumerate(c12) if q['name'] == p['name'])
        s2 = parse_claim(cmd(C, f'claimui toggle {i}'))
        steps.append((i, s2 and s2['checked'], s2 and s2['sum']))
        ticks_ok = ticks_ok and bool(s2) and i in s2['checked']
    want_idx = sorted(next(k for k, q in enumerate(c12) if q['name'] == p['name']) for p in pick)
    s3 = claim_state()
    check(f'e) ticking the most valuable rows within the budget: checked {want_idx}, sum "Selected: {used} of 8"',
          cw and st and st.get('checked') == [] and ticks_ok and s3 and s3['checked'] == want_idx and
          s3['sum'] == f'Selected: {used} of 8', f'start {st}; steps {steps}; now {s3}')
    extra = next((k for k, q in enumerate(c12) if k not in want_idx and q['value'] > 8 - used), None)
    if extra is not None:
        s4 = parse_claim(cmd(C, f'claimui toggle {extra}'))
        sumtxt = text(u.GetDlgItem(cw, CLAIM_IDC['sum'])) if cw else ''
        check(f'e) ticking a row past the budget ({c12[extra]["name"]}, worth {c12[extra]["value"]}) unticks it again: '
              f'"Selected: {used} of 8   (over the limit)"', s4 and s4['checked'] == want_idx and
              s4['sum'] == f'Selected: {used} of 8   (over the limit)' and sumtxt == s4['sum'], f'{s4} | "{sumtxt}"')
    else:
        log(f'  SKIP e) over-the-limit tick: every claimable row fits into the budget ({used} of 8 ticked)')
    before = chat(cp)
    if cw:
        claim_button(cw, 'ok')
    ok, boxes = claim_closed(ctx)
    names = [p['name'] for p in c12 if p['name'] in {q['name'] for q in pick}]
    line = f'{n1} claims {", ".join(names)} from {n2}.' if names else \
        f'{n1} leaves the choice of land to the peace conference.'
    okc = wait_chat(cp, line, before)
    t = terms(1, 2)
    r = wait_until(lambda: row(C, Lo), lambda r: r and r['claim'] == f'8:{len(c12)}:{len(names)}', 6)
    check(f'e) "Claim selected" (1093): window closed, Multiplayer window enabled, host terms claims {names}, chat '
          f'"{line}", client diploview claim=8:{len(c12)}:{len(names)}',
          cw and ok and not boxes and okc and t.get('claims') == names and r and
          r['claim'] == f'8:{len(c12)}:{len(names)}', f'{ok} {boxes} chat {okc} {t["raw"]} {r}')

    # g) the processed month applies the treaty (claimed land, ships, prestige, reparations); the client's dialog
    claims_g = t.get('claims') or []
    bv = nation_view((0, 1, 2))
    cb1, cb2 = pnames(poss_list(C, 'possessions 1')), pnames(poss_list(C, 'possessions 2'))
    log(f'  before the month: {fmt_view(bv)}')
    hc0, cc0, seen0, t_month = chat(hp), chat(cp), len(TREATY_SEEN), time.time()
    run_month(ctx, 'treaty')
    hmark, cmark = ctx.month_marks
    end = time.time() + 20
    while time.time() < end and not [d for d in TREATY_SEEN[seen0:] if d['who'] == 'client']:
        answer_dialogs(cp, 'client')
        time.sleep(0.5)
    drain(cp, 1, 'client')
    hlog, clog = log_since(H, hmark), log_since(C, cmark)
    av = nation_view((0, 1, 2))
    log(f'  after the month: {fmt_view(av)}')
    tl = treaty_lines(hlog, 1, 2)
    k, mode, s, pr, res = (int(tl[0][3]), tl[0][4], int(tl[0][5]), int(tl[0][6]), int(tl[0][7])) if tl else \
        (None, None, None, None, None)
    rmax = min(500, max(0, (bv[2]['R'] or 0) // 2))
    check(f'g) host log: one "treaty 1 over 2 (level 2): {len(claims_g)} possession(s) '
          f'({"claimed" if claims_g else "automatic"}), s ship(s) (0-3), prestige +2, resources -{rmax}"',
          len(tl) == 1 and int(tl[0][2]) == 2 and k == len(claims_g) and
          mode == ('claimed' if claims_g else 'automatic') and 0 <= s <= 3 and pr == 2 and res == rmax,
          [x.split('] ', 1)[-1] for x in log_lines(hlog, r'treaty \d+ over')] or 'no line')
    r12 = rel(1, 2)
    r = row(C, Lo)
    check('g) the war is over: rel 1 2 war=0; client row 2: Peace, nothing pending, no claim',
          r12['war'] == '0' and r and r['relation'] == 'Peace' and r['pending'] == '' and r['claim'] == '0:0:0',
          f'{r12} | {r}')
    moved, gained = set(bv[2]['poss']) - set(av[2]['poss']), set(av[1]['poss']) - set(bv[1]['poss'])
    check(f'g) host: the claimed possessions {claims_g} moved from 2 to 1 (possessions 1/2), nothing else',
          moved == gained == set(claims_g) and len(av[1]['poss']) == len(bv[1]['poss']) + len(claims_g) and
          len(av[2]['poss']) == len(bv[2]['poss']) - len(claims_g), f'moved {sorted(moved)}, gained {sorted(gained)}')
    ca1, ca2 = pnames(poss_list(C, 'possessions 1')), pnames(poss_list(C, 'possessions 2'))
    check('g) client (its new month): possessions 1/2 as on the host', ca1 == av[1]['poss'] and ca2 == av[2]['poss']
          and set(ca1) - set(cb1) == set(claims_g), f'client gained {sorted(set(ca1) - set(cb1))}, lost '
                                                    f'{sorted(set(cb2) - set(ca2))}')
    head = f'Peace treaty between {n1} and {n2}: {n1} wins a major victory.'
    ceded = [f'{x} is ceded from {n2} to {n1}.' for x in sorted(moved)] or [f'{n2} cedes no possessions']
    repar = [f'{n2} pays reparations (base resources -{res}).'] if res else []
    end_re = rf'Peace: ({re.escape(n1)}|{re.escape(n2)}) and ({re.escape(n1)}|{re.escape(n2)}) have ended their war ' \
             rf'\({re.escape(n1)} victorious\)\.'
    hc, cc = chat(hp), wait_month_chat(cp, cc0)
    miss = {who: new_chat_missing(c0, c, [head] + ceded + repar, end_re)
            for who, c0, c in (('host', hc0, hc), ('client', cc0, cc))}
    check('g) chat on both sides: the treaty (head, ceded possessions, reparations) and "Peace: ... (<1> victorious)."',
          not miss['host'] and not miss['client'], miss)
    shown = log_lines(clog, r'showing a peace treaty \((\d+) lines\)')
    dlg = [d for d in TREATY_SEEN[seen0:] if d['who'] == 'client']
    tok, tlines, tdet = treaty_text(dlg[0]['text'] if dlg else '', shown, head, ceded, s or 0, repar)
    miss = {who: new_chat_missing(c0, c, tlines) for who, c0, c in (('host', hc0, hc), ('client', cc0, cc))}
    check('g) client: "showing a peace treaty (n lines)" once; the game\'s "Peace treaty" dialog: heading, ceded '
          'possessions, every hand-over as "... as war reparation." plus its own sentences, reparations, n = the '
          'dialog\'s lines, each ending with one period; the same lines in both chats; answered',
          len(shown) == 1 and len(dlg) == 1 and tok and not miss['host'] and not miss['client'] and
          not treaty_dialog_open(cp), f'{[x.split("] ", 1)[-1] for x in shown]}; {tdet}; chat missing {miss}')
    check('g) host (not a party): no treaty dialog', not log_lines(hlog, r'showing a peace treaty') and
          not [d for d in TREATY_SEEN[seen0:] if d['who'] == 'host'], log_lines(hlog, r'showing a peace treaty'))
    log(f'  treaty month: nation 1 Prestige {bv[1]["P"]} -> {av[1]["P"]} (treaty +{pr}), nation 2 Prestige '
        f'{bv[2]["P"]} -> {av[2]["P"]} (war loss), host Prestige {bv[0]["P"]} -> {av[0]["P"]}, nation 2 BaseResources '
        f'{bv[2]["R"]} -> {av[2]["R"]} '
        f'(treaty -{res}), ships 1 {bv[1]["ships"]} -> {av[1]["ships"]}, 2 {bv[2]["ships"]} -> {av[2]["ships"]} '
        f'(treaty {s} handed over), funds 1 {bv[1]["funds"]} -> {av[1]["funds"]}, 2 {bv[2]["funds"]} -> '
        f'{av[2]["funds"]} (month changes included)')
    want2 = None if bv[2]['P'] is None else treaty_loss(bv[2]['P'], 2)
    host_events = [c for ts, who, c in ANSWERED if ts >= t_month and who == 'host' and c.startswith('TdlgEventAnswer')]
    host_ok = av[0]['P'] == bv[0]['P'] or bool(host_events)
    check(f'g) prestige: winner 1 +2, loser 2 {bv[2]["P"]} -> {want2} (P div 10 when >= 20 else 1, major -1, -1 more '
          f'above 20); the host (not a party) unchanged (unless it answered an event that month)',
          None not in (bv[1]['P'], av[1]['P'], want2) and av[1]['P'] == bv[1]['P'] + 2 and av[2]['P'] == want2 and
          host_ok, f'nation 1 {bv[1]["P"]} -> {av[1]["P"]}, nation 2 {bv[2]["P"]} -> {av[2]["P"]} (want {want2}), host '
                   f'{bv[0]["P"]} -> {av[0]["P"]}; host events {host_events}')
    # k) war score lines of the month's battles between 1 and 2: one per battle, totals consistent from A:B
    ws = [m for m in re.findall(r'war score (\d+)-(\d+): \+(\d+)/\+(\d+) -> (\d+):(\d+)', hlog) if m[:2] == ('1', '2')]
    cur, chain = (A, B), True
    for m in ws:
        nxt = (cur[0] + int(m[2]), cur[1] + int(m[3]))
        chain = chain and nxt == (int(m[4]), int(m[5]))
        cur = (int(m[4]), int(m[5]))
    seeded = log_lines(hlog, r'war score 1-2: first battle')
    log(f'  war score lines 1-2 in the treaty month: {len(ws)} {ws[:6]}' + ('' if ws else ' (no battle between 1 and '
                                                                            '2 this month)'))
    check('k) war score 1-2 in the processed month: every "+x/+y -> A:B" line adds up from the set score (no battle '
          'counted twice), no "first battle, already counted" line', chain and not seeded,
          f'{ws} | {seeded}')

    # h) automatic: nation 2 wins a minor victory, nobody claims (applydiplo: exact values, no month in between)
    before = chat(cp)
    rc = client_request(f'diplo war {Lo}', 'war', Lo)
    apply_diplo(ctx, lambda: rel(1, 2).get('war') == '1')
    v0 = warvp(1, 2)
    vl, vw = 100 * g + 300, 100 * g + 400
    v1, t = warvp(1, 2, vl, vw), terms(1, 2)
    r = wait_until(lambda: row(C, Lo), lambda r: r and r['vp'] == f'{vl}:{vw}', 6)
    check(f'h) new war 1-2 (score 0:0); warvp 1 2 {vl} {vw}: outlook winner=2 level=1; client row 2: "they are '
          f'winning: a peace now would be a minor defeat"', rc == 'ok' and rel(1, 2)['war'] == '1' and v0 and
          (v0['a'], v0['b']) == (0, 0) and t.get('winner') == '2' and t.get('level') == '1' and r and
          r['outlook'] == 'they are winning: a peace now would be a minor defeat', f'{rc} {v0} {t["raw"]} {r}')
    before = chat(cp)
    rh = cmd(H, f'diploas {Lo} peace {W}')
    r = wait_until(lambda: row(C, Lo), lambda r: r and 'p' in r['flags'], 6)
    rc = client_request(f'diplo peace {Lo}', 'peace', Lo)
    line = (f'{n1} accepts the peace offered by {n2}: {n2} wins a minor victory and may claim {n1}\'s land worth up '
            f'to 4.')
    okc = wait_chat(cp, line, before)
    t = terms(1, 2)
    r = wait_until(lambda: row(C, Lo), lambda r: r and r['pending'].startswith('peace agreed: they win'), 6)
    check('h) nation 2 offers peace, the client accepts: terms winner=2 loser=1 level=1 budget=4; chat; client row: '
          '"peace agreed: they win (minor), they may claim your land", claim=0:0:0',
          rh == 'ok' and rc == 'ok' and okc and (t.get('winner'), t.get('loser'), t.get('level'), t.get('budget')) ==
          ('2', '1', '1', '4') and r and r['pending'] == 'peace agreed: they win (minor), they may claim your land'
          and r['claim'] == '0:0:0', f'{rh} {rc} chat {okc} {t["raw"]} {r}')
    rc = cmd(C, f'claimland {Lo} {c21[0]["name"] if c21 else "Atlantis"}')
    check('h) the loser (client) cannot claim: "Only the winner of an agreed peace can claim possessions."',
          rc == 'error Only the winner of an agreed peace can claim possessions.', rc)
    c21n = poss_list(H, 'claimable 2 1') or []
    over = over_budget(c21n, 4)
    if over:
        worth = sum(p['value'] for p in over)
        rh = cmd(H, f'diploas {Lo} claim {W} {"|".join(p["name"] for p in over)}')
        check(f'h) the minor budget: nation 2 claiming land worth {worth} is refused ("you may claim up to 4")',
              rh.startswith('error') and f'worth {worth}; you may claim up to 4' in rh, rh)
    else:
        log(f'  SKIP h) minor over-budget claim: claimable 2 1 is worth {sum(p["value"] for p in c21n)}')
    auto, worth = greedy(c21n, 4)
    bv = nation_view((0, 1, 2))
    hm = log_mark(H)
    apply_diplo(ctx, lambda: rel(1, 2).get('war') == '0')
    time.sleep(1)
    hl = log_since(H, hm)
    av = nation_view((0, 1, 2))
    log(f'  automatic treaty: before {fmt_view(bv)}')
    log(f'  automatic treaty: after  {fmt_view(av)}')
    tl = treaty_lines(hl, 2, 1)
    k, mode, s, pr, res = (int(tl[0][3]), tl[0][4], int(tl[0][5]), int(tl[0][6]), int(tl[0][7])) if tl else \
        (None, None, None, None, None)
    rmax = min(100, max(0, (bv[1]['R'] or 0) // 2))
    check(f'h) applydiplo: one "treaty 2 over 1 (level 1): {len(auto)} possession(s) (automatic), s ship(s) (0-1), '
          f'prestige +1, resources -{rmax}" line (most valuable first within 4: worth {worth})',
          len(tl) == 1 and int(tl[0][2]) == 1 and k == len(auto) and mode == 'automatic' and 0 <= s <= 1 and
          pr == 1 and res == rmax, [x.split('] ', 1)[-1] for x in log_lines(hl, r'treaty \d+ over')] or 'no line')
    moved, gained = set(bv[1]['poss']) - set(av[1]['poss']), set(av[2]['poss']) - set(bv[2]['poss'])
    val = {p['name']: p['value'] for p in c21n}
    check(f'h) possessions: the automatic pick moved from 1 to 2 (claimable ones only, worth {worth})',
          moved == gained and len(moved) == len(auto) and moved <= set(val) and sum(val[x] for x in moved) == worth,
          f'moved {sorted(moved)}, gained {sorted(gained)}, expected e.g. {[p["name"] for p in auto]}')
    want1 = None if bv[1]['P'] is None else treaty_loss(bv[1]['P'], 1)
    check(f'h) exact: nation 2 Prestige +1, nation 1 Prestige {bv[1]["P"]} -> {want1} (minor: P div 10 when >= 20 else '
          f'1), nation 1 BaseResources -{res}, ships 1 -{s} / 2 +{s}',
          None not in (bv[2]['P'], av[2]['P'], bv[1]['R'], av[1]['R'], s, want1) and
          av[2]['P'] == bv[2]['P'] + 1 and av[1]['R'] == bv[1]['R'] - res and
          av[1]['ships'] == bv[1]['ships'] - s and av[2]['ships'] == bv[2]['ships'] + s and av[1]['P'] == want1,
          f'Prestige 2 {bv[2]["P"]}->{av[2]["P"]}, 1 {bv[1]["P"]}->{av[1]["P"]}; BaseResources 1 {bv[1]["R"]}->'
          f'{av[1]["R"]}; ships 1 {bv[1]["ships"]}->{av[1]["ships"]}, 2 {bv[2]["ships"]}->{av[2]["ships"]}')
    check('h) the host (not a party) keeps its Prestige: the loser\'s war loss must not reach the host\'s nation '
          '(TBuilderNation.PrestigeWarLoss runs form.ProportionalPrestigeLoss on PlayerNation)',
          bv[0]['P'] is not None and av[0]['P'] == bv[0]['P'], f'host Prestige {bv[0]["P"]} -> {av[0]["P"]}')
    head = f'Peace treaty between {n2} and {n1}: {n2} wins a minor victory.'
    end_re = rf'Peace: ({re.escape(n1)}|{re.escape(n2)}) and ({re.escape(n1)}|{re.escape(n2)}) have ended their war ' \
             rf'\({re.escape(n2)} victorious\)\.'
    time.sleep(1)
    cc = chat(cp)
    miss = new_chat_missing(before, cc, [head], end_re)
    check(f'h) war over (rel 1 2 war=0); chat (client) "{head}" and "Peace: ... ({n2} victorious)."; no treaty dialog '
          f'on the host (not a party)', rel(1, 2)['war'] == '0' and not miss and
          not log_lines(hl, r'showing a peace treaty'), f'missing {miss}; {cc[-400:]}')

    # i) host vs client: the host wins a major victory and claims land; DoPeace ends the host's war; its own dialog
    rh = cmd(H, f'diplo war {W}')
    apply_diplo(ctx, lambda: rel(0, 1).get('war') == '1')
    r01 = rel(0, 1)
    check('i) the host declares war on nation 1; applydiplo starts it (war=1, hostWars=1)',
          rh == 'ok' and r01['war'] == '1' and r01['hostWars'] == '1', f'{rh} {r01}')
    A0 = max(900, 100 * g + 500)
    v1, t = warvp(0, 1, A0, 100), terms(0, 1)
    r = wait_until(lambda: row(C, 0), lambda r: r and r['vp'] == f'100:{A0}', 6)
    check(f'i) warvp 0 1 {A0} 100: outlook winner=0 level=2; client row 0: vp 100:{A0}, "they are winning: a peace '
          f'now would be a major defeat"', v1 and t.get('winner') == '0' and t.get('level') == '2' and r and
          r['outlook'] == 'they are winning: a peace now would be a major defeat', f'{v1} {t["raw"]} {r}')
    before = chat(hp)
    rc = client_request('diplo peace 0', 'peace', 0)
    got = wait_chat(hp, f'{n1} offers peace to {n0}.', before)
    rh = cmd(H, f'diplo peace {W}')
    line = (f'{n0} accepts the peace offered by {n1}: {n0} wins a major victory and may claim {n1}\'s land worth up '
            f'to 8.')
    okc = wait_chat(hp, line, before)
    t = terms(0, 1)
    check('i) the client offers peace, the host accepts: terms winner=0 loser=1 level=2 budget=8; chat',
          rc == 'ok' and got and rh == 'ok' and okc and (t.get('winner'), t.get('loser'), t.get('level'),
                                                         t.get('budget')) == ('0', '1', '2', '8'),
          f'{rc} {rh} chat {got}/{okc} {t["raw"]}')
    c01n = poss_list(H, 'claimable 0 1') or []
    pick0, worth0 = greedy(c01n, 8)
    names0 = [p['name'] for p in pick0]
    before = chat(hp)
    rh = cmd(H, f'claimland {W} {"|".join(names0)}')
    line = f'{n0} claims {", ".join(names0)} from {n1}.' if names0 else \
        f'{n0} leaves the choice of land to the peace conference.'
    okc = wait_chat(hp, line, before)
    t = terms(0, 1)
    check(f'i) the host claims as the winner (claimland 1 {"|".join(names0)}): terms claims, chat "{line}"',
          rh == 'ok' and okc and t.get('claims') == names0, f'{rh} chat {okc} {t["raw"]}')
    bv = nation_view((0, 1, 2))
    seen0, hm = len(TREATY_SEEN), log_mark(H)
    cmd(H, 'applydiplo')
    end = time.time() + 25
    while time.time() < end:
        answer_dialogs(hp, 'host')
        if [d for d in TREATY_SEEN[seen0:] if d['who'] == 'host'] and not treaty_dialog_open(hp):
            break
        time.sleep(0.5)
    drain(hp, 2)
    hl = log_since(H, hm)
    r01 = rel(0, 1)
    av = nation_view((0, 1, 2))
    log(f'  host treaty: before {fmt_view(bv)}')
    log(f'  host treaty: after  {fmt_view(av)}')
    log('  host dialogs meanwhile:', [x.split('] ', 1)[-1] for x in log_lines(hl, r'notice:|showing a peace|treaty|'
                                                                               r'diplomacy: peace')])
    check('i) the war is over through the game\'s end-of-war routine: rel 0 1 war=0 warCounter=0 hostWars=0, '
          '"diplomacy: peace 1-0" (offerer-accepter)', r01['war'] == '0' and r01['warCounter'] == '0' and
          r01['hostWars'] == '0' and log_lines(hl, r'diplomacy: peace (0-1|1-0)'), r01)
    tl = treaty_lines(hl, 0, 1)
    k, mode, s, pr, res = (int(tl[0][3]), tl[0][4], int(tl[0][5]), int(tl[0][6]), int(tl[0][7])) if tl else \
        (None, None, None, None, None)
    rmax = min(500, max(0, (bv[1]['R'] or 0) // 2))
    check(f'i) one "treaty 0 over 1 (level 2): {len(names0)} possession(s) '
          f'({"claimed" if names0 else "automatic"}), s ship(s) (0-3), prestige +2, resources -{rmax}" line',
          len(tl) == 1 and int(tl[0][2]) == 2 and k == len(names0) and
          mode == ('claimed' if names0 else 'automatic') and 0 <= s <= 3 and pr == 2 and res == rmax,
          [x.split('] ', 1)[-1] for x in log_lines(hl, r'treaty \d+ over')] or 'no line')
    moved, gained = set(bv[1]['poss']) - set(av[1]['poss']), set(av[0]['poss']) - set(bv[0]['poss'])
    check(f'i) the host\'s claims {names0} moved from 1 to 0 (and stay there after DoPeace)',
          moved == gained == set(names0), f'moved {sorted(moved)}, gained {sorted(gained)}')
    dp = None if None in (bv[0]['P'], av[0]['P']) else av[0]['P'] - bv[0]['P']
    want1 = None if bv[1]['P'] is None else treaty_loss(bv[1]['P'], 2)
    check(f'i) exact: nation 1 BaseResources -{res}, ships 1 -{s} / 0 +{s}; host Prestige +1 (treaty +2, the game\'s '
          f'DoPeace "war ending without any major battle" -1; no other loss for the host); loser nation 1 Prestige '
          f'{bv[1]["P"]} -> {want1} (major)',
          s is not None and av[1]['R'] == bv[1]['R'] - res and av[1]['ships'] == bv[1]['ships'] - s and
          av[0]['ships'] == bv[0]['ships'] + s and dp == 1 and want1 is not None and av[1]['P'] == want1,
          f'BaseResources 1 {bv[1]["R"]}->{av[1]["R"]}; ships 1 {bv[1]["ships"]}->{av[1]["ships"]}, 0 '
          f'{bv[0]["ships"]}->{av[0]["ships"]}; host Prestige {bv[0]["P"]}->{av[0]["P"]}; nation 1 Prestige '
          f'{bv[1]["P"]}->{av[1]["P"]}')
    kept = [x.split('] ', 1)[-1] for x in log_lines(hl, r'end of war: prestige of player nation \d+ kept at')]
    want_kept = [f'end of war: prestige of player nation {n} kept at {p} (the game set 0)'
                 for n, p in ((1, want1), (2, bv[2]['P'])) if p]
    check('i) the joined players keep their Prestige through the game\'s end-of-war routine (DoPeace sets Prestige '
          f'of nations 1-8 to 0): nation 2 (not a party) unchanged, nation 1 at its treaty value; host log {want_kept}',
          None not in (bv[1]['P'], av[1]['P'], bv[2]['P'], av[2]['P']) and av[2]['P'] == bv[2]['P'] and
          av[1]['P'] == want1 and sorted(kept) == sorted(want_kept),
          f'nation 1 {bv[1]["P"]} -> {av[1]["P"]}, nation 2 {bv[2]["P"]} -> {av[2]["P"]}; log {kept}')
    shown = log_lines(hl, r'showing a peace treaty \((\d+) lines\)')
    dlg = [d for d in TREATY_SEEN[seen0:] if d['who'] == 'host']
    head = f'Peace treaty between {n0} and {n1}: {n0} wins a major victory.'
    ceded = [f'{x} is ceded from {n1} to {n0}.' for x in names0] or [f'{n1} cedes no possessions']
    repar = [f'{n1} pays reparations (base resources -{res}).'] if res else []
    tok, tlines, tdet = treaty_text(dlg[0]['text'] if dlg else '', shown, head, ceded, s or 0, repar)
    check('i) the host (a party) shows the game\'s "Peace treaty" dialog: log "showing a peace treaty (n lines)" = the '
          'dialog\'s lines (heading, ceded, hand-overs "... as war reparation." plus their own sentences, reparations, '
          'one period each), answered', len(shown) == 1 and len(dlg) == 1 and tok and not treaty_dialog_open(hp),
          f'{[x.split("] ", 1)[-1] for x in shown]}; {tdet}')
    log('  treaty text example (host):\n    ' + '\n    '.join(tlines))

    # j) a compromise: equal war score, no terms
    rc = client_request(f'diplo war {Lo}', 'war', Lo)
    apply_diplo(ctx, lambda: rel(1, 2).get('war') == '1')
    v1, t = warvp(1, 2, 300, 300), terms(1, 2)
    r = wait_until(lambda: row(C, Lo), lambda r: r and r['vp'] == '300:300', 6)
    check('j) war 1-2 again; warvp 1 2 300 300: outlook winner=-1 level=0; client row 2: "a peace now would be a '
          'compromise"', rc == 'ok' and rel(1, 2)['war'] == '1' and t.get('winner') == '-1' and
          t.get('level') == '0' and r and r['outlook'] == 'a peace now would be a compromise', f'{rc} {t["raw"]} {r}')
    before = chat(cp)
    rh = cmd(H, f'diploas {Lo} peace {W}')
    wait_until(lambda: row(C, Lo), lambda r: r and 'p' in r['flags'], 6)
    rc = client_request(f'diplo peace {Lo}', 'peace', Lo)
    line = f'{n1} accepts the peace offered by {n2}: a compromise peace without terms.'
    okc = wait_chat(cp, line, before)
    t = terms(1, 2)
    r = wait_until(lambda: row(C, Lo), lambda r: r and r['pending'] == 'peace agreed: compromise, no terms', 6)
    rc2 = cmd(C, f'claimland {Lo} {c12[0]["name"] if c12 else "Atlantis"}')
    rh2 = cmd(H, f'diploas {W} claim {Lo} {c12[0]["name"] if c12 else "Atlantis"}')
    check('j) peace agreed: terms winner=-1 level=0 budget=0, chat "a compromise peace without terms", client row '
          '"peace agreed: compromise, no terms"; claims refused on the client and the host',
          rh == 'ok' and rc == 'ok' and okc and (t.get('winner'), t.get('level'), t.get('budget')) == ('-1', '0', '0')
          and r and r['claim'] == '0:0:0' and rc2.startswith('error Only the winner') and
          rh2 == 'error Only the winner of the war can claim possessions.', f'{t["raw"]} {r} | {rc2} | {rh2}')
    bv = nation_view((0, 1, 2))
    hm = log_mark(H)
    apply_diplo(ctx, lambda: rel(1, 2).get('war') == '0')
    time.sleep(1)
    hl = log_since(H, hm)
    av = nation_view((0, 1, 2))
    line = 'have ended their war (compromise peace).'
    check('j) applydiplo: war over, chat "... have ended their war (compromise peace).", no treaty line; possessions, '
          'prestige and base resources of the host, 1 and 2 unchanged',
          rel(1, 2)['war'] == '0' and not new_chat_missing(before, chat(cp), [line]) and
          not log_lines(hl, r'treaty \d+ over') and
          all(av[n][f] == bv[n][f] for n in (0, 1, 2) for f in ('poss', 'P', 'R')),
          f'{fmt_view(bv)} -> {fmt_view(av)}; {log_lines(hl, "treaty")}')


# ---------------------------------------------------------------- victory points of joined players (0.2.6)

VP_BATTLE_RE = r'victory points: nation (\d+) \+(\d+) in its battle against the host -> (-?\d+)'
VP_SHARE_RE = r"victory points: nation (\d+) \+(\d+) of the host's enemies' (\d+) this month -> (-?\d+)"
VP_PEACE_RE = r'victory points: nation (\d+) is at peace; (-?\d+) -> 0'
VP_AIPEACE_RE = r'AI peace: VP of player nation (\d+) kept at (-?\d+) \(the game set (-?\d+)\)'
VP_SHOWN_RE = r'victory points shown: own (-?\d+), enemy (-?\d+)'
WAR_SCORE_RE = r'war score (\d+)-(\d+): \+(\d+)/\+(\d+) -> (\d+):(\d+)'
VP_PLAYERS = (1, STANDIN)  # the human nations the host credits (the client's and the stand-in's); 3..8 are AI
VP_AI = tuple(range(3, 9))


def vpinfo(port):
    """`vpinfo` -> dict(enemy=EnemyVP (on a client: its "Enemy VP" as shown), seen=the part of EnemyVP the host
    already gave out (-1 = not known yet), vp={nation: VP}, raw), or None."""
    m = re.match(r'^ok enemy=(-?\d+) seen=(-?\d+) vp=(\S*)$', cmd(port, 'vpinfo'))
    if not m:
        return None
    vp = {int(a): int(b) for a, b in (e.split(':') for e in m[3].split(',') if ':' in e)}
    return dict(enemy=int(m[1]), seen=int(m[2]), vp=vp, raw=m.group(0)[3:])


def battlevp(ctx, opp, vp_host, vp_opp, level='Minor'):
    """Host `battlevp`: the game's own HandleBattleAftermath against `opp` through the mod's hook, as after a tactical
    battle -> dict(ok, host=(a, b), enemy=(c, d), opp=(e, f), raw). The bridge runs it synchronously on the game's main
    thread, so the host's dialogs are answered meanwhile."""
    out = {}

    def run():
        try:
            out['r'] = ctl.cmd(H, f'battlevp {opp} {vp_host} {vp_opp} {level}', 130)
        except OSError as e:
            out['r'] = f'error {e!r}'

    th = threading.Thread(target=run, daemon=True)
    th.start()
    while th.is_alive():
        answer_dialogs(ctx.hp, 'host')
        th.join(0.3)
    r = out.get('r', '')
    m = re.match(r'^ok host=(-?\d+)->(-?\d+) enemy=(-?\d+)->(-?\d+) opp=(-?\d+)->(-?\d+)$', r)
    if not m:
        log('  !! battlevp ->', r)
        return dict(ok=False, host=(0, 0), enemy=(0, 0), opp=(0, 0), raw=r)
    v = [int(x) for x in m.groups()]
    return dict(ok=True, host=(v[0], v[1]), enemy=(v[2], v[3]), opp=(v[4], v[5]), raw=r[3:])


def wars_of(n):
    """The nations (0..8) at war with nation n on the host."""
    return [j for j in range(9) if j != n and rel(n, j).get('war') == '1']


def vp_shares(rest, enemies):
    """ShareEnemyVP's split: `rest` evenly among the host's enemies in index order, the remainder +1 to the first."""
    share, extra = divmod(rest, len(enemies)) if enemies else (0, 0)
    return {j: share + (1 if i < extra else 0) for i, j in enumerate(enemies)}


def vp_diff(a, b, nations):
    """{nation: (before, after)} for the nations whose VP differ between two vpinfo results."""
    return {n: (a['vp'].get(n), b['vp'].get(n)) for n in nations if a['vp'].get(n) != b['vp'].get(n)}


def apply_vp(ctx, want_seen, timeout=15):
    """applydiplo when the month-end step must share EnemyVP: done once `seen` (set first thing) reached want_seen."""
    return apply_diplo(ctx, lambda: (vpinfo(H) or {}).get('seen') == want_seen, timeout)


def client_accept_peace(ctx, nation):
    """The client accepts the peace `nation` offered with its Diplomacy button: the nation's row selected in the list
    (LVM_SETITEMSTATE), button 1084 reading "Accept peace", a posted BM_CLICK. Returns (button text, the host's
    `diplomacy request 1 peace <nation> accepted` lines)."""
    cp = ctx.cp
    cmd(C, 'mpwindow')
    time.sleep(0.5)
    lvh, btn = mp_ctl(cp, 'list'), mp_ctl(cp, 'peace')
    rows = lv_rows(cp, lvh)
    idx = next((i for i, (prm, _, _) in enumerate(rows) if prm == nation), None)
    if idx is not None:
        lv_select(cp, lvh, idx)
    bt = wait_until(lambda: text(btn), lambda s: s == 'Accept peace', 8)
    mark = log_mark(H)
    if bt == 'Accept peace':
        u.PostMessageW(btn, 0x00F5, 0, 0)  # BM_CLICK
    return bt, wait_log(H, mark, rf'diplomacy request 1 peace {nation} accepted', 8)


def scenario_victory_points(ctx):
    """0.2.6: victory points of joined players. The game credits whatever the host's enemies achieve in the host's
    wars to EnemyVP only (a joined nation's own VP never moved). The host now gives a human enemy the points of its
    battles at once (OnBattleVP), shares the rest of the month's EnemyVP among its enemies at month end
    (ShareEnemyVP; `seen` = the part already given out), resets a human nation at peace to VP 0, keeps the VP of
    human nations still at war through HandleAIPeace and keeps its own VP in the merge; the client shows the VP of
    its own enemies as "Enemy VP". Driven with `battlevp` (the game's HandleBattleAftermath), `enemyvpadd` (EnemyVP
    outside a battle, like a blockade) and `applydiplo`, so no month is processed while the host is at war; one month
    is processed with only the war 1-2 (merge and the client's view)."""
    hp = ctx.hp
    ALL = range(9)

    # a) startup
    avail = {}
    for port, who in ((H, 'host'), (C, 'client')):
        ln = log_lines(log_since(port), r'Peace treaties: ')
        avail[who] = bool(ln) and bool(re.search(r'Peace treaties: ok \(.*battleHooks=1/1/1\)', ln[0]))
        check(f'a) {who} log at startup: "Peace treaties: ok (... battleHooks=1/1/1)" (the VP hooks)', avail[who],
              [x.split('] ', 1)[-1] for x in ln[:1]] or 'no line')
    if not all(avail.values()):  # every further step needs the hooks: don't keep the game open for nothing
        check('victory points: the VP hooks are available on host and client (needed by every further check)', False,
              f'{avail}: the remaining checks were not run')
        return
    cv = vpinfo(C)
    shown = re.findall(VP_SHOWN_RE, log_since(C))
    check('a) client log "victory points shown: own V, enemy E" for every loaded month; the latest = the client\'s '
          'vpinfo (nation 1\'s VP, the shown Enemy VP)',
          bool(shown) and cv is not None and shown[-1] == (str(cv['vp'].get(1)), str(cv['enemy'])),
          f'{shown[-3:]} | {cv and cv["raw"]}')
    r01, r12 = rel(0, 1), rel(1, 2)
    v0 = vpinfo(H)
    log(f'  start: host vpinfo {v0 and v0["raw"]} | client vpinfo {cv and cv["raw"]} | wars of 1: {wars_of(1)}, '
        f'of 2: {wars_of(2)}')
    check('start: the host at war with nobody, nations 1 and 2 at peace with each other',
          r01['hostWars'] == '0' and r12['war'] == '0', f'0-1 {r01} | 1-2 {r12}')

    # b) the host at war with nation 1; the game's end-of-battle routine of a host battle against it
    mark = log_mark(H)
    rh = cmd(H, 'diplo war 1')
    acc = wait_log(H, mark, r'diplomacy request 0 war 1 accepted', 8)
    apply_diplo(ctx, lambda: rel(0, 1).get('war') == '1')
    r01, v1 = rel(0, 1), vpinfo(H)
    check('b) host `diplo war 1` (accepted) + applydiplo: at war with 1 (rel 0 1 war=1, hostWars=1)',
          rh == 'ok' and acc and r01['war'] == '1' and r01['hostWars'] == '1', f'{rh} {acc} {r01}')
    log(f'  b) at war: host vpinfo {v1["raw"]}')
    mark = log_mark(H)
    bv = battlevp(ctx, 1, 1000, 3000, 'Minor')
    v2, hl = vpinfo(H), log_since(H, mark)
    gain = bv['enemy'][1] - bv['enemy'][0]
    log(f'  b) battlevp 1 1000 3000 Minor -> {bv["raw"]} | host vpinfo {v2["raw"]}')
    check('b) the host\'s side as before: its VP and EnemyVP rise (battlevp host a->b, enemy c->d; vpinfo agrees)',
          bv['ok'] and bv['host'][1] > bv['host'][0] == v1['vp'][0] and gain > 0 and bv['enemy'][0] == v1['enemy']
          and v2['vp'][0] == bv['host'][1] and v2['enemy'] == bv['enemy'][1], bv['raw'])
    check(f'b) nation 1 (the joined enemy) VP +{gain} = the EnemyVP gain of the battle (0.2.5: unchanged)',
          bv['ok'] and bv['opp'] == (v1['vp'][1], v1['vp'][1] + gain) and v2['vp'][1] == v1['vp'][1] + gain,
          f'opp {bv["opp"]}, vpinfo nation 1 {v1["vp"][1]} -> {v2["vp"][1]}')
    got = re.findall(VP_BATTLE_RE, hl)
    check(f'b) host log "victory points: nation 1 +{gain} in its battle against the host -> {v2["vp"][1]}"',
          got == [('1', str(gain), str(v2['vp'][1]))], got)
    check('b) vpinfo seen == enemy (the battle\'s points count as given out)', v2['seen'] == v2['enemy'], v2['raw'])
    check('b) nation 2 and the AI nations 3-8 keep their VP', not vp_diff(v1, v2, (2,) + VP_AI),
          vp_diff(v1, v2, (2,) + VP_AI))

    # c) EnemyVP outside a battle (like a blockade) is shared at month end: nation 1 is the host's only enemy
    ra = cmd(H, 'enemyvpadd 101')
    va, enemies = vpinfo(H), wars_of(0)
    hl = apply_vp(ctx, va['enemy'])
    vc = vpinfo(H)
    rest = va['enemy'] - va['seen']
    got = re.findall(VP_SHARE_RE, hl)
    log(f'  c) enemyvpadd 101 -> {ra[3:]} | host enemies {enemies} | after applydiplo {vc["raw"]} | {got}')
    check('c) enemyvpadd 101: EnemyVP +101, seen unchanged (the reply is vpinfo)',
          ra.startswith('ok enemy=') and va['enemy'] == v2['enemy'] + 101 and va['seen'] == v2['seen'], ra)
    check('c) applydiplo shares the month\'s rest 101 with the host\'s only enemy: nation 1 +101',
          enemies == [1] and rest == 101 and vc['vp'][1] == va['vp'][1] + 101,
          f'enemies {enemies}, rest {rest}, nation 1 {va["vp"][1]} -> {vc["vp"][1]}')
    check(f'c) host log "victory points: nation 1 +101 of the host\'s enemies\' 101 this month -> {vc["vp"][1]}"',
          got == [('1', '101', '101', str(vc['vp'][1]))], got)
    check('c) seen == enemy; the share changes neither the host\'s VP nor its EnemyVP (the host\'s screen) nor '
          'nations 2-8', vc['seen'] == vc['enemy'] == va['enemy'] and vc['vp'][0] == va['vp'][0] and
          not vp_diff(va, vc, (2,) + VP_AI), f'{va["raw"]} -> {vc["raw"]}')
    sf = session_file()
    check(f'c) {SESSION_FILE} keeps enemyVPSeen={vc["seen"]}, stamped with diplomacyDate',
          sf.get('enemyVPSeen') == str(vc['seen']) and bool(sf.get('diplomacyDate')),
          {k: sf.get(k) for k in ('enemyVPSeen', 'diplomacyDate')})

    # d) a second enemy: the host at war with 1 and 2, the month's rest split evenly in index order
    mark = log_mark(H)
    rh = cmd(H, 'diplo war 2')
    acc = wait_log(H, mark, r'diplomacy request 0 war 2 accepted', 8)
    apply_diplo(ctx, lambda: rel(0, 2).get('war') == '1')
    r02, vd0 = rel(0, 2), vpinfo(H)
    check('d) host `diplo war 2` + applydiplo: at war with 1 and 2 (hostWars=2); a second war resets nothing (VP, '
          'EnemyVP, seen unchanged)', rh == 'ok' and acc and r02['war'] == '1' and r02['hostWars'] == '2' and
          vd0['enemy'] == vc['enemy'] and vd0['seen'] == vc['seen'] and not vp_diff(vc, vd0, ALL),
          f'{rh} {acc} {r02} | {vc["raw"]} -> {vd0["raw"]}')
    ra = cmd(H, 'enemyvpadd 101')
    va, enemies = vpinfo(H), wars_of(0)
    hl = apply_vp(ctx, va['enemy'])
    vd = vpinfo(H)
    rest = va['enemy'] - va['seen']
    want = vp_shares(rest, enemies)
    got = re.findall(VP_SHARE_RE, hl)
    want_lines = sorted((str(j), str(want[j]), str(rest), str(vd['vp'][j])) for j in enemies
                        if j in VP_PLAYERS and want[j] > 0)
    log(f'  d) enemyvpadd 101 -> {ra[3:]} | host enemies {enemies} | after applydiplo {vd["raw"]} | {got}')
    check('d) enemyvpadd 101 + applydiplo: shared evenly among the host\'s enemies [1, 2] in index order: nation 1 '
          '+51, nation 2 +50', enemies == [1, 2] and rest == 101 and vd['vp'][1] == va['vp'][1] + 51 and
          vd['vp'][2] == va['vp'][2] + 50, f'enemies {enemies}, rest {rest}, {va["raw"]} -> {vd["raw"]}')
    check(f'd) host log "victory points: nation K +x of the host\'s enemies\' {rest} this month -> V" for 1 and 2',
          sorted(got) == want_lines and len(got) == 2, f'{got} (want {want_lines})')
    check('d) seen == enemy; the host\'s VP, its EnemyVP and the AI nations unchanged by the share',
          vd['seen'] == vd['enemy'] == va['enemy'] and vd['vp'][0] == va['vp'][0] and not vp_diff(va, vd, VP_AI),
          f'{va["raw"]} -> {vd["raw"]}')

    # e) a drop: the game reset EnemyVP (DoPeace / WarStartsWithNation set it to 0) and it is below `seen` at month end
    E = vd['enemy']
    r1, r2 = cmd(H, f'enemyvpadd {-E}'), cmd(H, 'enemyvpadd 101')
    ve0 = vpinfo(H)
    hl = apply_vp(ctx, ve0['enemy'])
    ve = vpinfo(H)
    got = re.findall(VP_SHARE_RE, hl)
    check(f'e) a drop: EnemyVP {E} -> 0 (as the game resets it) -> 101, below seen {vd["seen"]}: applydiplo shares '
          f'nothing (no share line, every VP unchanged) and seen = enemy = 101',
          E > 101 and r1.startswith('ok') and r2.startswith('ok') and ve0['enemy'] == 101 and
          ve0['seen'] == vd['seen'] and not got and not vp_diff(ve0, ve, ALL) and ve['seen'] == ve['enemy'] == 101,
          f'{ve0["raw"]} -> {ve["raw"]}; {got}')

    # f) peace with 1 (the host stays at war with 2), the client accepts with its button; equal war score: compromise
    warvp(0, 1, 300, 300)
    rh = cmd(H, 'diplo peace 1')
    bt, acc = client_accept_peace(ctx, 0)
    r = wait_until(lambda: row(H, 1), lambda r: r and 'peace agreed' in r['pending'], 5)
    check('f) host offers peace to 1; the client accepts with its "Accept peace" button (1084): host row 1 "peace '
          'agreed"', rh == 'ok' and bt == 'Accept peace' and bool(acc) and r and 'peace agreed' in r['pending'],
          f'{rh}; button "{bt}"; {acc}; {r}')
    vf0 = vpinfo(H)
    hl = apply_diplo(ctx, lambda: rel(0, 1).get('war') == '0')
    vf, r01, w1 = vpinfo(H), rel(0, 1), wars_of(1)
    got = re.findall(VP_PEACE_RE, hl)
    log(f'  f) peace 0-1: {vf0["raw"]} -> {vf["raw"]} | {got}')
    check('f) applydiplo: peace 0-1, the host still at war with 2 (hostWars=1): no DoPeace, the host\'s VP and '
          'EnemyVP unchanged', r01['war'] == '0' and r01['hostWars'] == '1' and vf['vp'][0] == vf0['vp'][0] and
          vf['enemy'] == vf0['enemy'], f'{r01} | {vf0["raw"]} -> {vf["raw"]}')
    check(f'f) nation 1 at war with nobody: host log "victory points: nation 1 is at peace; {vf0["vp"][1]} -> 0", VP '
          f'0; nation 2 (still at war) keeps {vf0["vp"][2]}',
          not w1 and vf0['vp'][1] != 0 and got == [('1', str(vf0['vp'][1]))] and vf['vp'][1] == 0 and
          vf['vp'][2] == vf0['vp'][2], f'wars of 1 {w1}; {got}; {vf0["raw"]} -> {vf["raw"]}')

    # peace with 2 ends the host's last war (DoPeace); in the same month-end step the host declares war on 1 again
    warvp(0, 2, 300, 300)
    rh, ra, rw = cmd(H, 'diplo peace 2'), cmd(H, 'diploas 2 peace 0'), cmd(H, 'diplo war 1')
    vg0 = vpinfo(H)
    hl = apply_diplo(ctx, lambda: rel(0, 2).get('war') == '0' and rel(0, 1).get('war') == '1', 25)
    vg, r02, r01, w2 = vpinfo(H), rel(0, 2), rel(0, 1), wars_of(2)
    got = re.findall(VP_PEACE_RE, hl)
    log(f'  f) peace 0-2 + war 0-1 in one step: {vg0["raw"]} -> {vg["raw"]} | {got}')
    check('f) host offers peace to 2, the stand-in accepts (diploas 2 peace 0) + applydiplo: the host\'s last war '
          'ends, DoPeace resets the host\'s VP and EnemyVP to 0',
          rh == ra == 'ok' and r02['war'] == '0' and vg0['vp'][0] != 0 and vg['vp'][0] == 0 and vg['enemy'] == 0,
          f'{rh} {ra} {r02} | {vg0["raw"]} -> {vg["raw"]}')
    check(f'f) nation 2 at war with nobody: host log "victory points: nation 2 is at peace; {vg0["vp"][2]} -> 0", VP 0',
          not w2 and vg0['vp'][2] != 0 and got == [('2', str(vg0['vp'][2]))] and vg['vp'][2] == 0,
          f'wars of 2 {w2}; {got}; {vg["raw"]}')
    check('f) the host\'s new war on 1 started in the same step (rel 0 1 war=1, hostWars=1)',
          rw == 'ok' and r01['war'] == '1' and r01['hostWars'] == '1', f'{rw} {r01}')
    # The step reset EnemyVP after its own ShareEnemyVP (DoPeace; the new war): `seen` is re-synced by the DoPeace hook
    # and at the end of the step, so the new war's points are shared from 0 (0.2.6 first build: seen stayed at the old
    # score and the new war's first month shared nothing).
    shared = re.findall(VP_SHARE_RE, hl)
    check(f'f) re-sync: after that step seen == enemy == 0 (was seen {vg0["seen"]}), and no share line in it (the DoPeace '
          f'hook\'s ShareEnemyVP credits nothing twice)', vg['seen'] == vg['enemy'] == 0 and not shared,
          f'{vg0["raw"]} -> {vg["raw"]}; {shared}')
    ra = cmd(H, 'enemyvpadd 101')
    vh0 = vpinfo(H)
    hl = apply_vp(ctx, vh0['enemy'])
    vh = vpinfo(H)
    got = re.findall(VP_SHARE_RE, hl)
    log(f'  f) re-sync: enemyvpadd 101 -> {ra[3:]} | after applydiplo {vh["raw"]} | {got}')
    check(f'f) re-sync: enemyvpadd 101 + applydiplo in the new war: nation 1 (the host\'s only enemy) {vh0["vp"][1]} -> '
          f'{vh0["vp"][1] + 101}, "victory points: nation 1 +101 of the host\'s enemies\' 101 this month -> '
          f'{vh0["vp"][1] + 101}", seen == enemy == 101',
          ra.startswith('ok') and wars_of(0) == [1] and vh['vp'][1] == vh0['vp'][1] + 101 and
          got == [('1', '101', '101', str(vh['vp'][1]))] and vh['seen'] == vh['enemy'] == 101,
          f'{vh0["raw"]} -> {vh["raw"]}; {got}')

    # g) the players' war 1-2, and the host at war with both: the host's battles against 1 and 2 credit them; then the
    # host makes peace with both (the game's DoPeace) while 1 and 2 stay at war with each other, and one month is
    # processed (merge, the client's view). Battles only inside a host war that DoPeace ends: a battlevp at peace
    # leaves the host's VP and EnemyVP non-zero, and the game's DoPeace of a later war then skips its "war ending
    # without any major battle" prestige dent (treaty i failed with host Prestige +2 instead of +1).
    rc = client_request('diplo war 2', 'war', 2)
    mark = log_mark(H)
    rh = cmd(H, 'diplo war 2')
    acc = wait_log(H, mark, r'diplomacy request 0 war 2 accepted', 8)
    apply_diplo(ctx, lambda: rel(1, 2).get('war') == '1' and rel(0, 2).get('war') == '1')
    r12, r02, vj = rel(1, 2), rel(0, 2), vpinfo(H)
    check('g) client `diplo war 2` + host `diplo war 2` + applydiplo: 1-2 at war, the host at war with 1 and 2 '
          '(hostWars=2)', rc == 'ok' and rh == 'ok' and bool(acc) and r12['war'] == '1' and r02['war'] == '1' and
          r02['hostWars'] == '2', f'{rc} {rh} {acc}; 1-2 {r12}; 0-2 {r02}')
    for opp, a, b in ((1, 500, 2000), (2, 300, 1000)):
        mark = log_mark(H)
        bv = battlevp(ctx, opp, a, b, 'Minor')
        vn, hl = vpinfo(H), log_since(H, mark)
        gain, other = bv['enemy'][1] - bv['enemy'][0], 3 - opp
        got = re.findall(VP_BATTLE_RE, hl)
        log(f'  g) battlevp {opp} {a} {b} Minor -> {bv["raw"]} | host vpinfo {vn["raw"]}')
        check(f'g) battlevp {opp} {a} {b} Minor: nation {opp} +{gain} = the EnemyVP gain (log line), nation {other} '
              f'unchanged, seen == enemy', bv['ok'] and gain > 0 and vn['vp'][opp] == vj['vp'][opp] + gain and
              vn['vp'][other] == vj['vp'][other] and got == [(str(opp), str(gain), str(vn['vp'][opp]))] and
              vn['seen'] == vn['enemy'], f'{bv["raw"]} | {got} | {vj["raw"]} -> {vn["raw"]}')
        vj = vn
    warvp(0, 1, 300, 300)
    warvp(0, 2, 300, 300)
    rh = cmd(H, 'diplo peace 1')
    bt, acc = client_accept_peace(ctx, 0)
    wait_until(lambda: row(H, 1), lambda r: r and 'peace agreed' in r['pending'], 5)
    rh2, ra2 = cmd(H, 'diplo peace 2'), cmd(H, 'diploas 2 peace 0')
    hl = apply_diplo(ctx, lambda: rel(0, 1).get('war') == '0' and rel(0, 2).get('war') == '0', 25)
    vi, r01, r12 = vpinfo(H), rel(0, 1), rel(1, 2)
    got = re.findall(VP_PEACE_RE, hl)
    log(f'  g) peace 0-1 and 0-2: {vj["raw"]} -> {vi["raw"]} | {got}')
    check('g) host peace with 1 (the client\'s "Accept peace" button) and with 2 + applydiplo: the host\'s last war '
          'ends (hostWars=0), DoPeace resets the host\'s VP and EnemyVP to 0',
          rh == rh2 == ra2 == 'ok' and bt == 'Accept peace' and bool(acc) and r01['war'] == '0' and
          r01['hostWars'] == '0' and vj['vp'][0] != 0 and vi['vp'][0] == 0 and vi['enemy'] == 0,
          f'{rh} {rh2} {ra2}; button "{bt}"; {acc}; {r01}; {vj["raw"]} -> {vi["raw"]}')
    check(f'g) nations 1 and 2, still at war with each other, keep their VP {vj["vp"][1]}/{vj["vp"][2]} through the '
          f'host\'s DoPeace (no "is at peace" line)', r12['war'] == '1' and vi['vp'][1] == vj['vp'][1] and
          vi['vp'][2] == vj['vp'][2] and not got, f'1-2 {r12}; {got}; {vj["raw"]} -> {vi["raw"]}')
    shared = re.findall(VP_SHARE_RE, hl)
    check(f'g) re-sync after the host\'s DoPeace: seen == enemy == 0 (was {vj["seen"]}), no share line (nothing new to '
          f'share before DoPeace, nothing credited twice)', vi['seen'] == vi['enemy'] == 0 and not shared,
          f'{vj["raw"]} -> {vi["raw"]}; {shared}')
    if r01['war'] != '0' or r01['hostWars'] != '0':
        check('g) the month can be processed (the host at war with nobody)', False, r01)
        return
    cmd(H, 'enemyvpadd 77')  # a marker: the host's own "Enemy VP", unlike what the client shows (reset at the end)
    vj = vpinfo(H)
    credited, cpre = vj['vp'][1], vpinfo(C)
    hm, cm = log_mark(H), log_mark(C)
    run_month(ctx, 'victory_points')
    hl, cl = log_since(H, hm), log_since(C, cm)
    vk, w1 = vpinfo(H), wars_of(1)
    ws = [m for m in re.findall(WAR_SCORE_RE, hl) if {m[0], m[1]} == {'1', '2'}]
    g1 = sum(int(m[2]) if m[0] == '1' else int(m[3]) for m in ws)
    g2 = sum(int(m[3]) if m[0] == '1' else int(m[2]) for m in ws)
    log(f'  g) month: host {vj["raw"]} -> {vk["raw"]} | client before {cpre and cpre["raw"]} | wars of 1 {w1} | war '
        f'score 1-2 lines {ws} | AI peace lines {re.findall(VP_AIPEACE_RE, hl)} | share/peace lines '
        f'{re.findall(VP_SHARE_RE, hl)} {re.findall(VP_PEACE_RE, hl)} | merge {log_lines(hl, r"merge nation 1")[:1]}')
    check(f'g) host after the month: nation 1 VP = the credited {credited} + its 1-2 battle points {g1} (the merge kept '
          f'the host\'s VP; the player\'s copy had {cpre and cpre["vp"].get(1)}); nation 2 = {vj["vp"][2]} + {g2}',
          w1 == [2] and vk['vp'][1] == credited + g1 and vk['vp'][2] == vj['vp'][2] + g2 and
          bool(log_lines(hl, r'merge nation 1')), f'wars of 1 {w1}; {vj["raw"]} -> {vk["raw"]}; war score {ws}')
    cv = vpinfo(C)
    want_e = sum(max(0, vk['vp'][j]) for j in w1)
    shown = re.findall(VP_SHOWN_RE, cl)
    check(f'g) client vpinfo after the month: every nation\'s VP as on the host (nation 1: {vk["vp"][1]}), "Enemy VP" '
          f'= the VP of the nations at war with 1 {w1} = {want_e} (not the host\'s EnemyVP {vk["enemy"]})',
          cv is not None and cv['vp'] == vk['vp'] and cv['enemy'] == want_e and want_e > 0 and
          want_e != vk['enemy'], f'client {cv and cv["raw"]} | host {vk["raw"]}')
    check(f'g) client log "victory points shown: own {vk["vp"][1]}, enemy {want_e}" once for the new month',
          shown == [(str(vk['vp'][1]), str(want_e))], shown)
    check(f'i) the host\'s EnemyVP is its own: unchanged by the month and the merge ({vj["enemy"]}; the client showed '
          f'{cpre and cpre["enemy"]} and never sends [General])', vk['enemy'] == vj['enemy'],
          f'{vj["enemy"]} -> {vk["enemy"]}')

    # h) a real AI peace (HandleAIPeace sets VP 0 for nations 1-8) while the players' nations are at war with VP
    vh0 = vpinfo(H)
    keep = {n: vh0['vp'][n] for n in VP_PLAYERS if vh0['vp'][n] != 0 and wars_of(n)}
    cmd(H, 'settension 2 3 50')  # nation 2 also fights the AI nation 3
    r23 = rel(2, 3)
    hm = log_mark(H)
    cmd(H, 'aipeace 2 0')
    drain(hp, 6)
    r23b, r12, vh1 = rel(2, 3), rel(1, 2), vpinfo(H)
    got = re.findall(VP_AIPEACE_RE, log_since(H, hm))
    log(f'  h) aipeace 2 0: 2-3 {r23} -> {r23b}, 1-2 {r12} | {vh0["raw"]} -> {vh1["raw"]} | {got}')
    if r23['war'] == '1' and r23b['war'] == '0' and keep:
        want = sorted((str(n), str(p), '0') for n, p in keep.items())
        check(f'h) a real AI peace (aipeace 2 0 ends the AI war 2-3; the players\' war 1-2 goes on): host log "AI '
              f'peace: VP of player nation K kept at P (the game set 0)" for {sorted(keep)}',
              r12['war'] == '1' and sorted(got) == want, f'{got} (want {want}); 1-2 {r12}')
        check(f'h) the players\' nations at war keep their VP {keep}; the AI nations get the game\'s 0',
              all(vh1['vp'][n] == p for n, p in keep.items()) and all(vh1['vp'][n] == 0 for n in VP_AI),
              f'{vh0["raw"]} -> {vh1["raw"]}')
    else:
        log(f'  SKIP h) no AI peace with a player nation at war with VP != 0 (2-3 {r23} -> {r23b}, keep {keep})')

    # end: peace 1-2 (compromise), so the next scenario starts from peace
    warvp(1, 2, 300, 300)
    rc = client_request('diplo peace 2', 'peace', 2)
    ra = cmd(H, 'diploas 2 peace 1')
    vz0 = vpinfo(H)
    hl = apply_diplo(ctx, lambda: rel(1, 2).get('war') == '0')
    vz, r01 = vpinfo(H), rel(0, 1)
    got = sorted(re.findall(VP_PEACE_RE, hl))
    at_peace = [n for n in VP_PLAYERS if not wars_of(n)]
    want = sorted((str(n), str(vz0['vp'][n])) for n in at_peace if vz0['vp'][n])
    log(f'  end: peace 1-2: {vz0["raw"]} -> {vz["raw"]} | {got}')
    check('end: peace 1-2 (client offers, nation 2 accepts, compromise) + applydiplo: 1 and 2 at war with nobody, VP 0, '
          'a "nation K is at peace; V -> 0" line for each that had VP; the host at war with nobody',
          rc == 'ok' and ra == 'ok' and at_peace == list(VP_PLAYERS) and all(vz['vp'][n] == 0 for n in VP_PLAYERS)
          and got == want and r01['hostWars'] == '0', f'{rc} {ra}; at peace {at_peace}; {got} (want {want}); {r01}')
    rz = cmd(H, f'enemyvpadd {-vz["enemy"]}') if vz['enemy'] else 'ok'
    vz = vpinfo(H)
    check('end: the host\'s VP and EnemyVP back at 0 (marker removed), as after the game\'s DoPeace, so later scenarios '
          'meet a host without battles', rz.startswith('ok') and vz['vp'][0] == 0 and vz['enemy'] == 0, vz['raw'])


# ---------------------------------------------------------------- a joined player's "Copy game" (0.2.7)

COPY_SLOTS = (78, 79, 80)  # player_copy's bridge copies: the run refuses to start if one exists; deleted after the run
BUTTON_SLOTS = range(9, 2, -1)  # the game's Copy game dialog takes slots 1-9 only (81 became 9): the highest free one
_button_slot = [None]  # the slot the real button copies to, chosen free before the run (deleted after it)
PLAYER_COPY_RE = (r'player copy in slot (\d+): (.+) is the player nation, (.+) is nation (\d+) \(AI\); (\d+) sections, '
                  r'(\d+) commanders, (\d+) map fields, divisions (\d+) kept / (\d+) dropped, (at war|at peace)')
REL_KEY = re.compile(r'Tension|Allied|TechSharing|AITension\d+|AIAlliance\d+')
NUM_KEY = re.compile(r'Ship\d+BuildingNationIdx|.*A[UT]\d+Nation')
BATTLE_FILES = ('Autosave.sac', 'Autosave.off', 'BattleInfo.bbi')  # the copy must not keep the host's battle
_u32t = ctypes.WinDLL('user32')  # WM_SETTEXT with a string argument (Windows marshals it across processes)
_u32t.SendMessageW.restype = ctypes.c_ssize_t
_u32t.SendMessageW.argtypes = [wt.HWND, wt.UINT, ctypes.c_size_t, wt.LPCWSTR]


def slot_dir(slot):
    return os.path.join(SAVE, f'Game{slot}')


def read_save(path):
    """A save file (.bcs, MapData .dat, .off) as {section: {key: value}} ('' = lines before the first section; the first
    of a repeated key wins), or None."""
    try:
        with open(path, 'rb') as f:
            t = f.read().decode('utf-8-sig', errors='replace')
    except OSError:
        return None
    out, cur = {'': {}}, ''
    for ln in t.splitlines():
        if ln.startswith('[') and ln.endswith(']'):
            cur = ln[1:-1]
            out.setdefault(cur, {})
        elif '=' in ln:
            k, v = ln.split('=', 1)
            out[cur].setdefault(k, v)
    return out


def save_files(slot):
    """{file name: sha256} of a save slot folder ({} if there is none)."""
    return tree_digest(slot_dir(slot)) if os.path.isdir(slot_dir(slot)) else {}


def save_date(slot):
    """(Year, Month) of a slot's .bcs, or None while it can't be read."""
    g = (read_save(save_path(slot)) or {}).get('General')
    return (g.get('Year'), g.get('Month')) if g else None


def slot_snapshot(slot, d=None):
    d = d or slot_dir(slot)
    return dict(slot=slot, files=tree_digest(d) if os.path.isdir(d) else {},
                bcs=read_save(os.path.join(d, f'RTWGame{slot}.bcs')), map=read_save(os.path.join(d, f'MapData{slot}.dat')),
                off=read_save(os.path.join(d, f'RTWGame{slot}.off')))


def ship_ids(sec):
    return [v for k, v in (sec or {}).items() if re.fullmatch(r'Ship\d+Id', k)]


def nation_sections(doc, n):
    """[Nation<n>], [Nation<n>Ships], [Nation<n>Losses], ... (not [Nation<n><digit>...])."""
    p = f'Nation{n}'
    return [s for s in doc if s.startswith(p) and not (len(s) > len(p) and s[len(p)].isdigit())]


def swap_idx(v, k):
    return k if v == 0 else 0 if v == k else v


def is_int(v):
    return bool(re.fullmatch(r'-?\d+', v or ''))


def expected_copy(src, K, eut=None):
    """What the .bcs of a joined player's copy must hold (0.2.7 specification), from the source save `src`: every
    [Nation0*] section trades contents with [NationK*]; relations from K's point of view (the others' Tension/Allied
    = their relation with K, their AITension<K>/AIAlliance<K> = their relation with the old host, TechSharing 0, the
    new nation 0 keeps the player slot's own values); nation numbers 0 <-> K; the host's old ships without commanders;
    [General] war state for K; [EUT] = the player's cooldowns `eut` (None: unchanged). Returns (document, facts)."""
    def gi(doc, sec, key):
        v = doc.get(sec, {}).get(key, '0')
        return int(v) if is_int(v) else 0

    exp = {s: dict(kv) for s, kv in src.items()}
    pairs = 0
    for s in nation_sections(src, 0):
        o = f'Nation{K}' + s[7:]
        if o in src:
            exp[s], exp[o] = dict(src[o]), dict(src[s])
            pairs += 1

    def put(sec, key, val):
        if key in exp.get(sec, {}):
            exp[sec][key] = str(val)

    for key in list(exp['Nation0']):
        if REL_KEY.fullmatch(key) and key in src['Nation0']:
            exp['Nation0'][key] = src['Nation0'][key]
    for x in range(1, 9):
        sec = f'Nation{x}'
        if x == K:  # the old host, now AI nation K: its relations as the host had them
            put(sec, 'Tension', gi(src, sec, 'Tension'))
            put(sec, 'Allied', gi(src, sec, 'Allied'))
            for y in range(1, 9):
                put(sec, f'AITension{y}', 0 if y == K else gi(src, f'Nation{y}', 'Tension'))
                put(sec, f'AIAlliance{y}', 0 if y == K else gi(src, f'Nation{y}', 'Allied'))
        else:
            put(sec, 'Tension', gi(src, sec, f'AITension{K}'))
            put(sec, 'Allied', gi(src, sec, f'AIAlliance{K}'))
            put(sec, f'AITension{K}', gi(src, sec, 'Tension'))
            put(sec, f'AIAlliance{K}', gi(src, sec, 'Allied'))
            put(sec, f'AITension{x}', 0)
            put(sec, f'AIAlliance{x}', 0)
        put(sec, 'TechSharing', 0)
    swaps = commanders = 0
    hk = f'Nation{K}'
    for s, kv in exp.items():
        host_ships = s.startswith(hk) and len(s) > len(hk) and not s[len(hk)].isdigit()
        for k, v in kv.items():
            if not is_int(v):
                continue
            if NUM_KEY.fullmatch(k):
                kv[k] = str(swap_idx(int(v), K))
                swaps += kv[k] != v
            elif host_ships and re.fullmatch(r'Ship\d+CommanderId', k) and v != '-1':
                kv[k] = '-1'
                commanders += 1
    at_war = [x for x in range(1, 9) if gi(exp, f'Nation{x}', 'Tension') > 40]
    enemy_vp = sum(max(0, gi(exp, f'Nation{x}', 'VP')) for x in at_war)
    war = gi(src, 'General', 'War')
    put('General', 'War', (war if war > 0 else 1) if at_war else (-12 if war > 0 else war))
    put('General', 'EnemyVP', enemy_vp)
    put('General', 'ScenarioInProgress', -1)
    for key in ('BattleAlliedNationIdx', 'BattleEnemyAlliedNationIdx'):
        v = src['General'].get(key, '')
        if is_int(v):
            put('General', key, swap_idx(int(v), K))
    for key, val in (('InvasionTargetName', 'XXX'), ('EnemyInvasionTargetName', 'XXX'), ('ArmyOffensive', 0),
                     ('RevolutionPending', 0), ('LastMissionIdx', 20)):
        put('General', key, val)
    if war > 0 and not at_war:  # only the host was at war: as the game's DoPeace leaves it
        for key, val in (('TotalWar', 0), ('TFW', 1), ('HasBeenBattle', 0), ('AutoBuildSubs', 0)):
            put('General', key, val)
    if eut:
        for j in range(100):
            put('EUT', f'EUT{j}', eut[j])
    return exp, dict(pairs=pairs, swaps=swaps, commanders=commanders, at_war=at_war, enemy_vp=enemy_vp,
                     war=(war, exp['General'].get('War')), player_ids=set(ship_ids(exp.get('Nation0Ships'))))


def expected_map(src, K):
    """MapData: MapArea<a>MineLevel<0|K> trade values, MapArea...TakenFrom 0 <-> K. Returns (document, changed lines)."""
    exp, n = {s: dict(kv) for s, kv in src.items()}, 0
    for s, kv in src.items():
        for k, v in kv.items():
            m = re.fullmatch(r'(MapArea\d+MineLevel)(\d+)', k)
            if m and int(m[2]) in (0, K):
                exp[s][f'{m[1]}{swap_idx(int(m[2]), K)}'] = v
                n += 1
            elif k.startswith('MapArea') and k.endswith('TakenFrom') and is_int(v) and int(v) in (0, K):
                exp[s][k] = str(swap_idx(int(v), K))
                n += 1
    return exp, n


def expected_off(src, player_ids):
    """.off: officers kept; [CampaignDivisions] keeps the divisions whose ships are all the new player's (renumbered).
    Returns (document, kept, dropped)."""
    exp = {s: dict(kv) for s, kv in src.items()}
    cd = src.get('CampaignDivisions')
    if cd is None:
        return exp, 0, 0
    divs, other = {}, {}
    for k, v in cd.items():
        m = re.fullmatch(r'CampDiv(\d+)(\D.*)', k)
        if m:
            divs.setdefault(int(m[1]), []).append((m[2], v))
        elif k != 'CampDivNo':
            other[k] = v
    new, kept, dropped = {}, 0, 0
    for n in sorted(divs):
        if all(v in player_ids for f, v in divs[n] if re.fullmatch(r'ShipId\d+', f)):
            new.update({f'CampDiv{kept}{f}': v for f, v in divs[n]})
            kept += 1
        else:
            dropped += 1
    if divs or other:
        exp['CampaignDivisions'] = dict(CampDivNo=str(kept), **new, **other)
    return exp, kept, dropped


def doc_diff(exp, act):
    """[(section, key, expected, actual)] where two documents differ (key None: a section missing on one side)."""
    out = []
    for s in list(exp) + [s for s in act if s not in exp]:
        e, a = exp.get(s), act.get(s)
        if e is None or a is None:
            out.append((s, None, e is not None, a is not None))
            continue
        out += [(s, k, e.get(k), a.get(k)) for k in list(e) + [k for k in a if k not in e] if e.get(k) != a.get(k)]
    return out


def diff_kind(d):
    s, k = d[0], d[1]
    if s == 'EUT':
        return 'eut'
    if k and (NUM_KEY.fullmatch(k) or re.fullmatch(r'Ship\d+CommanderId', k) or 'BattleAllied' in k
              or 'BattleEnemyAllied' in k):
        return 'numbers'
    if s == 'General':
        return 'general'
    if k and re.fullmatch(r'Nation\d+', s) and REL_KEY.fullmatch(k):
        return 'relations'
    if s.startswith('Nation'):
        return 'sections'
    return 'other'


def verify_copy(src, to, K, eut=None, d=None):
    """The copy in slot `to` against the specification applied to the source snapshot `src` (slot_snapshot).
    Returns dict(diffs={kind: [...]}, facts, files=[problems], act=snapshot of the copy)."""
    act = slot_snapshot(to, d)
    exp_bcs, facts = expected_copy(src['bcs'], K, eut)
    exp_map, facts['map_fields'] = expected_map(src['map'] or {}, K)
    exp_off, facts['div_kept'], facts['div_dropped'] = expected_off(src['off'] or {}, facts['player_ids'])
    diffs = {}
    for d in doc_diff(exp_bcs, act['bcs'] or {}):
        diffs.setdefault(diff_kind(d), []).append(d)
    diffs['map'] = doc_diff(exp_map, act['map'] or {})
    diffs['off'] = doc_diff(exp_off, act['off'] or {})
    s, sf, af = src['slot'], src['files'], act['files']
    probs = [f'missing {n}' for n in (f'RTWGame{to}.bcs', f'RTWGame{to}.sta', f'MapData{to}.dat') if n not in af]
    if f'RTWGame{s}.off' in sf and f'RTWGame{to}.off' not in af:
        probs.append(f'missing RTWGame{to}.off')
    probs += [f'{n} left in the copy' for n in BATTLE_FILES + ('DesignFiles_swap.tmp',) if n in af]
    if af.get(f'RTWGame{to}.sta') != sf.get(f'RTWGame{s}.sta'):
        probs.append('.sta differs from the source')
    for i in range(9):
        want = sf.get(f'DesignFiles{swap_idx(i, K)}.des')
        if af.get(f'DesignFiles{i}.des') != want:
            probs.append(f'DesignFiles{i}.des != source DesignFiles{swap_idx(i, K)}.des')
    facts['battle_files_in_source'] = [n for n in BATTLE_FILES if n in sf]
    facts['copy_files'] = sorted(af)
    return dict(diffs=diffs, facts=facts, files=probs, act=act)


def client_cooldowns():
    """The joined player's event cooldowns (the 100 'used' values of its events_<hash>.ini for nation 1) or None."""
    for f in event_files():
        try:
            with open(os.path.join(LOG_DIR, f), encoding='utf-8', errors='replace') as fh:
                kv = dict(ln.split('=', 1) for ln in fh.read().splitlines() if '=' in ln)
        except OSError:
            continue
        used = [int(x) for x in kv.get('used', '').split(',') if is_int(x.strip())]
        if kv.get('campaign', '').endswith('|nation1') and len(used) == 100:
            return used
    return None


def visible_boxes(pid):
    """Visible message boxes and game dialogs of a copy: TMessageForm, #32770 and Tdlg* windows."""
    return [h for h in top_windows(pid)
            if e2e_test.class_name(h) in ('TMessageForm', '#32770') or e2e_test.class_name(h).startswith('Tdlg')]


def box_info(h):
    """(dict(cls, title, text, buttons), [(button caption, hwnd)]); text only for Win32 boxes (#32770): a VCL
    TMessageForm shows its text in a TLabel, which has no window."""
    k = e2e_test.class_name(h)
    btns = [(caption(c), c) for c in children(h) if e2e_test.class_name(c) in ('TButton', 'TBitBtn', 'Button')
            and u.IsWindowVisible(c) and u.IsWindowEnabled(c)]
    txt = ' '.join(text(c) for c in children(h) if e2e_test.class_name(c) == 'Static' and text(c)) if k == '#32770' else ''
    return dict(cls=k, title=e2e_test.window_text(h), text=txt.replace('\r', ' ').replace('\n', ' '),
                buttons=[b for b, _ in btns]), btns


def copy_dialogs(pid, confirm, done, who='client', timeout=30, quiet=2.5):
    """Answer the dialogs of a "Copy game" and record them until done(seen) holds and no dialog was open for `quiet` s:
    the game's overwrite confirmation (a box with Yes/OK and No/Cancel: Yes/OK if `confirm`, else No/Cancel), its
    "Saved game copied to slot N" (OK) and the mod's "Multiplayer" box (OK). Other Tdlg* dialogs are recorded and left
    alone (dismiss_dialogs is never used here: on an overwrite prompt it would press OK). Returns [dict(cls, title,
    text, buttons, confirm, pressed)]."""
    seen, pressed, end, last = [], {}, time.time() + timeout, time.time()
    while time.time() < end:
        assert_alive()
        boxes = visible_boxes(pid)
        for h in boxes:
            if time.time() - pressed.get(h, 0) < 1.5:
                continue
            info, btns = box_info(h)
            pos = next((b for b in btns if b[0].lower() in ('yes', 'ok')), None)
            neg = next((b for b in btns if b[0].lower() in ('no', 'cancel')), None)
            info['confirm'] = bool(pos and neg)
            b = None if info['cls'].startswith('Tdlg') else (pos if confirm else neg) if info['confirm'] else \
                pos or (btns[0] if len(btns) == 1 else None)
            info['pressed'] = b[0] if b else None
            if b:
                u.PostMessageW(b[1], 0x00F5, 0, 0)  # BM_CLICK
            pressed[h] = time.time() + (3600 if not b else 0)  # an unexpected dialog is recorded once
            seen.append(info)
            log(f'  {who} copy dialog: {info["cls"]}:"{info["title"]}" {info["buttons"]}'
                + (f' "{info["text"]}"' if info['text'] else '') + f' -> {info["pressed"]}')
        if boxes:
            last = time.time()
        if done(seen) and time.time() - last >= quiet:
            break
        time.sleep(0.3)
    return seen


def copy_lines(port, mark, to):
    return [x.split('] ', 1)[-1] for x in log_lines(log_since(port, mark), rf'player copy in slot {to}:|copy of slot \d+ to {to}\b')]


def transform_done(port, mark, to):
    """done() for copy_dialogs: the hook's log line for slot `to` is there and, after a rewrite (or a failed one), the
    mod's "Multiplayer" box was seen."""
    def done(seen):
        ln = copy_lines(port, mark, to)
        return bool(ln) and (not re.search(r'player copy in slot|failed', ln[0])
                             or any(d['cls'] == '#32770' and d['title'] == 'Multiplayer' for d in seen))
    return done


def sort_dialogs(seen):
    """(confirmations, the mod's "Multiplayer" boxes, the game's other boxes)."""
    conf = [d for d in seen if d['confirm']]
    mp = [d for d in seen if not d['confirm'] and d['cls'] == '#32770' and d['title'] == 'Multiplayer']
    return conf, mp, [d for d in seen if d not in conf and d not in mp]


def copy_with_button(ctx, slot):
    """The player's path: the strategic screen's "Copy game" (TBitBtn btbnSaveToOtherSlot) -> TdlgInputNumber
    (TSpinEdit seNumber, OK, Cancel) -> `slot` -> OK. Posted clicks and WM_SETTEXT only; nothing else answers the
    client's dialogs meanwhile. Returns ('ok' | 'skip', detail)."""
    cp = ctx.cp
    btn = next((h for top in windows_of(cp, 'TfrmBuildCamp') for h in children(top)
                if e2e_test.class_name(h) == 'TBitBtn' and caption(h) == 'Copy game'), None)
    if not btn:
        return 'skip', 'no "Copy game" TBitBtn on the client\'s TfrmBuildCamp'
    if not (u.IsWindowVisible(btn) and u.IsWindowEnabled(btn)):
        return 'skip', f'"Copy game" visible {bool(u.IsWindowVisible(btn))} enabled {bool(u.IsWindowEnabled(btn))}'
    u.PostMessageW(btn, 0x00F5, 0, 0)  # BM_CLICK
    dlg = wait_window(cp, 'TdlgInputNumber', 8)
    if not dlg:
        return 'skip', f'no TdlgInputNumber after BM_CLICK; windows: {describe_windows(cp)}'
    kids = children(dlg)
    spin = next((h for h in kids if e2e_test.class_name(h) == 'TSpinEdit'), None)
    ok = next((h for h in kids if e2e_test.class_name(h) == 'TButton' and caption(h) == 'OK'), None)
    cancel = next((h for h in kids if e2e_test.class_name(h) == 'TButton' and caption(h) == 'Cancel'), None)
    shown = text(spin) if spin else None
    if spin and ok:
        _u32t.SendMessageW(spin, 0x000C, 0, str(slot))  # WM_SETTEXT
        time.sleep(0.3)
    got = text(spin) if spin else None
    if got != str(slot):
        if cancel:
            u.PostMessageW(cancel, 0x00F5, 0, 0)
        wait_window(cp, 'TdlgInputNumber', 5, gone=True)
        return 'skip', f'TSpinEdit {bool(spin)}, OK {bool(ok)}; text {shown!r} -> {got!r}; cancelled'
    u.PostMessageW(ok, 0x00F5, 0, 0)
    gone = wait_window(cp, 'TdlgInputNumber', 5, gone=True)
    return 'ok', f'TdlgInputNumber "{e2e_test.window_text(dlg)}": seNumber {shown!r} -> {got!r}, OK (closed {gone})'


def buildcamp_idle(pid):
    """The strategic screen is visible and enabled and no dialog is open."""
    return not visible_boxes(pid) and any(u.IsWindowEnabled(h) for h in windows_of(pid, 'TfrmBuildCamp')
                                          if u.IsWindowVisible(h))


def single_player_month(ctx, slot, timeout=MONTH_TIMEOUT):
    """The client, out of the session, processes one month of the campaign in `slot` on its own: Turn, the game's
    dialogs answered (events: first answer). The game does not write the slot at the end of a month (the host's
    session saves it itself), so the month counts as done once the game's turn report (TdlgCampTurnRep "Turn
    messages") was answered and the strategic screen is idle for 3 s; no bridge call before that (the game's main
    thread is busy with the month). Then `save` writes the slot (a throwaway copy) and its date is read. Stops without
    answering anything if the battle screen (TfrmMain) or a dialog whose class names a battle shows up."""
    cp, d0, t0, n0 = ctx.cp, save_date(slot), time.time(), len(ANSWERED)
    r = cmd(C, 'turn')
    quiet = None
    while True:
        if time.time() - t0 > timeout:
            return dict(ok=False, why=f'timeout (turn report answered: {any("TdlgCampTurnRep" in a[2] for a in ANSWERED[n0:])}'
                        f'); windows: {describe_windows(cp)}', turn=r, d0=d0, d1=None, dt=time.time() - t0,
                        answered=ANSWERED[n0:])
        assert_alive()
        battle = [f'{e2e_test.class_name(h)}:"{e2e_test.window_text(h)[:40]}"' for h in top_windows(cp)
                  if e2e_test.class_name(h) == 'TfrmMain' or 'battle' in e2e_test.class_name(h).lower()]
        if battle:
            return dict(ok=False, why=f'battle: {battle}', turn=r, d0=d0, d1=None, dt=time.time() - t0,
                        answered=ANSWERED[n0:])
        select_first_answer(cp)
        answer_dialogs(cp, 'client')
        if any('TdlgCampTurnRep' in a[2] for a in ANSWERED[n0:]) and buildcamp_idle(cp):
            quiet = quiet or time.time()
            if time.time() - quiet >= 3:
                break
        else:
            quiet = None
        time.sleep(1.0)
    dt = time.time() - t0
    rs = cmd(C, 'save')
    time.sleep(1)
    return dict(ok=rs == 'ok', why='' if rs == 'ok' else f'save: {rs}', turn=r, d0=d0, d1=save_date(slot), dt=dt,
                answered=ANSWERED[n0:])


def next_month(d):
    y, m = int(d[0]), int(d[1])
    return (str(y + (m == 12)), str(m % 12 + 1))


def fmt_diffs(ds, n=5):
    return '; '.join(f'[{s}] {k}: want {e!r} got {a!r}' for s, k, e, a in ds[:n]) + (f' (+{len(ds) - n})' if len(ds) > n else '')


def scenario_player_copy(ctx):
    """0.2.7: a joined player's "Copy game" makes the copy the player's own campaign (nation K plays as nation 0, the
    host's nation becomes AI nation K). In the session: the client's copy (bridge `copygame` = the game's
    CopyGameToOtherSlot through the hook) checked key by key against the specification computed from slot 77; slot 77
    and the host's slot untouched; the host's own copy untouched; a copy cancelled at the overwrite prompt; the real
    "Copy game" button; one processed month (the session goes on). Out of the session: the client leaves, loads slot
    77 (it plays the host's nation there) and copies it (nation from rtw3mp_player.ini); then it loads its first copy
    and plays one month of it in single player."""
    hp, cp, K = ctx.hp, ctx.cp, 1
    st0, nat0 = ctl.status(C), nation_table(C)
    me, host = nat0[K]['name'], nat0[0]['name']
    src = slot_snapshot(CLIENT_SLOT)
    g1 = save_files(HOST_SLOT)
    eut = client_cooldowns()
    b77 = src['bcs']
    wars = [j for j in range(9) if j != K and int(b77.get(f'Nation{K}', {}).get('Tension', '0') if j == 0 else
                                                   b77.get(f'Nation{j}', {}).get(f'AITension{K}', '0') or 0) > 40]
    log(f'  start: client {st0.get("date")} slot {st0.get("slot")} playerIdx {st0.get("playerIdx")}; nation {K} '
        f'({me}) at war with {wars or "nobody"} in slot 77 (host: rel 0 1 {rel(0, 1)}, rel 1 2 {rel(1, 2)}); '
        f'client cooldowns {"from " + str(sum(1 for x in eut if x)) + " non-zero" if eut else "none"}; '
        f'slot 77 files {sorted(src["files"])}')

    # a) the client's copy in the session
    cm = log_mark(C)
    r = cmd(C, 'copygame 78')
    seen = copy_dialogs(cp, True, transform_done(C, cm, 78))
    conf, mp, game = sort_dialogs(seen)
    box = mp[0]['text'] if mp else ''
    want_box = [f'Slot 78 is your own copy of the campaign: you play {me}.',
                f'{host}, the host\'s nation, is run by the computer in this copy.']
    check(f'a) client `copygame 78`: the game\'s "Saved game copied" box (OK), then the mod\'s "Multiplayer" box naming '
          f'{me} and {host} (OK); no overwrite prompt, nothing left open',
          r == 'ok' and not conf and len(game) == 1 and game[0]['pressed'] == 'OK' and len(mp) == 1
          and mp[0]['pressed'] == 'OK' and all(w in box for w in want_box) and not visible_boxes(cp),
          f'{r}; {seen}; open: {describe_windows(cp)}')
    res = verify_copy(src, 78, K, eut)
    fx, dd, act = res['facts'], res['diffs'], res['act']['bcs'] or {}
    lines = copy_lines(C, cm, 78)
    m = re.search(PLAYER_COPY_RE, lines[0]) if lines else None
    want = ('78', me, host, str(K), str(fx['pairs']), str(fx['commanders']), str(fx['map_fields']), str(fx['div_kept']),
            str(fx['div_dropped']), 'at war' if fx['at_war'] else 'at peace')
    check(f'a) client log "player copy in slot 78: {me} is the player nation, {host} is nation {K} (AI); ..." with the '
          f'counts of the specification', len(lines) == 1 and m and m.groups() == want, f'{lines} (want {want})')
    check('a) Game78 files: RTWGame78.bcs/.sta/.off, MapData78.dat, DesignFiles0-8; .sta = the source\'s; no '
          'Autosave.sac/.off, BattleInfo.bbi or DesignFiles_swap.tmp', not res['files'],
          f'{res["files"]}; copy {fx["copy_files"]}; battle files in slot 77: {fx["battle_files_in_source"]}')
    log(f'  Game78: files {fx["copy_files"]}; slot 77 battle files {fx["battle_files_in_source"]}')
    s0, sk = src['bcs'], act
    names = (act.get('Nation0', {}).get('Name'), act.get(f'Nation{K}', {}).get('Name'))
    check(f'a) [Nation0] Name = {me}, [Nation{K}] Name = {host}; all {fx["pairs"]} [Nation0*]/[Nation{K}*] sections '
          f'traded (ship ids, coastal artillery, submarines, losses; other keys as the specification)',
          names == (me, host) and fx['pairs'] >= 4 and not dd.get('sections')
          and ship_ids(sk.get('Nation0Ships')) == ship_ids(s0.get(f'Nation{K}Ships'))
          and ship_ids(sk.get(f'Nation{K}Ships')) == ship_ids(s0.get('Nation0Ships')),
          f'names {names}; ships {len(ship_ids(sk.get("Nation0Ships")))}/{len(ship_ids(sk.get(f"Nation{K}Ships")))}; '
          f'{fmt_diffs(dd.get("sections", []))}')
    n1, n2 = sk.get(f'Nation{K}', {}), sk.get('Nation2', {})
    ex = (f'new Nation{K} ({host}) Tension {n1.get("Tension")} (old Nation{K} Tension {s0[f"Nation{K}"].get("Tension")}), '
          f'Nation2 Tension {n2.get("Tension")} (old Nation{K} AITension2 {s0[f"Nation{K}"].get("AITension2")}, old '
          f'Nation2 AITension{K} {s0["Nation2"].get(f"AITension{K}")}), Nation2 AITension{K} {n2.get(f"AITension{K}")} '
          f'(old Nation2 Tension {s0["Nation2"].get("Tension")})')
    check('a) relations exactly as specified (Tension/Allied/TechSharing/AITension*/AIAlliance* of Nation0-8, from '
          f'slot 77): {ex}', not dd.get('relations'), fmt_diffs(dd.get('relations', [])))
    log('  relations:', ex)
    check(f'a) nation numbers 0 <-> {K} (Ship*BuildingNationIdx, AU/AT*Nation, BattleAllied/EnemyAlliedNationIdx: '
          f'{fx["swaps"]} changed) and the host\'s ships without commanders ({fx["commanders"]} cleared)',
          not dd.get('numbers'), fmt_diffs(dd.get('numbers', [])))
    g0, g = s0['General'], act.get('General', {})
    check(f'a) [General]: War {g0.get("War")} -> {fx["war"][1]} ({me} at war with {fx["at_war"] or "nobody"}), EnemyVP '
          f'{fx["enemy_vp"]}, ScenarioInProgress -1, invasion targets XXX, ArmyOffensive/RevolutionPending 0, '
          f'LastMissionIdx 20; nothing else changed',
          not dd.get('general'), f'{fmt_diffs(dd.get("general", []))}; got War {g.get("War")} EnemyVP {g.get("EnemyVP")}')
    eut_src = [s0.get('EUT', {}).get(f'EUT{j}') for j in range(100)]
    check('a) [EUT] = the client\'s own event cooldowns' + (' (events_<hash>.ini)' if eut else
                                                             ' (no cooldown file: the source\'s values kept)'),
          not dd.get('eut'), fmt_diffs(dd.get('eut', [])))
    log(f'  EUT: client {[(j, x) for j, x in enumerate(eut or []) if x]} | source (host) '
        f'{[(j, x) for j, x in enumerate(eut_src) if x not in ("0", None)]}')
    check(f'a) MapData78: MineLevel 0/{K} and TakenFrom 0 <-> {K} ({fx["map_fields"]} fields changed), nothing else',
          not dd['map'], fmt_diffs(dd['map']))
    check(f'a) RTWGame78.off: officers kept, divisions {fx["div_kept"]} kept / {fx["div_dropped"]} dropped as specified',
          not dd['off'], fmt_diffs(dd['off']))
    check(f'a) design files swapped: DesignFiles0 = slot 77\'s DesignFiles{K}, DesignFiles{K} = its DesignFiles0, '
          f'the others identical', not [p for p in res['files'] if 'DesignFiles' in p], res['files'])
    check('a) nothing else in RTWGame78.bcs differs from slot 77', not dd.get('other'), fmt_diffs(dd.get('other', [])))

    # b) slot 77 and the host's slot untouched, the client still in the session
    st, nat = ctl.status(C), nation_table(C)
    check('b) slot 77 and the host\'s slot 1 byte-identical before and after the copy',
          save_files(CLIENT_SLOT) == src['files'] and save_files(HOST_SLOT) == g1,
          f'77 same {save_files(CLIENT_SLOT) == src["files"]}, 1 same {save_files(HOST_SLOT) == g1}')
    check('b) the client is still in the session: role client, planning, slot 77, playerIdx 1, same date and nations',
          (st.get('role'), st.get('phase'), st.get('slot'), st.get('playerIdx'), st.get('date')) ==
          ('client', 'planning', str(CLIENT_SLOT), str(K), st0.get('date')) and nat == nat0, f'{st} {nat == nat0}')

    # c) the host copies its own slot: untouched
    hm = log_mark(H)
    g1 = save_files(HOST_SLOT)
    r = cmd(H, 'copygame 79')
    seen = copy_dialogs(hp, True, lambda s: os.path.exists(save_path(79)) and any(d['pressed'] for d in s), who='host')
    conf, mp, game = sort_dialogs(seen)
    g79 = save_files(79)
    ren = {n.replace(f'RTWGame{HOST_SLOT}.', 'RTWGame79.').replace(f'MapData{HOST_SLOT}.', 'MapData79.'): h
           for n, h in g1.items()}
    diff79 = [n for n, h in g79.items() if ren.get(n) != h]
    n79 = (read_save(save_path(79)) or {}).get('Nation0', {}).get('Name')
    hl = log_lines(log_since(H, hm), r'player copy in slot|copy of slot')
    sth = ctl.status(H)
    check(f'c) host `copygame 79` from its own slot: the game\'s box only, Game79 = slot 1 file by file ([Nation0] '
          f'{n79}), no "player copy"/"copy of slot" line, the host still hosts slot 1 as nation 0',
          r == 'ok' and not conf and not mp and len(game) == 1 and 'RTWGame79.bcs' in g79 and not diff79
          and n79 == host and not hl and (sth.get('role'), sth.get('slot'), sth.get('playerIdx')) ==
          ('host', str(HOST_SLOT), '0'), f'{r}; {seen}; differ {diff79}; {n79}; {hl}; {sth}')

    # d) a copy cancelled at the game's overwrite prompt
    cm, g78 = log_mark(C), save_files(78)
    r = cmd(C, 'copygame 78')
    seen = copy_dialogs(cp, False, lambda s: bool(copy_lines(C, cm, 78)))
    conf, mp, game = sort_dialogs(seen)
    lines = copy_lines(C, cm, 78)
    check('d) client `copygame 78` onto the existing copy: the game\'s overwrite prompt answered No -> no other box, '
          'log "copy of slot 77 to 78: not copied", Game78 and slot 77 unchanged',
          r == 'ok' and len(conf) == 1 and conf[0]['pressed'] in ('No', 'Cancel') and not mp and not game
          and lines == ['copy of slot 77 to 78: not copied'] and save_files(78) == g78
          and save_files(CLIENT_SLOT) == src['files'], f'{r}; {seen}; {lines}; 78 same {save_files(78) == g78}')

    # g) the real "Copy game" button (its dialog takes slots 1-9: the highest slot that was free before the run)
    bs = _button_slot[0]
    if bs is None or os.path.exists(slot_dir(bs)):
        log(f'  SKIP g) the real "Copy game" button: no free slot in 3-9 (chosen {bs})')
        how, det = 'skip', ''
    else:
        cm = log_mark(C)
        how, det = copy_with_button(ctx, bs)
        log(f'  g) Copy game button: {how}: {det}')
    if how == 'ok':
        seen = copy_dialogs(cp, False, transform_done(C, cm, bs))
        conf, mp, game = sort_dialogs(seen)
        resb = verify_copy(src, bs, K, eut)
        lines = copy_lines(C, cm, bs)
        fb, f78 = resb['act']['files'], res['act']['files']
        same = (fb.get(f'RTWGame{bs}.bcs'), fb.get(f'MapData{bs}.dat')) == (f78.get('RTWGame78.bcs'), f78.get('MapData78.dat'))
        bad = [d for v in resb['diffs'].values() for d in v]
        check(f'g) the real "Copy game" button (BM_CLICK) -> TdlgInputNumber (seNumber {bs}, OK) -> the game\'s box and '
              f'the mod\'s box; "player copy in slot {bs}" line; Game{bs} as specified, its .bcs/MapData = Game78\'s',
              not conf and len(game) == 1 and len(mp) == 1
              and all(w.replace('Slot 78', f'Slot {bs}') in mp[0]['text'] for w in want_box) and len(lines) == 1
              and lines[0].startswith(f'player copy in slot {bs}: {me} is the player nation') and not bad
              and not resb['files'] and same, f'{det}; {seen}; {lines}; {fmt_diffs(bad)} {resb["files"]}; same as 78 {same}')
    elif det:
        log(f'  SKIP g) the real "Copy game" button: {det}')

    # the session goes on
    d0 = ctl.status(C).get('date')
    d1 = run_month(ctx, 'player_copy')
    st = ctl.status(C)
    merged = log_lines(log_since(H, ctx.month_marks[0]), rf'merge nation {K}\b')
    check('the session goes on after the copies: the month was processed (host "merge nation 1"), the client plans the '
          'new month in slot 77 as nation 1', d1 and d1 != d0 and merged and (st.get('role'), st.get('phase'),
          st.get('slot'), st.get('playerIdx')) == ('client', 'planning', str(CLIENT_SLOT), str(K)),
          f'{d0} -> {d1}; {merged[:1]}; {st}')

    # e) out of the session: the client leaves, opens slot 77 (the host's nation) and copies it
    cm = log_mark(C)
    rl = cmd(C, 'leave')
    time.sleep(1.5)
    role = ctl.status(C).get('role')
    try:
        with open(os.path.join(slot_dir(CLIENT_SLOT), 'rtw3mp_player.ini'), encoding='utf-8', errors='replace') as f:
            ini = f.read().strip()
    except OSError:
        ini = None
    r = cmd(C, f'load {CLIENT_SLOT} 0')
    st, nat = ctl.status(C), nation_table(C)
    boxes = [box_info(h)[0] for h in visible_boxes(cp)]
    check(f'e) client `leave` (role {role}), slot 77\'s rtw3mp_player.ini {ini!r}, `load 77 0`: slot 77 as nation 0 = '
          f'{host} (the host\'s nation, what the 0.2.6 copy played), no dialog',
          rl == 'ok' and role == 'none' and ini and re.search(rf'nation={K}\b', ini) and r == 'ok'
          and (st.get('slot'), st.get('playerIdx'), st.get('startBuild')) == (str(CLIENT_SLOT), '0', '2')
          and nat.get(0, {}).get('name') == host and not boxes, f'{r}; {st}; {nat.get(0)}; {boxes}')
    src80 = slot_snapshot(CLIENT_SLOT)
    cm = log_mark(C)
    r = cmd(C, 'copygame 80')
    seen = copy_dialogs(cp, False, transform_done(C, cm, 80))
    conf, mp, game = sort_dialogs(seen)
    lines = copy_lines(C, cm, 80)
    res80 = verify_copy(src80, 80, K, None)
    fx80 = res80['facts']
    m = re.search(PLAYER_COPY_RE, lines[0]) if lines else None
    want = ('80', me, host, str(K), str(fx80['pairs']), str(fx80['commanders']), str(fx80['map_fields']),
            str(fx80['div_kept']), str(fx80['div_dropped']), 'at war' if fx80['at_war'] else 'at peace')
    check('e) out of the session `copygame 80`: the game\'s box and the mod\'s box (naming both nations), the log line '
          f'"player copy in slot 80: {me} is the player nation, {host} is nation {K} (AI); ..." (nation from '
          'rtw3mp_player.ini)', r == 'ok' and not conf and len(game) == 1 and len(mp) == 1
          and all(w.replace('78', '80') in mp[0]['text'] for w in want_box) and m and m.groups() == want,
          f'{r}; {seen}; {lines} (want {want})')
    bad = [d for v in res80['diffs'].values() for d in v]
    a80 = res80['act']['bcs'] or {}
    check(f'e) Game80 as specified from slot 77\'s new month (relations, sections, numbers, [General], [EUT] kept '
          f'outside the session, MapData {fx80["map_fields"]} fields, divisions {fx80["div_kept"]}/{fx80["div_dropped"]}, '
          f'designs, files): [Nation0] {a80.get("Nation0", {}).get("Name")}, [Nation{K}] '
          f'{a80.get(f"Nation{K}", {}).get("Name")}', not bad and not res80['files']
          and (a80.get('Nation0', {}).get('Name'), a80.get(f'Nation{K}', {}).get('Name')) == (me, host),
          f'{fmt_diffs(bad)} {res80["files"]}')
    log(f'  Game80 facts: at war {fx80["at_war"]}, War {fx80["war"]}, EnemyVP {fx80["enemy_vp"]}, '
        f'{fx80["swaps"]} numbers, {fx80["commanders"]} commanders, {fx80["map_fields"]} map fields')

    # f) the key check: the copy is playable
    cm = log_mark(C)
    f78 = read_save(save_path(78))
    r = cmd(C, 'load 78 0')
    time.sleep(1.5)
    st, nat = ctl.status(C), nation_table(C)
    boxes = [box_info(h)[0] for h in visible_boxes(cp)]
    check('f) client `load 78 0`: slot 78, playerIdx 0, campaign loaded (startBuild 2), no dialog',
          r == 'ok' and (st.get('slot'), st.get('playerIdx'), st.get('startBuild')) == ('78', '0', '2') and not boxes,
          f'{r}; {st}; {boxes}')
    want_nat = {0: nat0[K], K: nat0[0], **{j: nat0[j] for j in nat0 if j not in (0, K)}}
    check(f'f) `nations`: nation 0 = {me} with its session funds/ships {nat0[K]["funds"]}/{nat0[K]["ships"]}, nation '
          f'{K} = {host} with {nat0[0]["funds"]}/{nat0[0]["ships"]}, the others unchanged', nat == want_nat,
          f'{nat} (want {want_nat})')
    rels = {j: dict(kv.split('=') for kv in (cmd(C, f'rel 0 {j}')[3:]).split()) for j in range(1, 9)}
    ai = {j: dict(kv.split('=') for kv in (cmd(C, f'rel {K} {j}')[3:]).split()) for j in range(2, 9)}
    ft = {j: f78[f'Nation{j}'].get('Tension') for j in range(1, 9)}
    fa = {j: f78[f'Nation{K}'].get(f'AITension{j}') for j in range(2, 9)}
    nwar = sum(1 for j in ft if int(ft[j]) > 40)
    check(f'f) relations in memory (the relation bars): `rel 0 j` = the copy\'s [Nation j] Tension, `rel {K} j` = '
          f'[Nation{K}] AITension j, warCounter = [General] War {f78["General"].get("War")}, hostWars {nwar}',
          all(rels[j].get('tension') == ft[j] and rels[j].get('war') == ('1' if int(ft[j]) > 40 else '0') for j in ft)
          and all(ai[j].get('tension') == fa[j] for j in fa) and rels[1].get('warCounter') == f78['General'].get('War')
          and rels[1].get('hostWars') == str(nwar),
          f'rel 0 j {[(j, rels[j].get("tension"), ft[j]) for j in ft]}; rel {K} j {[(j, ai[j].get("tension"), fa[j]) for j in fa]}; '
          f'{rels[1]}')
    p_host = poss_list(C, f'possessions {K}')
    if fx['at_war']:
        log(f'  WARNING: {me} is at war with {fx["at_war"]} in the copy: the month may generate a battle')
    sp = single_player_month(ctx, 78)
    ctx.months.append(('player_copy (slot 78 alone)', '/'.join(sp['d0'] or ()), '/'.join(sp['d1'] or ()), sp['dt']))
    log(f'  single-player month on slot 78: {sp["d0"]} -> {sp["d1"]} in {sp["dt"]:.0f} s; {len(sp["answered"])} '
        f'dialogs answered: {[a[2] for a in sp["answered"]][:12]}' + (f'; {sp["why"]}' if sp['why'] else ''))
    check('f) one month of the copy in single player (`turn`, the game\'s dialogs answered up to its turn report, no '
          'battle), then `save` writes slot 78 with the next month\'s date', sp['ok'] and sp['d0']
          and sp['d1'] == next_month(sp['d0']), f'{sp["why"]}; {sp["d0"]} -> {sp["d1"]}')
    st, nat2 = ctl.status(C), nation_table(C)
    p_host2 = poss_list(C, f'possessions {K}')
    cl = log_since(C, cm)
    bad = log_lines(cl, r'(?i)exception|AI suppressed|merge nation|player copy')
    check(f'f) after the month: responsive (slot 78, playerIdx 0, not busy), nation 0 {me}, {host} (AI) still has ships '
          f'({nat.get(K, {}).get("ships")} -> {nat2.get(K, {}).get("ships")}) and possessions ({len(p_host or [])} -> '
          f'{len(p_host2 or [])}); no exception or session line in the client log',
          (st.get('slot'), st.get('playerIdx'), st.get('busy')) == ('78', '0', '0')
          and nat2.get(0, {}).get('name') == me and nat2.get(K, {}).get('name') == host
          and nat2.get(K, {}).get('ships', 0) > 0 and p_host2 and bad == [], f'{st}; {nat2}; {bad[:3]}')
    log(f'  after the month: {nat2}; {host} possessions {pnames(p_host)} -> {pnames(p_host2)}')


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
    ('victory_points', 1, 'joined players\' victory points (0.2.6): a host battle against a joined nation credits '
                          'it (battlevp = the game\'s HandleBattleAftermath), EnemyVP outside battles (enemyvpadd) is '
                          'shared among the host\'s enemies at month end (1 enemy, 2 enemies 51/50, a drop shares '
                          'nothing), peace -> VP 0 (buttons, DoPeace), the merge keeps the host\'s VP and the client '
                          'shows its own enemies\' VP (one month with the war 1-2 only), AI peace keeps the VP of '
                          'player nations at war', scenario_victory_points),
    ('treaty', 1, 'peace terms of wars between players (0.2.5): war score (warvp) -> major/minor/compromise terms frozen '
                  'at acceptance, claims validated (client and host), the "Claim land..." window (real button, '
                  'list view, posted clicks), calling the peace off erases the terms, the processed month applies the '
                  'treaty (possessions, ships, prestige, reparations, chat, the client\'s treaty dialog, war score '
                  'lines); then via applydiplo: automatic minor victory of nation 2, host-vs-client victory (DoPeace, '
                  'the host\'s treaty dialog), compromise', scenario_treaty),
    ('player_copy', 1, 'a joined player\'s "Copy game" (0.2.7): the client\'s copy (copygame = the game\'s '
                       'CopyGameToOtherSlot through the hook) is its own campaign (its nation is nation 0, the host\'s '
                       'nation AI nation 1; relations, sections, numbers, war state, cooldowns, map, divisions, designs '
                       'checked key by key against slot 77); slot 77 and the host\'s slot untouched; the host\'s own copy '
                       'untouched; a copy cancelled at the overwrite prompt; the real button; one session month; out of '
                       'the session (rtw3mp_player.ini) the copy is made too, and the copy loads and plays one month in '
                       'single player (slots 78-80 and, for the button, the highest slot of 3-9 that was free; all '
                       'deleted after the run)', scenario_player_copy),
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


def preflight(g1, copy_slots=()):
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
    for s in copy_slots:  # player_copy writes these slots and deletes them afterwards: never someone's own save
        if os.path.exists(slot_dir(s)):
            problems.append(f'save slot {s} exists ({slot_dir(s)}); player_copy would overwrite and delete it: move it '
                            f'away first')
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


def remove_copy_slots(slots):
    """Delete player_copy's slots (preflight made sure none of them existed before the run). Returns those left."""
    for s in slots:
        for _ in range(10):
            shutil.rmtree(slot_dir(s), ignore_errors=True)
            if not os.path.exists(slot_dir(s)):
                break
            time.sleep(1)
    left = [s for s in slots if os.path.exists(slot_dir(s))]
    gone = [s for s in slots if not os.path.exists(slot_dir(s))]
    log(f'removed copy slots {gone}' + (f'; could NOT remove {left}' if left else ''))
    return left


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


def summary(names, open_s, ctx, saves_ok, left, ev_left=(), copies_left=()):
    print()
    log('==== summary ====')
    for name in names:
        rs = [r for r in results if r[0] == name]
        n_ok = sum(1 for r in rs if r[2])
        print(f'  {name:<16} {n_ok:>3}/{len(rs):<3} {"PASS" if rs and n_ok == len(rs) else "FAIL"}')
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
          + (f'event cooldown files LEFT: {list(ev_left)}' if ev_left else 'no events_*.ini left')
          + (f'; copy slots LEFT: {list(copies_left)}' if copies_left else ''))
    n = len(results)
    print(f'{n - len(failed)}/{n} checks passed', flush=True)
    return not failed


def main(argv=None):
    ap = argparse.ArgumentParser(description='RTW3 Multiplayer regression suite (two live game copies).')
    ap.add_argument('--list', action='store_true', help='list the scenarios and exit')
    ap.add_argument('--only', help='comma-separated scenarios to run (session is always included)')
    a = ap.parse_args(argv)
    if a.list:
        print(f'{"scenario":<16}{"months":<8}what it guards')
        for name, months, what, _ in SCENARIOS:
            print(f'{name:<16}{months:<8}{what}')
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
    copy_slots = ()
    if 'player_copy' in names:
        _button_slot[0] = next((s for s in BUTTON_SLOTS if not os.path.exists(slot_dir(s))), None)
        copy_slots = COPY_SLOTS + ((_button_slot[0],) if _button_slot[0] else ())
    problems = preflight(g1, COPY_SLOTS if copy_slots else ())
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
    saves_ok, left, ev_left, copies_left = False, [], [], []
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
        copies_left = remove_copy_slots(copy_slots) if copy_slots else []
        ev_left = remove_event_files('after the run')
        if ev_left:
            log('!! could not delete the event cooldown files', ev_left)
    passed = summary(names, open_s, ctx, saves_ok, left, ev_left, copies_left)
    return 0 if passed and saves_ok and not left and not ev_left and not copies_left else 1


if __name__ == '__main__':
    sys.exit(main())
