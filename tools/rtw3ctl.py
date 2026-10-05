"""Test helper for RTW3MP development.

  python rtw3ctl.py launch <bridge-port> [client-slot]   start RTW3.exe (Steam must be running) with the test bridge
  python rtw3ctl.py cmd <bridge-port> <command ...>     send one bridge command and print the reply
  python rtw3ctl.py wait <bridge-port> <key=value> [timeout-s]   poll `status` until it contains key=value

The bridge listens on 127.0.0.1 only and exists only when RTW3MP_BRIDGE_PORT is set at launch.
"""
import os, socket, subprocess, sys, time

GAME = os.environ.get('RTW3_DIR', r'F:\SteamLibrary\steamapps\common\Rule the Waves 3')


def documents_dir():
    """The user's Documents folder (also when it is redirected, e.g. to OneDrive)."""
    import ctypes
    buf = ctypes.create_unicode_buffer(260)
    if ctypes.windll.shell32.SHGetFolderPathW(None, 5, None, 0, buf) == 0:  # CSIDL_PERSONAL
        return buf.value
    return os.path.join(os.path.expanduser('~'), 'Documents')


SAVE_DIR = os.path.join(documents_dir(), 'My Games', 'Rule the Waves 3', 'Save')


def launch(port, slot=None):
    env = dict(os.environ)
    env['SteamAppId'] = '2008100'
    env['SteamGameId'] = '2008100'
    env['RTW3MP_BRIDGE_PORT'] = str(port)
    env['RTW3MP_LOG_TAG'] = str(port)
    if slot:
        env['RTW3MP_CLIENT_SLOT'] = str(slot)
    p = subprocess.Popen([os.path.join(GAME, 'RTW3.exe')], cwd=GAME, env=env)
    print(p.pid)


def cmd(port, line, timeout=130):
    with socket.create_connection(('127.0.0.1', int(port)), timeout=timeout) as s:
        s.sendall((line + '\n').encode())
        buf = b''
        while not buf.endswith(b'\n'):
            chunk = s.recv(65536)
            if not chunk:
                break
            buf += chunk
    return buf.decode(errors='replace').strip()


def status(port):
    r = cmd(port, 'status')
    out = {}
    for part in r[3:].split('|'):
        if '=' in part:
            k, v = part.split('=', 1)
            out[k] = v
    return out


if __name__ == '__main__':
    what = sys.argv[1]
    if what == 'launch':
        launch(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
    elif what == 'cmd':
        print(cmd(sys.argv[2], ' '.join(sys.argv[3:])))
    elif what == 'wait':
        key, val = sys.argv[3].split('=', 1)
        end = time.time() + float(sys.argv[4] if len(sys.argv) > 4 else 60)
        while time.time() < end:
            try:
                st = status(sys.argv[2])
                if st.get(key) == val:
                    print('ok', st)
                    sys.exit(0)
            except OSError:
                pass
            time.sleep(1)
        print('timeout', st if 'st' in dir() else '')
        sys.exit(1)
