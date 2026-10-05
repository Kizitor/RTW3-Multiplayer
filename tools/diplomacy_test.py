"""Live test of player diplomacy with two game instances (host + client) driven by the test bridge and posted
window messages (the user's mouse and keyboard are never used).

  python diplomacy_test.py

Requires Steam running and the mod installed. Save slot 1 holds a campaign (host = nation 0); it is backed up
first and restored at the end, together with the client slot 77. Nation 2 is reserved for a stand-in player
("Bot") so diplomacy between two joined nations can be tested with one client.
"""
import ctypes, ctypes.wintypes as wt, os, shutil, subprocess, sys, time
import e2e_test
import rtw3ctl as ctl
from e2e_test import dismiss_dialogs
from ui_mp import text, windows_of

H, C = 47701, 47702
SAVE = ctl.SAVE_DIR
BACKUP = os.path.join(os.environ.get('TEMP', '.'), 'rtw3mp_diplo_backup_Game1')
u = ctypes.windll.user32
u.GetDlgItem.restype = wt.HWND
u.GetDlgItem.argtypes = [wt.HWND, ctypes.c_int]
IDC = dict(label=1080, list=1081, war=1082, ally=1083, peace=1084, leave=1085, cancel=1086, chatlog=1070)
results = []


def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def check(name, ok, detail=''):
    results.append((name, bool(ok)))
    log(('PASS ' if ok else 'FAIL ') + name, detail)


def cmd(port, line):
    r = ctl.cmd(port, line)
    if not r.startswith('ok'):
        log('  !!', port, line, '->', r)
    return r


def rel(a, b):
    r = cmd(H, f'rel {a} {b}')
    return dict(kv.split('=') for kv in r[3:].split())


def row(port, nation):
    r = cmd(port, 'diploview')
    for part in r.split('rows=', 1)[1].split('; '):
        f = part.split('/')
        if len(f) >= 7 and f[0] == str(nation):
            return dict(nation=f[0], name=f[1], player=f[2], relation=f[3], tension=f[4], pending=f[5].strip('[]'),
                        flags=f[6])
    return None


def launch(port, slot=None):
    env = dict(os.environ, SteamAppId='2008100', SteamGameId='2008100', RTW3MP_BRIDGE_PORT=str(port),
               RTW3MP_LOG_TAG=str(port))
    if slot:
        env['RTW3MP_CLIENT_SLOT'] = str(slot)
    return subprocess.Popen([os.path.join(ctl.GAME, 'RTW3.exe')], cwd=ctl.GAME, env=env)


def drain(pid, seconds):
    """Answer the game's dialogs for a while (the game reuses dialog windows, so the click guard is reset)."""
    for _ in range(int(seconds)):
        e2e_test._clicks.clear()
        for c in dismiss_dialogs(pid):
            log('  dialog dismissed:', c)
        time.sleep(1)


def wait(port, pred, timeout, what, pids=()):
    e2e_test._clicks.clear()
    end = time.time() + timeout
    st = {}
    while time.time() < end:
        for p in pids:
            for c in dismiss_dialogs(p):
                log('  dialog dismissed:', c)
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


def answer_host_box(pid):
    """Fallback: the host's own 'Still waiting for ...' box (Yes = advance now)."""
    for d in windows_of(pid, '#32770'):
        if text(d) == 'Multiplayer':
            u.PostMessageW(u.GetDlgItem(d, 6), 0x00F5, 0, 0)
            log('  answered the host\'s waiting box')


def run_turn(hp, cp, date0):
    cmd(C, 'submit')
    wait(C, lambda s: s.get('submitted') == '1', 30, 'client submitted')
    wait(H, lambda s: 'All players are ready' in s.get('status', ''), 30, 'host sees everyone ready')
    cmd(H, 'turn')
    e2e_test._clicks.clear()
    end = time.time() + 240
    while time.time() < end:
        answer_host_box(hp)
        for p in (hp, cp):
            e2e_test._clicks.clear()  # the game reuses its message windows; the overall timeout bounds this loop
            for c in dismiss_dialogs(p):
                log('  dialog dismissed:', c)
        try:
            st = ctl.status(C)
            if st.get('phase') == 'planning' and st.get('date') != date0 and st.get('slot') == '77':
                time.sleep(2)
                return st.get('date')
        except OSError:
            pass
        time.sleep(1.5)
    raise RuntimeError(f'the month did not advance: host {ctl.status(H)}')


def scenario(hp, cp):
    if ctl.status(H).get('playerIdx') != '0':
        cmd(H, 'load 1 0')
        wait(H, lambda s: s.get('playerIdx') == '0' and s.get('startBuild') == '2', 90, 'host campaign', pids=(hp,))
    cmd(H, 'host Admiral 47624 0')
    cmd(C, 'join Captain 127.0.0.1 47624')
    time.sleep(3)
    cmd(C, 'claim 1')
    time.sleep(1.5)
    cmd(H, 'start')
    st = wait(C, lambda s: s.get('phase') == 'planning' and s.get('playerIdx') == '1' and s.get('slot') == '77', 120,
              'client planning as nation 1', pids=(hp, cp))
    date = st.get('date')
    cmd(H, 'reserve 2 Bot')
    time.sleep(1.5)
    log('month A:', date, '| nations:', cmd(H, 'nations')[3:120])
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
    date = run_turn(hp, cp, date)

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
    date = run_turn(hp, cp, date)

    log('month C:', date)
    r01, r02, r12 = rel(0, 1), rel(0, 2), rel(1, 2)
    log('  0-1', r01, '| 0-2', r02, '| 1-2', r12)
    check('host and client at war after the month', r01['war'] == '1' and r01['warCounter'] == '1', r01)
    check('client and nation 2 at war, their alliance gone', r12['war'] == '1' and r12['ally'] == '0', r12)
    check('host and nation 2 still allied (kept at 60 months)', r02['ally'] == '60', r02)
    if host_at_peace_b:
        hlog = open(os.path.join(os.path.dirname(SAVE), 'RTW3MP', f'rtw3mp_{H}.log'), encoding='utf-8',
                    errors='replace').read()
        check('the alliance countdown was topped up', hlog.count('diplomacy: alliance 0-2 = 60') >= 2)
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
    chat = text(mp_ctl(cp, 'chatlog'))
    check('the players\' war is restored at month end', rel(1, 2)['war'] == '1' and 'goes on' in chat, rel(1, 2))

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


def main():
    if 'RTW3.exe' in os.popen('tasklist /FI "IMAGENAME eq RTW3.exe" /NH').read():
        raise SystemExit('RTW3.exe is already running; close it first')
    g1 = os.path.join(SAVE, 'Game1')
    if os.path.exists(BACKUP):
        shutil.rmtree(BACKUP)
    shutil.copytree(g1, BACKUP)
    log('backed up', g1)
    procs = []
    try:
        procs.append(launch(H))
        time.sleep(4)
        procs.append(launch(C, 77))
        hp, cp = procs[0].pid, procs[1].pid
        for port in (H, C):
            end = time.time() + 90
            while time.time() < end:
                try:
                    if ctl.cmd(port, 'ping').startswith('ok'):
                        break
                except OSError:
                    pass
                time.sleep(1)
        scenario(hp, cp)
    except Exception as e:
        check('scenario completed', False, repr(e))
    finally:
        for p in procs:
            subprocess.run(['taskkill', '/PID', str(p.pid), '/F'], capture_output=True)
        time.sleep(2)
        shutil.rmtree(g1, ignore_errors=True)
        shutil.copytree(BACKUP, g1)
        shutil.rmtree(BACKUP, ignore_errors=True)
        shutil.rmtree(os.path.join(SAVE, 'Game77'), ignore_errors=True)
        log('restored', g1, 'and removed Game77')
    failed = [n for n, ok in results if not ok]
    log(f'{len(results) - len(failed)}/{len(results)} checks passed', 'FAILED: ' + '; '.join(failed) if failed else '')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
