"""End-to-end multiplayer test with two local game instances (host + client) driven via the test bridge.

  python e2e_test.py [--fresh]

Requires: Steam running, the mod installed, a campaign in save slot 1 (host plays nation 0).
Host bridge 47701, client bridge 47702 (client slot 77). Dialogs are dismissed with posted messages,
so the user's mouse and keyboard are never used.
"""
import ctypes, ctypes.wintypes as wt, sys, time
import rtw3ctl as ctl

H, C = 47701, 47702
u = ctypes.windll.user32
EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def cmd(port, line):
    r = ctl.cmd(port, line)
    if not r.startswith('ok'):
        log('  !!', port, line, '->', r)
    return r


def wait(port, pred, timeout=120, what=''):
    end = time.time() + timeout
    st = {}
    while time.time() < end:
        try:
            st = ctl.status(port)
            if pred(st):
                return st
        except OSError:
            pass
        time.sleep(1)
    raise SystemExit(f'timeout waiting for {what}: {st}')


def window_text(h):
    n = u.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u.GetWindowTextW(h, b, n + 1)
    return b.value


def class_name(h):
    b = ctypes.create_unicode_buffer(128)
    u.GetClassNameW(h, b, 128)
    return b.value


_clicks = {}


def dismiss_dialogs(pid):
    """Answer game dialogs (TMessageForm, Tdlg*) of process `pid`: pick the first option of event dialogs,
    then click OK/Close/Continue/Yes. Raises if the same dialog ignores repeated clicks (no endless loop)."""
    clicked = []
    tops = []

    def cb(h, l):
        p = wt.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u.IsWindowVisible(h):
            c = class_name(h)
            if c == 'TMessageForm' or c.startswith('Tdlg'):
                tops.append(h)
        return True

    u.EnumWindows(EnumProc(cb), 0)
    for top in tops:
        _clicks[top] = _clicks.get(top, 0) + 1
        if _clicks[top] > 4:
            raise SystemExit(f'dialog {class_name(top)} "{window_text(top)}" does not close; stopping')
        buttons, radios = [], []

        def ccb(h, l):
            if u.IsWindowVisible(h) and u.IsWindowEnabled(h):
                c = class_name(h)
                if c in ('TButton', 'TBitBtn'):
                    buttons.append((window_text(h).replace('&', ''), h))
                elif c == 'TRadioButton':
                    radios.append(h)
            return True

        u.EnumChildWindows(top, EnumProc(ccb), 0)
        # "Design ready for construction" (TdlgEventAnswer radios: Go to the build screen / Not now / Rework the
        # design): always answer "Not now"; the others open the build tab or the modal ship designer.
        not_now = [r for r in radios if window_text(r).replace('&', '').strip().lower() == 'not now']
        if not_now:
            if not u.SendMessageW(not_now[0], 0x00F0, 0, 0):  # BM_GETCHECK
                u.SendMessageW(not_now[0], 0x00F5, 0, 0)  # BM_CLICK selects it
        elif radios and not any(u.SendMessageW(r, 0x00F0, 0, 0) for r in radios):
            u.SendMessageW(radios[0], 0x00F5, 0, 0)  # choose the first answer
        pref = ['OK', 'Ok', 'Close', 'Continue', 'Yes', 'Exit', 'Done', 'Not now']
        target = None
        for want in pref:
            for t, h in buttons:
                if t.strip().lower() == want.lower():
                    target = (t, h)
                    break
            if target:
                break
        if not target and buttons:
            target = buttons[0]
        if target:
            sel = next((window_text(r).replace('&', '') for r in radios if u.SendMessageW(r, 0x00F0, 0, 0)), '')
            u.PostMessageW(target[1], 0x00F5, 0, 0)  # BM_CLICK
            clicked.append(f'{class_name(top)}:"{window_text(top)[:40]}"->{target[0]}' + (f' [{sel}]' if sel else ''))
    return clicked


def pid_of_bridge(port):
    r = cmd(port, 'windows')
    return None


def main():
    hpid = int(sys.argv[sys.argv.index('--hpid') + 1]) if '--hpid' in sys.argv else None
    log('host status', ctl.status(H).get('role'), 'client status', ctl.status(C).get('role'))
    if ctl.status(H).get('slot') != '1' or ctl.status(H).get('playerIdx') != '0':
        cmd(H, 'load 1 0')
        wait(H, lambda s: s.get('playerIdx') == '0' and s.get('startBuild') == '2', 60, 'host campaign')
    if ctl.status(H).get('role') != 'host':
        cmd(H, 'host Admiral 47624 0')
    if ctl.status(C).get('role') != 'client':
        cmd(C, 'join Captain 127.0.0.1 47624')
        time.sleep(3)
        cmd(C, 'claim 1')
        time.sleep(2)
    if ctl.status(H).get('phase') == 'lobby':
        cmd(H, 'start')
    st = wait(C, lambda s: s.get('phase') == 'planning' and s.get('playerIdx') == '1' and s.get('slot') == '77', 90,
              'client planning as Germany')
    log('client planning:', st.get('date'), 'player', st.get('playerIdx'))
    date0 = st.get('date')

    # Client makes decisions for Germany (nation 1).
    rp_before = cmd(C, 'nfield 1 ResearchPct')
    rp_new = 13 if rp_before.split()[-1] != '13' else 14
    cmd(C, f'nfield 1 ResearchPct {rp_new}')
    areas = cmd(C, 'areas')
    ship0 = cmd(C, 'shipinfo 1 0')
    log('client: ResearchPct', rp_before, '->', rp_new, '|', ship0)
    # order ship 0 to an area different from its location
    area_names = [a.split(':', 1) for a in areas[3:].split('; ') if ':' in a]
    loc = ship0.split(' loc=')[1].split(' ordered=')[0]
    target = next(i for i, n in area_names if n != loc)
    log('client: ordering ship 0 to area', target, dict(area_names)[target])
    log(cmd(C, f'shiporder 1 0 {target}'))
    hrp = cmd(H, 'nfield 1 ResearchPct')
    log('host sees Germany ResearchPct before turn:', hrp)

    cmd(C, 'submit')
    wait(C, lambda s: s.get('submitted') == '1', 30, 'client submitted')
    log('client submitted; host presses Turn')
    cmd(H, 'turn')
    t0 = time.time()
    hp = hpid
    while time.time() - t0 < 600:
        if hp:
            for c in dismiss_dialogs(hp):
                log('  host dialog dismissed:', c)
        try:
            cs = ctl.status(C)
            if cs.get('phase') == 'planning' and cs.get('date') != date0 and cs.get('slot') == '77':
                break
        except OSError:
            pass
        time.sleep(1.5)
    else:
        raise SystemExit('turn did not complete')
    log('client received new month:', cs.get('date'), 'player', cs.get('playerIdx'), 'status', cs.get('status'))
    log('host  Germany ResearchPct after turn:', cmd(H, 'nfield 1 ResearchPct'), '(expected', rp_new, ')')
    log('client Germany ResearchPct after turn:', cmd(C, 'nfield 1 ResearchPct'))
    log('host  ship:', cmd(H, 'shipinfo 1 0'))
    log('client ship:', cmd(C, 'shipinfo 1 0'))
    log('host nations:', cmd(H, 'nations'))


if __name__ == '__main__':
    main()
