"""Run one multiplayer month: client(s) submit, host presses Turn, host dialogs are auto-dismissed,
wait until every client has the next month.   python run_turn.py <host-pid> [client-port ...]"""
import sys, time
import rtw3ctl as ctl
from e2e_test import dismiss_dialogs, log

H = 47701
hpid = int(sys.argv[1])
clients = [int(p) for p in sys.argv[2:]] or [47702]
dates = {c: ctl.status(c).get('date') for c in clients}
for c in clients:
    print(c, ctl.cmd(c, 'submit'))
time.sleep(2)
log('host', ctl.cmd(H, 'turn'))
t0 = time.time()
while time.time() - t0 < 600:
    for d in dismiss_dialogs(hpid):
        log('  host dialog:', d)
    done = True
    for c in clients:
        st = ctl.status(c)
        if not (st.get('phase') == 'planning' and st.get('date') != dates[c]):
            done = False
    if done:
        break
    time.sleep(1.5)
for c in clients:
    st = ctl.status(c)
    log('client', c, st.get('date'), 'player', st.get('playerIdx'), st.get('status'))
log('host nations:', ctl.cmd(H, 'nations'))
