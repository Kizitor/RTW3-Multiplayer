"""Reproduce reported issues (current build), one game run:
  1) Start session greyed out after a player joined (host starts a New Game through the title screen)
  2) The Multiplayer window's [x] does not close it
  3) 'Give to AI' does nothing
Uses posted window messages only (never the real mouse/keyboard).  python repro_issues.py"""
import ctypes, ctypes.wintypes as wt, subprocess, sys, time, threading
import rtw3ctl as ctl
import ui_mp as ui
from e2e_test import dismiss_dialogs

u = ui.u
H, C = 47701, 47702
u.WindowFromPoint.restype = wt.HWND
RESULTS = []


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def record(name, ok, detail):
    RESULTS.append((name, ok, detail))
    log(('OK   ' if ok else 'ISSUE'), name, '-', detail)


def launch(port, slot=None):
    args = [sys.executable, 'rtw3ctl.py', 'launch', str(port)] + ([str(slot)] if slot else [])
    return int(subprocess.check_output(args).strip())


def enabled(pid, key):
    return bool(u.IsWindowEnabled(ui.ctl(pid, key)))


def state(pid):
    w = ui.mp_window(pid)
    return {k: enabled(pid, k) for k in ('start', 'release', 'claim')} | {
        'status': ui.text(u.GetDlgItem(w, ui.IDS['status'])).replace('\r\n', ' | ')}


def children(top, klass=None):
    out = []
    u.EnumChildWindows(top, ui.EnumProc(lambda h, l: (out.append(h) if (klass is None or ui.cls(h) == klass) else None) or True), 0)
    return out


def click_panel(panel, x, y):
    u.PostMessageW(panel, 0x201, 1, (y << 16) | x)
    time.sleep(0.1)
    u.PostMessageW(panel, 0x202, 0, (y << 16) | x)


def visible_panel(form):
    return [h for h in children(form, 'TPanel') if u.IsWindowVisible(h) and u.GetParent(h) == form]


def button(top, caption):
    for h in children(top):
        if ui.cls(h) in ('TButton', 'TBitBtn') and ui.text(h).replace('&', '') == caption and u.IsWindowVisible(h):
            return h
    return None


def new_game_via_title(pid, slot_row):
    form = ui.windows_of(pid, 'TfrmSelectNation2')[0]
    click_panel(visible_panel(form)[0], 107, 470)  # New Game
    time.sleep(2)
    click_panel(visible_panel(form)[0], 394, 456)  # 1900
    time.sleep(3)
    pn = visible_panel(form)[0]
    pnmap = [h for h in children(pn, 'TPanel') if u.IsWindowVisible(h) and u.GetParent(h) == pn]
    pnmap = max(pnmap, key=lambda h: len(children(h)))
    click_panel(pnmap, 74, 133)  # Great Britain
    time.sleep(1.5)
    u.PostMessageW(button(form, 'OK'), 0xF5, 0, 0)
    time.sleep(3)
    boxes = [h for h in children(form, 'TListBox') if u.IsWindowVisible(h)]
    slots = max(boxes, key=lambda h: u.SendMessageW(h, 0x018B, 0, 0))
    u.SendMessageW(slots, 0x0186, slot_row, 0)
    u.PostMessageW(button(form, 'OK'), 0xF5, 0, 0)


def campaign_turn_button(pid):
    camp = ui.windows_of(pid, 'TfrmBuildCamp')
    if not camp or not u.IsWindowVisible(camp[0]):
        return None, None
    for cap in ('Start', 'Continue', 'Turn'):
        b = button(camp[0], cap)
        if b:
            return b, cap
    return None, None


def x_button_test(hwnd, label):
    """Replay a title-bar close click: NC button down on HTCLOSE, then button up, as Windows posts them."""
    r = wt.RECT()
    u.GetWindowRect(hwnd, ctypes.byref(r))
    sx, sy = r.right - 14, r.top + 12  # centre of the close button
    pt = wt.POINT(sx, sy)
    ht = u.SendMessageW(hwnd, 0x0084, 0, (sy << 16) | (sx & 0xFFFF))  # WM_NCHITTEST
    cp = wt.POINT(sx, sy)
    u.ScreenToClient(hwnd, ctypes.byref(cp))
    u.PostMessageW(hwnd, 0x00A1, ht, (sy << 16) | (sx & 0xFFFF))  # WM_NCLBUTTONDOWN
    time.sleep(0.4)
    u.PostMessageW(hwnd, 0x0202, 0, ((cp.y & 0xFFFF) << 16) | (cp.x & 0xFFFF))  # WM_LBUTTONUP
    time.sleep(1.0)
    return ht, bool(u.IsWindowVisible(hwnd))


def control_window():
    """A plain Win32 window on its own thread with a normal message loop (proves the replay works)."""
    import tkinter as tk
    holder = {}

    def run():
        root = tk.Tk()
        root.title('control')
        root.geometry('300x120+100+100')
        root.update()
        holder['hwnd'] = u.GetParent(root.winfo_id()) or root.winfo_id()
        root.protocol('WM_DELETE_WINDOW', lambda: (holder.__setitem__('closed', True), root.destroy()))
        root.mainloop()

    t = threading.Thread(target=run, daemon=True)
    t.start()
    while 'hwnd' not in holder:
        time.sleep(0.1)
    time.sleep(0.5)
    return holder


hp = launch(H)
time.sleep(3)
cp = launch(C, 77)
for p in (hp, cp):
    for _ in range(60):
        if ui.windows_of(p, 'TfrmSelectNation2') and ui.open_mp(p):
            break
        time.sleep(1)
log('host', hp, 'client', cp)
u.PostMessageW(ui.ctl(hp, 'host'), 0xF5, 0, 0)
time.sleep(1.5)
log('host, nobody joined yet:', state(hp))
u.SendMessageW(ui.ctl(cp, 'ip'), 0x000C, 0, ctypes.c_wchar_p('127.0.0.1'))
u.PostMessageW(ui.ctl(cp, 'join'), 0xF5, 0, 0)
time.sleep(3)
log('host, after a player joined:', state(hp))
h = ui.ctl(cp, 'nation')
items, _ = ui.combo_items(h)
u.SendMessageW(h, 0x014E, next(i for i, t in enumerate(items) if t.startswith('Germany')), 0)
u.PostMessageW(ui.ctl(cp, 'claim'), 0xF5, 0, 0)
time.sleep(2)

# --- 1) New Game through the title screen, watching Start session at every step
new_game_via_title(hp, 1)  # slot "Game 2"
seen = []
end = time.time() + 240
while time.time() < end:
    b, cap = campaign_turn_button(hp)
    for d in dismiss_dialogs(hp):
        log('  host dialog:', d)
    if b:
        st = state(hp)
        if not seen or seen[-1][0] != cap:
            seen.append((cap, st['start'], st['status']))
            log(f'campaign button "{cap}": Start session enabled={st["start"]} | {st["status"]}')
        if cap == 'Turn':
            time.sleep(2)
            break
        u.PostMessageW(b, 0xF5, 0, 0)  # press Start / Continue like the host would
        time.sleep(2)
    time.sleep(1)
final = state(hp)
record('Start session clickable once the campaign is ready (button says Turn)', final['start'], final['status'])
record('Start session clickable during campaign setup (Start/Continue)', all(s[1] for s in seen if s[0] != 'Turn'),
       'states: ' + ', '.join(f'{c}={e}' for c, e, _ in seen))

# --- 3) Give to AI
cb = ui.ctl(hp, 'nation')
items, sel = ui.combo_items(cb)
gi = next((i for i, t in enumerate(items) if t.startswith('Germany')), -1)
u.SendMessageW(cb, 0x014E, gi, 0)
log('host nation list', items, 'selected', gi, 'Give to AI enabled', enabled(hp, 'release'))
u.PostMessageW(ui.ctl(hp, 'release'), 0xF5, 0, 0)
time.sleep(1.5)
boxes = [w for w in ui.windows_of(hp, '#32770') if u.IsWindowVisible(w)]
record('Give to AI asks for confirmation', bool(boxes), f'{len(boxes)} confirmation box(es)')
for w in boxes:
    yes = [c for c in children(w, 'Button') if ui.text(c).replace('&', '') == 'Yes']
    if yes:
        u.PostMessageW(yes[0], 0xF5, 0, 0)
time.sleep(2)
items, _ = ui.combo_items(ui.ctl(hp, 'nation'))
cs = ctl.status(C)
record('Give to AI releases the nation', not any(t.startswith('Germany  (') for t in items),
       f'host list {items[:3]}... client myNation={cs.get("myNation")}')

# --- 2) [x] on the Multiplayer window (control first)
ctrl = control_window()
ht, vis = x_button_test(ctrl['hwnd'], 'control')
log('control window: hit-test', ht, 'closed', ctrl.get('closed', False))
mp = ui.mp_window(hp)
u.ShowWindow(mp, 5)
time.sleep(0.5)
ht, still_visible = x_button_test(mp, 'mp')
record('[x] closes the Multiplayer window', not still_visible, f'hit-test {ht} (20=close button), still visible={still_visible}')

print('\nSUMMARY')
for n, ok, d in RESULTS:
    print(('  OK    ' if ok else '  ISSUE '), n)
print('PIDS', hp, cp)
