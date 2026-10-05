"""Drive the real Multiplayer window of a running instance (posted/sent window messages only).

  python ui_mp.py <pid> open                     click the title-screen Multiplayer button
  python ui_mp.py <pid> show                     dump the window state (status, players, nation list, buttons)
  python ui_mp.py <pid> set <field> <text>       field: name ip port pw limit chat
  python ui_mp.py <pid> click <button>           button: host join find leave claim release setlimit start advance send
  python ui_mp.py <pid> pick <text>              select the nation-list entry containing <text>
"""
import ctypes, ctypes.wintypes as wt, sys, time

u = ctypes.windll.user32
EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
u.GetDlgItem.restype = wt.HWND
u.GetDlgItem.argtypes = [wt.HWND, ctypes.c_int]
IDS = dict(name=1001, ip=1002, port=1003, pw=1004, host=1010, join=1011, find=1012, leave=1013, addrs=1020,
           status=1021, players=1030, nation=1040, claim=1041, limit=1050, setlimit=1051, start=1060,
           advance=1061, release=1062, chatlog=1070, chat=1071, send=1072)


def cls(h):
    b = ctypes.create_unicode_buffer(128)
    u.GetClassNameW(h, b, 128)
    return b.value


def text(h):
    n = u.SendMessageW(h, 0x000E, 0, 0)  # WM_GETTEXTLENGTH (works across processes, unlike GetWindowText)
    b = ctypes.create_unicode_buffer(n + 1)
    u.SendMessageW(h, 0x000D, n + 1, b)  # WM_GETTEXT
    return b.value


def windows_of(pid, klass):
    out = []

    def cb(h, l):
        p = wt.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and cls(h) == klass:
            out.append(h)
        return True

    u.EnumWindows(EnumProc(cb), 0)
    return out


def mp_window(pid):
    w = windows_of(pid, 'RTW3MP_Window')
    return w[0] if w else None


def open_mp(pid):
    for top in windows_of(pid, 'TfrmSelectNation2'):
        found = []
        u.EnumChildWindows(top, EnumProc(lambda h, l: (found.append(h) if cls(h) == 'RTW3MP_MenuButton' else None) or True), 0)
        if found:
            u.PostMessageW(found[0], 0x201, 1, (10 << 16) | 10)
            time.sleep(0.1)
            u.PostMessageW(found[0], 0x202, 0, (10 << 16) | 10)
            time.sleep(1.5)
            return mp_window(pid)
    return None


def ctl(pid, key):
    return u.GetDlgItem(mp_window(pid), IDS[key])


def combo_items(h):
    n = u.SendMessageW(h, 0x0146, 0, 0)  # CB_GETCOUNT
    items = []
    for i in range(max(n, 0)):
        ln = u.SendMessageW(h, 0x0149, i, 0)  # CB_GETLBTEXTLEN
        b = ctypes.create_unicode_buffer(ln + 2)
        u.SendMessageW(h, 0x0148, i, b)  # CB_GETLBTEXT
        items.append(b.value)
    return items, u.SendMessageW(h, 0x0147, 0, 0)  # CB_GETCURSEL


def show(pid):
    w = mp_window(pid)
    if not w:
        print('no Multiplayer window')
        return
    print('visible', bool(u.IsWindowVisible(w)))
    print('status:', text(u.GetDlgItem(w, IDS['status'])).replace('\r\n', ' | '))
    items, sel = combo_items(u.GetDlgItem(w, IDS['nation']))
    print('nation list:', items, 'selected', sel)
    lv = u.GetDlgItem(w, IDS['players'])
    print('players rows:', u.SendMessageW(lv, 0x1004, 0, 0))
    print('buttons:', {k: bool(u.IsWindowEnabled(u.GetDlgItem(w, IDS[k])))
                       for k in ('host', 'join', 'leave', 'claim', 'release', 'start', 'advance', 'send')})
    log = text(u.GetDlgItem(w, IDS['chatlog'])).strip().splitlines()
    print('chat:', log[-4:])


if __name__ == '__main__':
    pid = int(sys.argv[1])
    what = sys.argv[2]
    if what == 'open':
        print('window', open_mp(pid))
    elif what == 'show':
        show(pid)
    elif what == 'set':
        h = ctl(pid, sys.argv[3])
        u.SendMessageW(h, 0x000C, 0, ctypes.c_wchar_p(' '.join(sys.argv[4:])))  # WM_SETTEXT
    elif what == 'click':
        u.PostMessageW(ctl(pid, sys.argv[3]), 0x00F5, 0, 0)  # BM_CLICK
        time.sleep(1.5)
    elif what == 'pick':
        h = ctl(pid, 'nation')
        items, _ = combo_items(h)
        want = ' '.join(sys.argv[3:]).lower()
        idx = next((i for i, t in enumerate(items) if want in t.lower()), -1)
        print('select', idx, items[idx] if idx >= 0 else None)
        if idx >= 0:
            u.SendMessageW(h, 0x014E, idx, 0)  # CB_SETCURSEL
