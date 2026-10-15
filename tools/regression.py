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
import argparse, ctypes, ctypes.wintypes as wt, hashlib, os, random, re, shutil, struct, subprocess, sys, tempfile, threading, time

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
    recorded (TREATY_SEEN) before it is answered. An event answer that names a nation gets the first nation picked
    first (select_event_nation)."""
    capture_treaty(pid, who)
    select_event_nation(pid)
    select_officer(pid)
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


def run_month(ctx, label, driver=None):
    """Process one month: the client submits, the host waits for everyone, presses Turn and its dialogs (and the
    client's) are answered until the client plans the new month. Never polls the host's bridge meanwhile: its
    calls run on the game's main thread, which is busy with the month. Then the client's event roll of the new
    month is awaited (its dialog answered: an open event dialog refuses most bridge commands), and the per-month
    checks (intel reports, saved files, month package size, event roll) run.
    driver: a BattleDriver for a month with joined players' battles (it answers the client's dialogs, never
    dismiss_dialogs); without one, a battle the month sends the client anyway is declined (battle_dialogs)."""
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
    timeout = MONTH_TIMEOUT + (driver.extra_timeout if driver else 0)
    end = time.time() + timeout
    ev_seen = []  # the client's event dialogs (the new month's roll may show one before the month is detected)
    while time.time() < end:
        assert_alive()
        answer_host_box(ctx.hp)
        answer_dialogs(ctx.hp, 'host')
        if driver is not None:
            driver.poll()
        elif battle_up(ctx.cp):  # a battle nobody planned (nation 1 at war with an AI nation): declined
            battle_dialogs(ctx.cp, 'decline', UNPLANNED)
        else:
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
        time.sleep(0.5 if driver is not None else 1.5)
    raise RuntimeError(f'the month did not advance within {timeout} s ({label}); host windows: '
                       f'{describe_windows(ctx.hp)}; client windows: {describe_windows(ctx.cp)}')


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

def expected_version():
    """(MP_MOD_VERSION, MP_PROTOCOL) of this source tree (src/mod/common.h, read only)."""
    try:
        with open(os.path.join(REPO, 'src', 'mod', 'common.h'), encoding='utf-8') as f:
            txt = f.read()
    except OSError:
        return None, None
    v, p = re.search(r'#define MP_MOD_VERSION "([^"]+)"', txt), re.search(r'#define MP_PROTOCOL (\d+)', txt)
    return (v[1] if v else None), (p[1] if p else None)


def scenario_session(ctx):
    ver, proto = expected_version()
    want = f'RTW3MP {ver} starting (protocol {proto})'
    vl = {who: [x.split('] ', 1)[-1] for x in log_lines(log_since(port), r'RTW3MP \S+ starting \(protocol')][:1]
          for who, port in (('host', H), ('client', C))}
    check(f'both copies run the version of this source tree: "{want}" in both logs (src/mod/common.h)',
          ver and proto and all(v and v[0] == want for v in vl.values()), vl)
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


def select_event_nation(pid):
    """Event dialogs whose answer names a nation (TdlgEventAnswer's cbSelectNation, e.g. "... negotiating an agreement
    with"): select the answer dismiss_dialogs would select (the "Not now" radio, else the first radio in window order),
    then pick the first nation if the combo is shown and empty (CB_SETCURSEL, then CBN_SELCHANGE as a real pick). OK
    without a nation only brings a "Rule the Waves III" box and the same event again (a host loop seen in Jan 1901)."""
    for dlg in event_dialogs(pid):
        kids = children(dlg)
        radios = [h for h in kids if e2e_test.class_name(h) == 'TRadioButton' and u.IsWindowVisible(h)
                  and u.IsWindowEnabled(h)]
        if radios and not any(u.SendMessageW(h, 0x00F0, 0, 0) for h in radios):  # BM_GETCHECK
            pick = next((h for h in radios if caption(h).lower() == 'not now'), radios[0])
            u.SendMessageW(pick, 0x00F5, 0, 0)  # BM_CLICK
            time.sleep(0.3)
        for cb in kids:
            if e2e_test.class_name(cb) == 'TComboBox' and u.IsWindowVisible(cb) \
                    and u.SendMessageW(cb, 0x0147, 0, 0) == -1 and u.SendMessageW(cb, 0x0146, 0, 0) > 0:
                u.SendMessageW(cb, 0x014E, 0, 0)  # CB_SETCURSEL 0
                u.PostMessageW(u.GetParent(cb), 0x0111, (1 << 16) | (u.GetDlgCtrlID(cb) & 0xFFFF), cb)
                time.sleep(0.3)


def select_officer(pid):
    """The game's "Select Officer" dialog (TdlgSelectOfficer: lvOfficers + OK/Cancel, an officer to appoint): select
    the first officer if none is selected (LVM_SETITEMSTATE, as a click). OK without a selection only brings a "Rule
    the Waves III" box and the dialog again (a host loop seen in the merge month)."""
    for dlg in [h for h in windows_of(pid, 'TdlgSelectOfficer') if u.IsWindowVisible(h)]:
        lv = next((h for h in children(dlg) if e2e_test.class_name(h) == 'TListView'), None)
        n = u.SendMessageW(lv, LVM_GETITEMCOUNT, 0, 0) if lv else 0
        if n > 0 and u.SendMessageW(lv, 0x100C, -1, 0x2) == -1:  # LVM_GETNEXTITEM(-1, LVNI_SELECTED): none
            lv_select(pid, lv, 0)
            log(f'  selected the first of {n} officers in "Select Officer"')
            time.sleep(0.3)


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
    """Months the selected scenarios process (doctrine, research and events process their own if no later scenario
    does)."""
    n = sum(s[1] for s in SCENARIOS if s[0] in names)
    if 'doctrine' in names and 'design_study' not in names:
        n += 2
    order = [s[0] for s in SCENARIOS]
    if 'research' in names and planned_months([x for x in names if order.index(x) > order.index('research')]) == 0:
        n += 1
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


# ---------------------------------------------------------------- research screen (0.2.8)

PRIO = {'Low': 1, 'Medium': 2, 'High': 3}  # the Priority column of TfrmResearch.lvResearch <-> ResearchAreas[i].Prio
RESEARCH_LINE = r"research screen: opened for the player's nation (\d+) instead of nation 0"
RESEARCH_MAX = 12  # seResearchBudget.MaxValue; Button1Click refuses more with the game's own message
MN_GETHMENU, TCM_GETCURSEL = 0x01E1, 0x130B
_u32m = ctypes.WinDLL('user32')  # popup menus of a game copy (HMENU handles are valid across processes)
_u32m.GetMenuItemCount.argtypes = [wt.HMENU]
_u32m.GetMenuStringW.argtypes = [wt.HMENU, wt.UINT, wt.LPWSTR, ctypes.c_int, wt.UINT]
_u32m.GetMenuState.argtypes = [wt.HMENU, wt.UINT, wt.UINT]
_u32m.GetMenuState.restype = wt.UINT


def research(port, nation):
    """`research <nation>` as dict(pct, prio=[25 values]), or None."""
    m = re.match(r'^ok pct=(-?\d+) prio=([-\d,]+)$', cmd(port, f'research {nation}'))
    return dict(pct=int(m[1]), prio=[int(x) for x in m[2].split(',')]) if m else None


def fmt_research(r):
    return f'pct={r["pct"]} prio={",".join(map(str, r["prio"]))}' if r else str(r)


RESEARCH_RUNS = 100  # `researchrun` count: P(at least one of DoResearch's 5 % full re-rolls) = 1 - 0.95^100 = 99.4 %
RESEARCH_CONTROL = 8  # an AI nation no other scenario looks at (its research jumps ahead by RESEARCH_RUNS months)
RESEARCH_TEST_RE = (r'research test nation (\d+) x (\d+): before ok pct=(-?\d+) prio=([-\d,]+) after ok pct=(-?\d+) '
                    r'prio=([-\d,]+)')


def research_run(ctx, nation, count, timeout=180):
    """Host `researchrun <nation> <count>` (async: the game's TfrmBuildCamp.DoResearch for that nation `count` times
    through the mod's hook, on the main thread): waits for its `research test nation N x count: before ... after ...`
    line, answering the host's dialogs meanwhile (the game may show research messages; the host's bridge is not polled).
    Returns dict(reply, before, after (pct/prio dicts), kept (the hook's "kept" lines for that nation), line, seconds)."""
    mark, t0 = log_mark(H), time.time()
    reply = cmd(H, f'researchrun {nation} {count}')
    m = None
    while reply == 'ok' and time.time() - t0 < timeout:
        answer_dialogs(ctx.hp, 'host')
        m = re.search(RESEARCH_TEST_RE, log_since(H, mark))
        if m or 'research test: not possible' in log_since(H, mark):
            break
        time.sleep(0.5)
    time.sleep(0.5)
    answer_dialogs(ctx.hp, 'host')
    prio = lambda s: [int(x) for x in s.split(',')]  # noqa: E731
    kept = [x.split('] ', 1)[-1] for x in log_lines(log_since(H, mark), rf'research priorities of player nation '
                                                                         rf'{nation} kept \(\d+ changed')]
    return dict(reply=reply, line=m[0] if m else None, seconds=round(time.time() - t0, 1), kept=kept,
                before=dict(pct=int(m[3]), prio=prio(m[4])) if m else None,
                after=dict(pct=int(m[5]), prio=prio(m[6])) if m else None,
                other=log_lines(log_since(H, mark), r'research (test|priorities)')[:4])


def research_dialogs(pid):
    return [h for h in windows_of(pid, 'TfrmResearch') if u.IsWindowVisible(h)]


def message_forms(pid):
    return [h for h in top_windows(pid) if e2e_test.class_name(h) == 'TMessageForm']


def research_controls(dlg):
    """TfrmResearch (form resource TFRMRESEARCH): lvResearch (report view: Research area, Last research, Priority,
    Levels; PopupMenu High/Medium/Low/-/Details), seResearchBudget (TSpinEdit 0-12), tcResearch (tabs General, Naval
    guns), Button1 "OK" (mrOK), bnMedium "All medium", bnLow "All low" (every area)."""
    kids, k = children(dlg), e2e_test.class_name
    return dict(dlg=dlg, lv=next((h for h in kids if k(h) == 'TListView'), None),
                se=next((h for h in kids if k(h) == 'TSpinEdit'), None),
                tc=next((h for h in kids if k(h) == 'TTabControl'), None),
                ok=next((h for h in kids if k(h) == 'TButton' and caption(h) == 'OK'), None))


def research_areas():
    """{area name: index} from the game's own area table (`Data\\ResearchAreas3.dat`, read-only; headers `[Name i]`).
    The dialog lists only the areas the era offers (1900: 0-10, 12, 13) plus a "Naval guns" row (gun calibers), which
    is not in the table and not compared."""
    try:
        with open(os.path.join(ctl.GAME, 'Data', 'ResearchAreas3.dat'), encoding='latin-1') as f:
            return {m[1]: int(m[2]) for m in re.finditer(r'^\[(.+) (\d+)\]\s*$', f.read(), re.M)}
    except OSError:
        return {}


def research_view(pid, c):
    """What the dialog shows: the budget text, the tab, and the General tab's rows (area name, Priority text) with the
    area index of each row (None if not in the game's table) and its priority (Low/Medium/High = 1/2/3)."""
    rows = [(t[0], t[2]) for _, _, t in lv_rows(pid, c['lv'], 4)] if c['lv'] else []
    table = research_areas()
    tab = _u32.SendMessageW(c['tc'], TCM_GETCURSEL, 0, 0) if c['tc'] else None
    return dict(budget=text(c['se']) if c['se'] else None, rows=rows, tab=tab,
                areas=[(table.get(re.sub(r' \(Adv\)$', '', n)), PRIO.get(p)) for n, p in rows])


def shown_prio(view):
    """{area index: priority shown} for the rows whose area the game's table names."""
    return {i: p for i, p in (view or {}).get('areas', []) if i is not None}


def shows(view, r):
    """The dialog shows research values r: the budget and the priority of every listed area (at least 10 known)."""
    sp = shown_prio(view)
    return bool(view and r and view['budget'] == str(r['pct']) and len(sp) >= 10
                and all(r['prio'][i] == p for i, p in sp.items()))


def fmt_rview(view):
    return f'budget "{view["budget"]}" prio {shown_prio(view)} other rows ' \
           f'{[(n, p) for (n, p), (i, _) in zip(view["rows"], view["areas"]) if i is None]}' if view else str(view)


def open_research(port, pid, timeout=8):
    """`researchopen` (async: the Research button's handler on the main thread) -> (reply, controls or None)."""
    r = cmd(port, 'researchopen')
    dlg = wait_window(pid, 'TfrmResearch', timeout)
    return r, research_controls(dlg) if dlg else None


def close_research(pid, c, ok=False):
    """OK (BM_CLICK on Button1: Execute writes the budget back) or WM_CLOSE (mrCancel: the budget is not written;
    priorities change at once in the popup menu's handlers). True once the dialog is gone."""
    u.PostMessageW(c['ok'] if ok else c['dlg'], 0x00F5 if ok else 0x0010, 0, 0)
    return bool(wait_window(pid, 'TfrmResearch', 6, gone=True))


def popup_priority(pid, lv, row, label):
    """Set one area's priority without the mouse: select the row (LVM_SETITEMSTATE), post WM_CONTEXTMENU in its
    keyboard form (lParam -1) to the list, which opens the game's TPopupMenu (TrackPopupMenu, a #32768 window); inside
    the menu loop posted VK_DOWN/VK_UP move the highlight (checked with GetMenuState MF_HILITE on the menu's HMENU from
    MN_GETHMENU) and VK_RETURN picks the item, whose OnClick sets the selected area's priority. Returns a description;
    'picked' in it on success."""
    lv_select(pid, lv, row)
    time.sleep(0.3)
    u.PostMessageW(lv, 0x007B, lv, -1)  # WM_CONTEXTMENU
    end, menu = time.time() + 4, None
    while time.time() < end and not menu:
        time.sleep(0.2)
        menu = next((h for h in top_windows(pid) if e2e_test.class_name(h) == '#32768'), None)
    if not menu:
        return f'no popup menu after WM_CONTEXTMENU; windows {describe_windows(pid)}'
    hm = _u32.SendMessageW(menu, MN_GETHMENU, 0, 0)
    n = _u32m.GetMenuItemCount(hm) if hm else 0
    names = []
    for i in range(max(0, n)):
        b = ctypes.create_unicode_buffer(64)
        _u32m.GetMenuStringW(hm, i, b, 64, 0x400)  # MF_BYPOSITION
        names.append(b.value.replace('&', ''))
    idx = names.index(label) if label in names else None
    hil = None
    for _ in range(10 if idx is not None else 0):
        hil = next((i for i in range(n) if _u32m.GetMenuState(hm, i, 0x400) & 0x80), None)  # MF_HILITE
        if hil == idx:
            break
        u.PostMessageW(menu, 0x0100, 0x28 if hil is None or hil < idx else 0x26, 0)  # WM_KEYDOWN VK_DOWN / VK_UP
        time.sleep(0.25)
    if idx is None or hil != idx:
        u.PostMessageW(menu, 0x0100, 0x1B, 0)  # VK_ESCAPE
        return f'menu {names}: could not highlight "{label}" (at {hil}); escaped'
    u.PostMessageW(menu, 0x0100, 0x0D, 0)  # VK_RETURN
    left = wait_until(lambda: [h for h in top_windows(pid) if e2e_test.class_name(h) == '#32768'], lambda v: not v, 4)
    return f'menu {names}: picked "{label}" for row {row}' + (' (menu still open)' if left else '')


def research_lines(port, mark):
    return [x.split('] ', 1)[-1] for x in log_lines(log_since(port, mark), RESEARCH_LINE)]


def scenario_research(ctx):
    """0.2.8: the strategic screen's Research button opens TfrmResearch for BuilderNations[0]; on a joined player's PC
    that is the host's nation, so the player saw and edited the host's research budget and priorities (0.2.0 report).
    The mod's TfrmResearch.Execute hook opens the player's own nation there. Client: the dialog shows nation 1, a
    priority (popup menu) and the budget changed in it reach nation 1 only, more than 12 % is refused by the game's
    own message; host: the dialog is unchanged (nation 0). After the next processed month (shared; one own month if no
    later scenario processes any): the host has the client's values for nation 1 and its own for nation 0, the client
    still shows them; e) (ebfca72) host `researchrun 1 100`: the game's DoResearch with its AI research choices (5 % a
    month a full re-roll) keeps the joined nation's priorities (the hook's "kept" line); control `researchrun 8 100` on
    an AI nation (recorded, not protected)."""
    t0, cp, hp = time.time(), ctx.cp, ctx.hp
    rc0, rc1, rh0, rh1 = research(C, 0), research(C, 1), research(H, 0), research(H, 1)
    log(f'  research before: client 0 {fmt_research(rc0)} | client 1 {fmt_research(rc1)} | host 0 {fmt_research(rh0)} '
        f'| host 1 {fmt_research(rh1)}')
    if not check('research readable on host and client (25 areas each)',
                 all(r and len(r['prio']) == 25 for r in (rc0, rc1, rh0, rh1)), f'{rc0} {rc1} {rh0} {rh1}'):
        return
    if rc1['pct'] > RESEARCH_MAX:  # e.g. an AI-era 13 (fixture) or merge's 14: what the dialog shows, then cancelled
        r, c = open_research(C, cp)
        if c:
            log(f'  observed: nation 1 at {rc1["pct"]} % (> {RESEARCH_MAX}); the dialog shows "{text(c["se"])}"; '
                f'closed with WM_CLOSE: {close_research(cp, c)}; nation 1 now {fmt_research(research(C, 1))}')
    v = next(x for x in (10, 11, 9) if x != rc0['pct'])
    r = cmd(C, f'nfield 1 ResearchPct {v}')
    rc1 = research(C, 1)
    diff = [i for i in range(25) if rc1['prio'][i] != rc0['prio'][i]]
    check(f'setup: nation 1\'s research % set to {v} on the client (<= {RESEARCH_MAX}, nation 0 has {rc0["pct"]})',
          value(r) == str(v) and rc1['pct'] == v, f'{r}; {fmt_research(rc1)}')
    log(f'  nation 1 vs nation 0: pct {rc1["pct"]} vs {rc0["pct"]}, priorities differ in areas {diff}')

    # a) the client's Research button opens nation 1
    cm = log_mark(C)
    r, c = open_research(C, cp)
    if not check('a) client: researchopen opens TfrmResearch', r == 'ok' and c, f'{r}; {describe_windows(cp)}'):
        return
    view = research_view(cp, c)
    sp = shown_prio(view)
    log(f'  client dialog: tab {view["tab"]}, {fmt_rview(view)} ({len(view["rows"])} rows)')
    ln = research_lines(C, cm)
    check('a) client log: "research screen: opened for the player\'s nation 1 instead of nation 0"',
          ln == ["research screen: opened for the player's nation 1 instead of nation 0"], ln)
    check(f'a) client dialog shows nation 1\'s budget ({rc1["pct"]}), not nation 0\'s ({rc0["pct"]})',
          view['budget'] == str(rc1['pct']) and view['budget'] != str(rc0['pct']), view['budget'])
    shown_diff = [i for i in diff if i in sp]
    check('a) client dialog shows nation 1\'s priorities' + (f', not nation 0\'s (areas {shown_diff})' if shown_diff
                                                              else ''),
          shows(dict(view, budget=str(rc1['pct'])), rc1) and all(sp[i] != rc0['prio'][i] for i in shown_diff),
          f'shown {sp}; nation 1 {rc1["prio"]}; nation 0 {rc0["prio"]}')

    # b) the player changes one priority (popup menu) and the budget, then OK. Preferably an area whose row is not its
    # index (the list skips areas the era doesn't offer), so the row -> area mapping is exercised too.
    rowof = {i: k for k, (i, _) in enumerate(view['areas']) if i is not None}
    area = next((i for i in shown_diff if rowof[i] != i), shown_diff[0] if shown_diff else next(iter(sp), 0))
    row = rowof.get(area, 0)
    new_p = next(p for p in (3, 1, 2) if p not in (rc1['prio'][area], rc0['prio'][area]))
    label = next(k for k, p in PRIO.items() if p == new_p)
    how = popup_priority(cp, c['lv'], row, label)
    time.sleep(0.5)
    v2, mid1, mid0 = research_view(cp, c), research(C, 1), research(C, 0)
    want = dict(pct=rc1['pct'], prio=rc1['prio'][:area] + [new_p] + rc1['prio'][area + 1:])
    check(f'b) client: popup menu "{label}" on row {row} ({view["rows"][row][0] if row < len(view["rows"]) else "?"}, '
          f'area {area}) changes nation 1 only (dialog open)',
          'picked' in how and shown_prio(v2).get(area) == new_p and mid1 == want and mid0 == rc0,
          f'{how}; shown {shown_prio(v2)}; nation 1 {fmt_research(mid1)} (want {fmt_research(want)}); nation 0 '
          f'{fmt_research(mid0)}')
    log(f'  {how}')
    b = next(x for x in (7, 6, 5, 4) if x not in (rc1['pct'], rc0['pct']))
    _u32t.SendMessageW(c['se'], 0x000C, 0, str(b))  # WM_SETTEXT
    time.sleep(0.3)
    typed = text(c['se'])
    closed = close_research(cp, c, ok=True)
    boxes = message_forms(cp)
    check(f'b) client: budget {b} typed, OK closes the dialog without a message', typed == str(b) and closed and
          not boxes, f'typed {typed!r}, closed {closed}, message boxes {len(boxes)}')
    want['pct'] = b
    a1, a0 = research(C, 1), research(C, 0)
    check(f'b) client: nation 1 has the new budget and priority ({fmt_research(want)})', a1 == want,
          fmt_research(a1))
    check('b) client: nation 0 (the host\'s nation) unchanged', a0 == rc0, f'{fmt_research(a0)}, was {fmt_research(rc0)}')

    # d) more than 12 % is refused by the game's own message; the dialog is left with WM_CLOSE (budget not written)
    cm2 = log_mark(C)
    r, c = open_research(C, cp)
    v3 = research_view(cp, c) if c else None
    check('d) client: reopened, the dialog shows the player\'s new values (log line again)',
          c and shows(v3, want) and len(research_lines(C, cm2)) == 1,
          f'{r}; {fmt_rview(v3)}; lines {research_lines(C, cm2)}')
    if c:
        _u32t.SendMessageW(c['se'], 0x000C, 0, str(RESEARCH_MAX + 3))
        time.sleep(0.3)
        u.PostMessageW(c['ok'], 0x00F5, 0, 0)  # BM_CLICK OK
        boxes = wait_until(lambda: message_forms(cp), bool, 5)
        info = [box_info(h) for h in boxes]
        still = bool(research_dialogs(cp))
        during = research(C, 1)
        check(f'd) client: OK at {RESEARCH_MAX + 3} % -> the game\'s message box ("Research budget cannot be more '
              f'than 12% of total budget!"), the dialog stays open, nothing written',
              len(info) == 1 and info[0][0]['title'] == 'Rule the Waves III' and info[0][0]['buttons'] == ['OK']
              and still and during == want,
              f'{[i[0] for i in info]}; dialog open {still}; nation 1 {fmt_research(during)}')
        for _, btns in info:
            if btns:
                u.PostMessageW(btns[0][1], 0x00F5, 0, 0)  # its OK
        gone = wait_until(lambda: message_forms(cp), lambda v: not v, 4)
        open_after = bool(research_dialogs(cp))
        closed = close_research(cp, c)
        after = research(C, 1)
        st = ctl.status(C)
        check('d) client: message answered, the dialog still open; WM_CLOSE leaves it cleanly (nation 1 unchanged, '
              'no window left, bridge answers)',
              not gone and open_after and closed and after == want and not message_forms(cp)
              and not research_dialogs(cp) and st.get('phase') == 'planning',
              f'message left {len(gone)}, open after answer {open_after}, closed {closed}, nation 1 '
              f'{fmt_research(after)}, status {st.get("phase")}')

    # c) the host's own Research screen: unchanged behaviour (nation 0, no log line)
    hm = log_mark(H)
    r, hc = open_research(H, hp)
    if check('c) host: researchopen opens TfrmResearch', r == 'ok' and hc, f'{r}; {describe_windows(hp)}'):
        hv = research_view(hp, hc)
        hl = log_lines(log_since(H, hm), r'research screen:')
        check('c) host: no "research screen:" line and the dialog shows nation 0',
              not hl and shows(hv, rh0), f'lines {hl}; shown {fmt_rview(hv)}; nation 0 {fmt_research(rh0)}')
        hb = rh0['pct'] + 1 if rh0['pct'] < RESEARCH_MAX else rh0['pct'] - 1
        _u32t.SendMessageW(hc['se'], 0x000C, 0, str(hb))
        time.sleep(0.3)
        closed = close_research(hp, hc, ok=True)
        h0, h1 = research(H, 0), research(H, 1)
        hwant = dict(pct=hb, prio=rh0['prio'])
        check(f'c) host: budget {hb} + OK changes the host\'s nation 0 only', closed and h0 == hwant and h1 == rh1,
              f'closed {closed}; nation 0 {fmt_research(h0)} (want {fmt_research(hwant)}); nation 1 {fmt_research(h1)} '
              f'(was {fmt_research(rh1)})')
    else:
        hb, hwant = None, rh0
    log(f'  research set up in {time.time() - t0:.0f} s: client nation 1 {fmt_research(want)}, host nation 0 '
        f'{fmt_research(hwant)}; checked after the next processed month')

    st = dict(n=0, need=1)

    def hook(ctx, label):
        outer, _scenario[0] = _scenario[0], 'research'
        try:
            ctx.month_hooks.remove(hook)
            st['n'] = 1
            hmark, _ = ctx.month_marks
            hlog = log_since(H, hmark)
            h1, h0, c1, c0 = research(H, 1), research(H, 0), research(C, 1), research(C, 0)
            log(f'  research after {label}: host 1 {fmt_research(h1)} | host 0 {fmt_research(h0)} | client 1 '
                f'{fmt_research(c1)} | client 0 {fmt_research(c0)}')
            check(f'month ({label}): the host has the client\'s budget and priority for nation 1', h1 == want,
                  f'{fmt_research(h1)}, want {fmt_research(want)}; {log_lines(hlog, r"merge nation 1:")[:1]}')
            check(f'month ({label}): the host\'s nation 0 keeps the host\'s own values (the client changed nothing '
                  f'there)', h0 == hwant, f'{fmt_research(h0)}, want {fmt_research(hwant)}')
            check(f'month ({label}): the client still has its nation 1 values in the new month', c1 == want,
                  f'{fmt_research(c1)}, want {fmt_research(want)}')
            check(f'month ({label}): the client got the host\'s nation 0 values', c0 == h0,
                  f'client {fmt_research(c0)}, host {fmt_research(h0)}')
            cm3 = log_mark(C)
            r, c = open_research(C, ctx.cp)
            v4 = research_view(ctx.cp, c) if c else None
            closed = close_research(ctx.cp, c) if c else False
            check(f'month ({label}): the client\'s Research screen shows nation 1\'s values in the new month',
                  c and shows(v4, want) and len(research_lines(C, cm3)) == 1 and closed and research(C, 1) == want,
                  f'{r}; {fmt_rview(v4)}; lines {research_lines(C, cm3)}; closed {closed}')
            sup = log_lines(hlog, r'AI suppressed: DetermineAINationResearch for nation \d+')
            ai = [x for x in sup if re.search(rf'for nation {AI_NATION}\b', x)]
            if not sup:
                log(f'  SKIP-note: no DetermineAINationResearch suppression in {label} (the game did not call it)')
            check(f'month ({label}): host log "AI suppressed: DetermineAINationResearch for nation 1" (when the game '
                  f'calls it), never for AI nation {AI_NATION}',
                  (not sup or any(re.search(r'for nation 1$', x) for x in sup)) and not ai, sup[:3] or 'no line')
            kept_month = log_lines(hlog, r'research priorities of player nation \d+ kept')
            if kept_month:
                log(f'  the month\'s own DoResearch: {kept_month[:2]}')
            # e) the game's DoResearch runs AI research choices for every nation > 0 (some areas set to High by chance,
            # 5 % a month a full re-roll, mostly Medium); since ebfca72 a joined player's priorities are kept
            rr = research_run(ctx, 1, RESEARCH_RUNS)
            h1b = research(H, 1)
            log(f'  e) researchrun 1 {RESEARCH_RUNS}: {rr["seconds"]} s, before {fmt_research(rr["before"])}, after '
                f'{fmt_research(rr["after"])}; {len(rr["kept"])} kept lines {rr["kept"][:3]}')
            check(f'e) host `researchrun 1 {RESEARCH_RUNS}` (the game\'s DoResearch for joined nation 1 {RESEARCH_RUNS} '
                  f'times, ~99 % to hit its 5 % full re-roll): nation 1\'s distinctive priorities ({fmt_research(want)}) '
                  f'are the same after as before, and the hook logged at least one "research priorities of player '
                  f'nation 1 kept (N changed by the game\'s AI choices)"', rr['before'] and rr['after']
                  and rr['before']['prio'] == rr['after']['prio'] == want['prio'] and h1b and h1b['prio'] == want['prio']
                  and len(set(want['prio'][:23])) > 1 and rr['kept'],
                  f'reply {rr["reply"]}; {rr["line"] and rr["line"][:60]}...; before {fmt_research(rr["before"])}; '
                  f'after {fmt_research(rr["after"])}; memory {fmt_research(h1b)}; kept {len(rr["kept"])} '
                  f'{rr["kept"][:2]}; {rr["other"][:2]}')
            ctl_ = research_run(ctx, RESEARCH_CONTROL, RESEARCH_RUNS)
            moved = [i for i in range(25) if ctl_['before'] and ctl_['after']
                     and ctl_['before']['prio'][i] != ctl_['after']['prio'][i]]
            log(f'  e) control: researchrun {RESEARCH_CONTROL} {RESEARCH_RUNS} (AI nation): {ctl_["seconds"]} s, before '
                f'{fmt_research(ctl_["before"])}, after {fmt_research(ctl_["after"])}; {len(moved)} areas changed '
                f'{moved}; kept lines {ctl_["kept"]}')
            check(f'e) control: `researchrun {RESEARCH_CONTROL} {RESEARCH_RUNS}` on AI nation {RESEARCH_CONTROL} runs '
                  f'and is not protected (no "kept" line for it; its priorities may change: {len(moved)} areas did)',
                  ctl_['before'] and ctl_['after'] and not ctl_['kept'],
                  f'reply {ctl_["reply"]}; before {fmt_research(ctl_["before"])}; after {fmt_research(ctl_["after"])}')
            if hb is not None:  # the host's nation 0 back to its own value (affects nothing checked later)
                log(f'  host nation 0 research % back to {rh0["pct"]}:', cmd(H, f'nfield 0 ResearchPct {rh0["pct"]}'))
        finally:
            _scenario[0] = outer

    hook.scenario, hook.state = 'research', st
    ctx.month_hooks.append(hook)
    order = [s[0] for s in SCENARIOS]
    if planned_months([n for n in ctx.selected if order.index(n) > order.index('research')]) == 0:
        run_month(ctx, 'research')  # no later month to share


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
            return parse_save(f.read())
    except OSError:
        return None


def parse_save(raw):
    """read_save of a file's bytes (None stays None)."""
    if raw is None:
        return None
    t = raw.decode('utf-8-sig', errors='replace')
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


# ---------------------------------------------------------------- joined players' battles against AI nations

BATTLE_SLOT = CLIENT_SLOT + 10  # the client's battle slot (the mod's default: client slot + 10); deleted after the run
BATTLE_OK_RE = r"Joined players' battles: ok \(frmMain=\S+ checkAttack=\S+\)"
APPLIED_RE = (r'battle (\d+) applied: (\d+) ships changed \((\d+) fields\), (\d+) removed, VP (-?\d+)/(-?\d+), '
              r'prestige (-?\d+)(.*)$')
BATTLE_STATES = 'queued|fighting|fought|to the game|resolved by the game'
DEFAULT_BATTLE_TIMEOUT = 1800  # s: the host's default time limit for a battle (RTW3MP_BATTLE_TIMEOUT not set)
HOLD_LIMIT = 20  # s: `battles timeout` of the time-limit check
FIGHT_TIMEOUT = 900  # s a battle month may take longer than a normal one (a fought battle on auto-run)
SHIP_INTEL = ('PlayerIntel', 'EnemyClassName', 'EnemySpeed', 'EnemyBelt')  # the player's view of a ship, not merged
AUTORUN_AT = (1218 + 14, 5 + 14)  # sbAutoRunBattle (a 28x28 TSpeedButton) on TfrmMain.Panel1, form resource TFRMMAIN
BATTLE_UP = ('TdlgMapMessage', 'TdlgBattlePreview', 'TfrmMain', 'TdlgResults')
UNPLANNED = dict(answers=[])  # battle dialogs of months without a BattleDriver (declined; reported by `battles`)
_pressed = {}  # dialog window -> time the battle driver last pressed one of its buttons (a posted click takes a moment)


def battle_up(pid):
    """A battle dialog or the battle screen of a copy is up."""
    return any(e2e_test.class_name(h) in BATTLE_UP for h in top_windows(pid))


def dlg_buttons(top):
    """{caption (lower case, no '&'): hwnd} of the visible, enabled buttons of a dialog."""
    out = {}
    for h in children(top):
        if u.IsWindowVisible(h) and u.IsWindowEnabled(h) and e2e_test.class_name(h) in ('TButton', 'TBitBtn'):
            out.setdefault(caption(h).lower(), h)
    return out


def is_input_query(top):
    """Delphi's InputQuery form (the battle screen's "Add to history record"): an edit box with OK and Cancel."""
    k = e2e_test.class_name(top)
    if k in BATTLE_UP or k in ('TfrmBuildCamp', 'TMessageForm', 'TdlgEventAnswer', 'TdlgPictureMessage') \
            or k.startswith('RTW3MP_'):
        return False
    return any(e2e_test.class_name(h) == 'TEdit' for h in children(top)) and 'cancel' in dlg_buttons(top)


def select_radio(dlg, want):
    """Select the radio button captioned `want` of a TdlgEventAnswer (BM_CLICK, else the parent's BN_CLICKED)."""
    for h in children(dlg):
        if e2e_test.class_name(h) == 'TRadioButton' and caption(h) == want:
            if not u.SendMessageW(h, 0x00F0, 0, 0):  # BM_GETCHECK
                u.SendMessageW(h, 0x00F5, 0, 0)  # BM_CLICK
                if not u.SendMessageW(h, 0x00F0, 0, 0):
                    u.PostMessageW(u.GetParent(h), 0x0111, u.GetDlgCtrlID(h) & 0xFFFF, h)
                    time.sleep(0.3)
            return bool(u.SendMessageW(h, 0x00F0, 0, 0))
    return False


def battle_panel(frm):
    """TfrmMain's tool bar (Panel1: alTop, 39 px high); its speed buttons are graphic controls without a window."""
    best = None
    for h in children(frm):
        if e2e_test.class_name(h) != 'TPanel' or u.GetParent(h) != frm or not u.IsWindowVisible(h):
            continue
        r = _RECT()
        u.GetWindowRect(h, ctypes.byref(r))
        if r.bottom - r.top < 60 and r.right - r.left > 600 and (best is None or r.top < best[0]):
            best = (r.top, h)
    return best[1] if best else None


def click_autorun(frm):
    """The battle screen's "Auto run" speed button: a posted left click on Panel1 at the button's place (VCL hands
    the mouse messages of a panel to the graphic control under the point). Never the real mouse."""
    p = battle_panel(frm)
    if not p:
        return False
    lp = (AUTORUN_AT[1] << 16) | AUTORUN_AT[0]
    u.PostMessageW(p, 0x0200, 0, lp)  # WM_MOUSEMOVE
    u.PostMessageW(p, 0x0201, 1, lp)  # WM_LBUTTONDOWN (MK_LBUTTON)
    u.PostMessageW(p, 0x0202, 0, lp)  # WM_LBUTTONUP
    return True


def _press(rec, top, h, what):
    if h is None or time.time() - _pressed.get(top, 0) < 1.5:
        return False
    u.PostMessageW(h, 0x00F5, 0, 0)  # BM_CLICK
    _pressed[top] = time.time()
    entry = f'{e2e_test.class_name(top)}:"{e2e_test.window_text(top)[:40]}"->{what}'
    rec['answers'].append(entry)
    log('  client battle dialog:', entry)
    return True


def battle_dialogs(pid, action, rec):
    """One pass over a copy's battle dialogs, answered for `action`: 'fight' (map message and preview Accept, the
    battle screen's auto-run with its prompt "Yes", the results' "Exit battle", the history prompt Cancel), 'decline'
    (the preview's Decline) or 'hold' (the preview stays open until rec['release'] is set, then Decline). Other boxes:
    Yes before OK (Yes/No boxes in a battle ask to end the scenario), picture messages OK. `rec` collects what was
    seen. Never dismiss_dialogs while a battle is up: it would press the preview's first button."""
    for k in ('answers',):
        rec.setdefault(k, [])
    action = rec.get('force') or action  # a preview without an enabled Decline (the player's nation defends): fought
    tops = top_windows(pid)
    modal = any(e2e_test.class_name(w) == 'TMessageForm' or e2e_test.class_name(w).startswith('Tdlg') for w in tops)
    for top in tops:
        k = e2e_test.class_name(top)
        if k == 'TfrmMain':
            rec['frm'] = rec.get('frm') or time.time()
            if action == 'fight' and not rec.get('autorun') and rec.get('autorun_clicks', 0) < 3 \
                    and time.time() - rec.get('autorun_t', 0) > 8 and not modal:
                if click_autorun(top):
                    rec['autorun_clicks'] = rec.get('autorun_clicks', 0) + 1
                    rec['autorun_t'] = time.time()
                    log('  client battle screen: auto-run button clicked (posted)')
            continue
        query = is_input_query(top)
        if not (k == 'TMessageForm' or k.startswith('Tdlg') or query):
            continue
        b = dlg_buttons(top)
        first = next(iter(b.values()), None)
        if query:
            rec['inputquery'] = rec.get('inputquery') or e2e_test.window_text(top) or '?'
            _press(rec, top, b.get('cancel'), 'Cancel')
        elif k == 'TdlgMapMessage':
            rec['map'] = rec.get('map') or time.time()
            _press(rec, top, b.get('accept') or first, 'Accept')
        elif k == 'TdlgBattlePreview':
            rec['preview_t'] = rec.get('preview_t') or time.time()
            rec['preview_buttons'] = sorted(b)
            if action == 'fight':
                _press(rec, top, b.get('accept'), 'Accept')
            elif action == 'decline' or (action == 'hold' and rec.get('release')):
                if 'decline' in b:
                    _press(rec, top, b['decline'], 'Decline')
                elif _press(rec, top, b.get('accept'), 'Accept (Decline disabled)'):
                    rec['no_decline'] = True  # the game allows no decline here (the player's nation defends)
                    rec['force'] = 'fight'
                    rec['autorun'], rec['autorun_clicks'] = False, 0
        elif k == 'TdlgEventAnswer':
            radios = [caption(h) for h in children(top) if e2e_test.class_name(h) == 'TRadioButton'
                      and u.IsWindowVisible(h)]
            if 'Yes' in radios and 'No' in radios:  # "auto run the battle with both sides on AI ...?"
                rec['autorun_prompt'] = radios
                if select_radio(top, 'Yes') and _press(rec, top, b.get('ok'), 'OK [Yes]'):
                    rec['autorun'] = True
            else:
                select_first_answer(pid)
                _press(rec, top, b.get('ok') or first, 'OK')
        elif k == 'TdlgResults':
            rec['results'] = rec.get('results') or time.time()
            if _press(rec, top, b.get('exit battle') or b.get('view map') or first, 'Exit battle') and rec is UNPLANNED:
                for key in ('force', 'autorun', 'autorun_clicks', 'autorun_t', 'frm', 'results', 'preview_t'):
                    rec.pop(key, None)  # the next battle without a plan starts as 'decline' again
        elif k == 'TdlgPictureMessage':
            _press(rec, top, b.get('ok') or b.get('yes') or first, 'OK')
        elif k == 'TMessageForm':
            pick = 'yes' if 'yes' in b else 'ok' if 'ok' in b else None
            _press(rec, top, b.get(pick) if pick else first, pick or 'first')
        else:
            pick = next((p for p in ('ok', 'close', 'continue', 'yes', 'accept') if p in b), None)
            _press(rec, top, b.get(pick) if pick else first, pick or 'first')


def battles_list():
    """Host `battles list` -> dict(phase, timeout, battles={id: dict(K, J, area, state, why)}, raw) or None."""
    r = cmd(H, 'battles list')
    m = re.match(r'^ok phase=(\d) timeout=(\d+) battles=(.*)$', r)
    if not m:
        return None
    out = dict(phase=int(m[1]), timeout=int(m[2]), battles={}, raw=r)
    for e in m[3].split('; '):
        e = e.strip().rstrip(';').strip()
        mm = re.match(rf'^(\d+):(\d+)-(\d+):(.*?):({BATTLE_STATES})(?::(.*))?$', e)
        if mm:
            out['battles'][int(mm[1])] = dict(K=int(mm[2]), J=int(mm[3]), area=mm[4], state=mm[5], why=mm[6] or '')
    return out


def cstate():
    """Client `battles state` -> dict(state, id, nation, opp, area, slot, slotMode, aftermath, fight, raw)."""
    r = cmd(C, 'battles state')
    m = re.match(r'^ok state=(\S+) id=(-?\d+) nation=(-?\d+) opp=(-?\d+) area=(.*) slot=(\d+) slotMode=(\d) '
                 r'aftermath=(\d) fight=(\d)$', r)
    if not m:
        return dict(raw=r)
    return dict(state=m[1], id=int(m[2]), nation=int(m[3]), opp=int(m[4]), area=m[5], slot=int(m[6]),
                slotMode=m[7] == '1', aftermath=m[8] == '1', fight=m[9] == '1', raw=r)


def ships_by_id(sec):
    """{ship id: {field: value}} of a [NationNShips] section of read_save."""
    tmp = {}
    for k, v in (sec or {}).items():
        m = re.match(r'^Ship(\d+)([A-Za-z].*)$', k)
        if m:
            tmp.setdefault(int(m[1]), {})[m[2]] = v
    return {f['Id']: f for f in tmp.values() if f.get('Id')}


def strategic_enabled(pid):
    w = [h for h in top_windows(pid) if e2e_test.class_name(h) == 'TfrmBuildCamp']
    return bool(u.IsWindowEnabled(w[0])) if w else None


def log_secs(line):
    """Seconds of the day of a mod log line ('HH:MM:SS.mmm [tid] ...')."""
    m = re.match(r'^(\d\d):(\d\d):(\d\d)\.(\d{3})', line)
    return int(m[1]) * 3600 + int(m[2]) * 60 + int(m[3]) + int(m[4]) / 1000 if m else None


def battle_pairs(K):
    """Candidates (J's ships there, K's ships there, J, area) for an injected battle, best first: an AI nation J with
    a possession in a sea area where nation K has at least 4 ships (the game's CheckAttackLocation: a possession
    battle needs a possession of the defender in the area and the attacker's strength there > 3; a home-area battle
    a home area of both). From the host's save of the latest month and `possessions`."""
    bcs = read_save(save_path(HOST_SLOT)) or {}

    def locs(n):
        out = {}
        for f in ships_by_id(bcs.get(f'Nation{n}Ships')).values():
            a = f.get('LocationAreaName')
            out[a] = out.get(a, 0) + 1
        return out

    mine, cands = locs(K), []
    for j in range(1, 9):
        if j in (K, STANDIN):
            continue
        theirs = locs(j)
        for area in sorted({p['area'] for p in poss_list(H, f'possessions {j}') or []}):
            if mine.get(area, 0) >= 4:
                cands.append((theirs.get(area, 0), mine[area], j, area))
    return sorted(cands, reverse=True)


def inject(K, J, area):
    """Host `battles inject K J area` -> (battle id or None, reply)."""
    r = cmd(H, f'battles inject {K} {J} {area}')
    m = re.match(r'^ok (\d+)$', r)
    return (int(m[1]) if m else None), r


class BattleDriver:
    """run_month's client side for a month whose battle phase sends battles to the client. plan: {battle id: 'fight' |
    'decline' | 'hold'}; a battle the month found by itself (not in the plan) is declined. The battle being fought is
    the latest `battle N sent to` line of the host (one battle per player at a time). Records per battle: the dialogs
    answered, the battle slot as the client shows it, timings; the host's save when the battle phase began (s0) with
    the host's view then (only from that moment on the host's bridge is polled: its month is over), and the battle
    slot's save right after each fought battle (g87)."""
    extra_timeout = FIGHT_TIMEOUT

    def __init__(self, ctx, plan, K, J, fight_any=False):
        self.ctx, self.plan, self.K, self.J, self.fight_any = ctx, dict(plan), K, J, fight_any
        self.hmark, self.cmark = log_mark(H), log_mark(C)
        self.rec, self.g87, self.s1 = {}, {}, {}
        self.s0 = self.phase = self.label = None
        self.t0, self.t_phase, self.t_state = time.time(), None, 0

    def r(self, bid):
        """A battle's record. Planned 'fight' or 'hold' becomes 'decline' once a battle of the month was fought or held
        (several candidates are injected: the AI often avoids a battle); a battle not in the plan is fought if
        fight_any and none was fought yet, else declined."""
        if bid not in self.rec:
            planned = self.plan.get(bid)
            held = any(r['action'] == 'hold' and r.get('preview_t') for r in self.rec.values())
            if planned == 'fight' and self.fought() is not None or planned == 'hold' and held:
                action = 'decline'
            else:
                action = planned or ('fight' if self.fight_any and not self.fought() else 'decline')
            self.rec[bid] = dict(id=bid, action=action, planned=planned, answers=[], sent=time.time())
        return self.rec[bid]

    def fought(self):
        return next((b for b, r in self.rec.items() if r.get('outcome') == 'fought'), None)

    def current(self, h, c, last_sent):
        """The battle the client is busy with: the one the host had sent last when the client started its latest
        battle slot (`player copy in slot 87`), until its outcome line; before the first start, the last one sent.
        After a time limit the host sends the next battle while the client still shows the old one."""
        day0 = self.t0 - time.mktime(time.localtime(self.t0)[:3] + (0, 0, 0, 0, 0, -1))  # seconds of day at start

        def secs(line):
            t = log_secs(line)
            return t + 86400 if t is not None and t < day0 - 3600 else t  # past midnight

        starts = [secs(x) for x in log_lines(c, rf'player copy in slot {BATTLE_SLOT}:')]
        if not starts:
            return last_sent
        sends = [(secs(x), int(re.search(r'battle (\d+) sent to', x)[1])) for x in log_lines(h, r'battle \d+ sent to')]
        before = [b for t, b in sends if t is not None and t <= starts[-1] + 0.05]
        if not before:
            return last_sent
        cur = before[-1]
        return last_sent if self.rec.get(cur, {}).get('outcome') else cur

    def poll(self):
        h, c = log_since(H, self.hmark), log_since(C, self.cmark)
        if self.phase is None and 'battle phase: ' in h:
            self.t_phase = time.time()
            self.s0 = read_save(save_path(HOST_SLOT))
            time.sleep(1.0)  # the first battle is sent and the status set in the same tick
            st = ctl.status(H)
            self.phase = dict(status=st, list=battles_list(), enabled=strategic_enabled(self.ctx.hp),
                              nations=nation_table(H), vp=vpinfo(H), prestige=nint(H, self.K, 'Prestige'))
            log(f'  battle phase: host busy={st.get("busy")} status "{st.get("status")}", strategic screen enabled='
                f'{self.phase["enabled"]}, {self.phase["list"] and self.phase["list"]["raw"]}')
        for b in re.findall(r'battle (\d+) applied: ', h):  # the merged save, before the host loads it again
            if int(b) not in self.s1:
                s1 = read_save(save_path(HOST_SLOT))
                if s1 and 'General' in s1 and f'Nation{self.J}Ships' in s1:
                    self.s1[int(b)] = s1
        for b in re.findall(r'\] battle (\d+): (fought|declined|nobattle|failed|finished)', c):  # before the next one
            r2 = self.r(int(b[0]))
            if 'done' not in r2:
                r2['done'], r2['outcome'] = time.time(), b[1]
                if b[1] == 'fought':
                    self.g87[int(b[0])] = read_save(save_path(BATTLE_SLOT))
                log(f'  battle {b[0]}: {b[1]} after {r2["done"] - r2["sent"]:.0f} s; answered {r2["answers"]}')
        sent = re.findall(r'battle (\d+) sent to', h)
        if not sent:
            if battle_up(self.ctx.cp):  # should not happen: a battle before the host sent one
                battle_dialogs(self.ctx.cp, 'decline', UNPLANNED)
            return
        rec = self.r(self.current(h, c, int(sent[-1])))
        bid = rec['id']
        # a "resolved without you" notice counts for this battle only if it came after the battle became the current one
        # (an earlier battle that outlived the time limit, e.g. one whose Decline was disabled, logged one too)
        notices = len(re.findall(r'resolved (this|your) battle without you', c))
        rec.setdefault('notices0', notices)
        if rec['action'] == 'hold' and not rec.get('release') and (
                notices > rec['notices0'] or time.time() - rec['sent'] > HOLD_LIMIT + 60):
            rec['release'] = time.time()
            log(f'  battle {bid}: the host resolved it without the client; the preview is answered now')
        tops = top_windows(self.ctx.cp)
        act = rec.get('force') or rec['action']  # 'decline' becomes 'fight' when the preview allows no decline
        if act == 'fight' and 'aftermath_view' not in rec and (rec.get('results') or rec.get('frm')) \
                and any(is_input_query(w) or (rec.get('results') and e2e_test.class_name(w) == 'TMessageForm')
                        for w in tops):
            rec['aftermath_view'] = cstate()  # before the box after the results is answered (the slot mode ends then)
            log(f'  battle {bid} after its results: {rec["aftermath_view"].get("raw")}')
        battle_dialogs(self.ctx.cp, rec['action'], rec)
        act = rec.get('force') or rec['action']
        if 'slot_view' not in rec and (rec.get('frm') or rec.get('preview_t')) and act == 'fight':
            rec['slot_view'] = dict(status=ctl.status(C), nations=nation_table(C), state=cstate())
            log(f'  battle {bid} on the client: slot {rec["slot_view"]["status"].get("slot")} playerIdx '
                f'{rec["slot_view"]["status"].get("playerIdx")}, nation 0 '
                f'{rec["slot_view"]["nations"].get(0, {}).get("name")}; {rec["slot_view"]["state"].get("raw")}')
        if act == 'fight' and rec.get('frm') and 'done' not in rec and time.time() - self.t_state > 15 \
                and not any(e2e_test.class_name(w) != 'TfrmMain' for w in top_windows(self.ctx.cp)
                            if e2e_test.class_name(w) in BATTLE_UP or e2e_test.class_name(w).startswith('Tdlg')
                            or e2e_test.class_name(w) == 'TMessageForm'):
            self.t_state = time.time()
            ps = value(cmd(C, 'pocstate')) or ''
            log(f'  battle {bid} running: {ps[:150]}')
            rec.setdefault('states', []).append(ps)
            stalled = 'running=0 moving=0' in ps and 'finished=1' in ps and 'endgame=0' in ps
            if stalled and rec.get('autorun') and len(rec['states']) > 1 and 'running=0 moving=0' in rec['states'][-2]:
                log('  battle stalled at a boundary: pocrun 100000', cmd(C, 'pocrun 100000'))
            if rec.get('autorun_clicks', 0) >= 3 and not rec.get('autorun') and time.time() - rec['autorun_t'] > 10:
                # the auto-run button did not open its prompt: both sides to the AI, fastest speed, run (POC commands)
                rec['autorun'] = 'poc'
                for line in ('pocsideai 0 1 0', 'pocsideai 1 1 0', 'pocspeed 5', 'pocrun 100000'):
                    log(f'  {line}:', cmd(C, line)[:100])


def ship_changes(s0, g87, K, J):
    """The battle's changes from the host's save as sent (s0, host numbering) and the battle slot after the battle
    (g87: K is nation 0 there, J keeps its number): ({(n, id): {field: (before, after)}}, {(n, id) sunk})."""
    changed, sunk = {}, set()
    for n, slot_n in ((K, 0), (J, J)):
        before = ships_by_id((s0 or {}).get(f'Nation{n}Ships'))
        after = ships_by_id((g87 or {}).get(f'Nation{slot_n}Ships'))
        for sid, f0 in before.items():
            f1 = after.get(sid)
            if f1 is None:
                sunk.add((n, sid))
                continue
            ch = {k: (f0.get(k), v) for k, v in f1.items()
                  if f0.get(k) != v and k not in SHIP_INTEL and k != 'BuildingNationIdx'}
            if ch:
                changed[(n, sid)] = ch
    return changed, sunk


def merged_mismatches(s2, changed, sunk):
    """Where the host's save after the merge (s2) differs from the battle slot: [(n, id, field, want, got)], the sunk
    ships still there and the changed ships missing."""
    bad, still, missing = [], [], []
    by = {}
    for (n, sid), ch in changed.items():
        ships = by.setdefault(n, ships_by_id(s2.get(f'Nation{n}Ships')))
        f2 = ships.get(sid)
        if f2 is None:
            missing.append((n, sid))
            continue
        bad += [(n, sid, k, v[1], f2.get(k)) for k, v in ch.items() if f2.get(k) != v[1]]
    for n, sid in sunk:
        if sid in by.setdefault(n, ships_by_id(s2.get(f'Nation{n}Ships'))):
            still.append((n, sid))
    return bad, still, missing


def rejoin(ctx, K, fight):
    """The client leaves the session and joins again with `fightbattles fight`: the host gives the returning player
    ("Captain") its nation back and sends the month again (host `state seq N -> Captain (nation K, ...`, client
    `LoadCampaign slot 77 player K`). Returns (client status, host lines, client lines)."""
    cmd(C, 'leave')
    wait_until(lambda: ctl.status(H).get('players'), lambda p: p == '1', 15)
    hm, cm = log_mark(H), log_mark(C)
    r1 = cmd(C, f'fightbattles {1 if fight else 0}')
    r2 = cmd(C, f'join Captain 127.0.0.1 {MP_PORT}')
    log(f'  rejoin with fightbattles {1 if fight else 0}: {r1}, join {r2}')
    hpat, cpat = rf'state seq \d+ -> Captain \(nation {K},', rf'LoadCampaign slot {CLIENT_SLOT} player {K} -> ok'
    sent = wait_log(H, hm, hpat, 25)
    if not sent:
        log('  the nation was not given back: claim', cmd(C, f'claim {K}'))
        sent = wait_log(H, hm, hpat, 60)
    loaded = wait_log(C, cm, cpat, 60)
    st = wait(C, lambda s: s.get('phase') == 'planning' and s.get('myNation') == str(K) and s.get('playerIdx') == str(K)
              and s.get('slot') == str(CLIENT_SLOT), 60, 'the returning player plans its nation', pids=(ctx.hp,))
    time.sleep(2)
    cmd(C, 'mpwindow')
    time.sleep(1)
    return st, sent, loaded


def scenario_battles(ctx):
    """Joined players fight their battles against AI nations on their own PC (after 0.2.8). The host defers a battle
    the game would resolve between a joined nation and an AI nation, runs a battle phase after the month (save, lock,
    one battle per player at a time), the client fights it in its battle slot (Game87, its nation as nation 0; the
    game's CheckAttackLocation, preview, battle screen, results), the host merges the result (ships, losses, VP,
    prestige) and sends the new month. Battles are injected with `battles inject` (processed at the end of the next
    month) against an AI nation at war with nation 1 (`settension`). a) startup lines and defaults; b) one battle
    fought to its end on auto-run (two candidates a month, up to 3 months: the AI often avoids a battle) and its merge
    cross-checked against the battle slot's save; c) a declined battle and d) a battle left open past `battles timeout
    20` (three candidates each a month, up to 3 months); e) the client joins again with `fightbattles 0`: nothing is
    deferred. The war ends at the end."""
    K = 1
    hl, cl = [log_lines(log_since(p), r"Joined players' battles: ") for p in (H, C)]
    ok = any(re.search(BATTLE_OK_RE, x) for x in hl) and any(re.search(BATTLE_OK_RE, x) for x in cl)
    check('a) both logs: "Joined players\' battles: ok (frmMain=... checkAttack=...)"', ok, f'host {hl}; client {cl}')
    log('  ', hl[:1], cl[:1])
    if not ok:
        check('a) joined players\' battles available: the scenario stops here', False, 'startup line missing/UNAVAILABLE')
        return
    cs, bl = cstate(), battles_list()
    check(f'a) defaults: client `battles state` none, slot {BATTLE_SLOT}, slotMode 0, fight 1; host `battles list` '
          f'phase 0, timeout {DEFAULT_BATTLE_TIMEOUT}, no battles',
          cs.get('state') == 'none' and cs.get('slot') == BATTLE_SLOT and cs.get('slotMode') is False
          and cs.get('fight') is True and bl and bl['phase'] == 0 and bl['timeout'] == DEFAULT_BATTLE_TIMEOUT
          and not bl['battles'], f'{cs.get("raw")}; {bl and bl["raw"]}')
    nat = nation_table(H)
    me = nat[K]['name']
    cands = battle_pairs(K)
    log('  candidates (J ships there, K ships there, J, area):', cands[:5])
    if not check(f'setup: an AI nation with a possession in a sea area where nation {K} has >= 4 ships', cands, cands):
        return
    _, _, J, area = cands[0]
    them = nat[J]['name']
    tension0 = rel(K, J).get('tension', '5')
    cmd(H, f'settension {K} {J} 50')
    r = rel(K, J)
    check(f'setup: nation {K} ({me}) at war with AI nation {J} ({them}) (`settension {K} {J} 50`; was {tension0}); '
          f'battles in {area}', r.get('war') == '1', r)
    other = next(j for j in range(3, 9) if j not in (J, STANDIN))
    refused = {f'battles inject {K} {J} Nowhere Sea': 'no sea area',
               f'battles inject {other} {J} {area}': "K must be a joined player's nation",
               f'battles inject {K} {STANDIN} {area}': "K must be a joined player's nation",
               'battles resolve 99': 'ok 0'}
    got = {q: cmd(H, q) for q in refused}
    check('inject refused for an unknown area, an AI nation as K, the stand-in player nation as J; `battles resolve` of '
          'an unknown id resolves nothing', all(w in got[q] for q, w in refused.items()), got)

    # b) one battle fought to its end: two injected candidates a month (the AI often avoids a battle; the second is
    # declined once the first was fought), else a battle the month found by itself; up to 3 months
    fought, att, drivers = None, [], []
    for attempt in range(1, 4):
        hm = log_mark(H)
        cands_ids = [inject(K, J, area) for _ in range(2)]
        bid, rr = cands_ids[0]
        bl = battles_list()
        if attempt == 1:
            check(f'b) `battles inject {K} {J} {area}` -> ok <id>; host log "battle request <id> injected: nation {K} vs '
                  f'{J} in {area}"; `battles list` has it queued',
                  bid is not None and wait_log(H, hm, rf'battle request {bid} injected: nation {K} vs {J} in '
                                                       rf'{re.escape(area)}', 3)
                  and bl and bl['battles'].get(bid, {}).get('state') == 'queued', f'{rr}; {bl and bl["raw"]}')
        if bid is None:
            break
        if rel(K, J).get('war') != '1':
            log('  the war ended (AI peace?): again', cmd(H, f'settension {K} {J} 50'))
        drv = BattleDriver(ctx, {b: 'fight' for b, _ in cands_ids if b is not None}, K, J, fight_any=True)
        drv.label = f'battles A{attempt}'
        drivers.append(drv)
        date0 = ctl.status(C).get('date')
        date1 = run_month(ctx, drv.label, driver=drv)
        att += [(b, r.get('action'), r.get('outcome')) for b, r in sorted(drv.rec.items())]
        if drv.fought() is not None:
            fought = (drv.fought(), drv, date0, date1)
            break
        log(f'  no battle fought (attempt {attempt}): {[(b, r.get("outcome")) for b, r in drv.rec.items()]}; host: '
            f'{log_lines(log_since(H, drv.hmark), r"battle \d+: resolved")}')
    check('b) a battle fought on the client (attempts: id, action, outcome; "nobattle" = the game found no battle, '
          'e.g. the AI avoided it)', fought, att)
    if fought:
        battle_fought_checks(ctx, K, J, area, me, them, *fought)
    report_unplanned()

    # c) declined and d) left past the time limit, in one month (up to 2 months)
    r = cmd(H, f'battles timeout {HOLD_LIMIT}')
    bl = battles_list()
    check(f'd) `battles timeout {HOLD_LIMIT}` -> ok, `battles list` timeout={HOLD_LIMIT}',
          r == 'ok' and bl and bl['timeout'] == HOLD_LIMIT, f'{r}; {bl and bl["raw"]}')
    want, done, notes = ['decline', 'hold'], {}, []
    for drv in drivers:  # a battle the month found by itself and the driver declined in its preview counts for c)
        for b, rec in sorted(drv.rec.items()):
            if rec['action'] == 'decline' and rec.get('preview_t') and not rec.get('no_decline') \
                    and 'decline' not in done:
                done['decline'] = (b, drv)
    for attempt in range(1, 4):
        plan = {}
        for action in want:  # three candidates per missing action; a second preview of a 'hold' is declined
            for _ in range(3):
                bid, rr = inject(K, J, area)
                if bid is not None:
                    plan[bid] = action
        if rel(K, J).get('war') != '1':
            log('  the war ended (AI peace?): again', cmd(H, f'settension {K} {J} 50'))
        drv = BattleDriver(ctx, plan, K, J)
        drv.label = f'battles B{attempt}'
        drivers.append(drv)
        run_month(ctx, drv.label, driver=drv)
        for bid, rec in sorted(drv.rec.items()):
            action = rec['action']
            notes.append((bid, action, rec.get('outcome'), rec.get('preview_buttons'), rec.get('answers')))
            if rec.get('preview_t') and not (action == 'decline' and rec.get('no_decline')) \
                    and (action not in done or bid in plan and done[action][0] not in plan):
                done[action] = (bid, drv)
        want = [a for a in ('decline', 'hold') if a not in done]
        if not want:
            break
        log(f'  no battle preview for {want} (attempt {attempt}): {notes}')
    check('c/d) a declined and a held battle reached the game\'s battle preview (attempts: id, action, outcome, '
          'preview buttons, answers)', not want, notes)
    if 'decline' in done:
        battle_declined_checks(ctx, K, J, area, me, them, *done['decline'])
    if 'hold' in done:
        battle_timeout_checks(ctx, K, J, area, me, them, *done['hold'])
    r = cmd(H, f'battles timeout {DEFAULT_BATTLE_TIMEOUT}')
    check(f'd) `battles timeout {DEFAULT_BATTLE_TIMEOUT}` restores the default', r == 'ok'
          and (battles_list() or {}).get('timeout') == DEFAULT_BATTLE_TIMEOUT, r)
    natural_request_checks(ctx, K, J, me, them, drivers)
    report_unplanned()

    # e) opt-out: the client joins again with `fightbattles 0`
    st, sent, loaded = rejoin(ctx, K, fight=False)
    cs, hrow = cstate(), row(H, K)
    check(f'e) the client left and joined again with `fightbattles 0`: the host gave Captain nation {K} back and sent '
          f'the month (`state seq N -> Captain (nation {K}, ...`), the client loaded it and plans nation {K} in slot '
          f'{CLIENT_SLOT} (myNation {K}); `battles state` fight=0, none, slotMode 0',
          sent and loaded and hrow and hrow['player'] == 'Captain' and st.get('myNation') == str(K)
          and st.get('playerIdx') == str(K) and st.get('slot') == str(CLIENT_SLOT) and cs.get('fight') is False
          and cs.get('state') == 'none' and cs.get('slotMode') is False,
          f'{sent} {loaded}; host row {hrow}; {st}; {cs.get("raw")}')
    bid, rr = inject(K, J, area)
    check(f'e) `battles inject {K} {J} {area}` refused: "no connected player who fights battles for nation {K}"',
          bid is None and f'no connected player who fights battles for nation {K}' in rr, rr)
    if rel(K, J).get('war') != '1':
        log('  the war ended (AI peace?): again', cmd(H, f'settension {K} {J} 50'))
    hm, t0 = log_mark(H), time.time()
    run_month(ctx, 'battles C')
    hlog = log_since(H, hm)
    req = log_lines(hlog, r'battle request|battle phase|battle \d+ sent')
    pics = [a[2] for a in ANSWERED if a[0] >= t0 and a[1] == 'host' and 'TdlgPictureMessage' in a[2]]
    check(f'e) a processed month with nation {K} at war with {J} ({them}) and the opt-out: no "battle request", '
          f'battle phase or sent line in the host log (the game resolves its AI battles itself)', not req, req[:4])
    log(f'  e) host picture messages that month (the game\'s AI battle news, among others): {pics[:6]}')

    # end: the war is over, nothing left
    cmd(H, f'settension {K} {J} {tension0}')
    r, cs, bl = rel(K, J), cstate(), battles_list()
    check(f'end: nation {K} at peace with {J} again (tension {tension0}); client `battles state` none, slotMode 0; host '
          f'`battles list` phase 0, no battles', r.get('war') == '0' and cs.get('state') == 'none'
          and cs.get('slotMode') is False and bl and bl['phase'] == 0 and not bl['battles'],
          f'{r}; {cs.get("raw")}; {bl and bl["raw"]}')
    report_unplanned()


def report_unplanned():
    if UNPLANNED['answers']:
        log('  battle dialogs of months without a plan (declined):', UNPLANNED['answers'])
        UNPLANNED['answers'] = []


def battle_fought_checks(ctx, K, J, area, me, them, bid, drv, date0, date1):
    """b) after the month whose battle was fought: the battle phase, the battle slot, the dialogs, the client's and the
    host's log lines, the merged values (the host's save right after the merge = the save as sent + the battle's
    deltas; ship fields cross-checked with the battle slot's save), the values after the month on host and client."""
    hlog, clog, rec = log_since(H, drv.hmark), log_since(C, drv.cmark), drv.rec[bid]
    ph = drv.phase or {}
    pst, plist = ph.get('status', {}), ph.get('list') or {}
    req = log_lines(log_since(H), rf'battle request {bid}[ :]')  # an injected one was logged before the month
    barea = battle_area(log_since(H), bid, area)  # the fought battle's area (one the game found may be elsewhere)
    check(f'b) host log: the request of battle {bid}, "battle phase: N battle(s)", "battle {bid} sent to Captain"',
          req and re.search(r'battle phase: \d+ battle\(s\)', hlog) and re.search(rf'battle {bid} sent to Captain', hlog),
          req + log_lines(hlog, r'battle phase|battle \d+ sent'))
    first = min(plist.get('battles', {}) or [0])
    want_status = f'Battles in progress: Captain: {me} vs {them} in {area}'
    check(f'b) host during the battle phase: strategic screen locked (status busy=1, TfrmBuildCamp disabled), status '
          f'"{want_status}", `battles list` phase=1 with the first battle {first}:{K}-{J}:{area}:fighting',
          pst.get('busy') == '1' and ph.get('enabled') is False and pst.get('status') == want_status
          and plist.get('phase') == 1 and plist.get('battles', {}).get(first, {}).get('state') == 'fighting'
          and (plist['battles'][first]['K'], plist['battles'][first]['J'], plist['battles'][first]['area']) == (K, J, area),
          f'busy {pst.get("busy")}, enabled {ph.get("enabled")}, status "{pst.get("status")}", {plist.get("raw")}')
    sv = rec.get('slot_view') or {}
    sst, snat, scs = sv.get('status', {}), sv.get('nations', {}), sv.get('state', {})
    check(f'b) client in the battle slot: status slot {BATTLE_SLOT}, playerIdx 0; `nations` 0 = {me}; `battles state` '
          f'starting/fighting nation {K} opp {J} area {barea}, slot {BATTLE_SLOT}, slotMode 1',
          sst.get('slot') == str(BATTLE_SLOT) and sst.get('playerIdx') == '0' and snat.get(0, {}).get('name') == me
          and scs.get('state') in ('starting', 'fighting') and (scs.get('nation'), scs.get('opp'), scs.get('area'))
          == (K, J, barea) and scs.get('slotMode') is True,
          f'{sst.get("slot")}/{sst.get("playerIdx")}; nation 0 {snat.get(0, {}).get("name")}; {scs.get("raw")}')
    av = rec.get('aftermath_view') or {}
    check('b) client dialogs: the battle preview (Accept), the battle screen, its auto-run prompt (Yes), the results '
          '("Scenario results" -> Exit battle); `battles state` aftermath=1 at the next box (HandleBattleAftermath ran '
          'in the battle slot); the history prompt answered Cancel if the game asked',
          rec.get('preview_t') and rec.get('frm') and rec.get('autorun') is True and rec.get('results')
          and av.get('aftermath') is True and av.get('slotMode') is True,
          f'answers {rec.get("answers")}; autorun {rec.get("autorun")} prompt {rec.get("autorun_prompt")}; history '
          f'prompt {rec.get("inputquery")}; after the results {av.get("raw")}')
    log(f'  b) battle {bid}: preview after {(rec.get("preview_t") or 0) - rec["sent"]:.0f} s, battle screen after '
        f'{(rec.get("frm") or 0) - rec["sent"]:.0f} s, results after {(rec.get("results") or 0) - rec["sent"]:.0f} s, '
        f'done after {(rec.get("done") or 0) - rec["sent"]:.0f} s; history prompt {rec.get("inputquery")}; last states '
        f'{rec.get("states", [])[-1:]}')
    skips = log_lines(clog, r'battle slot: \w+ skipped')
    check('b) client log: "battle slot: DoStrategicTurn skipped" (the battle screen\'s close runs no month)',
          any('DoStrategicTurn skipped' in x for x in skips), skips)
    pair = rf'(0 attacks {J}|{J} attacks 0) in {re.escape(barea)} -> battle'
    starts = log_lines(clog, rf'StartPlayerBattle {pair}|battle {bid}: (fighting|fought)')
    check(f'b) client log: "StartPlayerBattle 0 attacks {J} (or {J} attacks 0) in {barea} -> battle", "battle {bid}: '
          f'fighting nation {K} vs {J} in {barea} (slot {BATTLE_SLOT})", "battle {bid}: fought"',
          any(re.search(pair, x) for x in starts) and any(f'fighting nation {K} vs {J} in {barea} (slot {BATTLE_SLOT})'
                                                          in x for x in starts) and any(': fought' in x for x in starts),
          starts)
    st, cs = ctl.status(C), cstate()
    check(f'b) client back in slot {CLIENT_SLOT} as nation {K} in the new month ({date0} -> {date1}); `battles state` '
          f'none, slotMode 0', st.get('slot') == str(CLIENT_SLOT) and st.get('playerIdx') == str(K)
          and date1 != date0 and cs.get('state') == 'none' and cs.get('slotMode') is False, f'{st}; {cs.get("raw")}')
    ap = [re.search(APPLIED_RE, x) for x in log_lines(hlog, rf'battle {bid} applied: ')]
    ap = ap[0] if ap else None
    check(f'b) host log: "battle {bid} applied: N ships changed (F fields), R removed, VP a/b, prestige p (player: ...)" '
          f'with N + R > 0', ap and int(ap[2]) + int(ap[4]) > 0, log_lines(hlog, rf'battle {bid} (applied|NOT)'))
    s0, g87 = drv.s0 or {}, drv.g87.get(bid) or {}
    s2 = read_save(save_path(HOST_SLOT)) or {}
    others = log_lines(hlog, r'resolved by the game: battle') + [  # the month's other battles, after this merge
        x for x in log_lines(hlog, r'battle \d+ applied: ') if f'battle {bid} applied' not in x]
    s1 = drv.s1.get(bid) or ({} if others else s2)  # the merged save (read at once), else the month's own save
    changed, sunk = ship_changes(s0, g87, K, J)
    bad1, still1, missing1 = merged_mismatches(s1, changed, sunk) if s1 else ([], [], ['no merged save read'])
    bad2, still2, missing2 = merged_mismatches(s2, changed, sunk)
    dmg = {key: ch for key, ch in changed.items() if 'RepairTime' in ch or 'Fate' in ch}
    ex = next(iter(dmg.items() or changed.items()), None)
    ex_txt = ''
    if ex:
        (n, sid), ch = ex
        f2 = ships_by_id(s2.get(f'Nation{n}Ships')).get(sid, {})
        ex_txt = (f'e.g. nation {n} ship {sid} {f2.get("Name")}: ' +
                  ', '.join(f'{k} {v[0]}->{v[1]} (host after the month {f2.get(k)})' for k, v in list(ch.items())[:5]))
    log(f'  b) battle slot vs the save as sent: {len(changed)} ships changed ({len(dmg)} with RepairTime/Fate), '
        f'{len(sunk)} sunk {sorted(sunk)[:6]}; {ex_txt}')
    check('b) cross-check: every ship field the battle changed (battle slot Game87 after the battle vs the host save as '
          'sent) has the battle slot\'s value in the host\'s merged save; the sunk ships are gone from it',
          g87 and s0 and s1 and changed and not bad1 and not still1 and not missing1,
          f'{len(changed)} changed, {len(sunk)} sunk; mismatches {bad1[:5]}; sunk still there {still1[:5]}; missing '
          f'{missing1[:5]}')
    check('b) ... and after the month: the host loaded the merged save (with another battle of the month merged or '
          'resolved by the game afterwards, its changes are reported instead)',
          (not bad2 and not still2 and not missing2) or others,
          f'mismatches {bad2[:5]}; sunk still there {still2[:5]}; missing {missing2[:5]}; other battles: {others}')
    if others and (bad2 or still2 or missing2):
        log(f'  b) after the month, changed again by the month\'s other battles ({others}): {bad2[:5]} missing '
            f'{missing2[:5]}')
    if ap:
        vp_k, vp_j, pres = int(ap[5]), int(ap[6]), int(ap[7])
        num = lambda d, n, k: int((d.get(f'Nation{n}') or {}).get(k, 0) or 0)  # noqa: E731
        b = (num(s0, K, 'VP'), num(s0, J, 'VP'), num(s0, K, 'Prestige'))
        m1 = (num(s1, K, 'VP'), num(s1, J, 'VP'), num(s1, K, 'Prestige'))
        check(f'b) the merged save: VP of {K} {b[0]}{vp_k:+d}, VP of {J} {b[1]}{vp_j:+d}, Prestige of {K} '
              f'{b[2]}{pres:+d} (the save as sent + the deltas of the applied line)',
              s1 and m1 == (b[0] + vp_k, b[1] + vp_j, b[2] + pres), f'{b} -> {m1}')
        vp, p_now = vpinfo(H), nint(H, K, 'Prestige')
        mem = (vp and vp['vp'].get(K), vp and vp['vp'].get(J), p_now)
        f2 = (num(s2, K, 'VP'), num(s2, J, 'VP'), num(s2, K, 'Prestige'))
        check(f'b) host after the month: VP {K}/{J} and Prestige {K} in memory = its save, = the merged values unless the '
              f'game resolved another battle afterwards ({len(others)})', mem == f2 and (mem == m1 or others),
              f'memory {mem}, save {f2}, merged {m1}; {others}')
        n_now = nation_table(H)
        n_ph = ph.get('nations') or {}
        sk = sum(1 for n, _ in sunk if n == K)
        sj = sum(1 for n, _ in sunk if n == J)
        check(f'b) host `nations` ship counts: {me} {n_ph.get(K, {}).get("ships")} - {sk} sunk, {them} '
              f'{n_ph.get(J, {}).get("ships")} - {sj} sunk; the applied line removed {ap[4]}',
              n_ph and (n_now[K]['ships'] == n_ph[K]['ships'] - sk and n_now[J]['ships'] == n_ph[J]['ships'] - sj
                        or others) and int(ap[4]) >= sk + sj,
              f'{me} {n_ph.get(K, {}).get("ships")} -> {n_now[K]["ships"]}, {them} {n_ph.get(J, {}).get("ships")} -> '
              f'{n_now[J]["ships"]}')
        cp_, cv = nint(C, K, 'Prestige'), vpinfo(C)
        check(f'b) the client got the values with the new month: Prestige of {K}, VP of {K} and its ship count = host\'s',
              cp_ == p_now and cv and cv['vp'].get(K) == mem[0]
              and nation_table(C).get(K, {}).get('ships') == n_now[K]['ships'],
              f'client Prestige {cp_}, ships {nation_table(C).get(K, {}).get("ships")}; vp {cv and cv["raw"]}')
    ch_after = chat(ctx.cp)
    lines = [f'* The battle of {me} against {them} in {barea} was fought by Captain.', '* Battle result sent to the host (']
    check('b) client chat: "* Battle result sent to the host (...)" and the host\'s "* The battle of ... was fought by '
          'Captain."', all(x in ch_after for x in lines), [x for x in lines if x not in ch_after])
    exc = log_lines(hlog, r'(?i)exception') + log_lines(clog, r'(?i)exception')
    bl = battles_list()
    check('b) no exception in either log during the month; host `battles list` phase 0 and empty afterwards',
          not exc and bl and bl['phase'] == 0 and not bl['battles'], f'{exc[:3]}; {bl and bl["raw"]}')


def battle_declined_checks(ctx, K, J, area, me, them, bid, drv):
    """c) a battle declined in the game's preview: the client reports it as declined and the host lets the game
    resolve it."""
    hlog, clog, rec = log_since(H, drv.hmark), log_since(C, drv.cmark), drv.rec[bid]
    area = battle_area(log_since(H), bid, area)
    out = log_lines(clog, rf'\] battle {bid}: ')
    check(f'c) battle {bid}: the client declined it in the game\'s preview (buttons {rec.get("preview_buttons")})',
          any('BattlePreview' in a and a.endswith('->Decline') for a in rec['answers']), rec['answers'])
    lines = log_lines(hlog, rf'battle {bid}: resolved by the game|battle {bid} resolved by the game: ')
    ch = chat(ctx.cp)
    want = [f'* The battle of {me} against {them} in {area} was resolved by the game (Captain declined it).',
            '* You declined the battle; the game resolves it.']
    check(f'c) reported as declined: client log "battle {bid}: declined", host "battle {bid}: resolved by the game '
          f'(Captain declined it)", both chat lines',
          any(x.endswith(f'battle {bid}: declined') for x in out) and any('(Captain declined it)' in x for x in lines)
          and all(x in ch for x in want), f'client {out}; host {lines}; chat missing {[x for x in want if x not in ch]}')
    gone = [x for x in lines if re.search(rf'battle {bid}: resolved by the game \(', x)]
    fin = [x for x in lines if re.search(r'resolved by the game: (battle|no battle)$', x)]
    seen = f'* The battle of {me} against {them} in {area} was resolved by the game ('
    check(f'c) the host had the game resolve it: "battle {bid}: resolved by the game (<why>)", then "battle {bid} '
          f'resolved by the game: battle|no battle" after the month, the host\'s chat line; the month went on',
          gone and fin and seen in ch and any(m[0] == drv_month(drv) for m in ctx.months), lines)


def drv_month(drv):
    """The label of the month a BattleDriver ran (set by scenario_battles)."""
    return getattr(drv, 'label', None)


def battle_area(hlog, bid, default):
    """The sea area of battle `bid` from its request line in the host log (injected or found by the game)."""
    req = log_lines(hlog, rf'battle request {bid}[ :]')
    m = re.search(r' in (.+?)(?: \(\d+:\d+ ships\).*)?$', req[0]) if req else None
    return m[1] if m else default


def natural_request_checks(ctx, K, J, me, them, drivers):
    """Battles the game itself found in the battle months (DoBattleBetweenAINations of nation K and the AI nation at
    war with it): deferred with the request line and the host's chat line, sent to the player and settled (fought,
    or resolved by the game)."""
    reqs, missing, chat_lines, seen = [], [], [], set()
    ch = chat(ctx.cp)
    for drv in drivers:
        hlog = log_since(H, drv.hmark)
        for m in re.finditer(r'battle request (\d+): nation (\d+) vs (\d+) in (.+?) \((\d+):(\d+) ships\), fought by '
                             r'(\S+)', hlog):
            bid, a, b, area = int(m[1]), int(m[2]), int(m[3]), m[4]
            if bid in seen:  # a driver's log runs on to the end: later months are in it too
                continue
            seen.add(bid)
            reqs.append((bid, a, b, area, m[5] + ':' + m[6], m[7]))
            settled = re.search(rf'battle {bid} (applied|resolved by the game): ', hlog)
            if {a, b} != {K, J} or m[7] != 'Captain' or not re.search(rf'battle {bid} sent to Captain', hlog) \
                    or not settled:
                missing.append(bid)
            chat_lines.append(f'* Battle! {me} meets {them} in {area}. Captain fights it after the month.')
    if not reqs:
        log(f'  SKIP natural requests: the game found no battle of {me} and {them} by itself in the battle months')
        return
    log(f'  battles the game found by itself (id, a, b, area, ships, player): {reqs}')
    check(f'b) battles the game found by itself ({len(reqs)}): "battle request N: nation a vs b in <area> (x:y ships), '
          f'fought by Captain", sent to Captain and settled (merged or resolved by the game); the host\'s chat line "* '
          f'Battle! {me} meets {them} in <area>. Captain fights it after the month."',
          not missing and all(x in ch for x in chat_lines), f'{reqs}; not settled {missing}; chat missing '
          f'{[x for x in set(chat_lines) if x not in ch]}')


def battle_timeout_checks(ctx, K, J, area, me, them, bid, drv):
    """d) the battle left open (preview) past `battles timeout 20`: the host resolves it, the client's late answer is
    ignored, the month goes on."""
    hlog, clog, rec = log_since(H, drv.hmark), log_since(C, drv.cmark), drv.rec[bid]
    sent = log_lines(hlog, rf'battle {bid} sent to')
    gone = log_lines(hlog, rf'battle {bid}: resolved by the game \(time limit\)')
    dt = log_secs(gone[0]) - log_secs(sent[0]) if sent and gone else None
    check(f'd) host log: "battle {bid}: resolved by the game (time limit)" {HOLD_LIMIT}-{HOLD_LIMIT + 10} s after '
          f'"battle {bid} sent"', dt is not None and HOLD_LIMIT <= dt <= HOLD_LIMIT + 10, f'{dt} s; {sent} {gone}')
    t_sent = log_secs(sent[0]) if sent else None
    told = [x for x in log_lines(clog, r'resolved (this|your) battle without you')
            if t_sent is not None and log_secs(x) >= t_sent]  # not an earlier battle's notice
    check('d) client: "The host resolved this battle without you (time limit) ..." while its preview was open',
          told and '(time limit)' in told[0] and rec.get('preview_t'), told)
    late = log_lines(hlog, rf'battle result {bid} from Captain ignored') + \
        log_lines(clog, rf'battle {bid}: finished after the host resolved it; result not sent')
    check(f'd) the client\'s later answer is not used: host "battle result {bid} from Captain ignored" (or the client '
          f'sent none)', late, log_lines(clog, rf'battle {bid}') + late)
    fin = log_lines(hlog, rf'battle {bid} resolved by the game: ')
    st, cs = ctl.status(C), cstate()
    check(f'd) the month went on: the game resolved it ({fin}), the client plans the new month in slot {CLIENT_SLOT}, '
          f'slot mode off', fin and st.get('slot') == str(CLIENT_SLOT) and st.get('playerIdx') == str(K)
          and cs.get('slotMode') is False and cs.get('state') == 'none', f'{st}; {cs.get("raw")}')


# ---------------------------------------------------------------- live player-vs-player battles (lockstep)

K2 = 47703  # lockstep_pvp: the second joined player's bridge (a third game copy)
K2_SLOT = 76  # its slot (its battle slot K2_SLOT + 10); neither may exist before the run, both are deleted afterwards
K2_NAME = 'Bot'  # it joins as the stand-in player of `session`: the host gives it the reserved nation 2 back
HOST_NAME, CLIENT_NAME = 'Admiral', 'Captain'
PVP_AREA = 'Northern Europe'
LS_HASH_RE = re.compile(r'lockstep hash ([AB]) t=(\S+) min=(\d+) all=([0-9a-f]+) .*? rng=([0-9a-f]+)')
LS_BAD_RE = (r'lockstep: (FATAL|exception)|lockstep hook \S+(: no target| failed)|lockstep hooks: INCOMPLETE|'
             r'exception handling|load of slot \d+ FAILED|could not be loaded again|The live battle failed')
LS_NOTE_RE = r'lockstep: WARNING|context frame .* dropped|FPU control word'
LS_LEAK_RE = r'lockstep: LEAK '  # `ls leakcheck 1`: the simulation state changed between two simulation steps
LS_TEST_EXC = 'e0545354'  # the exception code of `ls throw` (a test's own exception, not a failure)
LS_HOOKS_RE = r'lockstep hooks: (ok|INCOMPLETE)'
LS_EXCUSED = set()  # log lines a test provoked on purpose (the guard's negative control: its LEAK lines)
LEAKCHECK = {}  # port -> (pid, reply of `ls leakcheck 1`) for the copy now running on that port
LS_HOLD = 'lockstep: battle held at its start'
LS_CLOSE_RE = (r'battle slot: DoStrategicTurn skipped|AI suppressed: |DoEndOfTurn finished|live battle: leaving the battle '
               r'slot|LoadCampaign slot|battle \d+: (fought|finished)')  # a battle screen's close has begun
LS_GAME_END_RE = (r"lockstep: (minute|end) box '(No opposing ships|.*scenario is over|.*no ships left|Objective reached|"
                  r"Scenario absolute time limit|All ships belonging)")  # the game's own end of a battle
LS_OFF = (r'live battle ended \(|lockstep: start abandoned|lockstep: hold released|live battle with .* not started|'
          r'fought against the AI|lockstep: battle \d+ refused|The live battle could not start')
LS_MONTH_EXTRA = 1800  # s a month with live battles may take on top of MONTH_TIMEOUT (a battle: ~1.5 minutes per s)
MEM_GROWTH_MAX = 25  # MB a copy's private memory may grow per live battle (average over the repeated battles)
OPTS = dict(stress=False)  # --stress: the long variants and the lockstep_stress scenario
K2PROC = [None]  # the third copy's Popen while lockstep_pvp runs
_ANS = [None]  # the dialog answerer thread of the live battles (LsAnswerer)
KEPT_LOGS = []  # logs of copies that were killed and started again (copied before the restart)
LS_MATRIX = []  # (case, checks passed, checks, seconds, note) of the lockstep scenarios' cases


def scmd(port, line, timeout=60):
    """cmd() that does not raise: a copy that is gone (killed) or does not answer gives 'error ...'."""
    try:
        return cmd(port, line, timeout)
    except OSError as e:
        return f'error {e!r}'


def sstatus(port):
    try:
        return ctl.status(port)
    except OSError:
        return {}


def kvs(reply):
    """'ok a=1 b=x' -> {'a': '1', 'b': 'x'} ({} for an error reply)."""
    return dict(x.split('=', 1) for x in reply[3:].split() if '=' in x) if reply.startswith('ok') else {}


def lss(port):
    """`ls status` of a copy (on, role, st, bid, epoch, side, boundary, running, left, peer, peerDone, resyncs, end,
    proposal, leave, ...)."""
    return kvs(scmd(port, 'ls status', 30))


def lsn(port):
    """`ls session` of a copy (enabled, available, role, hostOpp, hostBid, testSlotMode, leave, slotMode, players)."""
    return kvs(scmd(port, 'ls session', 30))


def pst(port):
    return kvs(scmd(port, 'pocstate', 30))


def ls_lines(port, mark, pattern):
    return [ln.split('] ', 1)[-1] for ln in log_lines(log_since(port, mark), pattern)]


def ls_wait(fn, timeout, what, step=0.5):
    """Poll fn() until it returns something true; RuntimeError after `timeout` s."""
    end, v = time.time() + timeout, None
    while time.time() < end:
        assert_alive()
        v = fn()
        if v:
            return v
        time.sleep(step)
    raise RuntimeError(f'timeout ({timeout} s): {what}; last {v}')


def k2_pid():
    p = K2PROC[0]
    return p.pid if p is not None and p.poll() is None else None


_NATION1 = [None]


def nation1():
    """The name of nation 1 (the client's), for `claim <name>` (a claim by index needs the lobby list of the client)."""
    if not _NATION1[0]:
        _NATION1[0] = nation_table(H).get(1, {}).get('name') or '1'
    return _NATION1[0]


def who_of(port):
    return {H: 'host', C: 'client', K2: 'k2'}[port]


def pid_of(ctx, port):
    return {H: ctx.hp, C: ctx.cp, K2: k2_pid()}[port]


def ls_leakcheck(ctx, ports):
    """`ls leakcheck 1` on these copies (a per-process switch: again after a copy was started again). Every live battle
    of the suite runs with it; `ls_log_scan` asserts that no `lockstep: LEAK` line was logged."""
    for p in ports:
        pid = pid_of(ctx, p)
        if pid and LEAKCHECK.get(p, (None,))[0] == pid and LEAKCHECK[p][1] == 'ok leakcheck=1':
            continue
        r = scmd(p, 'ls leakcheck 1')
        LEAKCHECK[p] = (pid, r)
        log(f'  leak check on the {who_of(p)} (PID {pid}): {r}')


class LsAnswerer(threading.Thread):
    """Answers the dialogs of the game copies in the background while live battles run (posted BM_CLICK and radio
    selections only): battle preview and map message Accept (unless the copy is in `hold`), results "Exit battle",
    the ended battle screen's "View results" (copies in `view_results`), the auto-run prompt "Yes" for copies in
    `autorun` (else "No"), the history InputQuery Cancel, Yes/OK boxes, picture messages OK, event dialogs with their
    first answer ("Not now" if offered, a nation picked if needed), "Select Officer" with the first officer, the turn
    report Close, forms with a "Not now" button. Paused while run_month answers with answer_dialogs."""

    def __init__(self):
        super().__init__(daemon=True)
        self.pids = {}
        self.paused = True
        self.stop = False
        self.autorun, self.view_results, self.hold = set(), set(), set()
        self.pressed = {}
        self.seen = []  # (time, who, 'Class:"title"->what')
        self.exit_t = {}  # who -> when "Exit battle" was last pressed on its results
        self.exit_mark = {}  # who -> its log size then (the battle screen's close shows in the log: LS_CLOSE_RE)
        self.probe = []  # (time, s after "Exit battle", who, screen enabled, "View results" shown, closing): no dialog up

    def press(self, who, top, h, what):
        if h is None or time.time() - self.pressed.get(top, 0) < 1.5:
            return False
        u.PostMessageW(h, 0x00F5, 0, 0)  # BM_CLICK
        self.pressed[top] = time.time()
        e = f'{e2e_test.class_name(top)}:"{e2e_test.window_text(top)[:40]}"->{what}'
        self.seen.append((time.time(), who, e))
        ANSWERED.append((time.time(), who, e))
        log(f'  {who} dialog: {e}')
        return True

    def once(self, pid, who):
        tops = top_windows(pid)
        frm, pressed = None, False
        for top in tops:
            k = e2e_test.class_name(top)
            if k == 'TfrmMain':
                frm = top
                continue
            if k.startswith('RTW3MP') or k in ('TfrmBuildCamp', '#32770'):
                continue
            query = is_input_query(top)
            b = dlg_buttons(top)
            first = next(iter(b.values()), None)
            if not (k == 'TMessageForm' or k.startswith('Tdlg') or query):
                if 'not now' in b:  # e.g. a design ready for construction
                    pressed |= self.press(who, top, b['not now'], 'Not now')
                continue
            pressed = True  # a dialog is up: the battle screen is disabled behind it
            if query:
                self.press(who, top, b.get('cancel'), 'Cancel')
            elif k in ('TdlgBattlePreview', 'TdlgMapMessage'):
                if who not in self.hold:
                    self.press(who, top, b.get('accept') or first, 'Accept')
            elif k == 'TdlgResults':
                if self.press(who, top, b.get('exit battle') or b.get('view map') or first, 'Exit battle'):
                    self.exit_t[who] = time.time()
                    self.exit_mark[who] = log_mark({'host': H, 'client': C, 'k2': K2}[who])
            elif k == 'TdlgEventAnswer':
                radios = [caption(h) for h in children(top) if e2e_test.class_name(h) == 'TRadioButton'
                          and u.IsWindowVisible(h)]
                if 'Yes' in radios and 'No' in radios:  # the auto-run prompt
                    want = 'Yes' if who in self.autorun else 'No'
                    if select_radio(top, want):
                        self.press(who, top, b.get('ok'), f'OK [{want}]')
                else:
                    select_event_nation(pid)
                    self.press(who, top, b.get('ok') or first, 'OK')
            elif k == 'TdlgSelectOfficer':
                select_officer(pid)
                self.press(who, top, b.get('ok') or first, 'OK')
            elif k == 'TdlgPictureMessage':
                self.press(who, top, b.get('ok') or b.get('yes') or first, 'OK')
            elif k == 'TMessageForm':
                pick = 'yes' if 'yes' in b else 'ok' if 'ok' in b else None
                self.press(who, top, b.get(pick) if pick else first, pick or 'first')
            else:
                pick = next((p for p in ('ok', 'close', 'continue', 'yes', 'accept', 'not now') if p in b), None)
                self.press(who, top, b.get(pick) if pick else first, pick or 'first')
        if frm is None:
            self.exit_t.pop(who, None)
            self.exit_mark.pop(who, None)
            return
        btn = results_button(frm)
        since_exit = time.time() - self.exit_t.get(who, 0)
        port = {'host': H, 'client': C, 'k2': K2}.get(who)
        closing = who in self.exit_mark and bool(log_lines(log_since(port, self.exit_mark[who]), LS_CLOSE_RE))
        if since_exit < 30 and not pressed:  # the battle was left: is its screen still usable while the month runs?
            self.probe.append((time.time(), round(since_exit, 1), who, bool(u.IsWindowEnabled(frm)), bool(btn),
                               closing))
        # "View results" only as a player could press it: no dialog up, the screen enabled, and not while the screen is
        # closing after "Exit battle" (its close runs the rest of the month: a second press would run it again)
        if who in self.view_results and btn and not pressed and u.IsWindowEnabled(frm) and since_exit > 10 \
                and not closing:
            self.press(who, frm, btn, 'View results')

    def run(self):
        while not self.stop:
            if not self.paused:
                for pid, who in list(self.pids.items()):
                    try:
                        if pid:
                            self.once(pid, who)
                    except Exception as e:  # noqa: BLE001
                        log('  answerer:', repr(e))
            time.sleep(0.4)


def results_button(frm):
    """The ended battle screen's "View results" button (a TButton on TfrmMain), or None."""
    for h in children(frm):
        if e2e_test.class_name(h) == 'TButton' and u.IsWindowVisible(h) and u.IsWindowEnabled(h) \
                and 'result' in caption(h).lower():
            return h
    return None


def ls_ans(ctx):
    """The answerer thread (started once), its copies updated (a copy started again has a new PID)."""
    a = _ANS[0]
    if a is None:
        a = _ANS[0] = LsAnswerer()
        a.start()
    a.pids = {ctx.hp: 'host', ctx.cp: 'client'}
    if k2_pid():
        a.pids[k2_pid()] = 'k2'
    return a


def ls_quiet(ctx):
    """Pause the answerer (run_month and the other scenarios answer the dialogs themselves)."""
    a = ls_ans(ctx)
    a.paused = True
    a.view_results.clear()
    a.autorun.clear()
    a.hold.clear()
    time.sleep(0.8)
    return a


def ls_answered(ans, who, since, what):
    return [e for t, w, e in ans.seen if t >= since and w == who and what in e]


MONTHS = 'Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec'.split()


def month_after(d):
    """'Aug 1900' -> 'Sep 1900' (None if d is not such a date)."""
    try:
        m, y = d.split()
        i = MONTHS.index(m[:3])
        return f'{MONTHS[(i + 1) % 12]} {int(y) + (i == 11)}'
    except (ValueError, AttributeError):
        return None


def ls_both_running(A, B):
    a, b = lss(A), lss(B)
    return (a, b) if a.get('st') == 'running' and b.get('st') == 'running' and a.get('bid') == b.get('bid') else None


def ls_idle_equal(A, B):
    """Both copies between minutes at the same boundary, A's clock stopped, B's report of it in."""
    a, b = lss(A), lss(B)
    if a.get('st') == b.get('st') == 'running' and a.get('running') == b.get('running') == '0' and \
            a.get('left') == '0' and a.get('boundary') == b.get('boundary') and a.get('peerDone') == a.get('boundary'):
        return a, b
    return None


def ls_over(A):
    return lss(A).get('on') != '1'


def ls_run(A, B, n, frm, timeout=None):
    """`ls run n` asked by one copy; waits until both are idle at the same boundary (or A's live battle is over).
    Returns the minutes advanced."""
    a0 = int(lss(A).get('boundary') or 0)
    scmd(frm, f'ls run {n}')
    time.sleep(1.0)
    ls_wait(lambda: ls_idle_equal(A, B) or ls_over(A), timeout or 60 + 20 * min(n, 50), f'ls run {n}')
    return int(lss(A).get('boundary') or 0) - a0


def ls_run_total(A, B, n, frm):
    """n minutes in all (a break stops a run: run again)."""
    done = 0
    for _ in range(n + 5):
        if done >= n or ls_over(A):
            break
        done += max(0, ls_run(A, B, n - done, frm))
    return done


def ls_hashes(port, mark):
    out = {}
    for m in LS_HASH_RE.finditer(log_since(port, mark)):
        out.setdefault(int(m[3]), []).append((m[4], m[5]))
    return out


def ls_compare(A, ma, B, mb, label=''):
    """Per-minute state hashes (`lockstep hash A|B ... min=<m> all=<state> ... rng=<rng>`) of both copies since the
    marks: every minute both logged must be equal (the last line of a minute: a resync loads a minute again)."""
    a, b = ls_hashes(A, ma), ls_hashes(B, mb)
    both = sorted(set(a) & set(b))
    differ = [m for m in both if a[m][-1] != b[m][-1]]
    res = dict(minutes=len(both), first=both[0] if both else None, last=both[-1] if both else None, differ=differ)
    log(f'  hashes {label}: {len(both)} minutes compared ({res["first"]}..{res["last"]}), differ {differ[:8]}')
    return res


def ls_divs(port):
    out = []
    for e in scmd(port, 'ls divs')[3:].split(';'):
        p = e.strip().split(':')
        if len(p) >= 5 and p[0].isdigit():
            cs = p[3].split('/')
            try:
                out.append(dict(i=int(p[0]), side=int(p[1]), name=p[2], course=int(cs[0]), speed=int(cs[1]),
                                ships=int(p[4]), ai=len(p) > 5 and p[5] == 'ai'))
            except ValueError:
                pass
    return out


def ls_pick_div(port, side, min_ships=1):
    ds = [d for d in ls_divs(port) if d['side'] == side and d['ships'] >= min_ships]
    return next((d for d in ds if not d['ai']), ds[0] if ds else None)


def ls_all_ai(A, B):
    """Every division of each side to the game's AI, ordered by its own copy (`ls order i -1 -1 1`, an order batch):
    the AI seeks contact, so the battle has damage. Returns the number of orders."""
    n = 0
    for port, side in ((A, 0), (B, 1)):
        for d in ls_divs(port):
            if d['side'] == side and not d['ai'] and scmd(port, f'ls order {d["i"]} -1 -1 1') == 'ok':
                n += 1
    return n


def ls_end_both(A, B):
    """Both players ask for the end (`ls end` toggles a player's request: only the missing ones are sent)."""
    e = (lss(A).get('end') or '0/0').split('/')
    if e[0] != '1':
        scmd(A, 'ls end')
    if len(e) < 2 or e[1] != '1':
        scmd(B, 'ls end')


def ls_end_reset(A, B):
    """Withdraw end requests that are still pending (after a test of odd moments)."""
    for _ in range(3):
        e = (lss(A).get('end') or '0/0').split('/')
        if e == ['0', '0'] or lss(A).get('on') != '1':
            return e
        if e[0] == '1':
            scmd(A, 'ls end')
        if len(e) > 1 and e[1] == '1':
            scmd(B, 'ls end')
        time.sleep(1.5)
    return (lss(A).get('end') or '?').split('/')


def ls_drive_end(A, B, mode='agree', min_end=410, cap=900, chunk=60):
    """Drive a running live battle to its end at speed 5 in runs of `chunk` minutes asked alternately by A and B.
    'agree': from minute `min_end` both press End (the game refuses before 400 minutes / half the battle / with enemies
    near: 30 more minutes, then again). 'game': the game's own end; when the game proposes one (proposal=1, "far apart
    ... end the scenario?") both press End; the agreed end only from minute `cap`. Returns dict(minute, seconds,
    tries, refusals, proposals)."""
    t0, res, turn, last = time.time(), dict(tries=0, refusals=0, proposals=0), 0, {}
    scmd(A, 'ls speed 5')
    while True:
        a = lss(A)
        if a.get('on') != '1':
            break
        last = a
        if a.get('st') != 'running':
            time.sleep(1)
            continue
        m, prop = int(a.get('boundary') or 0), a.get('proposal') == '1'
        if mode == 'agree' and m < min_end:
            prop = False  # 'agree' plays on to the long agreed end; the game's proposal is the 'game' mode's end
        if prop or (mode == 'agree' and m >= min_end) or m >= cap:
            res['tries'] += 1
            res['proposals'] += int(prop)
            if res['tries'] > 20:
                raise RuntimeError(f'the battle did not end after {res["tries"]} agreed end requests')
            mk = log_mark(A)
            ls_end_both(A, B)
            ls_wait(lambda: ls_over(A) or ls_lines(A, mk, r"can't end yet"), 150, 'the agreed end')
            if not ls_over(A):
                res['refusals'] += 1
                ls_run_total(A, B, 30, A)  # 30 battle minutes, also when breaks stop each run after a minute
            continue
        ls_run(A, B, chunk, A if turn % 2 == 0 else B, timeout=480)
        turn += 1
        if time.time() - t0 > LS_MONTH_EXTRA - 120:
            raise RuntimeError(f'the battle did not end within {LS_MONTH_EXTRA - 120} s (minute {m})')
    res.update(minute=int(last.get('boundary') or 0), seconds=round(time.time() - t0, 1))
    log(f'  battle over at minute {res["minute"]} after {res["seconds"]} s ({mode}; {res["tries"]} end requests, '
        f'{res["refusals"]} refused, {res["proposals"]} proposed by the game)')
    return res


def ls_drive_endsoon(A, B, until=6):
    """'endsoon': the game itself ends the live battle INSIDE a minute (the path of 0.3.0's deferred close). From
    minute `until` (run there first) A sends `ls endsoon` (both copies shorten the scenario at the next released minute,
    so the game's own "Scenario absolute time limit is reached!" ends the battle inside it) and runs one minute; the
    answerer presses "Exit battle" in both results. Returns dict(minute, seconds, endsoon, endsoon_at, tries=0, ...)."""
    t0, res = time.time(), dict(tries=0, refusals=0, proposals=0, endsoon=None, endsoon_at=None)
    scmd(A, 'ls speed 5')
    m = int(lss(A).get('boundary') or 0)
    if m < until:
        ls_run_total(A, B, until - m, A)
    if not ls_over(A):
        ls_wait(lambda: ls_idle_equal(A, B), 60, 'both idle at a boundary before `ls endsoon`')
        res['endsoon_at'] = int(lss(A).get('boundary') or 0)
        res['endsoon'] = scmd(A, 'ls endsoon')
        scmd(A, 'ls run 1')
        ls_wait(lambda: ls_over(A), 240, 'the game ends the battle inside the next minute')
    res.update(minute=int(lss(A).get('boundary') or 0), seconds=round(time.time() - t0, 1), t_end=time.time())
    log(f'  battle over at minute {res["minute"]} after {res["seconds"]} s (`ls endsoon` at minute {res["endsoon_at"]}: '
        f'{res["endsoon"]})')
    return res


def ls_back(port, pid, slot, nation):
    """A joined player's copy back in its own slot as its nation: no live battle, no battle window, battle slot mode
    off. Returns its status or None."""
    st = sstatus(port)
    if st.get('slot') == str(slot) and st.get('playerIdx') == str(nation) and lss(port).get('on') == '0' \
            and lsn(port).get('slotMode') == '0' and not battle_up(pid):
        return st
    return None


def ls_client_back(ctx):
    return ls_back(C, ctx.cp, CLIENT_SLOT, 1)


def ls_start_test(ctx, tag, timeout=150):
    """The host's battle against nation 1 outside a month: `pocbattle 1 0` (the debug Test battle = the game's DoBattle;
    the answerer accepts its preview; battle slot mode, so its close runs no month). Waits until both copies run it in
    lockstep. Returns dict(hm, cm, t0, dt, a, b)."""
    ans = ls_ans(ctx)
    ans.paused = False
    ls_wait(lambda: ls_client_back(ctx), 120, 'the client idle in slot 77 before a new battle')
    hm, cm, t0 = log_mark(H), log_mark(C), time.time()
    r = cmd(H, 'pocbattle 1 0')
    a, b = ls_wait(lambda: ls_both_running(H, C), timeout, f'battle {tag} running on both')
    dt = time.time() - t0
    log(f'  battle {tag}: running on both after {dt:.1f} s ({r}); bid {a.get("bid")} epoch {a.get("epoch")}')
    return dict(hm=hm, cm=cm, t0=t0, dt=dt, a=a, b=b)


def autorun_click(ctx, st, who, pid, frm, port=None):
    """One step of fighting a (not live) battle screen on auto-run: its "Auto run" button (a posted click on the tool
    bar; the answerer says "Yes" to the prompt), up to 3 tries 8 s apart, then the POC fallback (both sides AI, speed 5,
    run) when the copy's bridge may be used (port); later a stalled run is restarted (pocrun 100000)."""
    ans = ls_ans(ctx)
    ans.autorun.add(who)
    ans.view_results.add(who)
    tops = top_windows(pid)
    if any(e2e_test.class_name(w) == 'TMessageForm' or e2e_test.class_name(w).startswith('Tdlg') for w in tops) \
            or results_button(frm):
        return
    now = time.time()
    if ls_answered(ans, who, st.setdefault('since', now), 'OK [Yes]') or st.get('poc'):
        if port is not None and now - st.get('t_state', 0) > 20:
            st['t_state'] = now
            ps = pst(port)
            stalled = ps.get('running') == '0' and ps.get('moving') == '0' and ps.get('finished') == '1' \
                and ps.get('endgame') == '0'
            if stalled and st.get('stalled'):
                log(f'  {who} battle stalled at a boundary: pocrun 100000', scmd(port, 'pocrun 100000'))
            st['stalled'] = stalled
        return
    if st.get('clicks', 0) < 3 and now - st.get('t', 0) > 8:
        if click_autorun(frm):
            st['clicks'] = st.get('clicks', 0) + 1
            st['t'] = now
            log(f'  {who} battle screen: auto-run button clicked (posted)')
    elif st.get('clicks', 0) >= 3 and port is not None and now - st.get('t', 0) > 12:
        st['poc'] = True
        for line in ('pocsideai 0 1 0', 'pocsideai 1 1 0', 'pocspeed 5', 'pocrun 100000'):
            log(f'  {who} {line}:', scmd(port, line)[:80])


def ls_finish_host(ctx, timeout=600):
    """The host's open battle screen (no longer live) fought to its end on auto-run and closed (results "Exit
    battle"). Outside a month the battle slot mode then ends (no month runs). Returns True when it is closed."""
    ans = ls_ans(ctx)
    ans.paused = False
    st, t0, gone = {}, time.time(), None
    while time.time() - t0 < timeout:
        assert_alive()
        if not battle_up(ctx.hp):  # closed for 3 s (the screen hides for a moment between its result dialogs)
            gone = gone or time.time()
            if time.time() - gone >= 3:
                break
            time.sleep(0.5)
            continue
        gone = None
        frm = next((h for h in top_windows(ctx.hp) if e2e_test.class_name(h) == 'TfrmMain'), None)
        if frm is not None and lss(H).get('on') != '1':
            autorun_click(ctx, st, 'host', ctx.hp, frm, H)
        time.sleep(1)
    ans.autorun.discard('host')
    ans.view_results.discard('host')
    closed = not battle_up(ctx.hp)
    log(f'  the host\'s battle {"closed" if closed else "NOT closed"} after {time.time() - t0:.0f} s')
    return closed


def ls_rejoin(ctx, fight=True):
    """The client leaves (if joined) and joins again as Captain with `fightbattles`; the host should give the
    returning player nation 1 back and send the month. If it does not within 25 s, `claim 1`. Returns (status, host
    line, client line, claimed)."""
    if sstatus(C).get('role') == 'client':
        scmd(C, 'leave')
        ls_wait(lambda: 'Captain(' not in lsn(H).get('players', 'Captain('), 20, 'the host saw the client leave')
    hm, cm = log_mark(H), log_mark(C)
    scmd(C, f'fightbattles {1 if fight else 0}')
    r = cmd(C, f'join {CLIENT_NAME} 127.0.0.1 {MP_PORT}')
    hpat, cpat = r'state seq \d+ -> Captain \(nation 1,', rf'LoadCampaign slot {CLIENT_SLOT} player 1 -> ok'
    sent, claimed = wait_log(H, hm, hpat, 25), False
    if not sent:
        claimed = True
        log(f'  the host did not give nation 1 back: claim {nation1()} ->', cmd(C, f'claim {nation1()}'))
        sent = wait_log(H, hm, hpat, 60)
    loaded = wait_log(C, cm, cpat, 60)
    st = wait(C, lambda s: s.get('phase') == 'planning' and s.get('myNation') == '1' and s.get('playerIdx') == '1'
              and s.get('slot') == str(CLIENT_SLOT), 60, 'the client plans nation 1 again')
    log(f'  rejoined with fightbattles {1 if fight else 0}: join {r}; {sent[:1]} {loaded[:1]}'
        + (' (claimed)' if claimed else ''))
    time.sleep(2)
    cmd(C, 'mpwindow')
    time.sleep(1)
    return st, sent, loaded, claimed


def ls_ensure_session(ctx):
    """The client is in the session as Captain, plans nation 1 in slot 77, fights battles and plays them live (after
    `battles` it fights none, after `player_copy` it left the session): otherwise it joins again."""
    st, hs = sstatus(C), lsn(H)
    nation1()
    ls_leakcheck(ctx, (H, C))
    if st.get('role') == 'client' and st.get('myNation') == '1' and st.get('slot') == str(CLIENT_SLOT) \
            and st.get('phase') == 'planning' and 'Captain(1,b,ls)' in hs.get('players', ''):
        return True
    log(f'  the client joins again for live battles (role {st.get("role")}, nation {st.get("myNation")}, slot '
        f'{st.get("slot")}; host players {hs.get("players")})')
    if lsn(C).get('enabled') != '1':
        scmd(C, 'ls enable 1')
    st, sent, loaded, _ = ls_rejoin(ctx, fight=True)
    return bool(sent and loaded)


def turn_button(pid):
    """(caption, visible, enabled) of the strategic screen's Turn/Submit button, or None."""
    for w in top_windows(pid):
        if e2e_test.class_name(w) == 'TfrmBuildCamp':
            for h in children(w):
                if e2e_test.class_name(h) in ('TButton', 'TBitBtn') and caption(h) in ('Turn', 'Submit'):
                    return caption(h), bool(u.IsWindowVisible(h)), bool(u.IsWindowEnabled(h))
    return None


def ls_after_checks(ctx, label):
    """j) after a live-battle case: no RTW3.exe but this run's copies, the client plans nation 1 in slot 77 (no live
    battle, battle slot mode off, no battle window, strategic screen enabled), the host's live battle and battle slot
    mode are off, slot 1's save readable as the host's month."""
    try:
        ls_wait(lambda: not battle_up(ctx.cp) and not battle_up(ctx.hp) and lss(H).get('on') == '0'
                and lsn(H).get('slotMode') == '0', 30, 'no battle left', step=1)
    except RuntimeError as e:
        log('  !!', e)
    mine = sorted(p.pid for p in PROCS if p.poll() is None)
    running = sorted(running_pids('RTW3.exe'))
    st, hst, cs, hn, cl = sstatus(C), sstatus(H), cstate(), lsn(H), lss(C)
    g = (read_save(save_path(HOST_SLOT)) or {}).get('General') or {}
    c77 = (read_save(save_path(CLIENT_SLOT)) or {}).get('General') or {}

    def as_date(gen):
        try:
            return f'{MONTHS[int(gen.get("Month")) - 1]} {gen.get("Year")}'
        except (TypeError, ValueError, IndexError):
            return None

    ok = running == mine and st.get('slot') == str(CLIENT_SLOT) and st.get('playerIdx') == '1' \
        and st.get('phase') == 'planning' and cs.get('slotMode') is False and cl.get('on') == '0' \
        and not battle_up(ctx.cp) and strategic_enabled(ctx.cp) and lss(H).get('on') == '0' \
        and hn.get('slotMode') == '0' and hn.get('hostOpp') == '-1' and not battle_up(ctx.hp) \
        and hst.get('slot') == str(HOST_SLOT) and hst.get('playerIdx') == '0' and as_date(g) == hst.get('date') \
        and as_date(c77) == st.get('date')
    return check(f'{label}: j) no RTW3.exe left but this run\'s copies; the client plans nation 1 in slot 77 (no live '
                 f'battle, battle slot mode off, no battle window, strategic screen enabled; its slot 77 save readable '
                 f'and of its month); the host: no live battle, battle slot mode off, no battle window, slot 1 open and '
                 f'its save readable and of the host\'s month', ok,
                 f'RTW3.exe {running} (ours {mine}); client {st.get("slot")}/{st.get("playerIdx")}/{st.get("phase")} '
                 f'{st.get("date")} save77 {as_date(c77)} {cs.get("raw")} ls on={cl.get("on")} enabled='
                 f'{strategic_enabled(ctx.cp)}; host {hn} slot {hst.get("slot")} {hst.get("date")} save1 {as_date(g)} '
                 f'(ScenarioInProgress {g.get("ScenarioInProgress")})')


def ls_log_scan(label, marks):
    """No lockstep error in the logs since the marks ({port: mark}); the hooks installed at most once per copy. And the
    leak check (`ls leakcheck 1`, on in every live battle of the suite) logged no `lockstep: LEAK` line (lines a test
    provoked on purpose are in LS_EXCUSED); its own `display wrote N sighting fields` lines (the guard kept a display
    write out of the simulation) and WARNING lines are reported."""
    bad = {who_of(p): [x for x in ls_lines(p, m, LS_BAD_RE) if LS_TEST_EXC not in x][:4] for p, m in marks.items()}
    notes = {who_of(p): ls_lines(p, m, LS_NOTE_RE)[:4] for p, m in marks.items()}
    hooks = {who_of(p): len(log_lines(log_since(p), LS_HOOKS_RE)) for p in marks}
    leaks = {who_of(p): [x for x in ls_lines(p, m, LS_LEAK_RE) if x not in LS_EXCUSED] for p, m in marks.items()}
    excused = {who_of(p): sum(1 for x in ls_lines(p, m, LS_LEAK_RE) if x in LS_EXCUSED) for p, m in marks.items()}
    guard = {who_of(p): len(ls_lines(p, m, r'lockstep: the display wrote \d+ sighting fields')) for p, m in marks.items()}
    on = {who_of(p): LEAKCHECK.get(p, (None, 'never'))[1] for p in marks}
    if any(notes.values()):
        log(f'  {label}: lockstep notes (reported): {notes}')
    log(f'  {label}: leak check {on}; guard catches (display wrote sighting fields) {guard}; LEAK lines provoked on '
        f'purpose {excused}')
    check(f'{label}: no lockstep error in the logs (FATAL, exception, hook failed/INCOMPLETE, load FAILED, battle '
          f'failed); the lockstep hooks installed at most once per copy', not any(bad.values())
          and all(v <= 1 for v in hooks.values()), f'{bad}; hooks lines {hooks}')
    return check(f'{label}: the leak check (`ls leakcheck 1` on every copy) found no display write into the simulation: '
                 f'no "lockstep: LEAK" line', all(v == 'ok leakcheck=1' for v in on.values())
                 and not any(leaks.values()), f'leak check {on}; LEAK lines {[(w, v[:3]) for w, v in leaks.items() if v]}')


def ls_keep_log(port, tag):
    """Copy a copy's log before it is started again (the mod renames it to .prev.log at start: one generation)."""
    dst = os.path.join(LOG_DIR, f'rtw3mp_{port}.{tag}.log')
    try:
        shutil.copyfile(log_path(port), dst)
        KEPT_LOGS.append(dst)
        log(f'  log of {who_of(port)} kept: {os.path.basename(dst)}')
    except OSError as e:
        log(f'  !! could not keep the log of {who_of(port)}: {e!r}')


def ls_kill(ctx, who):
    """taskkill /F of one of this run's copies (by its PID); it is taken out of PROCS first."""
    pid = ctx.hp if who == 'host' else ctx.cp
    p = next((x for x in PROCS if x.pid == pid), None)
    if p is not None:
        PROCS.remove(p)
    subprocess.run(['taskkill', '/PID', str(pid), '/F'], capture_output=True)
    if p is not None:
        try:
            p.wait(30)
        except subprocess.TimeoutExpired:
            log(f'  !! PID {pid} still running 30 s after taskkill')
    log(f'  killed the {who} (PID {pid}, taskkill /F)')


def ls_relaunch(ctx, who, extra=None, tag='restart'):
    """Start a killed copy again (same bridge, log tag and slot; extra environment for the client)."""
    port = H if who == 'host' else C
    ls_keep_log(port, f'{tag}{len(KEPT_LOGS) + 1}')
    if who == 'host':
        p = launch(H)
    else:
        p = launch(C, CLIENT_SLOT, dict({'RTW3MP_EVENT_CHANCE': str(ctx.event_chance)}, **(extra or {})))
    PROCS.append(p)
    if who == 'host':
        ctx.hp = p.pid
    else:
        ctx.cp = p.pid
    ok = wait_bridge(port)
    ls_ans(ctx)
    if ok:
        ls_leakcheck(ctx, (port,))
    log(f'  started the {who} again: PID {p.pid}, bridge {"up" if ok else "DOWN"}'
        + (f', env {extra}' if extra else ''))
    return ok


def restore_slot1_copy():
    """Slot 1 put back from this run's backup in the middle of the run (the backup is kept for the final restore)."""
    g1 = slot_dir(HOST_SLOT)
    for _ in range(10):
        try:
            if os.path.exists(g1):
                shutil.rmtree(g1)
            shutil.copytree(BACKUP, g1)
            if tree_digest(g1) == tree_digest(BACKUP):
                return True
        except OSError:
            pass
        time.sleep(1)
    return False


def k2_start(ctx):
    """lockstep_pvp: a third copy (bridge 47703, slot 76, battle slot 86) joins as "Bot"; the host gives it the reserved
    nation 2 back. Returns (join reply, host lines, status)."""
    p = launch(K2, K2_SLOT, {'RTW3MP_EVENT_CHANCE': '0'})
    PROCS.append(p)
    K2PROC[0] = p
    log('started the third copy PID', p.pid)
    if not wait_bridge(K2):
        raise RuntimeError('the third copy did not answer on its bridge')
    ls_leakcheck(ctx, (K2,))
    hm = log_mark(H)
    scmd(K2, 'fightbattles 1')
    r = cmd(K2, f'join {K2_NAME} 127.0.0.1 {MP_PORT}')
    sent = wait_log(H, hm, rf'state seq \d+ -> {K2_NAME} \(nation {STANDIN},', 30)
    if not sent:
        log('  the host did not give nation 2 back: claim 2 ->', cmd(K2, f'claim {STANDIN}'))
        sent = wait_log(H, hm, rf'state seq \d+ -> {K2_NAME} \(nation {STANDIN},', 60)
    st = wait(K2, lambda s: s.get('phase') == 'planning' and s.get('playerIdx') == str(STANDIN)
              and s.get('slot') == str(K2_SLOT), 90, 'the third copy plans nation 2 in slot 76')
    cmd(K2, 'mpwindow')
    ls_ans(ctx)
    return r, sent, st


def k2_stop(ctx):
    """The third copy leaves the session and is closed (its PID)."""
    p = K2PROC[0]
    if p is None:
        return
    scmd(K2, 'leave')
    try:
        ls_wait(lambda: f'{K2_NAME}(' not in lsn(H).get('players', f'{K2_NAME}('), 20, 'the host saw Bot leave')
    except RuntimeError as e:
        log('  !!', e)
    if p in PROCS:
        PROCS.remove(p)
    subprocess.run(['taskkill', '/PID', str(p.pid), '/F'], capture_output=True)
    try:
        p.wait(30)
    except subprocess.TimeoutExpired:
        pass
    K2PROC[0] = None
    ls_ans(ctx)
    log('closed the third copy PID', p.pid)


def ls_detect(k2):
    """A live battle running on two copies: B = a joined player's copy whose `ls status` shows a running battle as B,
    A = its peer (the host, or the other joined player). Returns (A, B) or None. Polls only the clients' bridges, and
    the host's only once its live battle runs (its month waits in the battle then)."""
    for p in [C] + ([K2] if k2 else []):
        b = lss(p)
        if b.get('on') == '1' and b.get('role') == 'B' and b.get('st') == 'running':
            A = {HOST_NAME: H, CLIENT_NAME: C, K2_NAME: K2}.get(b.get('peer'))
            if A is None or A == p:
                continue
            a = lss(A)
            if a.get('on') == '1' and a.get('role') == 'A' and a.get('st') == 'running' and a.get('bid') == b.get('bid'):
                return A, p
    return None


def host_live(rec):
    """From the host's log of the month (no bridge call): True while its battle is held/live, False once it ended or
    was refused, None if no live battle was tried."""
    lines = log_lines(log_since(H, rec['hmark']), rf'{LS_HOLD}|{LS_OFF}')
    return None if not lines else LS_HOLD in lines[-1]


def ls_tick_host(ctx, rec):
    """The host's battle screen when it is not (or no longer) live: fought on auto-run (the bridge is polled only for a
    stalled run while the battle screen is up)."""
    st = rec['autorun'].setdefault('host', {})
    frm = next((h for h in top_windows(ctx.hp) if e2e_test.class_name(h) == 'TfrmMain'), None)
    if frm is None:
        if st.get('since'):
            rec['autorun']['host'] = {}
        return
    st.setdefault('since', time.time())
    live = host_live(rec)
    if live or (live is None and time.time() - st['since'] < 15):
        return
    st.setdefault('why', 'not live' if live is None else 'live battle over')
    autorun_click(ctx, st, 'host', ctx.hp, frm, H)


def ls_tick_client(ctx, rec, port):
    """A joined player's battle screen that is not live (a battle against an AI nation, A's battle after it dropped B):
    fought on auto-run."""
    who, pid = who_of(port), pid_of(ctx, port)
    if not pid:
        return
    st = rec['autorun'].setdefault(who, {})
    frm = next((h for h in top_windows(pid) if e2e_test.class_name(h) == 'TfrmMain'), None)
    if frm is None:
        if st.get('since'):
            rec['autorun'][who] = {}
        return
    st.setdefault('since', time.time())
    ls = lss(port)
    if ls.get('on') == '1' or ls.get('leave') == '1' or time.time() - st['since'] < 6:
        return
    autorun_click(ctx, st, who, pid, frm, port)


def ls_drive_live(ctx, rec, A, B, mode):
    """Drive one live battle of a month (blocking; the answerer thread keeps answering every copy's dialogs). Every
    division goes to the AI (contact, damage), a few minutes run from both sides, B orders one of its divisions (AI off)
    and the order must show equally on both copies. Then by mode: 'drop<N>' = N live minutes, A drops B (A fights on
    alone on auto-run, B goes back to its slot); 'agree' / 'game' = ls_drive_end; 'killB' = the client's game is killed
    (taskkill /F) during a minute, the host must drop it, the client is started again (rec['relaunch_env']) and joins
    at once."""
    ans = ls_ans(ctx)
    a, b = lss(A), lss(B)
    r = dict(A=A, B=B, bid=a.get('bid'), epoch=a.get('epoch'), mode=mode, t=time.time(), a0=a, b0=b, sA=lsn(A),
             sB=lsn(B), pA=pst(A), pB=pst(B), hostOpp=lsn(H).get('hostOpp') if A == H else None)
    log(f'  live battle {r["bid"]}: A={who_of(A)} B={who_of(B)}, mode {mode}; A {r["sA"]}; B {r["sB"]}')
    if A == H:
        r['s_battle'] = read_save(save_path(HOST_SLOT))  # the campaign as the battle started (the live battle's save)
    ma, mb = rec['marks'][A], rec['marks'][B]
    try:
        r['ai'] = ls_all_ai(A, B)
        scmd(A, 'ls speed 3')
        r['run'] = ls_run_total(A, B, 2, A)
        d1 = ls_pick_div(B, 1)
        if d1:
            want = (d1['course'] + 90) % 360
            scmd(B, f'ls order {d1["i"]} {want} -1 0')
            r['run'] += ls_run_total(A, B, 2, B)
            da, db = {d['i']: d for d in ls_divs(A)}, {d['i']: d for d in ls_divs(B)}
            r['order'] = dict(div=d1['i'], want=want, A=da.get(d1['i'], {}).get('course'),
                              B=db.get(d1['i'], {}).get('course'))
        if mode.startswith('drop'):
            n = int(mode[4:] or 10)
            r['run'] += ls_run_total(A, B, max(1, n - r['run']), A)
            r['hash'] = ls_compare(A, ma, B, mb, f'battle {r["bid"]}')
            mk = log_mark(A)
            scmd(A, 'ls drop')
            ls_wait(lambda: ls_over(A), 60, 'A drops B')
            pidB = pid_of(ctx, B)
            slotB, natB = (CLIENT_SLOT, 1) if B == C else (K2_SLOT, STANDIN)
            t = time.time()
            ls_wait(lambda: ls_back(B, pidB, slotB, natB), 120, 'B back in its slot after the drop')
            r['back_s'] = round(time.time() - t, 1)
            r['drop'] = ls_lines(A, mk, r'dropping|left the live battle')
        elif mode in ('agree', 'game'):
            r.update(ls_drive_end(A, B, mode))
            r['t_end'] = time.time()
            r['hash'] = ls_compare(A, ma, B, mb, f'battle {r["bid"]}')
            ans.view_results.update((who_of(A), who_of(B)))
        elif mode == 'endsoon':
            r.update(ls_drive_endsoon(A, B))
            r['hash'] = ls_compare(A, ma, B, mb, f'battle {r["bid"]}')
            ans.view_results.update((who_of(A), who_of(B)))
        elif mode == 'killB':
            scmd(A, 'ls speed 0')
            ls_run(A, B, 1, A, timeout=180)
            r['hash'] = ls_compare(A, ma, B, mb, f'battle {r["bid"]}')
            scmd(A, 'ls run 5')
            ls_wait(lambda: lss(B).get('running') == '1', 60, 'B in the middle of a minute')
            r['kill_minute'] = lss(A).get('minute')
            mk, t = log_mark(H), time.time()
            ls_kill(ctx, 'client')
            rec['relaunched'] = True
            ls_wait(lambda: ls_lines(H, mk, r'left the live battle') and ls_over(H), 90,
                    'the host drops the killed client')
            r['drop_s'] = round(time.time() - t, 1)
            r['drop'] = ls_lines(H, mk, r'dropping Captain|left the live battle|live battle ended')
            ls_relaunch(ctx, 'client', rec.get('relaunch_env'), tag='kill')
            scmd(C, 'fightbattles 1')
            r['rejoin_mark'] = log_mark(H)
            r['join'] = cmd(C, f'join {CLIENT_NAME} 127.0.0.1 {MP_PORT}')
            time.sleep(15)
            r['rejoin_during'] = sstatus(C)
            log(f'  the client joined again while the host\'s battle runs: myNation '
                f'{r["rejoin_during"].get("myNation")}, phase {r["rejoin_during"].get("phase")}')
    except Exception as e:  # noqa: BLE001
        r['error'] = repr(e)
        log('  !! live battle', r['bid'], repr(e))
        if lss(A).get('on') == '1':
            log('  A drops B so that the month can go on:', scmd(A, 'ls drop'))
    r['linesA'] = ls_lines(A, ma, r'lockstep: (battle \d+ epoch \d+ (sent|running)|The battle is over|The live battle|.*left '
                                  r'the live battle|dropping|DESYNC|Synchronising)|live battle ')
    r['linesB'] = ls_lines(B, mb, r'lockstep: (battle loaded|battle \d+ epoch \d+ running|The battle is over|The live '
                                  r'battle|DESYNC)|live battle') if not (mode == 'killB' and B == C) else []
    r['desync'] = ls_lines(A, ma, r'DESYNC')
    return r


def rejoin_back(ctx, hmark, during):
    """How a player who joined again while the host's battle screen was open got its nation back (0.3.0): at once by
    the hello (HELLO matching during a session: `myNation` 1 while the battle is still open, chat "* Captain joined as
    <nation>") or by the host's Tick (`RematchReservations`: `reservation: Captain gets nation 1 back`, chat "* Captain is
    back as <nation>"). Reads the client's chat (opens its Multiplayer window). Returns dict(ok, path, ...)."""
    n1 = nation1()
    res = ls_lines(H, hmark, rf'reservation: {CLIENT_NAME} gets nation 1 back')
    cmd(C, 'mpwindow')
    time.sleep(1.5)
    lines = [x.strip() for x in chat(ctx.cp).splitlines()
             if re.search(rf'\* {CLIENT_NAME} (joined|is back as)', x)]
    hello = during.get('myNation') == '1'
    path = 'hello' if hello else 'reservation' if res else None
    want = f'* {CLIENT_NAME} joined as {n1}' if hello else f'* {CLIENT_NAME} is back as {n1}'
    return dict(ok=path is not None and any(want in x for x in lines), path=path, during=during.get('myNation'),
                reservation=res[:1], chat=lines[-2:], want=want)


def ls_claim_if_needed(ctx, rec, d0file):
    """A client that was started again during the host's month: if the host gave it no nation by the time its month is
    over (its save has the new date and its strategic screen is back), it claims nation 1 (recorded)."""
    st = sstatus(C)
    if st.get('myNation') == '1' or st.get('role') != 'client':
        return
    done = save_date(HOST_SLOT) not in (None, d0file) and not battle_up(ctx.hp) and strategic_enabled(ctx.hp)
    if not done:
        return
    rec.setdefault('host_done_t', time.time())
    if time.time() - rec['host_done_t'] > 20:
        rec['claimed'] = dict(st, lobby=cmd(C, 'diploview')[:200])
        log(f'  the host\'s month is over but the returning client has no nation ({st.get("myNation")}): claim '
            f'{nation1()} ->', cmd(C, f'claim {nation1()}'))


class StageWatcher(threading.Thread):
    """Reads the saves of a month's idea-1 battle phase the moment the logs announce them, from a thread (the next step
    overwrites them within ~0.3 s): the host's save at `battle phase:` (s0: the month after DoEndOfTurn, with its live
    battle), the merged save after each `battle N applied` (s1[N]) and the client's battle slot after each `battle N:
    fought` (g87[N], the battle as the game left it). Raw bytes; `stages()` parses them. Also the order of the steps."""

    def __init__(self, hmark, cmark):
        super().__init__(daemon=True)
        self.pos = {H: hmark, C: cmark}
        self.raw = dict(s0=None, s1={}, g87={})
        self.steps = []  # (time, 'phase' | 'applied N' | 'resolved N <how>' | 'fought N' | 'g87 N missed')
        self.stop = False

    def tail(self, port):
        try:
            if os.path.getsize(log_path(port)) < self.pos[port]:  # the copy was started again: a new log
                self.pos[port] = 0
            with open(log_path(port), 'rb') as f:
                f.seek(self.pos[port])
                data = f.read()
        except OSError:
            return []
        cut = data.rfind(b'\n') + 1
        self.pos[port] += cut
        return [ln for ln in data[:cut].decode('utf-8', errors='replace').splitlines() if 'lockstep hash' not in ln]

    @staticmethod
    def grab(slot):
        for _ in range(5):
            try:
                with open(save_path(slot), 'rb') as f:
                    return f.read()
            except OSError:
                time.sleep(0.03)
        return None

    def run(self):
        while not self.stop:
            try:
                for ln in self.tail(H):
                    if self.raw['s0'] is None and 'battle phase: ' in ln:
                        self.raw['s0'] = self.grab(HOST_SLOT)
                        self.steps.append((time.time(), 'phase'))
                    m = re.search(r'\] battle (\d+) applied: ', ln)
                    if m and int(m[1]) not in self.raw['s1']:
                        self.raw['s1'][int(m[1])] = self.grab(HOST_SLOT)
                        self.steps.append((time.time(), f'applied {m[1]}'))
                    m = re.search(r'\] battle (\d+) resolved by the game: (.*)$', ln)
                    if m:
                        self.steps.append((time.time(), f'resolved {m[1]} {m[2]}'))
                for ln in self.tail(C):
                    m = re.search(r'\] battle (\d+): fought', ln)
                    if m and int(m[1]) not in self.raw['g87']:
                        self.raw['g87'][int(m[1])] = self.grab(BATTLE_SLOT)
                        self.steps.append((time.time(), f'fought {m[1]}'))
            except Exception as e:  # noqa: BLE001
                self.steps.append((time.time(), f'watcher error {e!r}'))
            time.sleep(0.1)

    def stages(self):
        """dict(s0, s1={N: save}, g87={N: save}, steps) with the saves parsed."""
        return dict(s0=parse_save(self.raw['s0']), s1={b: parse_save(r) for b, r in self.raw['s1'].items()},
                    g87={b: parse_save(r) for b, r in self.raw['g87'].items()}, steps=[s for _, s in self.steps])


def ls_month(ctx, label, modes=('agree',), k2=False, relaunch_env=None):
    """One processed month that may hold live battles (the host's own battle against nation 1 found by the game's
    CheckForBattles2, live battles between joined players, joined players' battles against AI nations). The answerer
    thread answers every copy's dialogs; each live battle that starts is driven by the next of `modes` (ls_drive_live);
    a battle screen that is not live is fought on auto-run. The host's bridge is polled only while its live battle
    runs. Ends when the client (and K2) plan the new month; then the event roll and month_checks (the roll check is
    skipped when the client was started again). Returns a record."""
    ans = ls_quiet(ctx)
    for hook in list(ctx.pre_month_hooks):
        hook(ctx, label, len(ctx.months) + 1)
    date0, d0file = sstatus(C).get('date'), save_date(HOST_SLOT)
    ports = [H, C] + ([K2] if k2 else [])
    rec = dict(label=label, date0=date0, live=[], marks={p: log_mark(p) for p in ports}, relaunched=False,
               autorun={}, relaunch_env=relaunch_env, claimed=None, chat0=chat(ctx.cp), t0=time.time())
    rec['hmark'], rec['cmark'] = rec['marks'][H], rec['marks'][C]
    ctx.month_marks = (rec['hmark'], rec['cmark'])
    watcher = StageWatcher(rec['hmark'], rec['cmark'])
    watcher.start()
    cmd(C, 'submit')
    if k2:
        cmd(K2, 'submit')
    wait(C, lambda s: s.get('submitted') == '1', 30, 'client submitted', pids=(ctx.hp, ctx.cp))
    if k2:
        wait(K2, lambda s: s.get('submitted') == '1', 30, 'K2 submitted', pids=(ctx.hp,))
    wait(H, lambda s: 'All players are ready' in s.get('status', ''), 30, 'host sees everyone ready',
         pids=(ctx.hp, ctx.cp))
    ans.paused = False
    t_turn = time.time()
    cmd(H, 'turn')
    modes, st = list(modes), {}
    end = time.time() + MONTH_TIMEOUT + LS_MONTH_EXTRA
    try:
        while True:
            if time.time() > end:
                raise RuntimeError(f'the month did not advance within {MONTH_TIMEOUT + LS_MONTH_EXTRA} s ({label}); host '
                                   f'windows: {describe_windows(ctx.hp)}; client windows: {describe_windows(ctx.cp)}')
            assert_alive()
            answer_host_box(ctx.hp)
            live = ls_detect(k2)
            if live:
                mode = modes.pop(0) if len(modes) > 1 else modes[0]
                rec['live'].append(ls_drive_live(ctx, rec, live[0], live[1], mode))
                continue
            ls_tick_host(ctx, rec)
            for p in ports[1:]:
                ls_tick_client(ctx, rec, p)
            if not any(battle_up(pid_of(ctx, p)) for p in ports if pid_of(ctx, p)):
                ans.view_results.clear()
            if rec['relaunched'] and rec['claimed'] is None:
                ls_claim_if_needed(ctx, rec, d0file)
            st = sstatus(C)
            done = st.get('phase') == 'planning' and st.get('date') not in (None, '', date0) \
                and st.get('slot') == str(CLIENT_SLOT) and st.get('playerIdx') == '1' and st.get('myNation') == '1'
            if done and k2:
                s2 = sstatus(K2)
                done = s2.get('phase') == 'planning' and s2.get('date') == st.get('date') and s2.get('slot') == str(K2_SLOT)
            if done:
                break
            time.sleep(1.0)
    finally:
        watcher.stop = True
    time.sleep(2)
    watcher.join(5)
    rec['stages'] = watcher.stages()
    ls_quiet(ctx)
    dt = time.time() - rec['t0']
    ctx.months.append((label, date0, st.get('date'), dt))
    log(f'month processed ({label}): {date0} -> {st.get("date")} in {dt:.0f} s; live battles '
        f'{[(b["bid"], b["mode"], b.get("error")) for b in rec["live"]]}')
    check(f'{label}: the month advanced by exactly one month ({date0} -> {st.get("date")})',
          st.get('date') == month_after(date0), log_lines(log_since(H, rec['hmark']), r'DoEndOfTurn finished'))
    wait_event_roll(ctx, 0 if rec['relaunched'] else rec['cmark'])
    month_checks(ctx, label, rec['hmark'], None if rec['relaunched'] else rec['cmark'])
    for hook in list(ctx.month_hooks):
        hook(ctx, label)
    rec.update(date1=st.get('date'), seconds=dt, answers=[(t, w, e) for t, w, e in ans.seen if t >= t_turn])
    return rec


def ls_host_battle(rec):
    """The host's battle of the month against nation 1 as its log shows it: 'live' (held and started), 'ai' (fought
    against the AI: the player's opt-out), 'none'."""
    hl = log_since(H, rec['hmark'])
    if re.search(r'battle against Captain: fought against the AI', hl):
        return 'ai'
    if LS_HOLD in hl:
        return 'live'
    return 'none'


def ls_case(ctx, name, fn):
    """Run one case of a lockstep scenario; an exception fails it (one check) and the scenario recovers (A drops B, the
    host's battle finished outside a month, the client back in the session) instead of stopping. Records the case for
    the stress matrix."""
    log(f'--- {name} ---')
    n0, t0, note = len(results), time.time(), ''
    try:
        note = fn() or ''
    except Exception as e:  # noqa: BLE001
        check(f'{name}: completed', False, repr(e))
        note = repr(e)[:160]
        for pid, who in ((ctx.hp, 'host'), (ctx.cp, 'client')):
            log(f'  {who} windows:', describe_windows(pid))
        for port in (H, C):
            log(f'  ls {who_of(port)}:', scmd(port, 'ls status'))
            log(f'  log {who_of(port)} tail:\n    ' + '\n    '.join(log_since(port).splitlines()[-10:]))
        ls_recover(ctx)
    rs = results[n0:]
    LS_MATRIX.append((name, sum(1 for r in rs if r[2]), len(rs), round(time.time() - t0), str(note)))


def ls_recover(ctx):
    """After a failed case outside a month: no live battle, the host's battle finished, the client in the session."""
    try:
        if lss(H).get('on') == '1':
            log('  recover: A drops B', scmd(H, 'ls drop'))
            time.sleep(3)
        if lss(C).get('on') == '1':
            log('  recover: B leaves', scmd(C, 'ls abort'))
        ls_wait(lambda: ls_client_back(ctx) or not sstatus(C), 120, 'the client back after the failure')
        if battle_up(ctx.hp):
            ls_finish_host(ctx)
        ls_ensure_session(ctx)
    except Exception as e:  # noqa: BLE001
        log('  !! recovery failed:', repr(e))
        raise


def ls_start_checks(tag, s):
    """b) a test battle started live: preview accepted, held, saved and sent by the host, loaded by the client from its
    battle slot; roles, sides, ids; the host's battle in battle slot mode (outside a month)."""
    ans = _ANS[0]
    a, b = s['a'], s['b']
    prev = ls_answered(ans, 'host', s['t0'], 'TdlgBattlePreview')
    held = ls_lines(H, s['hm'], re.escape(LS_HOLD))
    sent = ls_lines(H, s['hm'], rf'lockstep: battle {a.get("bid")} epoch 1 sent \(\d+ files, \d+ bytes')
    loaded = ls_lines(C, s['cm'], rf'lockstep: battle loaded from slot {BATTLE_SLOT} ')
    check(f'{tag}: the host\'s preview accepted, the battle held at its start, saved and sent ("battle <bid> epoch 1 sent '
          f'(n files, b bytes ...)"), loaded by the client from its battle slot {BATTLE_SLOT}; both run it within 150 s '
          f'(start {s["dt"]:.1f} s)', prev and held and sent and loaded, f'{prev[:1]} {held[:1]} {sent[:1]} {loaded[:1]}')
    pa, pb, hn, cn = pst(H), pst(C), lsn(H), lsn(C)
    check(f'{tag}: roles host A side 0 / client B side 1, same battle id and epoch; pocstate playerSide 0 / 1; host '
          f'`ls session` hostOpp=1 testSlotMode=1 slotMode=1; client role=B slotMode=1',
          a.get('role') == 'A' and a.get('side') == '0' and b.get('role') == 'B' and b.get('side') == '1'
          and a.get('bid') == b.get('bid') and a.get('epoch') == b.get('epoch') and pa.get('playerSide') == '0'
          and pb.get('playerSide') == '1' and hn.get('hostOpp') == '1' and hn.get('testSlotMode') == '1'
          and hn.get('slotMode') == '1' and cn.get('role') == 'B' and cn.get('slotMode') == '1',
          f'A {a.get("role")}/{a.get("side")}/{a.get("bid")}/{a.get("epoch")} B {b.get("role")}/{b.get("side")}/'
          f'{b.get("bid")}/{b.get("epoch")}; playerSide {pa.get("playerSide")}/{pb.get("playerSide")}; host {hn}; '
          f'client {cn}')


def ls_orders_check(tag, A=H, B=C):
    """Orders of both sides given at the same paused boundary are released with the next minute and applied
    identically on both copies."""
    ma, mb = log_mark(A), log_mark(B)
    d1, d0 = ls_pick_div(B, 1), ls_pick_div(A, 0)
    if not (d0 and d1):
        return check(f'{tag}: a division on each side for the orders', False, f'{d0} {d1}')
    w1, w0 = (d1['course'] + 90) % 360, (d0['course'] + 45) % 360
    r1 = scmd(B, f'ls order {d1["i"]} {w1} {max(d1["speed"] - 2, 4)}')
    r0 = scmd(A, f'ls order {d0["i"]} {w0} {max(d0["speed"] - 1, 4)}')
    time.sleep(1.0)
    ls_run(A, B, 1, A)
    da, db = {d['i']: d for d in ls_divs(A)}, {d['i']: d for d in ls_divs(B)}
    got = dict(B_on_A=da.get(d1['i'], {}).get('course'), B_on_B=db.get(d1['i'], {}).get('course'),
               A_on_A=da.get(d0['i'], {}).get('course'), A_on_B=db.get(d0['i'], {}).get('course'), want_B=w1, want_A=w0)
    rb, ra = ls_lines(A, ma, r'orders #\d+ of .* received'), ls_lines(B, mb, r'orders #\d+ of .* received')
    return check(f'{tag}: orders of both sides at the same paused boundary (B: division {d1["i"]} course {w1}, A: '
                 f'division {d0["i"]} course {w0}) applied identically on both copies with the next minute; each copy '
                 f'received the other\'s order batch', r0 == r1 == 'ok' and got['B_on_A'] == got['B_on_B'] == w1
                 and got['A_on_A'] == got['A_on_B'] == w0 and rb and ra, f'{r0} {r1} {got} recv A {rb[:1]} B {ra[:1]}')


def ls_desync_check(tag, port, A=H, B=C):
    """An injected desync (`ls desync`: this copy's next minute differs) is caught at the next boundary and the battle
    is synchronised again (a new epoch); the minutes after it are equal."""
    mk, e0 = log_mark(A), int(lss(A).get('epoch') or 0)
    scmd(port, 'ls desync')
    ls_run(A, B, 1, A)
    ls_wait(lambda: int(lss(A).get('epoch') or 0) > e0 and ls_idle_equal(A, B), 120, f'resync after the {tag} desync')
    ma, mb = log_mark(A), log_mark(B)
    ls_run_total(A, B, 2, B)
    after = ls_compare(A, ma, B, mb, f'{tag} after the resync')
    des, syn = ls_lines(A, mk, r'DESYNC at minute'), ls_lines(A, mk, r'Synchronising the battle again')
    return check(f'{tag}: an injected desync on the {who_of(port)} is caught ("DESYNC at minute m"), the battle is '
                 f'synchronised again (epoch {e0} -> {lss(A).get("epoch")}), the next 2 minutes are equal',
                 des and syn and after['minutes'] >= 2 and not after['differ'], f'{des[:1]} {syn[:1]} {after}')


def ls_detach_check(tag, A=H, B=C):
    """B detaches a ship of one of its divisions between minutes (`ls detach`, as the battle screen's Detach): the
    action runs at once on B and is replayed on A with the next release; both copies then have one division more and
    the next minutes are equal."""
    d1 = ls_pick_div(B, 1, 2)
    if not d1:
        return check(f'{tag}: a division of the client\'s side with 2 ships to detach from', False, ls_divs(B)[:6])
    ma, mb = log_mark(A), log_mark(B)
    n0 = (len(ls_divs(A)), len(ls_divs(B)))
    r = scmd(B, f'ls detach {d1["i"]} 1')
    time.sleep(1.5)
    ls_run_total(A, B, 2, A)
    n1 = (len(ls_divs(A)), len(ls_divs(B)))
    hc = ls_compare(A, ma, B, mb, 'after the detach')
    des = ls_lines(A, ma, r'DESYNC|Synchronising the battle again')
    return check(f'{tag}: the client detaches a ship of its division {d1["i"]} ({d1["name"]}) between minutes (`ls '
                 f'detach`): replayed on the host with the next minute, one division more on both copies ({n0} -> {n1}), '
                 f'the next 2 minutes equal, no DESYNC or resync', r.startswith('ok') and n0[0] == n0[1]
                 and n1[0] == n1[1] and n1[0] >= n0[0] + 1 and hc['minutes'] >= 2 and not hc['differ'] and not des,
                 f'{r}; {n0} -> {n1}; {hc}; {des[:2]}')


def ls_guard_check(tag, A=H, B=C):
    """The display's writes of the ships' sighting fields stay out of the simulation (the round-3 fix): `ls dispwrite`
    (every ship's sighting fields changed outside the simulation, as the battle map's GlobeRender does) on B, then on
    A, each followed by 2 minutes: the guard counted them ("the display wrote N sighting fields since the last boundary
    (kept out of the simulation)"), no LEAK, no DESYNC, the minutes equal."""
    res = {}
    for port in (B, A):
        ma, mb = log_mark(A), log_mark(B)
        r = scmd(port, 'ls dispwrite')
        ls_run_total(A, B, 2, A)
        mk = ma if port == A else mb
        res[who_of(port)] = dict(write=r, guard=ls_lines(port, mk, r'display wrote \d+ sighting')[:1],
                                 leak=ls_lines(port, mk, LS_LEAK_RE)[:2], desync=ls_lines(A, ma, r'DESYNC'),
                                 hash=ls_compare(A, ma, B, mb, f'after a display write on the {who_of(port)}'))
    ok = all(re.match(r'^ok [1-9]', v['write']) and v['guard'] and not v['leak'] and not v['desync']
             and v['hash']['minutes'] >= 2 and not v['hash']['differ'] for v in res.values())
    return check(f'{tag}: display writes of every ship\'s sighting fields (`ls dispwrite`, as GlobeRender) on the client, '
                 f'then on the host: kept out of the simulation ("the display wrote N sighting fields ... (kept out of the '
                 f'simulation)"), no LEAK, no DESYNC, the next 2 minutes equal each time', ok,
                 {w: dict(write=v['write'], guard=v['guard'], leak=v['leak'], desync=v['desync'][:1],
                          hash=(v['hash']['minutes'], v['hash']['differ'])) for w, v in res.items()})


def ls_drop_checks(ctx, tag, why='test'):
    """A drops B (`ls drop`): the AI commands side 1, the client goes back to slot 77 as nation 1, the host's battle
    goes on alone (2 more minutes)."""
    hm, cm, t = log_mark(H), log_mark(C), time.time()
    scmd(H, 'ls drop')
    st = ls_wait(lambda: ls_client_back(ctx), 120, 'the client back in slot 77 after the drop')
    dt = time.time() - t
    hl = ls_lines(H, hm, r'dropping Captain|left the live battle')
    cl = ls_lines(C, cm, r'The live battle ended on this PC|live battle: leaving the battle slot|'
                         rf'LoadCampaign slot {CLIENT_SLOT} player 1 -> ok')
    m0 = int(pst(H).get('min') or 0)
    scmd(H, 'pocrun 2')
    time.sleep(1)
    ps = ls_wait(lambda: (lambda p: p if int(p.get('min') or 0) >= m0 + 1 and p.get('running') == '0'
                          and p.get('moving') == '0' else None)(pst(H)), 120, 'the host runs 2 minutes alone')
    check(f'{tag}: the host drops the client: "dropping Captain: {why}", "Captain left the live battle ({why}). The AI '
          f'commands Germany now"; the client back in slot 77 as nation 1 ({dt:.1f} s: "The live battle ended on this '
          f'PC", "leaving the battle slot", LoadCampaign slot 77); the host\'s battle goes on alone (pocrun 2: minute '
          f'{m0} -> {ps.get("min")})', len(hl) >= 2 and len(cl) >= 3 and st and int(ps.get('min') or 0) >= m0 + 1,
          f'{hl} | {cl}')
    return dt


def ls_end_refused_check(tag, A=H, B=C):
    """Both players ask for the end before minute 400: the game's End routine refuses on both copies at the next
    boundary and the battle goes on."""
    ma, mb = log_mark(A), log_mark(B)
    ls_end_both(A, B)
    ls_wait(lambda: ls_lines(A, ma, r"can't end yet") and ls_lines(B, mb, r"can't end yet"), 90, 'the refused end')
    time.sleep(1.5)
    a, b = lss(A), lss(B)
    return check(f'{tag}: both players ask for the end before minute 400: the game refuses on both copies ("The battle '
                 f'can\'t end yet: ...") and the battle goes on (both running, end requests cleared)',
                 a.get('st') == b.get('st') == 'running' and a.get('end') == '0/0',
                 f'{ls_lines(A, ma, "can.t end yet")[:1]} | A st={a.get("st")} end={a.get("end")}, B st={b.get("st")}')


def scenario_lockstep(ctx):
    """Live player-vs-player battles (lockstep), the host against the joined player, two copies (short; from the research
    driver ls_live.py). a) both copies offer live battles, the host lists Captain(1,b,ls); the client changes its plans
    (ResearchPct, not submitted). b) the host's battle against nation 1 (`pocbattle 1 0`: the debug Test battle = the
    game's DoBattle, outside a month, so in battle slot mode) starts live: preview, held, saved, sent, loaded by the
    client in its battle slot 87 as side 1. c) runs asked by both sides, equal per-minute hashes. d) orders of both sides
    at one paused boundary applied identically; the client detaches a ship (an action replayed on the host); display
    writes of the sighting fields on each copy (`ls dispwrite`) stay out of the simulation. e) an injected desync is
    caught and resynchronised. f) an end asked by
    both before minute 400 is refused by the game on both, the battle goes on. g) the host drops the client: the AI
    commands side 1, the host's battle goes on, the client is back in slot 77 with its plans (canLoad saved them). h)
    the host's battle on auto-run to its end, closed without a month. i) after-checks. k) one normal month: the plans
    reached the host. l) no lockstep error in the logs, the hooks installed once, no LEAK (the leak check is on in every
    live battle of the suite)."""
    marks0 = {H: log_mark(H), C: log_mark(C)}
    ls_ensure_session(ctx)
    hs, cs = lsn(H), lsn(C)
    avail = hs.get('available') == '1' and cs.get('available') == '1'
    check('a) both copies offer live battles: `ls session` enabled=1 available=1 on host and client; the host lists '
          'Captain(1,b,ls) (fights battles, plays them live)', avail and hs.get('enabled') == cs.get('enabled') == '1'
          and 'Captain(1,b,ls)' in hs.get('players', ''), f'host {hs}; client {cs}')
    if not avail:
        check('a) live battles available on both copies: the scenario stops here', False, f'{hs} {cs}')
        return
    pct0 = nint(C, 1, 'ResearchPct')
    plan = 9 if pct0 != 9 else 8
    cmd(C, f'nfield 1 ResearchPct {plan}')
    date0 = sstatus(H).get('date')
    log(f'  a) the client\'s plans (not submitted): nation 1 ResearchPct {pct0} -> {plan}')

    def battle():
        s = ls_start_test(ctx, 'lockstep')
        ls_start_checks('b)', s)
        scmd(H, 'ls speed 3')
        ra, rb = ls_run_total(H, C, 3, H), ls_run_total(H, C, 2, C)
        hc = ls_compare(H, s['hm'], C, s['cm'], 'runs')
        check(f'c) runs asked by the host ({ra} minutes) and by the client ({rb}) at speed 3: every minute\'s state hash '
              f'is equal on both copies (>= 5 minutes), no DESYNC', hc['minutes'] >= 5 and not hc['differ']
              and not ls_lines(H, s['hm'], 'DESYNC'), hc)
        ls_orders_check('d)')
        ls_detach_check('d) detach')
        ls_guard_check('d) guard')
        ls_desync_check('e)', C)
        ls_end_refused_check('f)')
        ls_drop_checks(ctx, 'g)')
        v = nint(C, 1, 'ResearchPct')
        check(f'g) the client\'s plans survived its live battle: nation 1 ResearchPct {v} (planned {plan}; canLoad saved '
              f'slot 77 before the battle was loaded)', v == plan, v)
        closed = ls_finish_host(ctx)
        hn = ls_wait(lambda: (lambda n: n if n.get('slotMode') == '0' else None)(lsn(H)), 30, 'slot mode off')
        hst, hl = sstatus(H), log_since(H, s['hm'])
        check('h) the host\'s battle (alone) fought to its end on auto-run and closed without a month ("battle slot: '
              'DoStrategicTurn skipped", the date unchanged); battle slot mode off afterwards',
              closed and hn.get('slotMode') == '0' and hst.get('date') == date0
              and 'battle slot: DoStrategicTurn skipped' in hl, f'{hn} date {date0} -> {hst.get("date")}')

    ls_case(ctx, 'lockstep: a live test battle', battle)
    ls_quiet(ctx)
    ls_after_checks(ctx, 'i)')
    run_month(ctx, 'lockstep')
    hv, cv = nint(H, 1, 'ResearchPct'), nint(C, 1, 'ResearchPct')
    check(f'k) one normal month afterwards: the client\'s plans reached the host (nation 1 ResearchPct host {hv}, client '
          f'{cv}, planned {plan})', hv == cv == plan, f'{hv} {cv}')
    ls_log_scan('l)', marks0)


def pvp_lines(hlog, bid):
    return log_lines(hlog, rf'battle (request )?{bid}[ :]|relay {bid} ')


def scenario_lockstep_pvp(ctx):
    """Live battles between two joined players (three copies; from the research driver ls_pvp.py). a) a third copy
    joins as "Bot" and gets the reserved nation 2. b) war 1-2 (`settension 1 2 50`), `battles injectpvp 1 2 Northern
    Europe`, processed months (up to 3: the attacker's CheckAttackLocation may find no battle) until a live battle is
    fought: the battle phase sends it to the attacker's copy (A, its battle slot) and the defender's copy commands the
    other side (B, its battle slot), the host relays; runs and an order of B with equal hashes; default: 10 live
    minutes, then A drops B and fights on alone on auto-run; --stress: to the agreed end (>= 400 minutes) and an
    idea-1 battle of nation 1 against an AI nation injected into the same month (the host runs them one after the
    other). c) the result merged, the new month on all three copies, ship counts equal. d) peace, the third copy
    leaves (nation 2 stays reserved for Bot) and is closed."""
    marks0 = {H: log_mark(H), C: log_mark(C)}
    stress = OPTS['stress']
    ls_ensure_session(ctx)
    r, sent, st = k2_start(ctx)
    marks0[K2] = 0
    hs = lsn(H)
    check(f'a) a third copy joins as "{K2_NAME}" (bridge {K2}, slot {K2_SLOT}): the host gives it the reserved nation '
          f'{STANDIN} and sends the month; it plans nation {STANDIN} in slot {K2_SLOT}; the host lists Captain(1,b,ls) '
          f'and {K2_NAME}({STANDIN},b,ls)', r == 'ok' and sent and st.get('playerIdx') == str(STANDIN)
          and 'Captain(1,b,ls)' in hs.get('players', '') and f'{K2_NAME}({STANDIN},b,ls)' in hs.get('players', ''),
          f'{r} {sent[:1]} {st.get("slot")}/{st.get("playerIdx")} {hs.get("players")}')
    t12 = rel(1, 2).get('tension', '5')
    cmd(H, 'settension 1 2 50')
    r12 = rel(1, 2)
    check('setup: nations 1 and 2 (both joined players) at war (`settension 1 2 50`)', r12.get('war') == '1', r12)
    J = area = tJ = None
    if stress:
        cands = battle_pairs(1)
        if cands:
            _, _, J, area = cands[0]
            tJ = rel(1, J).get('tension', '5')
            cmd(H, f'settension 1 {J} 50')
            log(f'  interplay: nation 1 at war with AI nation {J} too; an idea-1 battle in {area} each month')
    mode = 'agree' if stress else 'drop10'
    done, attempts = None, []
    for attempt in range(1, 4):
        hm = log_mark(H)
        if rel(1, 2).get('war') != '1':
            cmd(H, 'settension 1 2 50')
        rp = cmd(H, f'battles injectpvp 1 2 {PVP_AREA}')
        pbid = int(rp.split()[1]) if re.match(r'^ok \d+$', rp) else None
        ibid = None
        if J is not None:
            if rel(1, J).get('war') != '1':
                cmd(H, f'settension 1 {J} 50')
            ibid, _ = inject(1, J, area)
        rec = ls_month(ctx, f'lockstep_pvp M{attempt}', modes=[mode], k2=True)
        hlog = log_since(H, hm)
        lives = [b for b in rec['live'] if b['A'] in (C, K2)]
        applied = {int(x) for x in re.findall(r'battle (\d+) applied: ', hlog)}
        attempts.append((attempt, rp, [(b['bid'], b['mode'], b.get('error')) for b in lives], sorted(applied),
                         log_lines(hlog, r'battle \d+: resolved by the game')[:3]))
        if lives:
            done = (rec, lives, hm, pbid, ibid)
            break
    check(f'b) a live battle between nations 1 and 2 fought within 3 months (`battles injectpvp 1 2 {PVP_AREA}`; '
          f'attempts: month, inject, live battles, merged ids, resolved by the game)', done, attempts)
    if done:
        pvp_checks(ctx, stress, *done)
    cmd(H, f'settension 1 2 {t12}')
    if J is not None:
        cmd(H, f'settension 1 {J} {tJ}')
    k2_stop(ctx)
    hs, r2 = lsn(H), row(H, STANDIN)
    check(f'd) peace 1-2 again; the third copy left and is closed: the host lists only Captain, nation {STANDIN} stays '
          f'reserved for {K2_NAME}', rel(1, 2).get('war') == '0' and f'{K2_NAME}(' not in hs.get('players', '')
          and r2 and r2['player'] == K2_NAME, f'{rel(1, 2)} {hs.get("players")} {r2}')
    ls_after_checks(ctx, 'd)')
    ls_log_scan('e)', marks0)


def pvp_checks(ctx, stress, rec, lives, hm, pbid, ibid):
    b = lives[0]
    A, B = b['A'], b['B']
    hlog = log_since(H, hm)
    req = log_lines(hlog, r'battle request \d+: nation \d vs \d in .* fought live by \w+ and \w+')
    sentl = log_lines(hlog, r'battle \d+ sent to \w+ \(\d+ bytes\), live against \w+')
    relay = log_lines(hlog, r'lockstep relay \d+ ended')
    applied = log_lines(hlog, APPLIED_RE)
    check('b) host log: the request "battle request N: nation a vs b in <area> (x:y ships), fought live by <p1> and <p2>", '
          '"battle N sent to <p> (B bytes), live against <q>", the relay ended with the result, "battle N applied: X '
          'ships changed (F fields), R removed, VP a/b ..."', req and sentl and relay and applied,
          f'{req[:2]} {sentl[:2]} {relay[:2]} {applied[:2]}')
    la, lb = ls_lines(A, rec['marks'][A], r'StartPlayerBattle|lockstep: battle \d+ epoch 1 sent|battle \d+: (fought|'
                                                                   r'fighting)'), \
        ls_lines(B, rec['marks'][B], r'lockstep: battle loaded from slot (8[67])|live battle: leaving the battle slot')
    check(f'b) the attacker\'s copy ({who_of(A)}) fought it in its battle slot as A (StartPlayerBattle, "battle .. epoch 1 '
          f'sent", "battle N: fighting ..."), the defender\'s copy ({who_of(B)}) loaded it from its battle slot as B and '
          f'left the battle slot afterwards', la and lb and any('epoch 1 sent' in x for x in la)
          and any('battle loaded from slot' in x for x in lb) and any('leaving the battle slot' in x for x in lb),
          f'A {la[:4]} | B {lb[:3]}')
    h = b.get('hash') or {}
    need = 400 if stress else 8
    check(f'b) live: every minute\'s state hash equal on both players\' copies (>= {need} minutes compared), no DESYNC',
          h.get('minutes', 0) >= need and not h.get('differ') and not b.get('desync') and not b.get('error'),
          f'{h} {b.get("desync")} {b.get("error")}')
    o = b.get('order') or {}
    check('b) an order of B (one of its divisions, course +90, AI off) shows the same course on both copies',
          o and o.get('A') == o.get('B') == o.get('want'), o)
    if stress:
        endl = ls_lines(A, rec['marks'][A], r'The battle is over \(ended by agreement')
        endb = ls_lines(B, rec['marks'][B], r'The battle is over \(ended by agreement')
        check('b) the battle ended by agreement on both copies (>= 400 minutes; the game refuses earlier)',
              endl and endb and b.get('minute', 0) >= 400, f'minute {b.get("minute")} {endl[:1]} {endb[:1]}')
    else:
        fought = ls_lines(A, rec['marks'][A], r'battle \d+: fought')
        check('b) A dropped B after 10 live minutes ("dropping <B>: test"), B went back to its own slot, A fought on '
              'alone on auto-run and sent the result ("battle N: fought")', b.get('drop') and b.get('back_s') is not None
              and fought, f'{b.get("drop")} back {b.get("back_s")} s {fought[:1]}')
    s1, s2, sh = sstatus(C), sstatus(K2), sstatus(H)
    nh, n1, n2 = nation_table(H), nation_table(C), nation_table(K2)
    ships = {n: (nh.get(n, {}).get('ships'), n1.get(n, {}).get('ships'), n2.get(n, {}).get('ships')) for n in (1, 2)}
    check('c) the new month on all three copies (host, Captain in slot 77 as nation 1, Bot in slot 76 as nation 2); the '
          'ship counts of nations 1 and 2 equal on the three copies',
          s1.get('date') == s2.get('date') == sh.get('date') == rec['date1'] and s1.get('slot') == str(CLIENT_SLOT)
          and s2.get('slot') == str(K2_SLOT) and s2.get('playerIdx') == str(STANDIN)
          and all(len(set(v)) == 1 and v[0] is not None for v in ships.values()),
          f'dates {sh.get("date")}/{s1.get("date")}/{s2.get("date")}; ships {ships}')
    if stress and ibid is not None:
        def t_of(lines):
            return log_secs(lines[0]) if lines else None
        p_sent = t_of(sentl)
        t_relay = t_of(relay)
        isent = log_lines(hlog, rf'battle {ibid} sent to Captain')
        idone = log_lines(hlog, rf'battle {ibid}( applied|: resolved by the game)')
        t_isent, t_idone = t_of(isent), t_of(idone)
        apart = (t_relay is not None and t_isent is not None and t_isent >= t_relay) or \
            (t_idone is not None and p_sent is not None and p_sent >= t_idone)
        check(f'h) interplay: the idea-1 battle {ibid} (nation 1 vs an AI nation) of the same month and the live battle '
              f'ran one after the other for Captain (one battle per player at a time, in the host\'s order) and both were '
              f'settled (the idea-1 battle merged or resolved by the game)', idone and apart,
              f'live sent {sentl[:1]} relay {relay[:1]} | idea-1 sent {isent[:1]} done {idone[:1]}')


def ls_war01(ctx):
    """The host at war with nation 1 (host `diplo war 1` + applydiplo). Returns rel 0 1."""
    if rel(0, 1).get('war') != '1':
        mark = log_mark(H)
        cmd(H, 'diplo war 1')
        wait_log(H, mark, r'diplomacy request 0 war 1 accepted', 8)
        apply_diplo(ctx, lambda: rel(0, 1).get('war') == '1')
    return rel(0, 1)


def ls_peace01(ctx):
    """Peace between the host and nation 1 with an equal war score (a compromise: no treaty): the host offers, the client
    accepts with its "Accept peace" button, applydiplo. Returns rel 0 1."""
    if rel(0, 1).get('war') == '1':
        warvp(0, 1, 300, 300)
        cmd(H, 'diplo peace 1')
        bt, acc = client_accept_peace(ctx, 0)
        log(f'  peace 0-1: the client\'s button "{bt}", {acc[:1]}')
        apply_diplo(ctx, lambda: rel(0, 1).get('war') == '0')
    return rel(0, 1)


SHIP_BATTLE_FIELDS = ('RepairTime', 'BattleStars', 'NumberOfLogEntries', 'Status', 'Fate', 'CrewQuality')


def lost_ship(f0, f1):
    """A ship lost between two saves of it: its Fate is no longer 'XXX' (in service), e.g. "Sunk in battle off ..."."""
    a, z = f0.get('Fate') or 'XXX', f1.get('Fate') or 'XXX'
    return z != 'XXX' and z != a


def battle_effects(s_battle, s_post, nations=(0, 1), slot_nations=None):
    """What happened to the campaign ships of `nations` between two saves (e.g. the live battle's save at its start and
    the host's save at the battle phase start): the ships that took part (a battle star or a new "Took part"/action log
    entry), the ships lost (gone from the list, or their Fate no longer 'XXX', whether they took part or not: a loss
    outside a battle too), the [NationNLosses] counters of the nation's ship types that changed (`lost`; the others,
    e.g. MSLost and AUXLost, in `abstract`) and `counted` = the sum of the `lost` increases. `slot_nations` {n: section index in s_post}
    reads s_post in battle slot numbering (Game87: K is nation 0)."""
    out = {}
    for n in nations:
        m = (slot_nations or {}).get(n, n)
        b0, b1 = ships_by_id((s_battle or {}).get(f'Nation{n}Ships')), ships_by_id((s_post or {}).get(f'Nation{m}Ships'))
        part, gone, sunk, rep = [], [], [], {}
        for sid, f0 in b0.items():
            f1 = b1.get(sid)
            if f1 is None:
                gone.append((sid, f0.get('Name'), f0.get('ShipType')))
                continue
            if lost_ship(f0, f1):
                sunk.append((sid, f0.get('Name'), f0.get('ShipType'), f1.get('Fate')))
            logs0 = [v for k, v in f0.items() if k.startswith('LogEntry') and v]
            new = [v for k, v in f1.items() if k.startswith('LogEntry') and v and v not in logs0]
            if int(f1.get('BattleStars') or 0) > int(f0.get('BattleStars') or 0) or \
                    any(re.search(r'took part|in action|battle', x, re.I) for x in new):
                part.append(sid)
                rep[sid] = (f0.get('Name'), f0.get('RepairTime'), f1.get('RepairTime'), f0.get('Status'),
                            f1.get('Status'), f1.get('Fate'))
        l0, l1 = (s_battle or {}).get(f'Nation{n}Losses', {}), (s_post or {}).get(f'Nation{m}Losses', {})
        # Counters of types without campaign ships (no such ShipType in the nation's list) are only reported: MSLost
        # (merchant ships of the month's trade war) and AUXLost (auxiliaries, e.g. transports in a battle) never come
        # with a ship of the list.
        types = {f.get('ShipType') for f in list(b0.values()) + list(b1.values())}
        changed = {k: (l0.get(k), v) for k, v in l1.items() if l0.get(k) != v}
        lost = {k: v for k, v in changed.items() if k.endswith('Lost') and k[:-4] in types}
        out[n] = dict(part=part, gone=gone, sunk=sunk, repair=rep, lost=lost,
                      counted=sum(int(v or 0) - int(a or 0) for a, v in lost.values()),
                      abstract={k: v for k, v in changed.items() if k not in lost},
                      merchants=(l0.get('MSLost'), l1.get('MSLost')))
    return out


def losses_explained(eff):
    """Every warship loss the counters added has a lost ship (gone or Fate) in the same window."""
    return all(e['counted'] <= len(e['gone']) + len(e['sunk']) for e in eff.values())


def effects_txt(eff):
    return '; '.join(f'nation {n}: losses {e["lost"] or "-"}, lost ships {(e["gone"] + e["sunk"])[:4] or "-"}'
                     + (f', other counters {e["abstract"]}' if e['abstract'] else '')
                     for n, e in eff.items() if e['lost'] or e['gone'] or e['sunk'] or e['abstract'])


def month_stages(rec, b, s_post, nations):
    """The live battle's month step by step (saves read by StageWatcher): [(label, before, after)]: the live battle and
    the month end (its save at the start -> the host's save at the battle phase start, after DoEndOfTurn), each idea-1
    merge (the save as that battle was sent -> the merged save), what came after the last merge (the game's resolution
    of declined or unfought battles, the new month's save). Without a battle phase: one step to the month's save."""
    st = rec.get('stages') or {}
    if not b.get('s_battle'):
        return []
    pts = [('live battle start', b.get('s_battle'))]
    if st.get('s0'):
        pts.append(('battle phase start', st['s0']))
    pts += [(f'battle {n} merged', st['s1'][n]) for n in sorted(st.get('s1') or {})]
    pts.append(('after the month', s_post))
    pts = [p for p in pts if p[1]]
    return [(f'{l0} -> {l1}', a, z, battle_effects(a, z, nations)) for (l0, a), (l1, z) in zip(pts, pts[1:])]


def same_ships(sa, sb, nations=(0, 1)):
    """Differences between two saves in the ships of `nations` (ids, the battle fields) and their Losses sections."""
    diffs = []
    for n in nations:
        a, b = ships_by_id(sa.get(f'Nation{n}Ships')), ships_by_id(sb.get(f'Nation{n}Ships'))
        if set(a) != set(b):
            diffs.append((n, 'ids', sorted(set(a) ^ set(b))[:6]))
        for sid in set(a) & set(b):
            for k in SHIP_BATTLE_FIELDS:
                if a[sid].get(k) != b[sid].get(k):
                    diffs.append((n, sid, k, a[sid].get(k), b[sid].get(k)))
        if sa.get(f'Nation{n}Losses') != sb.get(f'Nation{n}Losses'):
            diffs.append((n, 'Losses', sa.get(f'Nation{n}Losses'), sb.get(f'Nation{n}Losses')))
    return diffs


LS_DEFER_RE = r'lockstep: "Exit battle" in the results: the battle screen closes after this minute'
LS_DEFER_SEQ = (LS_DEFER_RE, r'lockstep: The battle is over \(', r'live battle ended \(normal\)',
                r'lockstep: closing the battle screen \("Exit battle" in the results\)')


def deferred_close_checks(tag, rec, b, hl, cl):
    """0.3.0: a live battle the game ends INSIDE a minute (its results' "Exit battle" is pressed during the minute's
    step) closes only after that minute: on both copies `"Exit battle" in the results: the battle screen closes after
    this minute`, then the normal end at the boundary ("The battle is over (...)", "live battle ended (normal)"), then
    `closing the battle screen ("Exit battle" in the results)`, in this order; the host's month after it (FormClose ->
    DoStrategicTurn -> DoEndOfTurn) runs with the game's own dialogs, answered by the test ("Turn messages"), and no box
    goes through the battle's minute-box policy (no "minute box", skipped picture message, "Battle: ..." or WARNING
    line between the close and "DoEndOfTurn finished")."""
    def seq(t):
        lines, idx = (t or '').splitlines(), []
        for p in LS_DEFER_SEQ:
            i = next((k for k in range(idx[-1] + 1 if idx else 0, len(lines)) if re.search(p, lines[k])), None)
            if i is None:
                return None, lines
            idx.append(i)
        return idx, lines

    order = {}
    for who, t in (('host', hl), ('client', cl)):
        idx, lines = seq(t)
        order[who] = [lines[i].split('] ', 1)[-1][:70] for i in idx] if idx else None
    idx, lines = seq(hl)
    policy, eot = [], False
    for ln in lines[idx[-1] + 1:] if idx else []:
        if 'DoEndOfTurn finished' in ln:
            eot = True
            break
        if re.search(r'lockstep: (minute box|picture message .* skipped|Battle: |WARNING)', ln):
            policy.append(ln.split('] ', 1)[-1][:100])
    t_end = b.get('t_end') or rec['t0']
    month = [e for t, w, e in (_ANS[0].seen if _ANS[0] else []) if w == 'host' and t >= t_end
             and not e.startswith('TdlgResults')]
    check(f'{tag}: the game ended the battle inside a minute and the battle screen closed only after that minute: on both '
          f'copies "\\"Exit battle\\" in the results: the battle screen closes after this minute" -> "The battle is over" '
          f'-> "live battle ended (normal)" -> "closing the battle screen", in this order; the host\'s month after it ran '
          f'with normal dialogs (answered by the test, "Turn messages" among them), none answered by the battle\'s '
          f'minute-box policy (no minute box / skipped picture message / "Battle: ..." / WARNING after the close) up to '
          f'"DoEndOfTurn finished"', order['host'] and order['client'] and eot and not policy
          and any(e.startswith('TdlgCampTurnRep') for e in month),
          f'host {order["host"]} | client {order["client"]} | policy lines after the close {policy[:4]} | DoEndOfTurn '
          f'{eot} | host dialogs after the end {month[:8]}')


def host_battle_checks(ctx, tag, rec, b, plan, before):
    """The host's battle against nation 1 inside a real processed month (CheckForBattles2 -> DoBattle during the host's
    turn -> lockstep -> end -> FormClose -> DoStrategicTurn -> DoEndOfTurn -> the new month to the client)."""
    hl, cl = log_since(H, rec['hmark']), log_since(C, rec['cmark']) if not rec['relaunched'] else ''
    order = [log_lines(hl, p)[:1] for p in (r'host turn: \d+ submissions merged, running turn', re.escape(LS_HOLD),
                                            r'lockstep: battle \d+ epoch 1 sent', r'live battle \d+ started with Captain',
                                            r'lockstep: battle \d+ epoch 1 running at minute 0')]
    secs = [log_secs(x[0]) if x else None for x in order]
    prev = ls_answered(_ANS[0], 'host', rec['t0'], 'TdlgBattlePreview')
    check(f'{tag}: the host\'s month (Turn after the merge) started a battle against nation 1 (CheckForBattles2 -> '
          f'DoBattle): the host\'s preview accepted, then held, sent, started with Captain and running at minute 0 - in '
          f'this order; host `ls session` hostOpp=1 testSlotMode=0 (a real month: no battle slot mode)',
          prev and None not in secs and secs == sorted(secs) and b['sA'].get('testSlotMode') == '0'
          and b['sA'].get('slotMode') == '0' and b.get('hostOpp') == '1',
          f'{prev[:1]} {[x[0][:70] if x else None for x in order]} {b["sA"]}')
    eot = log_lines(hl, r'DoEndOfTurn finished')
    check(f'{tag}: the host\'s month ran once: one DoEndOfTurn, the date one month on ({rec["date0"]} -> {rec["date1"]})',
          len(eot) == 1 and rec['date1'] == month_after(rec['date0']), f'{len(eot)} DoEndOfTurn lines {eot[:3]}')
    probe = [p for p in _ANS[0].probe if p[0] >= rec['t0'] and p[2] == 'host']
    if probe:
        usable = [p for p in probe if p[3] and p[4]]
        log(f'  {tag}: after "Exit battle" the host\'s battle screen was up without a dialog {len(probe)} times, '
            f'{len(usable)} of them enabled with "View results" (closing: {sum(1 for p in usable if p[5])}); '
            f'first {probe[:3]}')
    loaded = log_lines(cl, rf'lockstep: battle loaded from slot {BATTLE_SLOT} ') if cl else ['(client restarted)']
    check(f'{tag}: the client (submitted, waiting for the month) loaded the battle from its battle slot {BATTLE_SLOT} and '
          f'ran it as B (side 1)', loaded and b['b0'].get('role') == 'B' and b['b0'].get('side') == '1',
          f'{loaded[:1]} {b["b0"].get("role")}/{b["b0"].get("side")}')
    h = b.get('hash') or {}
    mode = b['mode']
    need = max(6, (b.get('minute') or 0) - 5) if mode in ('agree', 'game') else 6
    check(f'{tag}: live ({mode}): every minute\'s state hash equal on both copies (>= {need} minutes compared), no DESYNC; '
          f'the client\'s order (course +90) shows the same on both', h.get('minutes', 0) >= need and not h.get('differ')
          and not b.get('desync') and not b.get('error') and (b.get('order') or {}).get('A') == (b.get('order') or {}).get('B')
          == (b.get('order') or {}).get('want'), f'{h} {b.get("desync")} {b.get("error")} {b.get("order")}')
    if mode in ('agree', 'game', 'endsoon'):
        ends_a = log_lines(hl, r'lockstep: The battle is over \(|live battle ended \(')
        ends_b = log_lines(cl, r'lockstep: The battle is over \(|live battle ended \(') if cl else []
        game_end = log_lines(hl, LS_GAME_END_RE)
        agreed = any('ended by agreement' in x for x in ends_a) and any('ended by agreement' in x for x in ends_b)
        if mode == 'agree':
            check(f'{tag}: the battle ended on both copies at minute >= 400 ({b.get("minute")}), '
                  f'{"by agreement" if agreed else "by the game itself" if game_end else "?"} (end requests '
                  f'{b.get("tries")}, refused by the game {b.get("refusals")}; after a refusal the game may end it first)',
                  (b.get('minute') or 0) >= 400 and ends_a and ends_b and (agreed or game_end),
                  f'{ends_a[-2:]} | {ends_b[-2:]}')
        elif mode == 'game':
            capped = agreed and not game_end and not b.get('proposals') and (b.get('minute') or 0) >= 900
            check(f'{tag}: the game ended the battle at minute {b.get("minute")} on both copies: by itself '
                  f'({(game_end or ["-"])[-1][-90:]}), by its proposal agreed ({b.get("proposals")}), or, when it did '
                  f'neither within 900 minutes, by the agreed end at that cap ({capped})',
                  (game_end or b.get('proposals') or capped) and ends_a and ends_b, f'{ends_a[-2:]} | {ends_b[-2:]}')
        else:
            tl = {w: log_lines(t, r'lockstep: TEST scenario length \d+ -> \d+: the game ends the battle at the end of '
                                  r'minute \d+') for w, t in (('host', hl), ('client', cl))}
            ge = {'host': game_end, 'client': log_lines(cl, LS_GAME_END_RE) if cl else []}
            mins = {int(re.search(r'minute (\d+)$', v[-1])[1]) for v in tl.values() if v}
            check(f'{tag}: `ls endsoon` at minute {b.get("endsoon_at")} ({b.get("endsoon")}): on both copies the game '
                  f'itself ended the battle inside the next minute ("TEST scenario length a -> b: the game ends the battle '
                  f'at the end of minute M", then the game\'s "Scenario absolute time limit is reached!"), over at '
                  f'minute M = {b.get("minute")}', all(tl.values()) and all(ge.values()) and len(mins) == 1
                  and b.get('minute') in mins and (b.get('endsoon') or '').startswith('ok'),
                  f'{ {w: [x.split("] ", 1)[-1][:90] for x in v[-1:]] for w, v in tl.items()} } '
                  f'{ {w: [x.split("] ", 1)[-1][:60] for x in v[-1:]] for w, v in ge.items()} }')
        # a battle the game ends inside a minute: its results' "Exit battle" closes the screen and FormClose runs the rest
        # of the host's month; that must not happen inside the battle's simulation step (minute-box policy, shared RNG)
        hlines, nested, seen_end = hl.splitlines(), [], False
        for ln in hlines:
            if re.search(LS_GAME_END_RE, ln):
                seen_end = True
                continue
            if seen_end and re.search(r'live battle ended \(', ln):
                break
            if seen_end and re.search(r"lockstep: (minute box|picture message .* skipped|WARNING)", ln):
                nested.append(ln.split('] ', 1)[-1][:110])
        warn = log_lines(hl, r'lockstep: WARNING .* inside a simulation step')
        check(f'{tag}: both copies report the end as the normal end of the battle ("The battle is over (...)"), not "the '
              f'battle screen was closed"; the host\'s month after the battle runs outside the battle\'s simulation step '
              f'(no "WARNING ... handled inside a simulation step")', ends_a and ends_b
              and not any('screen was closed' in x for x in ends_a + ends_b) and not warn,
              f'host {ends_a[-2:]} | client {ends_b[-2:]} | warnings {warn[:2]} | boxes answered by the battle\'s policy '
              f'after the game\'s end: {nested[:6]}')
        if mode == 'endsoon' or re.search(LS_DEFER_RE, hl):
            deferred_close_checks(tag, rec, b, hl, cl)
    elif mode.startswith('drop'):
        ar = ls_answered(_ANS[0], 'host', rec['t0'], 'OK [Yes]')
        check(f'{tag}: the host dropped the client after its live minutes, the client went back to slot 77 at once, the '
              f'host\'s battle went on against the AI to its end (auto-run)', b.get('drop') and b.get('back_s') is not None
              and ar, f'{b.get("drop")} back {b.get("back_s")} s; auto-run {ar[:1]}')
    tdna = chat(ctx.cp).count('turn did not advance') - rec['chat0'].count('turn did not advance')
    pkg = log_lines(hl, r'state seq \d+ -> Captain \(nation 1,')
    hst, cst = sstatus(H), sstatus(C)
    hn = lsn(H)
    check(f'{tag}: the host\'s month completed after the battle (FormClose -> DoStrategicTurn -> DoEndOfTurn) and the new '
          f'month reached the client once (one "state seq N -> Captain (nation 1, ..." line, no "turn did not advance"); '
          f'both in {rec["date1"]}; host live battle and slot mode off', hst.get('date') == cst.get('date') == rec['date1']
          and rec['date1'] != rec['date0'] and len(pkg) == 1 and tdna <= 0 and hn.get('hostOpp') == '-1'
          and hn.get('slotMode') == '0' and cst.get('slot') == str(CLIENT_SLOT) and cst.get('playerIdx') == '1',
          f'{rec["date0"]} -> host {hst.get("date")} client {cst.get("date")}; packages {len(pkg)}; "did not advance" '
          f'+{tdna}; {hn}')
    s_post, c_post = read_save(save_path(HOST_SLOT)), read_save(save_path(CLIENT_SLOT))
    stages = month_stages(rec, b, s_post, (0, 1))
    win, _, _, eff = stages[0] if stages else ('live battle start -> after the month', None, None,
                                                battle_effects(b.get('s_battle'), s_post))
    rec['stage_effects'] = stages
    log(f'  {tag}: battle effects ({win}): ' + '; '.join(
        f'nation {n}: {len(e["part"])} took part, {len(e["gone"])} gone {e["gone"][:3]}, lost {e["sunk"][:4]}, losses '
        f'{e["lost"]} (counters without campaign ships, e.g. MSLost/AUXLost: {e["abstract"]}), repair/status/fate changed '
        f'{[v for v in e["repair"].values() if v[1] != v[2] or v[3] != v[4]][:5]}'
        for n, e in eff.items()))
    if len(stages) > 1:
        log(f'  {tag}: the month step by step: ' + ' | '.join(f'{lab}: {effects_txt(e) or "no losses"}'
                                                             for lab, _, _, e in stages))
    diffs = same_ships(s_post or {}, c_post or {})
    check(f'{tag}: the battle is in the campaign ({win}): ships of both nations took part (battle star / "Took part" log '
          f'entry); every warship loss counted in [NationNLosses] (counters of types with no campaign ships, MSLost and '
          f'AUXLost, only reported) comes with a lost '
          f'ship (gone, or its Fate no longer "XXX", e.g. "Sunk in battle ..."; the month\'s idea-1 battles and the '
          f'game\'s resolutions after the battle phase are checked apart); the client\'s month (slot '
          f'77) has the same ships, battle fields (RepairTime, BattleStars, log entries, Status, Fate, CrewQuality) and '
          f'losses for nations 0 and 1 as the host\'s save', all(eff[n]['part'] or eff[n]['gone'] for n in (0, 1))
          and losses_explained(eff) and not diffs,
          f'{[(n, len(e["part"]), len(e["gone"]), len(e["sunk"]), e["lost"]) for n, e in eff.items()]}; '
          f'differences {diffs[:5]}')
    nh, nc = nation_table(H), nation_table(C)
    vh, vc = vpinfo(H), vpinfo(C)
    vpl = re.findall(VP_BATTLE_RE, hl)
    check(f'{tag}: ship counts of nations 0 and 1 equal on host and client; VP of 0 and 1 equal on host and client'
          + (f'; "victory points: nation 1 +{vpl[0][1]} in its battle against the host -> {vpl[0][2]}" and nation 1 >= '
             f'that after the month' if vpl else ' (no VP line: nation 1 scored nothing in the battle)'),
          all(nh.get(n, {}).get('ships') == nc.get(n, {}).get('ships') for n in (0, 1)) and vh and vc
          and vh['vp'].get(0) == vc['vp'].get(0) and vh['vp'].get(1) == vc['vp'].get(1)
          and (not vpl or vh['vp'].get(1, -1) >= int(vpl[-1][2])),
          f'ships {[(nh.get(n, {}).get("ships"), nc.get(n, {}).get("ships")) for n in (0, 1)]}; vp host '
          f'{vh and vh["raw"]} client {vc and vc["raw"]}; lines {vpl}; before {before.get("vp")}')
    hv, cv = nint(H, 1, 'ResearchPct'), nint(C, 1, 'ResearchPct')
    check(f'{tag}: the client\'s plans for that month were not lost (nation 1 ResearchPct {plan} set before the submit: '
          f'host {hv}, client {cv})', hv == cv == plan, f'{hv} {cv}')
    return eff


def idea1_merge_checks(tag, rec, K, J):
    """h2) the month's idea-1 merges, from the saves StageWatcher read at once: for each merged battle N the host's save
    as that battle was sent (the save before the merge), the battle slot after the battle (Game87, K = nation 0 there)
    and the merged save: [NationKLosses] and [NationJLosses] of the merged save = the battle slot's (the merge adds
    result - sent), and every warship loss the merge added comes with a lost ship (gone or Fate) in the merged save.
    The battle slot's own accounting and what the game's resolution of the month's other battles did after the last
    merge are reported."""
    st = rec.get('stages') or {}
    s1, g87, prev = st.get('s1') or {}, st.get('g87') or {}, st.get('s0')
    rows, bad = [], []
    for n in sorted(s1):
        merged, slot = s1[n], g87.get(n)
        if not (prev and merged):
            bad.append(n)
            rows.append((n, 'save not read'))
            prev = merged or prev
            continue
        eff_m = battle_effects(prev, merged, (K, J))
        ok, row = losses_explained(eff_m), dict(merge=effects_txt(eff_m) or 'no losses')
        if slot and 'General' in slot:
            eff_s = battle_effects(prev, slot, (K, J), slot_nations={K: 0})
            same = all(merged.get(f'Nation{x}Losses') == slot.get(f'Nation{y}Losses') for x, y in ((K, 0), (J, J)))
            row.update(slot=effects_txt(eff_s) or 'no losses', same_losses=same,
                       slot_explained=losses_explained(eff_s))
            ok = ok and same
        else:
            row['slot'] = 'not read (rewritten by the next battle first)'
        rows.append((n, row))
        if not ok:
            bad.append(n)
        prev = merged
    after = [(lab, effects_txt(e) or 'no losses') for lab, _, _, e in (rec.get('stage_effects') or [])
             if lab.endswith('-> after the month') and 'merged' in lab]
    resolved = [s for s in st.get('steps', []) if s.startswith('resolved')]
    log(f'  {tag}: h2) idea-1 merges: {rows}; after the last merge (the game\'s resolution {resolved}): {after}')
    check(f'{tag}: h2) each idea-1 battle merged this month (saves read at once: as sent -> battle slot Game87 -> merged): '
          f'the merged [Nation{K}Losses]/[Nation{J}Losses] = the battle slot\'s, and every warship loss the merge added '
          f'comes with a lost ship (gone or Fate) in the merged save' + ('' if s1 else ' (none merged this month)'),
          not bad, f'{rows}; bad {bad}')


def scenario_lockstep_month(ctx):
    """The host's battle against the joined player inside a REAL processed month (the path test battles skip). The host
    at war with nation 1 (`diplo war 1` + applydiplo); months are processed until the game's CheckForBattles2 starts a
    battle against nation 1 during the host's turn (random: up to 8 months). Each time: the host's preview, the lockstep
    start on both, the live battle (every division on the AI for contact; default: 12 live minutes, then the host drops
    the client and fights on against the AI on auto-run; --stress: two battles, the first ended by agreement (or by the
    game) at >= 400 minutes, the second ended by the game itself INSIDE a minute (`ls endsoon` after 6 minutes: 0.3.0's
    deferred close, the host's month after it with normal dialogs), the second's months with idea-1 battles of nation 1
    against an AI nation too, their merges checked step by step), the end,
    FormClose -> DoStrategicTurn -> DoEndOfTurn (idea 1's battle phase), the new month to the client; the campaign: the
    battle's ships and losses in the host's save and in the client's month for both nations, VP, the client's plans of
    that month (ResearchPct set before its submit). Then peace (compromise) and one normal month."""
    marks0 = {H: log_mark(H), C: log_mark(C)}
    stress = OPTS['stress']
    ls_ensure_session(ctx)
    ls_quiet(ctx)
    r01 = ls_war01(ctx)
    check('setup: the host at war with nation 1 (`diplo war 1` accepted + applydiplo)', r01.get('war') == '1', r01)
    modes = ['agree', 'endsoon'] if stress else ['drop12']
    J = area = tJ = None
    fought, attempts = [], []
    for attempt in range(1, 9):
        if len(fought) >= len(modes):
            break
        k = len(fought)
        if rel(0, 1).get('war') != '1':
            ls_war01(ctx)
        if stress and k == 1 and J is None:
            cands = battle_pairs(1)
            if cands:
                _, _, J, area = cands[0]
                tJ = rel(1, J).get('tension', '5')
                log(f'  interplay: nation 1 at war with AI nation {J} too; idea-1 battles in {area} injected each month')
        if J is not None and k == 1:
            if rel(1, J).get('war') != '1':
                cmd(H, f'settension 1 {J} 50')
            for _ in range(2):
                inject(1, J, area)
        pct = nint(C, 1, 'ResearchPct')
        plan = 9 if pct != 9 else 8
        cmd(C, f'nfield 1 ResearchPct {plan}')
        before = dict(vp=vpinfo(H) and vpinfo(H)['raw'])
        rec = ls_month(ctx, f'lockstep_month M{attempt}', modes=[modes[k]])
        kind = ls_host_battle(rec)
        hb = [b for b in rec['live'] if b['A'] == H]
        hl = log_since(H, rec['hmark'])
        attempts.append((attempt, rec['date0'], kind, [(b['bid'], b['mode'], b.get('error')) for b in rec['live']],
                         log_lines(hl, r'battle phase: \d+')[:1]))
        if kind != 'none' and not hb:
            check(f'M{attempt}: the host\'s battle against nation 1 ran live (the client fights battles, plays them live)',
                  False, f'{kind}: {log_lines(hl, LS_OFF)[:3]}')
        if hb:
            tag = f'battle {len(fought) + 1} ({modes[k]})'
            n0 = len(results)
            host_battle_checks(ctx, tag, rec, hb[0], plan, before)
            if J is not None and k == 1:
                phase = log_lines(hl, r'battle phase: \d+ battle')
                live_t = log_secs((log_lines(hl, r'lockstep: battle \d+ epoch 1 sent') or [''])[0])
                ph_t = log_secs(phase[0]) if phase else None
                sent1 = log_lines(hl, r'battle \d+ sent to Captain')
                settled = log_lines(hl, r'battle \d+ applied: |battle \d+: resolved by the game')
                check(f'{tag}: h) interplay: in the same month the idea-1 battle phase (nation 1 vs AI nation {J}) ran after '
                      f'the host\'s live battle (DoTurn2 -> battle, then DoEndOfTurn -> the battle phase) and its battles '
                      f'were sent to Captain and settled', phase and live_t is not None and ph_t is not None
                      and ph_t >= live_t and sent1 and len(settled) >= len(sent1),
                      f'live {live_t} phase {phase[:1]} sent {sent1[:3]} settled {settled[:3]}')
                idea1_merge_checks(tag, rec, 1, J)
            rs = results[n0:]
            b = hb[0]
            LS_MATRIX.append((f'real month {rec["date0"]}: host battle {b.get("bid")} ({modes[k]})',
                              sum(1 for r in rs if r[2]), len(rs), round(rec['seconds']),
                              f'minutes {(b.get("hash") or {}).get("minutes")}, end at {b.get("minute") or "drop"}'))
            fought.append(rec)
    check(f'the game started {len(modes)} host battle(s) against nation 1 within 8 months (CheckForBattles2 is random; '
          f'attempts: month, date, kind, live battles, battle phase)', len(fought) >= len(modes), attempts)
    if J is not None:
        cmd(H, f'settension 1 {J} {tJ}')
    r01 = ls_peace01(ctx)
    check('peace 0-1 again (compromise: the host offers, the client\'s "Accept peace" button, applydiplo)',
          r01.get('war') == '0', r01)
    hm = log_mark(H)
    run_month(ctx, 'lockstep_month peace')
    check('the next normal month works (both in the new month; no live battle, no battle phase)',
          LS_HOLD not in log_since(H, hm) and 'battle phase:' not in log_since(H, hm), log_lines(log_since(H, hm),
                                                                                                    LS_HOLD)[:2])
    ls_after_checks(ctx, 'lockstep_month')
    ls_log_scan('lockstep_month', marks0)


def ls_spam(A, B, seconds=60, seed=7, kinds='clock+orders'):
    """Both players send commands in quick succession (each ~2-3 a second) for `seconds`. kinds: 'clock' = run/pause/
    speed only; 'orders' = division orders only, while A asks for one more minute every ~1.2 s; 'clock+orders' = all.
    Returns {port: (commands sent, error replies)}."""
    stop, out = time.time() + seconds, {A: [0, [], 0], B: [0, [], 0]}

    def worker(port, side, rnd):
        divs = [d['i'] for d in ls_divs(port) if d['side'] == side]
        t_run = 0
        while time.time() < stop:
            k = rnd.random()
            if kinds == 'orders':
                k = 0.8 if divs else 0.25
                if port == A and time.time() - t_run > 1.2:
                    t_run, k = time.time(), 0.0
            elif kinds == 'clock':
                k = k * 0.7
            if k < 0.3:
                line = f'ls run {1 if kinds == "orders" else rnd.choice((1, 1, 2, 3))}'
            elif k < 0.5:
                line = 'ls pause'
            elif k < 0.7:
                line = f'ls speed {rnd.randint(2, 5)}'
            elif divs:
                line = f'ls order {rnd.choice(divs)} {rnd.randrange(360)} {rnd.randint(4, 18)}'
            else:
                line = 'ls pause'
            r = scmd(port, line, 30)
            out[port][0] += 1
            if r == 'error division':  # the division is gone (sunk, merged): not a failure; the list is read again
                divs = [d['i'] for d in ls_divs(port) if d['side'] == side]
                out[port][2] += 1
            elif not r.startswith('ok'):
                out[port][1].append(f'{line} -> {r}')
            time.sleep(rnd.uniform(0.15, 0.6))

    th = [threading.Thread(target=worker, args=(p, s, random.Random(seed + s)), daemon=True) for p, s in ((A, 0), (B, 1))]
    for t in th:
        t.start()
    for t in th:
        t.join(seconds + 60)
    gone = {who_of(p): v[2] for p, v in out.items() if v[2]}
    if gone:
        log(f'  spam: orders to divisions that no longer exist (the list was read again): {gone}')
    return {p: (n, errs) for p, (n, errs, _) in out.items()}


def ls_resync_once(port, A=H, B=C):
    """An injected desync on one copy and the resync that follows (no check). Returns A's status afterwards."""
    e0 = int(lss(A).get('epoch') or 0)
    scmd(port, 'ls desync')
    ls_run(A, B, 1, A)
    return ls_wait(lambda: (lambda a: a if (int(a.get('epoch') or 0) > e0 and ls_idle_equal(A, B)) or a.get('on') != '1'
                            else None)(lss(A)), 120, 'the resync after an injected desync')


def mem(ctx):
    return private_mb(ctx.hp), private_mb(ctx.cp)


def scenario_lockstep_stress(ctx):
    """Stress and bad game states of live battles (--stress only). Test battles (`pocbattle 1 0`, battle slot mode):
    S1 spam of run/pause/speed/order from both sides, end requests during a resync and right after a drop; S2 end
    requests while the client loads the battle, the client leaves (`ls abort`); S3 the resync counter (2 desyncs, 12
    good minutes reset it, 3 more stay live, the 4th makes the host give side 1 to the AI), then desync storms injected
    on the client and on the host (4 each: one DESYNC per injected minute, the minute after each resync equal including
    the RNG part, the 4th -> the AI takes side 1); S4 a silent client and a client that never loads (12-s / 15-s
    timeouts), S5 a silent host; S7 `ls throw` / `ls stall` / `ls nest` on each copy and the guard's negative control;
    memory and log watch (no LEAK) over these 9 battles in a row; S6 the client killed at a boundary (the host drops it,
    goes on), started again and joining during the host's battle; f) a long battle (every division on the AI) at speed 5
    to its end, then a normal month. Real months (the host at war with nation 1): P1 the client opts out
    (`fightbattles 0`): the host's battle against it is fought against the AI; P2 the client killed in the middle of a
    minute of the host's live battle: dropped, the host's battle and month go on, the client started again (with
    RTW3MP_LIVE_BATTLES=0) joins and gets its month; P3 that client (live battles off) is fought against the AI; P4
    live again; peace, a normal month. Last: b) the host killed in a battle: the client leaves the battle and is not
    stuck, joins a new session of the host started again from the restored slot 1, and a month is processed."""
    ans = ls_ans(ctx)
    ls_ensure_session(ctx)
    marks0 = {H: log_mark(H), C: log_mark(C)}
    memory = [('start', *mem(ctx))]

    def s1():
        ls_start_test(ctx, 'S1')
        scmd(H, 'ls speed 3')
        for kinds in ('clock', 'orders', 'clock+orders'):
            if lss(H).get('on') != '1':  # the resync limit gave side 1 to the AI: a new battle
                ls_start_test(ctx, f'S1 {kinds}')
                scmd(H, 'ls speed 3')
            ma, mb = log_mark(H), log_mark(C)
            sp = ls_spam(H, C, 40, kinds=kinds)
            scmd(H, 'ls pause')
            ls_wait(lambda: ls_idle_equal(H, C), 300, 'both idle after the spam')
            ls_end_reset(H, C)
            ls_run_total(H, C, 3, H)
            hc, a = ls_compare(H, ma, C, mb, f'spam {kinds}'), lss(H)
            des = ls_lines(H, ma, r'DESYNC at minute')
            clk = ls_lines(H, ma, r'release of minute \d+ at minute|clock difference')
            syn = ls_lines(H, ma, r'Synchronising the battle again')
            check(f'S1 d) spam ({kinds}): {sp[H][0]} host and {sp[C][0]} client commands in 40 s '
                  f'({dict(clock="run/pause/speed", orders="division orders, the host runs a minute every 1.2 s").get(kinds, "run/pause/speed/orders")}); '
                  f'afterwards both idle at the same boundary (no deadlock), 3 more minutes run; every minute equal, no '
                  f'DESYNC, no clock difference, no resync, no drop, no error reply', a.get('on') == '1'
                  and hc['minutes'] >= 3 and not hc['differ'] and not des and not clk and not syn
                  and not sp[H][1] and not sp[C][1],
                  f'{hc}; DESYNC {des[:3]}; clock {clk[:4]}; resyncs {len(syn)}; errors {sp[H][1][:3]} {sp[C][1][:3]}')
        if lss(H).get('on') != '1':
            ls_start_test(ctx, 'S1 e')
            scmd(H, 'ls speed 3')
            ls_run_total(H, C, 2, H)
        e0, mk = int(lss(H).get('epoch') or 0), log_mark(H)
        scmd(H, 'ls resync')
        time.sleep(0.3)
        rb, ra = scmd(C, 'ls end'), scmd(H, 'ls end')
        ls_wait(lambda: int(lss(H).get('epoch') or 0) > e0 and ls_idle_equal(H, C), 150, 'the resync')
        time.sleep(4)
        left = ls_end_reset(H, C)
        ma, mb = log_mark(H), log_mark(C)
        ls_run_total(H, C, 2, C)
        hc, a = ls_compare(H, ma, C, mb, 'after the end during a resync'), lss(H)
        check(f'S1 e) both ask for the end during a resync (replies host "{ra}", client "{rb}"): the battle is synchronised '
              f'again and stays live (no end before minute 400), the next 2 minutes equal, requests left {left}',
              a.get('on') == '1' and a.get('st') == 'running' and hc['minutes'] >= 2 and not hc['differ'],
              f'{hc}; {ls_lines(H, mk, "can.t end yet|Synchronising|battle is over")[:3]}')
        hm = log_mark(H)
        scmd(H, 'ls drop')
        rb, ra = scmd(C, 'ls end'), scmd(H, 'ls end')
        st = ls_wait(lambda: ls_client_back(ctx), 120, 'the client back after the drop')
        check(f'S1 e) end requests right after the drop: answered without a hang (client "{rb}", host "{ra}"), the client '
              f'back in slot 77, the host\'s battle goes on alone', st and ls_over(H) and battle_up(ctx.hp)
              and ls_lines(H, hm, r'left the live battle'), ls_lines(H, hm, r'dropping|left the live')[:2])
        memory.append(('S1', *mem(ctx)))

    def s2():
        ans.paused = False
        ls_wait(lambda: ls_client_back(ctx), 120, 'the client idle')
        hm, cm = log_mark(H), log_mark(C)
        cmd(H, 'pocbattle 1 0')
        b = ls_wait(lambda: (lambda x: x if x.get('on') == '1' else None)(lss(C)), 150, 'the client starts loading', 0.2)
        rb, ra = scmd(C, 'ls end'), scmd(H, 'ls end')
        a, b2 = ls_wait(lambda: ls_both_running(H, C), 150, 'running after the end during the load')
        time.sleep(2)
        left = ls_end_reset(H, C)
        scmd(H, 'ls speed 3')
        ma, mb = log_mark(H), log_mark(C)
        ls_run_total(H, C, 3, H)
        hc = ls_compare(H, ma, C, mb, 'after the end during the load')
        check(f'S2 e) both ask for the end while the client loads the battle (client st={b.get("st")}; replies client '
              f'"{rb}", host "{ra}"): the battle starts, stays live and in sync (3 minutes equal), requests left {left}',
              lss(H).get('on') == '1' and hc['minutes'] >= 3 and not hc['differ'],
              f'{hc}; {ls_lines(H, hm, "not started yet|can.t end|battle is over")[:2]} '
              f'{ls_lines(C, cm, "not started yet|can.t end")[:2]}')
        hm, t = log_mark(H), time.time()
        scmd(C, 'ls abort')
        st = ls_wait(lambda: ls_client_back(ctx), 120, 'the client back after leaving')
        ls_wait(lambda: ls_over(H), 90, 'the host notes the client left')
        check(f'S2: the client leaves the battle (`ls abort`): back in slot 77; the host goes on alone after '
              f'{time.time() - t:.0f} s ("{(ls_lines(H, hm, r"dropping|ended the battle") or ["?"])[0]}")', st
              and battle_up(ctx.hp), ls_lines(H, hm, r'dropping|ended the battle|left the live')[:2])
        memory.append(('S2', *mem(ctx)))

    def s3():
        ls_start_test(ctx, 'S3')
        scmd(H, 'ls speed 3')
        ls_run_total(H, C, 2, H)
        mk = log_mark(H)
        for port in (C, H):
            ls_resync_once(port)
        r1 = lss(H).get('resyncs')
        ls_run_total(H, C, 12, H)
        r2 = lss(H).get('resyncs')
        for port in (C, H, C):
            ls_resync_once(port)
        a3 = lss(H)
        hm = log_mark(H)
        scmd(C, 'ls desync')
        ls_run(H, C, 1, H)
        st = ls_wait(lambda: ls_client_back(ctx), 150, 'the client back after the resync limit')
        drop = ls_lines(H, hm, r'dropping Captain: the battles could not be kept in sync|left the live battle')
        check(f'S3 c) desync storm: 2 desyncs (resyncs {r1}), 12 good minutes reset the counter ({r2}), 3 more desyncs '
              f'stay live (on={a3.get("on")}, resyncs {a3.get("resyncs")}); the 4th within 10 minutes makes the host give '
              f'side 1 to the AI ("the battles could not be kept in sync"); the client back in slot 77, the host\'s battle '
              f'goes on; {len(ls_lines(H, mk, "DESYNC at minute"))} DESYNC lines, '
              f'{len(ls_lines(H, mk, "Synchronising the battle again"))} resyncs', r1 == '2' and r2 == '0'
              and a3.get('on') == '1' and a3.get('resyncs') == '3' and len(drop) >= 2 and st and battle_up(ctx.hp),
              drop)
        memory.append(('S3', *mem(ctx)))

    def storm(port):
        """c) a desync storm injected on one copy (`ls desync` before every other minute): after each resync one clean
        minute that must be equal on both copies including the RNG part; more than 3 resyncs without 10 good minutes in
        between (the 4th injection within 10 minutes) make the host give side 1 to the AI; no hang."""
        who = who_of(port)
        ls_start_test(ctx, f'S3 storm on the {who}')
        scmd(H, 'ls speed 3')
        ls_run(H, C, 1, H)
        hm, cm, t0 = log_mark(H), log_mark(C), time.time()
        rec = dict(injected=0, clean=[])
        for i in range(6):
            if lss(H).get('on') != '1':
                break
            e0 = int(lss(H).get('epoch') or 0)
            scmd(port, 'ls desync')
            rec['injected'] += 1
            scmd(H, 'ls run 1')
            ls_wait(lambda: lss(H).get('on') != '1' or (int(lss(H).get('epoch') or 0) > e0 and ls_idle_equal(H, C)),
                    150, f'a resync or the drop after desync {i + 1} on the {who}')
            if lss(H).get('on') != '1':
                break
            ma, mb = log_mark(H), log_mark(C)
            ls_run(H, C, 1, C)
            c = ls_compare(H, ma, C, mb, f'the clean minute after resync {i + 1}')
            ha, hb = ls_hashes(H, ma), ls_hashes(C, mb)
            rec['clean'].append(dict(minute=c['last'], equal=c['minutes'] >= 1 and not c['differ'],
                                     A=[v[-1] for v in ha.values()][-1:], B=[v[-1] for v in hb.values()][-1:]))
        st = ls_wait(lambda: ls_client_back(ctx), 150, f'the client back after the resync limit ({who})')
        des = ls_lines(H, hm, r'DESYNC at minute')
        inj = ls_lines(port, hm if port == H else cm, r'TEST desync')
        drop = ls_lines(H, hm, r'dropping Captain: the battles could not be kept in sync')
        rng = [bool(re.search(r'-([0-9a-f]+), \S+ [0-9a-f]+-\1\b', x)) for x in des]
        log(f'  storm on the {who}: {rec["injected"]} injected in {time.time() - t0:.0f} s; DESYNC lines (RNG part equal '
            f'in the desynced minute: {rng}): {des}; clean minutes {rec["clean"]}')
        check(f'S3 c) desync storm on the {who}: {rec["injected"]} desyncs injected, each caught in its own minute '
              f'({len(des)} DESYNC lines), after each resync the next minute equal on both copies including the RNG part '
              f'({sum(1 for c in rec["clean"] if c["equal"])}/{len(rec["clean"])}); the 4th makes the host give side 1 to '
              f'the AI ("the battles could not be kept in sync"), the host\'s battle goes on alone, the client back in '
              f'slot 77 (no hang)', rec['injected'] == 4 and len(des) == 4 and len(inj) == 4 and len(rec['clean']) == 3
              and all(c['equal'] for c in rec['clean']) and drop and st and ls_over(H) and battle_up(ctx.hp),
              f'{des} | {rec["clean"]} | {drop[:1]}')
        memory.append((f'S3 {who}', *mem(ctx)))

    def s4():
        for p in (H, C):
            scmd(p, 'ls timeout 12 12 60')
        try:
            ls_start_test(ctx, 'S4')
            scmd(H, 'ls speed 3')
            ls_run(H, C, 1, H)
            hm, cm, t = log_mark(H), log_mark(C), time.time()
            scmd(C, 'ls mute 1')
            scmd(H, 'ls run 3')
            ls_wait(lambda: ls_over(H), 90, 'the host drops the silent client')
            da = time.time() - t
            scmd(C, 'ls mute 0')
            st = ls_wait(lambda: ls_client_back(ctx), 120, 'the silent client leaves')
            db = time.time() - t
            check(f'S4: a silent client (`ls mute 1`, timeouts 12 s): the host drops it after {da:.0f} s (AI commands side '
                  f'1) and goes on; the client leaves after {db:.0f} s and is back in slot 77',
                  da < 45 and st and ls_lines(H, hm, r'left the live battle') and battle_up(ctx.hp),
                  f'{ls_lines(H, hm, r"dropping")[:1]} {ls_lines(C, cm, r"ended on this PC")[:1]}')
            memory.append(('S4', *mem(ctx)))
            # a client that never loads: muted from the start, the host's load timeout (15 s)
            for p in (H, C):
                scmd(p, 'ls timeout 30 30 15')
            scmd(C, 'ls mute 1')
            hm, cm, t = log_mark(H), log_mark(C), time.time()
            cmd(H, 'pocbattle 1 0')
            gone = ls_wait(lambda: ls_lines(H, hm, r'left the live battle|start abandoned|could not start|not loaded'),
                           150, 'the host gives up waiting for the client\'s load')
            dt = time.time() - t
            scmd(C, 'ls mute 0')
            time.sleep(3)
            sent = ls_lines(H, hm, r'lockstep: battle \d+ epoch 1 sent')
            check(f'S4 loadto: a client that never loads the battle (muted from the start): the host sent it '
                  f'({len(sent)} "sent" line), gives up after its 15-s load timeout ({dt:.0f} s after the start: '
                  f'"{gone[-1][:90]}"), commands both sides itself; the client never left slot 77',
                  sent and dt < 90 and ls_over(H) and battle_up(ctx.hp) and ls_client_back(ctx)
                  and not ls_lines(C, cm, r'battle loaded from slot'), f'{gone[:2]} {lss(C).get("on")}')
            memory.append(('S4 loadto', *mem(ctx)))
        finally:
            for p in (H, C):
                scmd(p, 'ls mute 0')
                scmd(p, 'ls timeout 30 30 120')

    def s5():
        for p in (H, C):
            scmd(p, 'ls timeout 12 12 60')
        try:
            ls_start_test(ctx, 'S5')
            scmd(H, 'ls speed 3')
            ls_run(H, C, 1, C)
            hm, cm, t = log_mark(H), log_mark(C), time.time()
            scmd(H, 'ls mute 1')
            st = ls_wait(lambda: ls_client_back(ctx), 120, 'the client leaves: the host is silent')
            db = time.time() - t
            scmd(H, 'ls mute 0')
            ls_wait(lambda: ls_over(H), 60, 'the host drops the client')
            check(f'S5: a silent host (`ls mute 1`): the client leaves after {db:.0f} s ("no contact with Admiral for 12 '
                  f's") and is back in slot 77; the host drops it and goes on', db < 60 and st
                  and ls_lines(C, cm, r'no contact with Admiral') and battle_up(ctx.hp),
                  f'{ls_lines(C, cm, r"ended on this PC")[:1]} {ls_lines(H, hm, r"dropping")[:1]}')
        finally:
            for p in (H, C):
                scmd(p, 'ls mute 0')
                scmd(p, 'ls timeout 30 30 120')
        memory.append(('S5', *mem(ctx)))

    def s7():
        """Robustness (the round-2 review fixes, live): an exception in this copy's next MoveAllShips (`ls throw`), a
        released minute that does not start (`ls stall`, the watchdog), a resync while a nested message loop runs (`ls
        nest 8`: B loads / A sends only from the outermost message loop), each on the client and on the host; each
        recovers with a resync and the battle stays live. Then the guard's negative control: the sighting guard off on
        the client (`ls guard 0`) + a display write -> DESYNC and a resync, the leak check reports it (those LEAK lines
        are excused); guard on again, the minutes after it equal."""
        ls_start_test(ctx, 'S7')
        scmd(H, 'ls speed 3')
        res = {}

        def synced_after(e0):
            a, b = lss(H), lss(C)
            return ls_idle_equal(H, C) if int(a.get('epoch') or 0) > e0 and a.get('epoch') == b.get('epoch') else None

        def fresh():  # more than 3 resyncs without 10 good minutes drop the client: 11 good minutes reset the count
            if int(lss(H).get('resyncs') or 0) >= 2:
                scmd(H, 'ls speed 5')
                ls_run_total(H, C, 11, H)
                scmd(H, 'ls speed 3')

        for kind in ('throw', 'stall'):
            for port in (C, H):
                fresh()
                ma, mb = log_mark(H), log_mark(C)
                e0 = int(lss(H).get('epoch') or 0)
                scmd(port, f'ls {kind}')
                scmd(H, 'ls run 2' if kind == 'throw' else 'ls run 1')
                time.sleep(2)
                ls_wait(lambda: synced_after(e0), 180, f'{kind} on the {who_of(port)} recovered')
                pat = (r'TEST exception|context frame MoveAllShips dropped|minute from \d+ failed' if kind == 'throw'
                       else r'TEST minute start refused|minute from \d+ failed')
                res[f'{kind} {who_of(port)}'] = dict(
                    lines=ls_lines(port, ma if port == H else mb, pat)[:3],
                    resync=ls_lines(H, ma, r'resync requested|Synchronising the battle again')[-2:],
                    on=lss(H).get('on'))
        for port in (C, H):
            fresh()
            ma, mb = log_mark(H), log_mark(C)
            e0 = int(lss(H).get('epoch') or 0)
            scmd(port, 'ls nest 8')
            time.sleep(1)
            scmd(H, 'ls resync')
            ls_wait(lambda: synced_after(e0), 120, f'the resync after a nested loop on the {who_of(port)}')
            nl = ls_lines(port, ma if port == H else mb, r'nested message loop|waiting for the outermost|battle loaded '
                                                         r'from slot|battle \d+ epoch \d+ sent')
            done = [i for i, x in enumerate(nl) if 'loop done' in x][:1]
            after = [i for i, x in enumerate(nl) if ('loaded from' in x if port == C else ' sent' in x)][:1]
            res[f'nest {who_of(port)}'] = dict(lines=nl[:5], waited=any('waiting for the outermost' in x for x in nl),
                                               order=bool(done and after and done[0] < after[0]))
        ma, mb = log_mark(H), log_mark(C)
        ls_run_total(H, C, 3, C)
        hc = ls_compare(H, ma, C, mb, 'after the robustness tests')
        log(f'  S7 robust: {res}')
        check('S7 robust: `ls throw` (an exception in MoveAllShips) and `ls stall` (a minute that does not start: the '
              'watchdog) on the client and on the host each end in a resync (the minute marked failed); a resync during '
              'a nested message loop (`ls nest 8`) waits for the outermost loop on the client (load) and on the host '
              '(send); the battle stays live, the next 3 minutes equal',
              all(v['lines'] and v['resync'] and v['on'] == '1' for k, v in res.items() if k[:5] in ('throw', 'stall'))
              and all(v['waited'] and v['order'] for k, v in res.items() if k.startswith('nest'))
              and lss(H).get('on') == '1' and hc['minutes'] >= 3 and not hc['differ'],
              f'{res}; {hc}')
        fresh()
        ma, mb = log_mark(H), log_mark(C)
        e0 = int(lss(H).get('epoch') or 0)
        try:
            scmd(C, 'ls guard 0')
            w = scmd(C, 'ls dispwrite')
            scmd(H, 'ls run 2')
            ls_wait(lambda: ls_lines(H, ma, r'DESYNC at minute') or lss(H).get('on') != '1', 120,
                    'the negative control desyncs')
        finally:  # at once: with the guard off the battle map's own drawing leaks too (a natural desync every minute)
            scmd(C, 'ls guard 1')
        ls_wait(lambda: synced_after(e0), 150, 'the negative control is synchronised again')
        time.sleep(1)
        leaks = ls_lines(C, mb, LS_LEAK_RE)
        LS_EXCUSED.update(leaks)
        des = ls_lines(H, ma, r'DESYNC at minute')
        ma2, mb2 = log_mark(H), log_mark(C)
        ls_run_total(H, C, 2, H)
        hc = ls_compare(H, ma2, C, mb2, 'after the guard is on again')
        late = ls_lines(C, mb2, LS_LEAK_RE)
        check(f'S7 guard negative control: with the sighting guard off on the client (`ls guard 0`) its display write '
              f'({w}) reaches the simulation: DESYNC ({des[:1]}) and a resync; the leak check reports it ({len(leaks)} '
              f'LEAK lines, e.g. "{(leaks or ["-"])[0][:110]}"); guard on again (at the DESYNC): the next 2 minutes '
              f'equal, no LEAK after the resync', des and leaks and hc['minutes'] >= 2 and not hc['differ']
              and not late and lss(H).get('on') == '1', f'{hc}; later LEAK {late[:2]}')
        memory.append(('S7', *mem(ctx)))
        mk = log_mark(H)
        scmd(H, 'ls drop')  # the next case starts a new battle: the client must be back in its slot
        ls_wait(lambda: ls_client_back(ctx), 120, 'the client back after S7')
        ls_wait(lambda: ls_over(H), 30, 'the host alone after S7')
        log('  S7: the host dropped the client to end the case:', ls_lines(H, mk, r'dropping')[:1])

    for name, fn in (('S1 spam + end during a resync / after a drop', s1), ('S2 end during the load + client leaves', s2),
                     ('S3 desync counter reset + resync limit', s3), ('S3 desync storm on the client (B)', lambda: storm(C)),
                     ('S3 desync storm on the host (A)', lambda: storm(H)), ('S4 silent client + client never loads', s4),
                     ('S5 silent host', s5), ('S7 robust: throw / stall / nested loop + guard off', s7)):
        ls_case(ctx, name, fn)
    for p in (H, C):
        scmd(p, 'ls timeout 30 30 120')
    log('  memory (private MB) host / client: ' + '; '.join(f'{n} {h}/{c}' for n, h, c in memory))
    grow = {who: [b[i] - a[i] for a, b in zip(memory, memory[1:]) if a[i] is not None and b[i] is not None]
            for who, i in (('host', 1), ('client', 2))}
    avg = {w: (sum(v[1:]) / len(v[1:]) if len(v) > 1 else None) for w, v in grow.items()}
    check(f'g) {len(memory) - 1} live battles in a row: private memory of host and client grows by at most '
          f'{MEM_GROWTH_MAX} MB per battle (average after the first; host {avg["host"]}, client {avg["client"]} MB)',
          len(memory) >= 6 and all(v is not None and v <= MEM_GROWTH_MAX for v in avg.values()),
          ' | '.join(f'{n} {h}/{c}' for n, h, c in memory))
    ls_log_scan('g) after the repeated battles', marks0)
    ls_stress_part2(ctx, memory)


def ls_stress_part2(ctx, memory):
    ans = ls_ans(ctx)

    def s6():
        ls_start_test(ctx, 'S6')
        scmd(H, 'ls speed 3')
        ls_run_total(H, C, 2, H)
        ls_wait(lambda: ls_idle_equal(H, C), 60, 'a boundary')
        hm, t = log_mark(H), time.time()
        ls_kill(ctx, 'client')
        ls_wait(lambda: ls_over(H) and ls_lines(H, hm, r'left the live battle'), 60, 'the host drops the killed client')
        dt = time.time() - t
        m0 = int(pst(H).get('min') or 0)
        scmd(H, 'pocrun 2')
        ps = ls_wait(lambda: (lambda p: p if int(p.get('min') or 0) >= m0 + 1 and p.get('running') == '0'
                              and p.get('moving') == '0' else None)(pst(H)), 120, 'the host runs 2 minutes alone')
        check(f'S6 a) the client\'s game killed at a boundary (taskkill /F): the host drops it after {dt:.1f} s ("dropping '
              f'Captain: Captain disconnected", "The AI commands Germany now") and its battle goes on (minute {m0} -> '
              f'{ps.get("min")})', dt < 60 and ls_lines(H, hm, r'dropping Captain: Captain disconnected'),
              ls_lines(H, hm, r'dropping|left the live')[:2])
        ls_relaunch(ctx, 'client', tag='kill')
        hm2 = log_mark(H)
        scmd(C, 'fightbattles 1')
        rj = cmd(C, f'join {CLIENT_NAME} 127.0.0.1 {MP_PORT}')
        time.sleep(15)
        during = sstatus(C)
        closed = ls_finish_host(ctx)
        given = wait_log(H, hm2, r'state seq \d+ -> Captain \(nation 1,', 25)
        claimed = None
        if not given:
            claimed = dict(sstatus(C), claim1=cmd(C, 'claim 1'))
            time.sleep(8)
            claimed['by_index_gave'] = bool(wait_log(H, hm2, r'state seq \d+ -> Captain \(nation 1,', 2))
            if not claimed['by_index_gave']:
                log(f'  `claim 1` gave nothing (the lobby list of the returning client: {cmd(C, "diploview")[:160]}); '
                    f'claim {nation1()} ->', cmd(C, f'claim {nation1()}'))
            given = wait_log(H, hm2, r'state seq \d+ -> Captain \(nation 1,', 60)
        st = wait(C, lambda s: s.get('phase') == 'planning' and s.get('myNation') == '1' and s.get('slot') ==
                  str(CLIENT_SLOT), 90, 'the client plans nation 1 again')
        back = rejoin_back(ctx, hm2, during)
        check('S6 a) the client started again and joined while the host\'s battle was still open: it gets nation 1 back '
              '(0.3.0: at once by the hello, `myNation` 1 while the battle is open and "* Captain joined as <nation>", or '
              'by the host\'s `reservation: Captain gets nation 1 back` and "* Captain is back as <nation>") and its '
              'month, without claiming it again',
              rj == 'ok' and closed and given and claimed is None and st.get('playerIdx') == '1' and back['ok'],
              f'join {rj}; during the battle myNation {during.get("myNation")} phase {during.get("phase")}; '
              f'{"CLAIMED (" + str(claimed.get("myNation")) + ", " + str(claimed.get("phase")) + ", claim 1 gave " + str(claimed.get("by_index_gave")) + ")" if claimed else "given"}'
              f'; {given[:1]}; {back}')

    def f_long():
        s = ls_start_test(ctx, 'f long')
        n_ai = ls_all_ai(H, C)  # the game's AI seeks contact and fights it out: the battle reaches one of its own ends
        log(f'  f) {n_ai} divisions on the AI (ordered from their own copies)')
        r = ls_drive_end(H, C, 'game', cap=1200)
        hc = ls_compare(H, s['hm'], C, s['cm'], 'long battle')
        ans.view_results.update(('host', 'client'))
        ls_wait(lambda: not battle_up(ctx.hp) and ls_client_back(ctx), 240, 'both battle screens closed', step=1)
        ends = ls_lines(H, s['hm'], r'lockstep: The battle is over \(|live battle ended \(') + \
            ls_lines(C, s['cm'], r'lockstep: The battle is over \(|live battle ended \(')
        game_end = ls_lines(H, s['hm'], LS_GAME_END_RE)
        how = (game_end or ['agreed end after the cap' if r['tries'] else '?'])[-1][:80]
        check(f'f) a long battle at speed 5 to its end: minute {r["minute"]} in {r["seconds"]} s, {how} (end requests '
              f'{r["tries"]}, refused {r["refusals"]}, proposed by the game {r["proposals"]}); every minute equal on both '
              f'copies ({hc["minutes"]} compared), no DESYNC; both closed it, the client back in slot 77',
              r['minute'] >= 400 and hc['minutes'] >= r['minute'] - 5 and not hc['differ'] and ends
              and not ls_lines(H, s['hm'], 'DESYNC'), f'{hc}; {ends[-3:]}')
        ls_quiet(ctx)
        run_month(ctx, 'lockstep_stress f')
        return how

    ls_case(ctx, 'S6 client killed at a boundary', s6)
    ls_quiet(ctx)
    ls_after_checks(ctx, 'S6')
    ls_case(ctx, 'f) long battle to the game\'s own end + month', f_long)
    ls_quiet(ctx)
    ls_after_checks(ctx, 'f)')

    # real months: the host at war with nation 1
    def months_until(tag, mode, want_kind, relaunch_env=None, n=6):
        recs = []
        for i in range(1, n + 1):
            if rel(0, 1).get('war') != '1':
                ls_war01(ctx)
            rec = ls_month(ctx, f'lockstep_stress {tag}{i}', modes=[mode], relaunch_env=relaunch_env)
            recs.append(rec)
            kind = ls_host_battle(rec)
            log(f'  {tag}{i}: host battle against nation 1: {kind}')
            if kind != 'none':
                return rec, kind, recs
        return None, 'none', recs

    def p1():
        ls_quiet(ctx)
        ls_rejoin(ctx, fight=False)
        hs = lsn(H)
        r01 = ls_war01(ctx)
        rec, kind, recs = months_until('P1 M', 'agree', 'ai')
        check(f'P1 i) the client opted out (`fightbattles 0`, host lists Captain(1,ls) without b); the host at war with '
              f'it; a host battle against nation 1 within {len(recs)} month(s)', rec is not None and r01.get('war') == '1'
              and 'Captain(1,ls)' in hs.get('players', ''), f'{hs.get("players")} {[(r["date0"], ls_host_battle(r)) for r in recs]}')
        if rec:
            hl = log_since(H, rec['hmark'])
            ar = ls_answered(ans, 'host', rec['t0'], 'OK [Yes]')
            check('P1 i) the host fights it against the AI as before ("battle against Captain: fought against the AI (the '
                  'player doesn\'t fight battles)", no lockstep start, the client never in a battle; the host\'s battle on '
                  'auto-run); its month completes and reaches the client', kind == 'ai' and LS_HOLD not in hl
                  and not rec['live'] and ar and rec['date1'] != rec['date0']
                  and "the player doesn't fight battles" in hl, f'{log_lines(hl, "fought against the AI")[:1]} {ar[:1]}')

    def p2():
        ls_quiet(ctx)
        if lsn(C).get('enabled') != '1':
            scmd(C, 'ls enable 1')
        ls_rejoin(ctx, fight=True)
        rec, kind, recs = months_until('P2 M', 'killB', 'live', relaunch_env={'RTW3MP_LIVE_BATTLES': '0'})
        check(f'P2 a) a live host battle against nation 1 in a real month within {len(recs)} month(s)',
              rec is not None and kind == 'live' and rec['live'], [(r['date0'], ls_host_battle(r)) for r in recs])
        if not rec or not rec['live']:
            return
        b = rec['live'][0]
        ar = ls_answered(ans, 'host', rec['t0'], 'OK [Yes]')
        check(f'P2 a) the client killed in the middle of a minute (minute {b.get("kill_minute")}, taskkill /F): the host '
              f'drops it after {b.get("drop_s")} s ("Captain disconnected"; the AI commands Germany), its battle goes on '
              f'(auto-run) and its month completes ({rec["date0"]} -> {rec["date1"]})', b.get('drop_s') is not None
              and b['drop_s'] < 60 and any('Captain disconnected' in x for x in b.get('drop', [])) and ar
              and rec['date1'] != rec['date0'] and not b.get('error'), f'{b.get("drop")} {ar[:1]} {b.get("error")}')
        st = sstatus(C)
        dur = b.get('rejoin_during') or {}
        back = rejoin_back(ctx, b.get('rejoin_mark') or rec['hmark'], dur)
        check('P2 a) the client started again (RTW3MP_LIVE_BATTLES=0) joined while the host\'s battle ran: it gets its '
              'nation back (0.3.0: by the hello during the session or the host\'s `reservation: Captain gets nation 1 '
              'back`, with its chat line) and the new month without claiming it again; it plans nation 1 in slot 77',
              b.get('join') == 'ok' and not rec['claimed'] and st.get('myNation') == '1'
              and st.get('slot') == str(CLIENT_SLOT) and st.get('date') == rec['date1'] and back['ok'],
              f'during the battle: myNation {dur.get("myNation")} phase {dur.get("phase")}; after the month: '
              f'{"CLAIMED " + str((rec["claimed"] or {}).get("myNation")) if rec["claimed"] else "given"}; {st}; {back}')
        rolls = roll_lines(log_since(C))
        check('P2 a) the restarted client\'s month loads (the event roll of the new month logged)', rolls, rolls)

    def p3():
        cs, hs = lsn(C), lsn(H)
        rec, kind, recs = months_until('P3 M', 'agree', 'ai')
        check('P3 i) the client runs with RTW3MP_LIVE_BATTLES=0 (`ls session` enabled=0; the host lists Captain(1,b) '
              f'without ls); a host battle against nation 1 within {len(recs)} month(s)', rec is not None
              and cs.get('enabled') == '0' and 'Captain(1,b)' in hs.get('players', ''),
              f'{cs.get("enabled")} {hs.get("players")} {[(r["date0"], ls_host_battle(r)) for r in recs]}')
        if rec:
            hl = log_since(H, rec['hmark'])
            check('P3 i) the host fights it against the AI ("fought against the AI (no live battles on the player\'s '
                  'PC)", no lockstep start); its month completes and reaches the client', kind == 'ai'
                  and 'no live battles on the player' in hl and LS_HOLD not in hl and rec['date1'] != rec['date0'],
                  log_lines(hl, 'fought against the AI')[:1])

    for name, fn in (('P1 opt-out fightbattles 0 in a real month', p1),
                     ('P2 client killed mid-minute in a real month', p2),
                     ('P3 opt-out RTW3MP_LIVE_BATTLES=0 in a real month', p3)):
        ls_case(ctx, name, fn)

    def p4():
        ls_quiet(ctx)
        scmd(C, 'ls enable 1')
        ls_rejoin(ctx, fight=True)
        r01 = ls_peace01(ctx)
        hs = lsn(H)
        check('P4: live battles on again (`ls enable 1`, rejoin: Captain(1,b,ls)); peace 0-1 (compromise)',
              'Captain(1,b,ls)' in hs.get('players', '') and r01.get('war') == '0', f'{hs.get("players")} {r01}')
        hm = log_mark(H)
        run_month(ctx, 'lockstep_stress after the war')
        check('P4: the next normal month works (no host battle)', LS_HOLD not in log_since(H, hm), '')

    ls_case(ctx, 'P4 live again, peace, normal month', p4)
    ls_quiet(ctx)
    ls_after_checks(ctx, 'P4')
    ls_log_scan('P1-P4', {H: 0, C: 0})
    log('  memory (private MB) host / client: ' + '; '.join(f'{n} {h}/{c}' for n, h, c in memory + [('end', *mem(ctx))]))
    ls_case(ctx, 'b) host killed in a battle', lambda: ls_host_kill(ctx))
    ls_quiet(ctx)
    ls_after_checks(ctx, 'b)')


def ls_host_kill(ctx):
    """b) The host's game killed during a minute of a live battle: the client leaves the battle at once (connection
    lost), is back in slot 77, not stuck (battle slot mode off, strategic screen enabled, its Turn button usable), can
    leave and join a new session: slot 1 is put back from this run's backup (the killed host had saved its battle into
    it), the host started again, a new session (claim 1, start), one month."""
    ls_start_test(ctx, 'b) host kill')
    scmd(H, 'ls speed 3')
    ls_run_total(H, C, 2, H)
    scmd(H, 'ls speed 0')
    ls_run(H, C, 1, H, timeout=180)
    scmd(H, 'ls run 5')
    ls_wait(lambda: lss(C).get('running') == '1', 60, 'the client in the middle of a minute')
    cm, t = log_mark(C), time.time()
    ls_kill(ctx, 'host')
    st = ls_wait(lambda: (lambda s: s if s.get('role') == 'none' and ls_client_back(ctx) else None)(sstatus(C)), 90,
                 'the client back in slot 77 without a host')
    dt = time.time() - t
    cl = ls_lines(C, cm, r'The live battle ended on this PC|live battle: leaving the battle slot|'
                         rf'LoadCampaign slot {CLIENT_SLOT} player 1 -> ok|Disconnected')
    cs, tb, en = cstate(), turn_button(ctx.cp), strategic_enabled(ctx.cp)
    check(f'b) the host\'s game killed during a minute: the client leaves the battle after {dt:.1f} s ("The live battle '
          f'ended on this PC: ..."), is back in slot 77 as nation 1, not stuck: battle slot mode off, strategic screen '
          f'enabled, its Turn button visible and enabled', dt < 60 and st and len(cl) >= 3 and cs.get('slotMode') is False
          and en and tb and tb[1] and tb[2], f'{cl} | {cs.get("raw")} | turn {tb} enabled {en}')
    rl = scmd(C, 'leave')
    ok = restore_slot1_copy()
    check('b) slot 1 put back from this run\'s backup (the killed host had saved its battle into it)', ok, '')
    ls_relaunch(ctx, 'host', tag='hostkill')
    hm = log_mark(H)
    cmd(H, f'load {HOST_SLOT} 0')
    wait(H, lambda s: s.get('playerIdx') == '0' and s.get('startBuild') == '2', 120, 'host campaign')
    r = cmd(H, f'host {HOST_NAME} {MP_PORT} 0')
    scmd(C, 'fightbattles 1')
    rj = cmd(C, f'join {CLIENT_NAME} 127.0.0.1 {MP_PORT}')
    time.sleep(3)
    rc = cmd(C, 'claim 1')
    time.sleep(1.5)
    cmd(H, 'start')
    st = wait(C, lambda s: s.get('phase') == 'planning' and s.get('playerIdx') == '1' and
              s.get('slot') == str(CLIENT_SLOT), 120, 'client planning as nation 1')
    cmd(H, f'reserve {STANDIN} Bot')
    cmd(C, 'mpwindow')
    time.sleep(1)
    ctx.intel_prev = intel_snapshot(ctx, hm, 'session restart')
    hs = lsn(H)
    check(f'b) the client leaves ("{rl}") and joins a new session of the host started again ({r}): join {rj}, claim '
          f'{rc}, it plans nation 1 in slot 77 ({st.get("date")}); the host lists Captain(1,b,ls)',
          r == 'ok' and rj == 'ok' and rc == 'ok' and 'Captain(1,b,ls)' in hs.get('players', ''), hs.get('players'))
    ls_quiet(ctx)
    run_month(ctx, 'lockstep_stress after the host restart')


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
    ('research', 0, 'a joined player\'s Research screen (0.2.8): the Research button opens the player\'s own nation, not '
                    'the host\'s (log line); a priority (popup menu, posted keys) and the budget changed there reach '
                    'nation 1 only and the host after the month; > 12 % refused by the game\'s message; the host\'s '
                    'own screen unchanged (nation 0). Checked after the next processed month (one own month if no '
                    'later scenario processes any)', scenario_research),
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
    ('battles', 3, 'joined players fight their battles against AI nations on their own PC: an injected battle (war '
                   '1-J by settension) is deferred to the battle phase after the month (save, strategic screen '
                   'locked, sent to the player), fought in the client\'s battle slot (Game87, nation 0 = the player; '
                   'the game\'s preview, battle screen on auto-run, results, history prompt; DoStrategicTurn skipped) '
                   'and merged on the host (ships cross-checked with the battle slot\'s save, VP, prestige, counts); a '
                   'declined battle and one left past `battles timeout 20` are resolved by the game (late answer '
                   'ignored); a client that joined with `fightbattles 0` gets nothing deferred (one more month if '
                   'the game finds no battle; the war ends afterwards)', scenario_battles),
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
    ('lockstep', 1, 'live player-vs-player battles, host vs the joined player (short): a test battle (`pocbattle 1 0`, '
                    'battle slot mode) starts live on both copies (preview, hold, save, send, load in slot 87), runs '
                    'from both sides with equal per-minute hashes, orders of both sides, a detach replayed, display '
                    'writes of the sighting fields kept out of the simulation (`ls dispwrite`), an injected desync '
                    'resynced, an end refused before minute 400, the host drops the client (AI takes side 1, the client '
                    'back in slot 77 with its unsubmitted plans), the host finishes alone without a month; one normal '
                    'month (the plans reach the host); no lockstep error and no LEAK in the logs (the leak check is on in '
                    'every live battle of the suite; joins the session again if needed)',
     scenario_lockstep),
    ('lockstep_pvp', 1, 'live battle between two joined players (a third copy joins as "Bot", nation 2, slot 76): war '
                        '1-2, `battles injectpvp`, months (up to 3) until the battle phase fights it live (attacker A, '
                        'defender B, the host relays), equal hashes, an order of B; default: 10 live minutes, then A '
                        'drops B and finishes on auto-run; --stress: to the agreed end plus an idea-1 battle in the same '
                        'month; result merged, the new month on all three copies; the third copy leaves and is closed',
     scenario_lockstep_pvp),
    ('lockstep_month', 2, 'the host\'s battle against the joined player inside a REAL processed month: the host at war '
                          'with nation 1, months (up to 8) until CheckForBattles2 starts it; preview, lockstep start, '
                          'live battle (default: 12 live minutes, then the host drops the client and finishes against '
                          'the AI; --stress: two battles, the first ended by agreement, the second by the game itself '
                          'inside a minute (`ls endsoon`: the screen closes after the minute, normal end on both, the '
                          'host\'s month with its own dialogs), idea-1 battles in the second\'s months (each merge '
                          'checked against the battle slot), FormClose -> DoStrategicTurn -> DoEndOfTurn, the new month '
                          'to the client; '
                          'ships, losses (step by step) and VP of both nations equal in the host\'s save and the '
                          'client\'s month, the client\'s plans kept; peace and a normal month', scenario_lockstep_month),
    ('lockstep_stress', 6, '--stress only: spam of run/pause/speed/order, end requests during a resync / the load / '
                           'after a drop, the resync counter, desync storms on the client and on the host (the resync '
                           'limit, the RNG part equal after each resync), silent client / host, a client that never '
                           'loads, an exception / a stalled minute / a nested message loop on each copy, the guard\'s '
                           'negative control, 9 live battles in a row (memory, logs, LEAK), the client killed at a '
                           'boundary and in the middle of a minute of a real month (it joins again during the host\'s '
                           'battle and gets nation 1 back), a long battle to its end, the '
                           'opt-outs fightbattles 0 and RTW3MP_LIVE_BATTLES=0 in real months, the host killed in a '
                           'battle (client not stuck, new session from the restored slot 1)', scenario_lockstep_stress),
]
STRESS_ONLY = ('lockstep_stress',)  # not in the default run: --stress or --only


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


def preflight(g1, copy_slots=(), k2_slots=()):
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
    for s in k2_slots:  # lockstep_pvp's second joined player (its slot and battle slot): deleted after the run
        if os.path.exists(slot_dir(s)):
            problems.append(f'save slot {s} exists ({slot_dir(s)}); lockstep_pvp\'s third copy would overwrite and '
                            f'delete it: move it away first')
    if os.path.exists(slot_dir(BATTLE_SLOT)):  # the client's battle slot: written by the mod, deleted after the run
        problems.append(f'save slot {BATTLE_SLOT} exists ({slot_dir(BATTLE_SLOT)}); the client\'s battles would '
                        f'overwrite it and the run deletes it: move it away first')
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


def mod_info(dist=None):
    """Log whether the installed mod matches the build in `dist` (default: the repo's dist\\) and the host's version
    line."""
    dist = dist or os.path.join(REPO, 'dist')
    inst = os.path.join(ctl.GAME, 'RTW3MP.dll')
    built = os.path.join(dist, 'RTW3MP.dll')
    if os.path.exists(built):
        same = file_hash(inst) == file_hash(built) and \
            file_hash(os.path.join(ctl.GAME, 'version.dll')) == file_hash(os.path.join(dist, 'version.dll'))
        log('installed mod', f'matches {dist}' if same else f'DIFFERS from {dist} (install the build first?)')
    else:
        log('installed mod: no build in', dist)
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
    return remove_slot(CLIENT_SLOT)


def remove_slot(slot):
    d = os.path.join(SAVE, f'Game{slot}')
    for _ in range(10):
        shutil.rmtree(d, ignore_errors=True)
        if not os.path.exists(d):
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


def summary(names, open_s, ctx, saves_ok, left, ev_left=(), copies_left=(), battle_left=False):
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
          + (f'; copy slots LEFT: {list(copies_left)}' if copies_left else '')
          + (f'; battle slot Game{BATTLE_SLOT} LEFT' if battle_left else ''))
    if LS_MATRIX:
        print('  live battle cases (checks passed/checks, seconds, note):')
        for case, n_ok, n, secs, note in LS_MATRIX:
            print(f'    {case:<52} {n_ok:>2}/{n:<2} {"PASS" if n_ok == n else "FAIL"} {secs:>5} s  {note[:90]}')
    if KEPT_LOGS:
        print('  logs of copies started again:', ', '.join(os.path.basename(p) for p in KEPT_LOGS))
    n = len(results)
    print(f'{n - len(failed)}/{n} checks passed', flush=True)
    return not failed


def main(argv=None):
    ap = argparse.ArgumentParser(description='RTW3 Multiplayer regression suite (two live game copies).')
    ap.add_argument('--list', action='store_true', help='list the scenarios and exit')
    ap.add_argument('--only', help='comma-separated scenarios to run (session is always included)')
    ap.add_argument('--dist', default=os.environ.get('RTW3_DIST'),
                    help='folder of the build the installed mod is compared with (default: env RTW3_DIST, else the '
                         'repo\'s dist\\); the runner never installs')
    ap.add_argument('--stress', action='store_true',
                    help='the long live-battle variants (lockstep_pvp and lockstep_month to their end, two real-month '
                         'battles) and the lockstep_stress scenario (bad game states; ~40 min more)')
    a = ap.parse_args(argv)
    if a.list:
        print(f'{"scenario":<16}{"months":<8}what it guards')
        for name, months, what, _ in SCENARIOS:
            print(f'{name:<16}{months:<8}{what}')
        print(f'default: all but {", ".join(STRESS_ONLY)} (--stress), in this order '
              f'({sum(s[1] for s in SCENARIOS if s[0] not in STRESS_ONLY)} months processed)')
        return 0
    OPTS['stress'] = a.stress
    names = [s[0] for s in SCENARIOS if s[0] not in STRESS_ONLY or a.stress]
    if a.only:
        want = [w.strip() for w in a.only.split(',') if w.strip()]
        bad = [w for w in want if w not in [s[0] for s in SCENARIOS]]
        if bad:
            ap.error(f'unknown scenario(s) {bad}; known: {[s[0] for s in SCENARIOS]}')
        names = [s[0] for s in SCENARIOS if s[0] in want or s[0] == 'session']
    g1 = os.path.join(SAVE, f'Game{HOST_SLOT}')
    copy_slots = ()
    if 'player_copy' in names:
        _button_slot[0] = next((s for s in BUTTON_SLOTS if not os.path.exists(slot_dir(s))), None)
        copy_slots = COPY_SLOTS + ((_button_slot[0],) if _button_slot[0] else ())
    k2_slots = (K2_SLOT, K2_SLOT + 10) if 'lockstep_pvp' in names else ()
    problems = preflight(g1, COPY_SLOTS if copy_slots else (), k2_slots)
    for p in problems:
        log('cannot start:', p)
    if problems:
        return 2
    stale = remove_event_files('before the run')
    if stale:
        log('cannot start: event cooldown files that cannot be deleted:', stale)
        return 2
    for fn in os.listdir(LOG_DIR) if os.path.isdir(LOG_DIR) else []:  # kept logs of an earlier run's restarted copies
        if re.match(r'^rtw3mp_\d+\.(kill|hostkill|restart)\d+\.log$', fn):
            os.remove(os.path.join(LOG_DIR, fn))
    shutil.copytree(g1, BACKUP)
    log('backed up save slot', HOST_SLOT, '| scenarios:', ', '.join(names))
    ctx, procs, t_open = Ctx(), PROCS, time.time()
    ctx.fixture = saved_intel(save_path(HOST_SLOT))
    saves_ok, left, ev_left, copies_left, battle_left = False, [], [], [], False
    try:
        procs.append(launch(H))
        time.sleep(4)
        procs.append(launch(C, CLIENT_SLOT, {'RTW3MP_EVENT_CHANCE': str(ctx.event_chance)}))
        ctx.hp, ctx.cp = procs[0].pid, procs[1].pid
        log('started host PID', ctx.hp, 'client PID', ctx.cp)
        _scenario[0] = 'session'
        up = {port: wait_bridge(port) for port in (H, C)}
        if check('both copies answer on the test bridge', all(up.values()), up):
            mod_info(a.dist)
            run_scenarios(ctx, names)
        else:
            for n in names[1:]:
                _scenario[0] = n
                check(f'{n}: run', False, 'not run: the game did not start')
    except KeyboardInterrupt:
        check('suite: completed', False, 'interrupted')
    finally:
        if _ANS[0] is not None:
            _ANS[0].stop = True
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
        battle_left = not remove_slot(BATTLE_SLOT)  # preflight made sure it did not exist before the run
        if battle_left:
            log(f'!! could not remove Game{BATTLE_SLOT}')
        k2_left = [s for s in k2_slots if not remove_slot(s)]
        if k2_left:
            log(f'!! could not remove the third copy\'s slots {k2_left}')
        battle_left = battle_left or bool(k2_left)
        copies_left = remove_copy_slots(copy_slots) if copy_slots else []
        ev_left = remove_event_files('after the run')
        if ev_left:
            log('!! could not delete the event cooldown files', ev_left)
    passed = summary(names, open_s, ctx, saves_ok, left, ev_left, copies_left, battle_left)
    return 0 if passed and saves_ok and not left and not ev_left and not copies_left and not battle_left else 1


if __name__ == '__main__':
    sys.exit(main())
