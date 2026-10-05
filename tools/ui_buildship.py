"""Lay down a ship on a running RTW3 instance through the real Build ship dialog (posted messages only).
  python ui_buildship.py <pid> [design-row]"""
import ctypes, ctypes.wintypes as wt, sys, time

u = ctypes.windll.user32
EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
pid = int(sys.argv[1])
row = int(sys.argv[2]) if len(sys.argv) > 2 else 0


def cls(h):
    b = ctypes.create_unicode_buffer(128)
    u.GetClassNameW(h, b, 128)
    return b.value


def text(h):
    n = u.GetWindowTextLengthW(h)
    b = ctypes.create_unicode_buffer(n + 1)
    u.GetWindowTextW(h, b, n + 1)
    return b.value


def tops():
    out = []

    def cb(h, l):
        p = wt.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u.IsWindowVisible(h):
            out.append(h)
        return True

    u.EnumWindows(EnumProc(cb), 0)
    return out


def children(top):
    out = []

    def cb(h, l):
        out.append(h)
        return True

    u.EnumChildWindows(top, EnumProc(cb), 0)
    return out


def find_top(c):
    for h in tops():
        if cls(h) == c:
            return h
    return None


def button(top, caption):
    for h in children(top):
        if cls(h) in ('TButton', 'TBitBtn') and text(h).replace('&', '') == caption and u.IsWindowVisible(h):
            return h
    return None


camp = find_top('TfrmBuildCamp')
b = button(camp, 'Build ship')
print('build button', b)
u.PostMessageW(b, 0xF5, 0, 0)
time.sleep(2)
dlg = find_top('TdlgBuildShip')
print('dialog', dlg)
lv = [h for h in children(dlg) if cls(h) == 'TListView'][0]
cnt = u.SendMessageW(lv, 0x1004, 0, 0)  # LVM_GETITEMCOUNT
print('designs listed', cnt)
y = 24 + 8 + row * 17
u.PostMessageW(lv, 0x201, 1, (y << 16) | 40)
time.sleep(0.1)
u.PostMessageW(lv, 0x202, 0, (y << 16) | 40)
time.sleep(1)
name = [h for h in children(dlg) if cls(h) == 'TEdit']
print('name edit', [text(h) for h in name])
ok = button(dlg, 'OK')
u.PostMessageW(ok, 0xF5, 0, 0)
time.sleep(1.5)
for i in range(4):
    mf = find_top('TMessageForm')
    if not mf:
        break
    yes = button(mf, 'Yes') or button(mf, 'OK')
    print('message form ->', text(yes) if yes else None)
    if yes:
        u.PostMessageW(yes, 0xF5, 0, 0)
    time.sleep(1)
print('dialog still open:', bool(find_top('TdlgBuildShip')))
