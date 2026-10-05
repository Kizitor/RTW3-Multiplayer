"""Live lobby test through the real UI (title button, Multiplayer window, Load Game screen).
python live_lobby_test.py"""
import ctypes, ctypes.wintypes as wt, subprocess, sys, time
import rtw3ctl as ctl
import ui_mp as ui

u = ui.u
H, C = 47701, 47702


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def launch(port, slot=None):
    args = [sys.executable, 'rtw3ctl.py', 'launch', str(port)] + ([str(slot)] if slot else [])
    return int(subprocess.check_output(args).strip())


def wait_ready(pid):
    for _ in range(60):
        if ui.windows_of(pid, 'TfrmSelectNation2'):
            tops = ui.windows_of(pid, 'TfrmSelectNation2')
            found = []
            u.EnumChildWindows(tops[0], ui.EnumProc(lambda h, l: (found.append(h) if ui.cls(h) == 'RTW3MP_MenuButton' else None) or True), 0)
            if found:
                return
        time.sleep(1)
    raise SystemExit('title screen/button not ready')


def show(tag, pid):
    print(f'--- {tag}')
    ui.show(pid)


def visible_button(top, caption):
    out = []
    u.EnumChildWindows(top, ui.EnumProc(lambda h, l: (out.append(h) if ui.cls(h) in ('TButton', 'TBitBtn')
                                                       and ui.text(h).replace('&', '') == caption
                                                       and u.IsWindowVisible(h) else None) or True), 0)
    return out[0] if out else None


def load_game_via_title(pid, slot_row=0):
    form = ui.windows_of(pid, 'TfrmSelectNation2')[0]
    panels = []
    u.EnumChildWindows(form, ui.EnumProc(lambda h, l: (panels.append(h) if ui.cls(h) == 'TPanel' and u.IsWindowVisible(h)
                                                       and u.GetParent(h) == form else None) or True), 0)
    pn = panels[0]
    u.PostMessageW(pn, 0x201, 1, (470 << 16) | 304)  # "Load Game"
    time.sleep(0.1)
    u.PostMessageW(pn, 0x202, 0, (470 << 16) | 304)
    time.sleep(2)
    boxes = []
    u.EnumChildWindows(form, ui.EnumProc(lambda h, l: (boxes.append(h) if ui.cls(h) == 'TListBox' and u.IsWindowVisible(h) else None) or True), 0)
    slots = max(boxes, key=lambda h: u.SendMessageW(h, 0x018B, 0, 0))
    u.SendMessageW(slots, 0x0186, slot_row, 0)  # LB_SETCURSEL
    ok = visible_button(form, 'OK')
    log('load: slots', u.SendMessageW(slots, 0x018B, 0, 0), 'OK button', ok)
    u.PostMessageW(ok, 0xF5, 0, 0)


hp = launch(H)
time.sleep(3)
cp = launch(C, 77)
wait_ready(hp)
wait_ready(cp)
log('host pid', hp, 'client pid', cp)
ui.open_mp(hp)
ui.open_mp(cp)
u.PostMessageW(ui.ctl(hp, 'host'), 0xF5, 0, 0)
time.sleep(1.5)
u.SendMessageW(ui.ctl(cp, 'ip'), 0x000C, 0, ctypes.c_wchar_p('127.0.0.1'))
u.SendMessageW(ui.ctl(cp, 'name'), 0x000C, 0, ctypes.c_wchar_p('Friend'))
u.PostMessageW(ui.ctl(cp, 'join'), 0xF5, 0, 0)
time.sleep(3)
show('HOST after hosting (no campaign yet)', hp)
show('CLIENT after joining', cp)

for pid, want in ((hp, 'Great Britain'), (cp, 'Great Britain'), (cp, 'Germany')):
    h = ui.ctl(pid, 'nation')
    items, _ = ui.combo_items(h)
    idx = next(i for i, t in enumerate(items) if t.startswith(want))
    u.SendMessageW(h, 0x014E, idx, 0)
    u.PostMessageW(ui.ctl(pid, 'claim'), 0xF5, 0, 0)
    time.sleep(2)
show('HOST after both picked', hp)
show('CLIENT after both picked', cp)

load_game_via_title(hp, 0)
time.sleep(8)
show('HOST after loading the campaign (title screen > Load Game > Game 1)', hp)
show('CLIENT after host loaded campaign', cp)

u.PostMessageW(ui.ctl(hp, 'start'), 0xF5, 0, 0)
end = time.time() + 90
while time.time() < end:
    st = ctl.status(C)
    if st.get('phase') == 'planning' and st.get('slot') == '77' and st.get('playerIdx') == '1':
        break
    time.sleep(1)
log('client game:', st.get('date'), 'playerIdx', st.get('playerIdx'), 'slot', st.get('slot'))
show('HOST after Start session', hp)
show('CLIENT after Start session', cp)
print('PIDS', hp, cp)
