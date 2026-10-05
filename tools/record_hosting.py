"""Record the host's side of a multiplayer session as a captioned tutorial video (no real mouse/keyboard).

Captures every visible window of the host's game process (game screens, dialogs, Multiplayer window) with
PrintWindow, composites them at their screen positions into 1920x1080 frames, draws the step caption and a
ring on each click, then encodes docs/hosting.mp4 and key stills.   python record_hosting.py"""
import ctypes, ctypes.wintypes as wt, os, shutil, subprocess, sys, threading, time
from PIL import Image, ImageDraw, ImageFont
import rtw3ctl as ctl
import ui_mp as ui
from e2e_test import dismiss_dialogs

u = ui.u
g = ctypes.windll.gdi32
u.SetProcessDPIAware()
H, C = 47701, 47702
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS = os.path.join(REPO, 'docs')
TMP = os.path.join(os.environ.get('TEMP', REPO), 'rtw3mp_rec')
FFMPEG = os.environ.get('FFMPEG') or shutil.which('ffmpeg') or os.path.join(
    os.environ.get('LOCALAPPDATA', ''), r'universal-modder\ffmpeg\bin\ffmpeg.exe')
W, HGT = 1920, 1080
ANCHOR = (20, 20)
FONT = ImageFont.truetype(r'C:\Windows\Fonts\segoeuib.ttf', 34)
FONT_BIG = ImageFont.truetype(r'C:\Windows\Fonts\segoeuib.ttf', 64)
FONT_SUB = ImageFont.truetype(r'C:\Windows\Fonts\segoeui.ttf', 36)

state = {'caption': '', 'card': None, 'speed': 1.0, 'ring': None, 'snap': None, 'run': True, 'refresh_title': False}
frames = []  # (t, path, speed)


class BMI(ctypes.Structure):
    _fields_ = [('biSize', wt.DWORD), ('biWidth', ctypes.c_long), ('biHeight', ctypes.c_long), ('biPlanes', wt.WORD),
                ('biBitCount', wt.WORD), ('biCompression', wt.DWORD), ('biSizeImage', wt.DWORD),
                ('biXPelsPerMeter', ctypes.c_long), ('biYPelsPerMeter', ctypes.c_long), ('biClrUsed', wt.DWORD),
                ('biClrImportant', wt.DWORD)]


def grab(h, flags=2):
    r = wt.RECT()
    u.GetWindowRect(h, ctypes.byref(r))
    w, hh = r.right - r.left, r.bottom - r.top
    if w <= 0 or hh <= 0:
        return None, r
    hdc = u.GetWindowDC(h)
    mdc = g.CreateCompatibleDC(hdc)
    bmp = g.CreateCompatibleBitmap(hdc, w, hh)
    g.SelectObject(mdc, bmp)
    u.PrintWindow(h, mdc, flags)
    bmi = BMI(ctypes.sizeof(BMI), w, -hh, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(w * hh * 4)
    g.GetDIBits(mdc, bmp, 0, hh, buf, ctypes.byref(bmi), 0)
    g.DeleteObject(bmp)
    g.DeleteDC(mdc)
    u.ReleaseDC(h, hdc)
    img = Image.frombuffer('RGBA', (w, hh), buf, 'raw', 'BGRA', 0, 1).convert('RGB')
    if flags == 2 and img.getextrema() == ((0, 0), (0, 0), (0, 0)):
        return grab(h, 0)
    return img, r


def process_windows(pid):
    out = []

    def cb(h, l):
        p = wt.DWORD()
        u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and u.IsWindowVisible(h) and ui.cls(h) not in ('TApplication',):
            r = wt.RECT()
            u.GetWindowRect(h, ctypes.byref(r))
            if r.right - r.left > 40 and r.bottom - r.top > 40:
                out.append(h)
        return True

    u.EnumWindows(ui.EnumProc(cb), 0)
    return out  # top of z-order first


_title_cache = {}


def white_frac(img):
    data = img.resize((64, 48)).convert('L').tobytes()
    return sum(1 for b in data if b > 245) / len(data)


def grab_title(h):
    """The title screen is static but sometimes renders blank into PrintWindow: capture it best-of-N and reuse it
    until the driver changes it (state['refresh_title'])."""
    r = wt.RECT()
    u.GetWindowRect(h, ctypes.byref(r))
    cached = _title_cache.get(h)
    if cached is not None and not state['refresh_title'] and cached.size == (r.right - r.left, r.bottom - r.top):
        return cached, r
    best = None
    for _ in range(8):
        shot, r = grab(h)
        if shot is None:
            return None, r
        wf = white_frac(shot)
        if best is None or wf < best[0]:
            best = (wf, shot, r)
        if wf < 0.15:
            break
        time.sleep(0.08)
    _title_cache[h] = best[1]
    state['refresh_title'] = False
    return best[1], best[2]


def compose(pid):
    """Only the game's own windows are captured (PrintWindow), never the screen."""
    if state['card']:
        img = Image.new('RGB', (W, HGT), (18, 24, 34))
        d = ImageDraw.Draw(img)
        t, s = state['card']
        d.text((W // 2, HGT // 2 - 40), t, font=FONT_BIG, fill=(240, 240, 240), anchor='mm')
        d.text((W // 2, HGT // 2 + 40), s, font=FONT_SUB, fill=(170, 200, 230), anchor='mm')
        return img
    canvas = Image.new('RGB', (2560, 1440), (18, 24, 34))
    for h in reversed(process_windows(pid)):
        shot, r = grab_title(h) if ui.cls(h) == 'TfrmSelectNation2' else grab(h)
        if shot:
            canvas.paste(shot, (r.left, r.top))
    rects = []
    for h in (main_form(pid), ui.mp_window(pid)):
        if h and u.IsWindowVisible(h):
            r = wt.RECT()
            u.GetWindowRect(h, ctypes.byref(r))
            rects.append((r.left, r.top, r.right, r.bottom))
    if rects:
        bx0 = max(0, min(r[0] for r in rects) - 14)
        by0 = max(0, min(r[1] for r in rects) - 14)
        bx1 = min(2560, max(r[2] for r in rects) + 14)
        by1 = min(1440, max(r[3] for r in rects) + 14)
    else:
        bx0, by0, bx1, by1 = 0, 0, W, HGT
    area_h = HGT - 74
    scale = min(W / (bx1 - bx0), area_h / (by1 - by0))
    cw, ch = int((bx1 - bx0) * scale), int((by1 - by0) * scale)
    ox, oy = (W - cw) // 2, (area_h - ch) // 2
    img = Image.new('RGB', (W, HGT), (18, 24, 34))
    img.paste(canvas.crop((bx0, by0, bx1, by1)).resize((cw, ch), Image.LANCZOS), (ox, oy))
    d = ImageDraw.Draw(img)
    ring = state['ring']
    if ring and time.time() - ring[2] < 0.9:
        x, y = ox + (ring[0] - bx0) * scale, oy + (ring[1] - by0) * scale
        for rad, col in ((30, (255, 210, 0)), (34, (0, 0, 0))):
            d.ellipse((x - rad, y - rad, x + rad, y + rad), outline=col, width=5 if rad == 30 else 2)
    if state['caption']:
        d.rectangle((0, HGT - 74, W, HGT), fill=(10, 14, 22))
        d.text((32, HGT - 37), state['caption'], font=FONT, fill=(255, 255, 255), anchor='lm')
        if state['speed'] > 1:
            d.text((W - 32, HGT - 37), f'x{state["speed"]:g}', font=FONT, fill=(255, 210, 0), anchor='rm')
    return img


def recorder(pid):
    os.makedirs(TMP, exist_ok=True)
    n = 0
    while state['run']:
        t0 = time.time()
        try:
            img = compose(pid)
        except Exception as e:
            print('frame error', e)
            time.sleep(0.2)
            continue
        path = os.path.join(TMP, f'f{n:05d}.jpg')
        img.save(path, quality=88)
        frames.append((t0, path, state['speed']))
        if state['snap']:
            img.resize((1280, 720), Image.LANCZOS).save(os.path.join(DOCS, state['snap'] + '.jpg'), quality=86)
            state['snap'] = None
        n += 1
        time.sleep(max(0, 0.12 - (time.time() - t0)))


def center(h):
    r = wt.RECT()
    u.GetWindowRect(h, ctypes.byref(r))
    return (r.left + r.right) // 2, (r.top + r.bottom) // 2


def ring_at(x, y):
    state['ring'] = (x, y, time.time())
    time.sleep(0.6)


def click_hwnd(h, pause=1.0):
    ring_at(*center(h))
    u.PostMessageW(h, 0xF5, 0, 0)  # BM_CLICK
    time.sleep(pause)


def click_panel(panel, x, y, pause=1.0):
    p = wt.POINT(x, y)
    u.ClientToScreen(panel, ctypes.byref(p))
    ring_at(p.x, p.y)
    u.PostMessageW(panel, 0x201, 1, (y << 16) | x)
    time.sleep(0.1)
    u.PostMessageW(panel, 0x202, 0, (y << 16) | x)
    time.sleep(pause)


def move(h, x, y):
    u.SetWindowPos(h, 0, x, y, 0, 0, 0x0001 | 0x0004 | 0x0010)  # NOSIZE | NOZORDER | NOACTIVATE


def caption(text, hold=0.0):
    state['caption'] = text
    time.sleep(hold)


def main_form(pid):
    for cls in ('TfrmBuildCamp', 'TfrmSelectNation2'):
        for h in ui.windows_of(pid, cls):
            if u.IsWindowVisible(h):
                return h
    return None


def place(pid):
    f = main_form(pid)
    move(f, *ANCHOR)
    mp = ui.mp_window(pid)
    if mp and u.IsWindowVisible(mp):
        r = wt.RECT()
        u.GetWindowRect(f, ctypes.byref(r))
        move(mp, r.right + 12, ANCHOR[1])


def children(top, klass=None):
    out = []
    u.EnumChildWindows(top, ui.EnumProc(lambda h, l: (out.append(h) if (klass is None or ui.cls(h) == klass) else None) or True), 0)
    return out


def button(top, cap):
    for h in children(top):
        if ui.cls(h) in ('TButton', 'TBitBtn', 'Button') and ui.text(h).replace('&', '') == cap and u.IsWindowVisible(h):
            return h
    return None


def launch(port, slot=None):
    args = [sys.executable, 'rtw3ctl.py', 'launch', str(port)] + ([str(slot)] if slot else [])
    return int(subprocess.check_output(args).strip())


shutil.rmtree(TMP, ignore_errors=True)
os.makedirs(DOCS, exist_ok=True)
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
    while not ui.windows_of(hp, 'TfrmSelectNation2'):
        time.sleep(0.5)
    time.sleep(3)
    place(hp)
    threading.Thread(target=recorder, args=(hp,), daemon=True).start()

    state['card'] = ('Hosting a multiplayer session', 'Rule the Waves 3 Multiplayer mod')
    time.sleep(3)
    state['card'] = None
    caption('1. On the title screen, click Multiplayer (or press Ctrl+Shift+M)', 1.2)
    title = ui.windows_of(hp, 'TfrmSelectNation2')[0]
    mb = children(title, 'RTW3MP_MenuButton')[0]
    ring_at(*center(mb))
    u.PostMessageW(mb, 0x201, 1, (10 << 16) | 10)
    u.PostMessageW(mb, 0x202, 0, (10 << 16) | 10)
    for _ in range(20):
        if ui.mp_window(hp) and u.IsWindowVisible(ui.mp_window(hp)):
            break
        time.sleep(0.2)
    place(hp)
    time.sleep(0.3)
    mp = ui.mp_window(hp)
    # Example addresses for the docs (shows what a Hamachi address looks like; hides this PC's real IP).
    u.SendMessageW(ui.ctl(hp, 'addrs'), 0x000C, 0,
                   ctypes.c_wchar_p('Your VPN address: 25.17.204.86 (Hamachi)\r\nOther addresses: 192.168.1.20'))
    time.sleep(1.2)

    caption('2. Enter your name (optional: password, turn time limit) and press Host game', 0.8)
    u.SendMessageW(ui.ctl(hp, 'name'), 0x000C, 0, ctypes.c_wchar_p('Admiral'))
    time.sleep(1.2)
    click_hwnd(ui.ctl(hp, 'host'), 2.0)
    caption('Give the other players your Hamachi (VPN) address shown here', 0.3)
    ring_at(*center(ui.ctl(hp, 'addrs')))
    time.sleep(2.5)

    caption('3. Players join and pick a nation', 0.5)
    ctl.cmd(C, 'join Captain 127.0.0.1 47624')
    time.sleep(2.5)
    ctl.cmd(C, 'claim Germany')
    time.sleep(2.5)
    state['snap'] = 'hosting-1-lobby'

    caption('4. Optional: choose the nation you will play and press Claim nation', 0.8)
    combo = ui.ctl(hp, 'nation')
    items, _ = ui.combo_items(combo)
    ring_at(*center(combo))
    u.SendMessageW(combo, 0x014E, next(i for i, t in enumerate(items) if t.startswith('Great Britain')), 0)
    time.sleep(1.0)
    click_hwnd(ui.ctl(hp, 'claim'), 2.0)

    caption('5. Press Start session: it starts as soon as your campaign is ready', 0.8)
    click_hwnd(ui.ctl(hp, 'start'), 2.8)
    state['snap'] = 'hosting-2-start-session'
    time.sleep(0.5)
    for box in [w for w in ui.windows_of(hp, '#32770') if u.IsWindowVisible(w)]:
        ok = button(box, 'OK')
        if ok:
            click_hwnd(ok, 1.5)

    caption('6. Start a New Game, or Load Game (shown here)', 0.8)
    pn = [h for h in children(title, 'TPanel') if u.IsWindowVisible(h) and u.GetParent(h) == title][0]
    click_panel(pn, 304, 470, 0.4)
    state['refresh_title'] = True
    time.sleep(1.6)
    boxes = [h for h in children(title, 'TListBox') if u.IsWindowVisible(h)]
    slots = max(boxes, key=lambda h: u.SendMessageW(h, 0x018B, 0, 0))
    u.SendMessageW(slots, 0x0186, 0, 0)
    state['refresh_title'] = True
    time.sleep(1.2)
    state['speed'] = 3.0
    click_hwnd(button(title, 'OK'), 0.5)
    for _ in range(100):
        camp = ui.windows_of(hp, 'TfrmBuildCamp')
        if camp and u.IsWindowVisible(camp[0]):
            break
        time.sleep(0.2)
    time.sleep(0.5)
    place(hp)
    state['speed'] = 1.0
    caption('The campaign opens, the players\' picks are matched and the session starts', 0.5)
    for _ in range(60):
        if ctl.status(C).get('phase') == 'planning':
            break
        time.sleep(0.5)
    time.sleep(3)
    state['snap'] = 'hosting-3-session-started'

    caption('7. Plan your month; the player list shows who is ready', 0.5)
    ctl.cmd(C, 'submit')
    time.sleep(3.5)
    caption('8. Press Turn when you are ready', 0.8)
    camp = ui.windows_of(hp, 'TfrmBuildCamp')[0]
    date0 = ctl.status(C).get('date')
    click_hwnd(button(camp, 'Turn'), 0.5)
    caption('Answer the game\'s usual end-of-turn questions', 0.0)
    state['speed'] = 2.5
    end = time.time() + 300
    while time.time() < end:
        time.sleep(1.0)
        dismiss_dialogs(hp)
        s = ctl.status(C)
        if s.get('phase') == 'planning' and s.get('date') != date0:
            break
    state['speed'] = 1.0
    caption('9. The new month goes to every player automatically', 4.0)
    state['snap'] = 'hosting-4-next-month'
    time.sleep(1.0)
    state['caption'] = ''
    state['card'] = ('Rule the Waves 3 Multiplayer', 'Host: Multiplayer > Host game > Start session > Turn')
    time.sleep(3)
finally:
    state['run'] = False
    time.sleep(0.5)
    for p in (hp, cp):
        subprocess.run(['taskkill', '/PID', str(p), '/F'], capture_output=True)
    print('games closed; frames', len(frames))

# Encode with real timing (sped-up sections shortened).
lst = os.path.join(TMP, 'list.txt')
with open(lst, 'w') as f:
    for i, (t, path, sp) in enumerate(frames):
        nxt = frames[i + 1][0] if i + 1 < len(frames) else t + 0.5
        f.write(f"file '{path}'\nduration {max(0.02, (nxt - t) / sp):.3f}\n")
    f.write(f"file '{frames[-1][1]}'\n")
out = os.path.join(DOCS, 'hosting.mp4')
subprocess.run([FFMPEG, '-y', '-loglevel', 'error', '-f', 'concat', '-safe', '0', '-i', lst, '-vf',
                'fps=30,format=yuv420p', '-c:v', 'libx264', '-crf', '23',
                '-preset', 'slow', '-movflags', '+faststart', out], check=True)
print('wrote', out, os.path.getsize(out) // 1024, 'KB')
