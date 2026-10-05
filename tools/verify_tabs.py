"""Regression check: a client viewing the 'Ships under construction' tab receives a new month without
the game's 'List index out of bounds' error.  python verify_tabs.py"""
import ctypes, ctypes.wintypes as wt, subprocess, sys, time
import rtw3ctl as ctl
from e2e_test import dismiss_dialogs, log, class_name, window_text, u, EnumProc

H, C = 47701, 47702


def pid_windows(pid):
    out = []

    def cb(h, l):
        p = wt.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u.IsWindowVisible(h):
            out.append(h)
        return True

    u.EnumWindows(EnumProc(cb), 0)
    return out


def child_of_class(top, cls):
    found = []
    u.EnumChildWindows(top, EnumProc(lambda h, l: (found.append(h) if class_name(h) == cls else None) or True), 0)
    return found[0] if found else None


def wait_bridge(port):
    for _ in range(60):
        try:
            if ctl.cmd(port, 'ping', timeout=5).startswith('ok'):
                return
        except OSError:
            time.sleep(1)
    raise SystemExit('bridge not up')


hp = int(subprocess.check_output([sys.executable, 'rtw3ctl.py', 'launch', str(H)]).strip())
time.sleep(3)
cp = int(subprocess.check_output([sys.executable, 'rtw3ctl.py', 'launch', str(C), '77']).strip())
wait_bridge(H)
wait_bridge(C)
time.sleep(3)
log('load', ctl.cmd(H, 'load 1 0'))
log('host', ctl.cmd(H, 'host Admiral 47624 0'))
log('join', ctl.cmd(C, 'join Captain 127.0.0.1 47624'))
time.sleep(4)
log('start', ctl.cmd(H, 'start'))
end = time.time() + 90
while time.time() < end:
    st = ctl.status(C)
    if st.get('phase') == 'planning' and st.get('slot') == '77' and st.get('playerIdx') == '1':
        break
    time.sleep(1)
log('client planning', st.get('date'), 'player', st.get('playerIdx'))
camp = [h for h in pid_windows(cp) if class_name(h) == 'TfrmBuildCamp'][0]
tabs = child_of_class(camp, 'TTabControl')
u.PostMessageW(tabs, 0x201, 1, (8 << 16) | 192)
u.PostMessageW(tabs, 0x202, 0, (8 << 16) | 192)
time.sleep(1)
date0 = st.get('date')
log('client submit', ctl.cmd(C, 'submit'))
time.sleep(2)
log('host turn', ctl.cmd(H, 'turn'))
end = time.time() + 300
while time.time() < end:
    for d in dismiss_dialogs(hp):
        log('  host dialog:', d)
    st = ctl.status(C)
    if st.get('phase') == 'planning' and st.get('date') != date0 and st.get('submitted') == '0' and st.get('busy') == '0':
        break
    time.sleep(1.5)
errors = [window_text(h) for h in pid_windows(cp) if class_name(h) == 'TMessageForm']
log('client now', st.get('date'), 'player', st.get('playerIdx'), 'busy', st.get('busy'))
log('client message boxes:', errors or 'none')
log('host pid', hp, 'client pid', cp)
