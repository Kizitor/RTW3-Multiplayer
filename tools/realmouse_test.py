"""Real-mouse test of the Multiplayer window (needs the user's OK: it moves the real mouse for ~30 s).

Covers the reported issues: Start session in the lobby, Give to AI, [x] closing the window.
python realmouse_test.py [idle-seconds-to-wait-for]"""
import ctypes, ctypes.wintypes as wt, subprocess, sys, time
import rtw3ctl as ctl
import ui_mp as ui

u = ui.u
k32 = ctypes.windll.kernel32
H, C = 47701, 47702
IDLE_S = int(sys.argv[1]) if len(sys.argv) > 1 else 45
RESULTS = []


class LII(ctypes.Structure):
    _fields_ = [('cbSize', wt.UINT), ('dwTime', wt.DWORD)]


def idle_s():
    l = LII()
    l.cbSize = ctypes.sizeof(l)
    u.GetLastInputInfo(ctypes.byref(l))
    return (k32.GetTickCount() - l.dwTime) / 1000


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def record(name, ok, detail=''):
    RESULTS.append((name, ok))
    log('OK   ' if ok else 'ISSUE', name, '-', detail)


class Interrupted(Exception):
    pass


_last = [None]


def click(x, y, pause=0.7):
    p = wt.POINT()
    u.GetCursorPos(ctypes.byref(p))
    if _last[0] and (abs(p.x - _last[0][0]) > 3 or abs(p.y - _last[0][1]) > 3):
        raise Interrupted(f'cursor moved by someone else to {p.x},{p.y}')
    u.SetCursorPos(x, y)
    _last[0] = (x, y)
    time.sleep(0.15)
    u.mouse_event(0x0002, 0, 0, 0, 0)
    time.sleep(0.08)
    u.mouse_event(0x0004, 0, 0, 0, 0)
    time.sleep(pause)


def foreground(hwnd):
    u.keybd_event(0x12, 0, 0, 0)
    u.SetForegroundWindow(hwnd)
    u.keybd_event(0x12, 0, 2, 0)
    time.sleep(0.4)


def center(h):
    r = wt.RECT()
    u.GetWindowRect(h, ctypes.byref(r))
    return (r.left + r.right) // 2, (r.top + r.bottom) // 2


def close_button_point(hwnd):
    r = wt.RECT()
    u.GetWindowRect(hwnd, ctypes.byref(r))
    hit = lambda x, y: u.SendMessageW(hwnd, 0x0084, 0, ((y & 0xFFFF) << 16) | (x & 0xFFFF))
    for y in range(r.top + 2, r.top + 40):
        xs = [x for x in range(r.right - 2, r.right - 80, -1) if hit(x, y) == 20]
        if xs:
            return (xs[0] + xs[-1]) // 2, y + 6
    return None


def msgboxes(pid):
    return [w for w in ui.windows_of(pid, '#32770') if u.IsWindowVisible(w)]


def box_button(box, caption):
    found = []
    u.EnumChildWindows(box, ui.EnumProc(lambda h, l: (found.append(h) if ui.cls(h) == 'Button' and ui.text(h).replace('&', '') == caption else None) or True), 0)
    return found[0] if found else None


def answer_box(pid, caption):
    boxes = msgboxes(pid)
    for b in boxes:
        btn = box_button(b, caption)
        if btn:
            click(*center(btn), 1.5)
    return boxes


def pick(pid, prefix):
    combo = ui.ctl(pid, 'nation')
    items, _ = ui.combo_items(combo)
    idx = next(i for i, t in enumerate(items) if t.startswith(prefix))
    foreground(ui.mp_window(pid))
    click(*center(combo), 0.8)  # open the drop-down
    r = wt.RECT()
    u.GetWindowRect(combo, ctypes.byref(r))
    item_h = u.SendMessageW(combo, 0x0154, 0, 0)  # CB_GETITEMHEIGHT
    click(r.left + 40, r.bottom + 4 + idx * item_h + item_h // 2, 0.8)
    return ui.combo_items(combo)


def launch(port, slot=None):
    args = [sys.executable, 'rtw3ctl.py', 'launch', str(port)] + ([str(slot)] if slot else [])
    return int(subprocess.check_output(args).strip())


log(f'waiting until the PC has been idle for {IDLE_S}s (current {idle_s():.0f}s)...')
deadline = time.time() + 1800
while idle_s() < IDLE_S:
    if time.time() > deadline:
        raise SystemExit('PC never idle; real-mouse test not run')
    time.sleep(2)
hp = launch(H)
time.sleep(2)
cp = launch(C, 77)
try:
    for port in (H, C):
        for _ in range(60):
            try:
                if ctl.cmd(port, 'ping', timeout=3).startswith('ok'):
                    break
            except OSError:
                time.sleep(1)
    time.sleep(3)
    ctl.cmd(H, 'host Admiral 47624 0')
    ctl.cmd(C, 'join Captain 127.0.0.1 47624')
    time.sleep(3)
    ctl.cmd(C, 'claim Germany')
    ctl.cmd(H, 'mpwindow')
    time.sleep(2)
    mp = ui.mp_window(hp)
    log('setup: host on the title screen, a player joined and picked Germany')

    # 1) Start session in the lobby before any campaign: clickable, explains, then starts by itself later
    st = ui.ctl(hp, 'start')
    record('Start session is clickable after a player joined (no campaign yet)', bool(u.IsWindowEnabled(st)))
    foreground(mp)
    click(*center(st), 1.2)
    record('Start session explains what to do when no campaign is open', bool(answer_box(hp, 'OK')))
    hint = ui.text(u.GetDlgItem(mp, ui.IDS['status']))
    record('window says the session will start automatically', 'automatically' in hint, hint.replace('\r\n', ' | '))

    # 2) Give to AI before a campaign: removes the player's pick
    items, sel = pick(hp, 'Germany')
    record('real click selects Germany in the drop-down', sel >= 0 and items[sel].startswith('Germany'),
           items[sel] if sel >= 0 else 'nothing')
    rel = ui.ctl(hp, 'release')
    record('Give to AI is clickable', bool(u.IsWindowEnabled(rel)))
    click(*center(rel), 1.2)
    record('Give to AI asks for confirmation', bool(answer_box(hp, 'Yes')))
    items, _ = ui.combo_items(ui.ctl(hp, 'nation'))
    record("Give to AI removed the player's pick", not any(t.startswith('Germany  (') for t in items), str(items[:2]))

    # 3) Player picks again; host loads the campaign -> the requested session starts by itself
    ctl.cmd(C, 'claim Germany')
    time.sleep(2)
    ctl.cmd(H, 'load 1 0')
    s = {}
    end = time.time() + 60
    while time.time() < end:
        s = ctl.status(C)
        if s.get('phase') == 'planning' and s.get('slot') == '77' and s.get('playerIdx') == '1':
            break
        time.sleep(1)
    record('session auto-started when the campaign was ready; player commands Germany',
           s.get('phase') == 'planning' and s.get('playerIdx') == '1', f'{s.get("date")} player {s.get("playerIdx")}')

    # 4) Give to AI during the session, real clicks
    ctl.cmd(H, 'mpwindow')
    time.sleep(1)
    pick(hp, 'Germany')
    click(*center(ui.ctl(hp, 'release')), 1.2)
    answer_box(hp, 'Yes')
    time.sleep(1)
    cs = ctl.status(C)
    record('Give to AI during the session releases Germany', cs.get('myNation') == '-1', f'player myNation={cs.get("myNation")}')

    # 5) Title-bar [x] on the host's and the player's window
    for pid, port, who in ((hp, H, 'host'), (cp, C, 'player')):
        ctl.cmd(port, 'mpwindow')
        time.sleep(1)
        w = ui.mp_window(pid)
        foreground(w)
        click(*close_button_point(w), 1.5)
        record(f"[x] closes the {who}'s Multiplayer window", not u.IsWindowVisible(w))
except Interrupted as e:
    log('ABORTED:', e)
finally:
    for p in (hp, cp):
        subprocess.run(['taskkill', '/PID', str(p), '/F'], capture_output=True)
    log('games closed')
print('\nSUMMARY')
for n, ok in RESULTS:
    print('  OK    ' if ok else '  ISSUE ', n)
