"""Offline self-test of tools/regression.py: never starts the game, never reads or writes the game's saves, logs or
Preferences.

  python tools/regression_selftest.py                  the parsers and log patterns against sample replies and log lines
                                                       (the formats of src/mod, lines of the 0.4.0 live proofs), the
                                                       runner's plan: --list, --dry-run and the fixture/Preferences helpers
                                                       in a sandbox folder (made in the current folder, deleted after)
  python tools/regression_selftest.py --logs <folder>  also the parsers over a recorded run's mod logs
                                                       (rtw3mp_<bridge port>[.<tag>].log, ls_trace_*.txt): a report

Exit code 0 when every check passed. Run it after every change of the suite's parsers, patterns or runner options.
"""
import argparse
import contextlib
import io
import os
import re
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import regression as R  # noqa: E402

RESULTS = []


SESSION_FILES = os.environ.get('RTW3_SESSION_FILES', '')  # (optional: the developers' snapshots)


def check(name, ok, detail=''):
    RESULTS.append((name, bool(ok)))
    print(f'{"PASS" if ok else "FAIL"} {name}' + ('' if ok else f'  -- {detail}'))
    return bool(ok)


# ---------------------------------------------------------------- samples (the bridge's replies, the mods' log lines)

FORCES = ('ok 0:0:Great Britain Main force:divs=0,1:lead=0:changed=0:turnaway=0:ai=0:withdraw=0:mission=0; '
          '1:1:Germany Main force:divs=2,3,4:lead=2:changed=1:turnaway=12:ai=0:withdraw=0:mission=1; '
          '2:0:Great Britain Coastal forces:divs=16,17:lead=-:changed=0:turnaway=0:ai=1:withdraw=0:mission=1;')
FORCES_DIV = ('ok 0:0:GB Battle Division 2:force=0:admin=0:lead=-:role=0:flead=1:ai=0; '
              '1:0:GB Destroyer Division 28:force=0:admin=0:lead=0:role=2:flead=0:ai=1; '
              '3:1:Germany Battle Division 3:force=1:admin=0:lead=2:role=1:flead=0:ai=0;')
PENDING = 'ok n=2 #1:lead:0:Great Britain Main force>GB Destroyer Division 28; #2:torp:5:A12#0>A33#0;'
LANDCAP = ('ok landcap0=0:GB Battle Division 11 landcap1=4:France Battle Division 8 swapped=1 mine=1 feature=1 rule=1 '
           'calls=8 other=1 airfields=27:0:-;28:0:0;29:1:4;30:1:-;31:0:-;')
AIRFORCE = ('ok force=0 name=Great Britain Main force side=0 cap=1 nightcap=1 left=185 right=285 dist=300 interval=0 '
            'twophase=0 repeat=0 nightsearch=0 ordered=0000000000111110000 current=0,0,3,0 flagship=Repulse feature=1 | '
            'landcap0=- landcap1=- swapped=0 mine=0 feature=1 rule=1 calls=0 other=0 airfields=27:0:-;28:0:-; | batch=#4')
AIRLIST = ('ok n=3 mark=43 afid=43 scout=0 launches=1 replays=2 failed=0 sorts=0 next=0/1 feature=1 auto=0 fresh=1 | '
           '1:0:m0:s0:o0:5/0:l0:p-:L-:g0:c0:t0.000,0.000:at0.000,0.000:59 Squadron; '
           '1100000:1:m3:s2:o2:3/3:l4:p42:L-:g0:c0:t-4.123,50.250:at-4.500,48.380:N strike #58; '
           '7:0:m1:s10:o0:2/0:l0:p-:L-:g0:c1:t0.000,0.000:at-4.100,50.300:Recon: west;')
STRIKE = ('ok visible=1 division=24:France Carrier Division 3 target=0.000,0.000 fresh=1 ready=0 (Pause the battle first: '
          'aircraft are launched, readied and stood down between minutes.) hwnd=1ec176a lv=31e13fc launch=14f140a '
          'ready=a3142c standdown=2631438 autoselect=167145e cancel=fe140c rows=0:34:0;1:36:1;2:-:0;')
REJOINABLE = ('ok on=1 bid=100000 opp=1 peer=Captain side=1 dropMinute=4 minute=4 age=2 epoch=1 pending=0 why=Captain '
              'disconnected')
RESUMABLE = ('ok on=1 held=1 rejoinable=1 bid=100003 epoch=1 opp=1 slot=1 slotMode=1 serial=1 loadedAt=0 minute=0 age=2 '
             'pending=0 marker=match:bid100003:epoch1:min0:faw11:slotmode1 peer=Captain why=the battle was loaded from the '
             'host\'s save')
MISSILESTAT = ('ok faw=10 agreed=10 box=1 boxEnabled=1 feature=11 calls=42 held=0/2 heldCalls=0/5 | side0 ships=4 '
               'heavySSM=8 mediumSSM=0 lightSSM=12 heavySAM=0 mediumSAM=16 lightSAM=24 pending=1 air=2 msub=0/0 | side1 '
               'ships=3 heavySSM=4 mediumSSM=2 lightSSM=0 heavySAM=8 mediumSAM=0 lightSAM=12 pending=0 air=0 msub=1/1 | '
               'otherAir=0 subs=2 | intercepted shotDown=2/1 bySAM=2/0 decoyed=0/1 | nations 0:4:msam1:antimissile1:c8=0:'
               'gate0 1:3:msam1:antimissile1:c8=0:gate0 | orders=1 pendingOrders=0 replays=1 fired=2 skipped=0 '
               'ssmRefused=0 subsUnchecked=0 last=Kirov mount 2: 2 of 2 (a location, bearing 270)')
MOUNTS = ('ok ship=Fame side=0 sunk=0 speed=15 type=9 fired=0 mounts=2 0:type=0:pos=Q:tubes=1:left=1:reload=0:reloads=0:'
          'destroyed=0:sub=0; 1:type=0:pos=V:tubes=2:left=0:reload=4:reloads=0:destroyed=0:sub=?;')
MARKER = 'bid=100003\nepoch=2\nminute=7\nslotmode=1\nfaw=11\nautostrike=0\nhost=Admiral\ns0=Great Britain\ns1=Germany\n'
TORP = ('ok queued=1 target=0 mount=1 | result: ordered for minute 64 | solution: 1 HIGH 3 tubes | ship=Fame button=1 | '
        'solutions: 0 LOW 2 tubes [mount 0]; 1 HIGH 3 tubes [mount 1]; | targets 2: 0 Brandenburg; 1 Woerth;')
TORP_NONE = ('ok queued=0 target=1 (no firing solution) | ship=Fame button=1 | solutions: | targets 2: 0 Brandenburg; '
             '1 Woerth;')
MISSILES = ('ok queued=2 missiles=6 target=0 | result: 2 launchers | summary: - | shooters: 0:Kirov:1; 1:Grozny:1; '
            '2:K-45:0; | targets 1: 0 Enterprise;')
# twopc (0.4.1, engine level 15): `ls prefs` and `ls mapview` replies and the log lines of the live proofs nb4-nb8
PREFS_A = ('ok AIControl=2 FloatWarning=2 ReadyPopup=0 BreakOnNewSighting=1 AirApproachMessage=1 AirSurfaceMessageLvl=0 '
           'LetAIControlForce=0 LOScalc=1 SoundOption=1 Autosave=60 FloatplaneLaunch=1 RealismSetting=1 '
           'NoFriendlyTorpHits=0 BSSMHC=40 | DarkMode=0 Scale=0 SoundVolume=25 MainFormState=0')
PREFS_B = ('ok AIControl=2 FloatWarning=2 ReadyPopup=0 BreakOnNewSighting=1 AirApproachMessage=1 AirSurfaceMessageLvl=0 '
           'LetAIControlForce=0 LOScalc=1 SoundOption=1 Autosave=60 FloatplaneLaunch=1 RealismSetting=1 '
           'NoFriendlyTorpHits=0 BSSMHC=40 | DarkMode=1 Scale=1 SoundVolume=5 MainFormState=0 | after the battle: '
           'AIControl 2 -> 0, RealismSetting 1 -> -1, BSSMHC 40 -> 35')
MAPVIEW_B = 'ok view=1 on=1 passes=25 side0=7/15 wake0=1 flags0=0/8 side1=31/31 wake1=31 flags1=14/14 playerSide=1'
MAPVIEW_A = 'ok view=0 on=1 passes=25 side0=15/15 wake0=15 flags0=8/8 side1=1/31 wake1=0 flags1=0/14 playerSide=0'
MAPVIEW_040 = 'ok view=1 on=1 passes=3 side0=15/15 wake0=15 flags0=8/8 side1=4/31 wake1=4 flags1=14/14 playerSide=1'
STATE_DUMP = ('RTW3MP 0.4.1 DESYNC report: B side 1 battle 100000 epoch 1 minute 6 (boundary here 6, engine level 15)\r\n'
              'raw boundary 6\r\nD0 deadbeef Side=01000000\r\n  S0.0 Fake Sunk=00 SightingLevel=05000000\r\n'
              'state\r\nt=12:36\r\n'
              'D0 GB Battle Division 2 DivisionCourse=58000000 DivisionSpeed=0c000000 Side=00000000 Formation=00\r\n'
              '  S0.0 Revenge ShipLocation=0102030405060708 Sunk=00 Sinking=00000000 SightingLevel=02000000 '
              'RadarSightingLevel=00000000 LastSighted=0c000000 SeenByHumanControlledShips=01 IdentifiedAsClass= '
              'TargetMain=CL Hela ManuallySelectedTarget=00\r\n'
              '  S0.1 Royal Sovereign Sunk=00 SightingLevel=00000000 RadarSightingLevel=01000000 '
              'SeenByHumanControlledShips=00\r\n'
              '    M0 TorpsLeft=01000000\r\n'
              'D1 GB Destroyer Division 28 Side=00000000\r\n'
              '  S1.0 Hood Sunk=01 SightingLevel=03000000 RadarSightingLevel=00000000 SeenByHumanControlledShips=01\r\n'
              'D2 Germany Battle Division 3 Side=01000000\r\n'
              '  S2.0 Wörth Sunk=00 SightingLevel=00000000 RadarSightingLevel=00000000 SeenByHumanControlledShips=00\r\n'
              '  S2.1 S 22 Sunk=00 SightingLevel=01000000 RadarSightingLevel=00000000 SeenByHumanControlledShips=01\r\n'
              'D3 Neutral Side=03000000\r\n  S3.0 Liner Sunk=00 SightingLevel=01000000 SeenByHumanControlledShips=01\r\n'
              'F0 Great Britain Main force divs=GB Battle Division 2 Side=00000000 AIControlled=00\r\n')
LINES2 = {
    'prefs': "01:12:21.003 [26604] lockstep: battle preferences: Admiral's for the load and the simulation (AIControl 0 "
             "-> 2, RealismSetting 2 -> 1, BSSMHC 35 -> 40); this PC's own come back after the battle",
    'prefs back': "01:13:48.100 [26604] lockstep: this PC's own battle preferences again (AIControl 2 -> 0, BSSMHC 40 -> "
                  "35)",
    'prefs dialog': "01:25:38.200 [34584] lockstep: the Preferences dialog changed battle preferences the simulation uses "
                    "(RealismSetting 1 -> -1): this battle's values put back",
    'ensign': "01:25:36.010 [34584] lockstep: the battle screen shows side 1's ensign",
    'locale': "01:11:20.500 [26604] TEST: RTW3MP_TEST_LOCALE=comma: decimal separator '.' -> ',', thousands '.', date "
              "'.', list ';'",
    'queued': '01:12:19.800 [26604] lockstep: battle 100000 epoch 1 queued for loading',
    'loaded': '01:12:22.500 [26604] lockstep: battle loaded from slot 87 (seed 2841265903, minute 0, t=12:26) in 2484 ms',
}
PROXY_LOG = ('01:11:52 proxy 127.0.0.1:47634 -> 127.0.0.1:47624: delay 120 ms, jitter +-60 ms, bandwidth 4000 kbit/s per '
             'direction\n01:11:57 connection 1 from 127.0.0.1:61001\n01:12:17 connection 1: up 10 chunks 1234 bytes '
             '(bandwidth wait max 0.00 s), down 20 chunks 2000000 bytes (max 3.10 s)\n01:20:00 connection 1 closed: up 12 '
             'chunks 2234 bytes (bandwidth wait max 0.01 s), down 40 chunks 6000000 bytes (max 3.90 s)\n01:21:00 '
             'connection 2 from 127.0.0.1:61005\n01:21:20 connection 2: up 1 chunks 10 bytes (bandwidth wait max 0.00 s), '
             'down 9 chunks 900000 bytes (max 1.75 s)\n')

HASH_A = ('14:20:23.436 [16412] lockstep hash A t=12:17 min=1 all=211145f3f7c0b80f pos=c52dff4c dmg=0794691d '
          'ammo=96d69bf4 sight=d0e55762 ident=a01a6238 ord=fe85cd41 air=4cdf56bf torp=67f80c33 force=67662527 '
          'sub=b7f67793 ships=51(30/21) sunk=0 divs=28 af=0 torps=0 draws sim=2110 ui=18 other=1026 rng=5b3ec0dc speed=3 '
          'date=1901-07-01\n'
          '14:20:23.612 [16412] lockstep hash A t=12:18 min=2 all=e2e8d28422c79fa6 pos=7379a5dd ident=a01a6238 rng=62da13ac '
          'speed=3 date=1901-07-01\n'
          '14:20:30.001 [16412] lockstep hash A t=12:18 min=2 all=ffff000011112222 pos=7379a5dd ident=a01a6238 rng=62da13ac '
          'speed=3 date=1901-07-01\n')
HASH_B = ('14:20:23.440 [ 6828] lockstep hash B t=12:17 min=1 all=211145f3f7c0b80f pos=c52dff4c ident=a01a6238 '
          'draws sim=2110 ui=3 other=7 rng=5b3ec0dc speed=3 date=1901-07-01\n'
          '14:20:30.010 [ 6828] lockstep hash B t=12:18 min=2 all=ffff000011112222 pos=7379a5dd ident=0badf00d '
          'rng=62da13ac speed=3 date=1901-07-01\n')

LINES = {
    'deferred ok': "10:45:05.241 [ 9972] lockstep: deferred Change of lead division (Great Britain Main force, new lead GB "
                   "Destroyer Division 28) of side 0 carried out at minute 0 (given for minute 0)",
    'deferred no': "10:45:40.100 [ 9972] lockstep: deferred Battle turn-away (Great Britain Main force, new lead GB Battle "
                   "Division 2) of side 0 (given for minute 1) not carried out at minute 1: the divisions' lead chain loops",
    'deferred torp': "12:01:00.000 [ 9972] lockstep: deferred torpedo salvo of side 1 carried out at minute 64 (given for "
                     "minute 64): Fame mount 1 at Brandenburg, course 271.5, speed 30, range 4000, manual 1: 3 torpedoes in "
                     "the water (3 -> 0 left, reload 12)",
    'deferred late': "12:02:00.000 [ 9972] lockstep: deferred Torpedo salvo of Fame of side 1 (given for minute 65) not "
                     "carried out at minute 66: it was meant for minute 65 and arrived too late",
    'deferred msl': "12:03:00.000 [ 9972] lockstep: deferred missile salvo of side 0 carried out at minute 30 (given for "
                    "minute 30): Kirov mount 2 (type 4) at a location, bearing 270, 2 of 2: 2 fired, 2 missiles in the air "
                    "(8 -> 6 left, reload 0)",
    'quiet box': "10:45:05.241 [ 9972] lockstep: deferred action's box 'Force lead division changed!\rYou might want to "
                 "update the lead division setting for other divisions.' answered 1 (chat)",
    'load date kept': '14:20:19.001 [16412] lockstep: the load: campaign date 1901-07-01 kept (the battle save said '
                      '1901-07-29)',
    'load date same': '14:20:19.001 [16412] lockstep: resume: campaign date 1901-07-01 (as the battle save: 1901-07-01)',
    'load vp back': "14:20:19.001 [16412] load: the game's blockade check gave victory points the save doesn't have; "
                    "put back: nation 0 1502 -> 1402, EnemyVP 1100 -> 1000 (blockaded: 1 3)",
    'load vp kept': "14:20:19.001 [16412] load: the game's blockade check gave victory points: nation 0 1402 -> 1502 "
                    "(blockaded: 1); kept: no multiplayer session (a load outside a session is the game's own)",
    'ident': "14:20:19.002 [16412] lockstep: the simulation's ship identification as the battle save leaves it (type 22, "
             "no class); this PC's display had identified 17 ship(s) during the load and keeps that",
    'fastest': 'lockstep: France turned Fastest available on (from the next minute).',
    'autorun q': 'lockstep: auto-run question (hand over) answered 1',
    'autorun rel': 'lockstep: auto-run home for side 1 at minute 7 (2 forces ordered to the closest friendly base; auto-run '
                   '01)',
    'landcap cover': 'lockstep: land CAP of side 1 over France Battle Division 8: Naval air station Brest covers it; the '
                     'game alone would have tasked Naval air station Plymouth of side 0 (t=06:50)',
    'landcap cover own': 'lockstep: land CAP of side 0 over GB Battle Division 11: Naval air station Plymouth covers it '
                         '(t=06:50)',
    'nightcap': 'lockstep: night CAP as before the load: on for 2 of 4 forces (2 put back)',
    'launch': "lockstep: launch from the screen: A42#0 -> AF1100001 'N strike #58' (3 aircraft, mission 3, ordered state 2, "
              "loadout 4, spot 0) at t=09:39",
    'replay': "lockstep: replayed launch from A41#0 -> AF1100000 'None #57'",
    'rejoin host': 'rejoin: Captain is back as nation 1: live battle 100000 starts again with them at its next minute '
                   'boundary (resumed from the host\'s save)',
    'rejoinable': 'lockstep: rejoin: battle 100000 (epoch 1) can be rejoined by Captain (Germany) while it goes on against '
                  'the computer (minute 4, t=08:34: Captain disconnected)',
}


def samples():
    f = R.parse_forces(FORCES)
    check('parse_forces: three forces, divisions, lead (None for -), the turn-away cooldown, ai, withdraw, mission',
          [(x['i'], x['side'], x['name'], x['divs'], x['lead'], x['turnaway'], x['ai'], x['mission']) for x in f] ==
          [(0, 0, 'Great Britain Main force', [0, 1], 0, '0', '0', '0'),
           (1, 1, 'Germany Main force', [2, 3, 4], 2, '12', '0', '1'),
           (2, 0, 'Great Britain Coastal forces', [16, 17], None, '0', '1', '1')], f)
    check('parse_forces: an error reply gives []', R.parse_forces('error no battle') == [])
    fd = R.parse_forces_div(FORCES_DIV)
    check('parse_forces_div: the divisions by index (force, lead None for -, role, flead, ai)',
          sorted(fd) == [0, 1, 3] and fd[1] == dict(side=0, name='GB Destroyer Division 28', force=0, admin='0', lead=0,
                                                    role='2', flead='0', ai='1') and fd[0]['lead'] is None, fd)
    p = R.parse_pending(PENDING)
    check('parse_pending: the pending deferred actions (seq, kind, minute, objects)',
          [(x['seq'], x['kind'], x['minute'], x['objs']) for x in p] ==
          [(1, 'lead', 0, ['Great Britain Main force', 'GB Destroyer Division 28']), (2, 'torp', 5, ['A12#0', 'A33#0'])], p)
    lc = R.parse_landcap(LANDCAP)
    check('parse_landcap: the requests with spaces in the names, the swap, the airfields and their capped divisions',
          lc.get('landcap0') == '0:GB Battle Division 11' and lc.get('landcap1') == '4:France Battle Division 8'
          and lc.get('swapped') == '1' and lc.get('mine') == '1' and lc.get('calls') == 8 and lc.get('other') == 1
          and lc.get('airfields') == [(27, 0, None), (28, 0, 0), (29, 1, 4), (30, 1, None), (31, 0, None)], lc)
    check('landcap_shared: the display swap and the copy\'s side left out',
          set(R.landcap_shared(lc)) == {'landcap0', 'landcap1', 'calls', 'other', 'airfields'})
    af = R.parse_airforce(AIRFORCE)
    check('parse_airforce: the settings as ints, settings without the count-downs, the land-CAP tail, the batch',
          af.get('force') == 0 and af.get('name') == 'Great Britain Main force' and af.get('left') == 185
          and af.get('nightcap') == 1 and af.get('ordered') == '0000000000111110000' and 'current=' not in af['settings']
          and af['landcap'].get('rule') == '1' and af.get('batch') == '#4' and af.get('flagship') == 'Repulse', af)
    head, items, rest = R.parse_airlist(AIRLIST)
    check('parse_airlist: the head\'s counters, every formation (ids, missions, states, numbers, places, names with '
          'colons), the items text', head.get('n') == '3' and head.get('auto') == '0' and len(items) == 3
          and items[1] == dict(id=1100000, side=1, m=3, s=2, o=2, num=3, loaded=3, l='4', p='42', L='-', g='0', c='0',
                               t='-4.123,50.250', at='-4.500,48.380', name='N strike #58')
          and items[2]['name'] == 'Recon: west' and rest.startswith('1:0:m0'), items)
    st = R.parse_strike(STRIKE)
    check('parse_strike: visible, fresh, the Ready state with its reason, the button handles (ready= twice), the rows',
          st.get('visible') == '1' and st.get('fresh') == '1' and st.get('readyok', '').startswith('0 (Pause')
          and st.get('readybtn') == 0xa3142c and st.get('launch') == 0x14f140a and st.get('lv') == 0x31e13fc
          and st.get('rows') == [(0, 34, False), (1, 36, True), (2, None, False)], st)
    st1 = R.parse_strike(STRIKE.replace('ready=0 (Pause the battle first: aircraft are launched, readied and stood down '
                                        'between minutes.)', 'ready=1'))
    check('parse_strike: ready=1 and the Ready button\'s handle told apart', st1.get('readyok') == '1'
          and st1.get('readybtn') == 0xa3142c, st1)
    rj, rs = R.parse_tail_why(REJOINABLE), R.parse_tail_why(RESUMABLE)
    check('parse_tail_why: `ls rejoinable` and `ls resumable` with the reason\'s spaces',
          rj.get('on') == '1' and rj.get('peer') == 'Captain' and rj.get('why') == 'Captain disconnected'
          and rs.get('held') == '1' and rs.get('marker', '').startswith('match:bid100003:epoch1:min0:')
          and rs.get('why') == 'the battle was loaded from the host\'s save', (rj, rs))
    ms = R.parse_missilestat(MISSILESTAT)
    check('parse_missilestat: Fire at will (faw, agreed, box), both sides\' stocks and counts, the interceptions, the '
          'nations, the salvo counters, the last salvo with spaces', ms.get('faw') == '10' and ms.get('box') == '1'
          and ms.get('held') == '0/2' and ms['side0'].get('heavySSM') == '8' and ms['side1'].get('msub') == '1/1'
          and ms.get('orders') == '1' and ms.get('fired') == '2' and ms.get('last', '').startswith('Kirov mount 2')
          and ms.get('intercepted') == {'shotDown': '2/1', 'bySAM': '2/0', 'decoyed': '0/1'}
          and ms.get('nations', '').startswith('0:4:msam1') and 'gate0 1:3' not in ms, ms)
    st2 = R.missile_stocks(ms)
    check('missile_stocks: per side the stocks both copies must agree on, and the interceptions',
          st2[0].get('pending') == '1' and st2[1].get('heavySAM') == '8' and 'faw' not in st2[0]
          and st2['x'].get('bySAM') == '2/0', st2)
    mo = R.parse_mounts(MOUNTS)
    check('parse_mounts: the ship and its mounts (left, reload, sub ?)', mo.get('ship') == 'Fame' and mo.get('n') == 2
          and mo['mounts'][1] == dict(type='0', pos='V', tubes='2', left='0', reload='4', reloads='0', destroyed='0',
                                      sub='?'), mo)
    mk = R.parse_marker(MARKER)
    check('parse_marker: the marker\'s key=value lines', mk.get('bid') == '100003' and mk.get('s0') == 'Great Britain'
          and mk.get('faw') == '11', mk)
    t1, t0 = R.parse_torp(TORP), R.parse_torp(TORP_NONE)
    check('parse_torp: queued, the ship, the button\'s enabled state, the mount, the result; no solution: queued=0, no '
          'mount', t1['queued'] and t1['ship'] == 'Fame' and t1['button'] == '1' and t1['mount'] == 1
          and t1['result'] == 'ordered for minute 64' and not t0['queued'] and t0['mount'] is None, (t1, t0))
    m1 = R.parse_missiles(MISSILES)
    check('parse_missiles: launchers queued, missiles, the result', m1['queued'] == 2 and m1['missiles'] == 6
          and m1['result'] == '2 launchers', m1)
    ga, gb = R.hash_groups(HASH_A), R.hash_groups(HASH_B)
    cmp_ = R.groups_compare(ga, gb)
    check('hash_groups/groups_compare: per minute the last line counts (a resync loads a minute again); `ident=` that '
          'differs at minute 2 is found, `all=` and `date=` equal', cmp_['minutes'] == 2 and cmp_['differ'] ==
          {'all': [], 'ident': [2], 'date': []} and cmp_['values']['date'] == ['1901-07-01'], cmp_)
    d = R.deferred_lines('\n'.join(LINES[k] for k in ('deferred ok', 'deferred no', 'deferred torp', 'deferred late',
                                                      'deferred msl')))
    check('deferred_lines: carried out (what, side, minute, given for), not carried out (why), torpedo and missile '
          'salvoes with their rest', [(x['what'], x['side'], x['ok'], x['m'], x['g']) for x in d] == [
              ('Change of lead division (Great Britain Main force, new lead GB Destroyer Division 28)', 0, True, 0, 0),
              ('Battle turn-away (Great Britain Main force, new lead GB Battle Division 2)', 0, False, 1, 1),
              ('torpedo salvo', 1, True, 64, 64), ('Torpedo salvo of Fame', 1, False, 66, 65),
              ('missile salvo', 0, True, 30, 30)]
          and d[1]['why'] == "the divisions' lead chain loops" and 'torpedoes in the water' in d[2]['rest']
          and 'arrived too late' in d[3]['why'] and 'missiles in the air' in d[4]['rest'], d)
    qb = R.quiet_boxes(LINES['quiet box'])
    check('quiet_boxes: the box\'s first line (its text has \\r), the answer, chat or quiet',
          qb == [('Force lead division changed!', 1, 'chat')], qb)
    ld = R.load_dates(LINES['load date kept'] + '\n' + LINES['load date same'])
    check('load_dates: "kept (the battle save said ...)" and "(as the battle save: ...)"',
          ld == [('the load', '1901-07-01', '1901-07-29'), ('resume', '1901-07-01', '1901-07-01')], ld)
    check('IDENT_LOAD_RE: the identification line of a live load', re.search(R.IDENT_LOAD_RE, LINES['ident']))
    m, mk = re.search(R.LOAD_VP_RE, LINES['load vp back']), re.search(R.LOAD_VP_RE, LINES['load vp kept'])
    check('LOAD_VP_RE (0.4.0): the load\'s blockade credit put back (what, the blockaded nations); the "kept" line of a '
          'load outside a session is not a put-back', m and m[1] == 'nation 0 1502 -> 1402, EnemyVP 1100 -> 1000'
          and m[2].split() == ['1', '3'] and mk is None, m and m.groups())
    lk = ['14:29:08.153 [ 1444] lockstep: LEAK the simulation state changed outside the simulation (sight) between '
          'EndOfTurnProcedures and a shared section at t=05:12; display routines in between: tvShipsChange,GlobeRender,'
          'FillShipList,GetTypeAndNameForEnemy,GetTypeAndNameForEnemy; Mecklenburg.SightingLevel 01000000->02000000',
          '14:29:08.181 [50616] lockstep: LEAK the simulation state changed outside the simulation (sight) between '
          'EndOfTurnProcedures and a shared section at t=05:12; display routines in between: none; Le Redoutable.'
          'RadarSightingLevel 01000000->02000000',
          '14:29:08.181 [50616] lockstep: LEAK the simulation state changed outside the simulation (pos,sight) between '
          'EndOfTurnProcedures and a shared section at t=05:12; display routines in between: none; Revenge.'
          'CurrentSpeed 2d000000->12000000 Revenge.SightingLevel 01000000->02000000']
    check('testcur_leak: a LEAK naming only the sighting guard\'s fields (group sight) is the `ls testcurspeed` baseline\'s; '
          'one with another group or field is not', [R.testcur_leak(x) for x in lk] == [True, True, False],
          [R.testcur_leak(x) for x in lk])
    saved_divs = R.ls_divs
    try:  # (the 1935 battle of dist_vp_2's default run: air stations only; and one with a carrier division per side)
        R.ls_divs = lambda port: R.__dict__['_divs_fixture']
        R._divs_fixture = [dict(i=0, side=0, name='GB Battle Division 2', ships=3), dict(i=4, side=1,
                           name='France Battle Division 3', ships=2), dict(i=9, side=0, name='GB Battery 3', ships=1)]
        none = R.carrier_divs(8001)
        R._divs_fixture += [dict(i=5, side=0, name='GB Carrier Division 4', ships=2),
                            dict(i=6, side=1, name='France Carrier Division 3', ships=0)]
        sunk = R.carrier_divs(8001)
        R._divs_fixture[-1]['ships'] = 1
        both = R.carrier_divs(8001)
    finally:
        R.ls_divs = saved_divs
        R.__dict__.pop('_divs_fixture', None)
    check('carrier_divs: a side\'s carrier divisions with ships (the strike dialog\'s precondition), none for air stations '
          'and batteries', none == {0: [], 1: []} and sunk == {0: ['GB Carrier Division 4'], 1: []}
          and both == {0: ['GB Carrier Division 4'], 1: ['France Carrier Division 3']}, (none, sunk, both))
    m = re.search(R.FASTEST_REQ_RE, LINES['fastest'])
    check('FASTEST_REQ_RE: the nation and on/off', m and m.groups() == ('France', 'on'), m and m.groups())
    m = re.search(R.AUTORUN_Q_RE, LINES['autorun q'])
    check('AUTORUN_Q_RE: the question and the answer', m and m.groups() == ('hand over', '1'), m and m.groups())
    m = re.search(R.AUTORUN_REL_RE, LINES['autorun rel'])
    check('AUTORUN_REL_RE: on/home/off, the side, the minute, the forces sent home, the agreed pair',
          m and m.groups() == ('home', '1', '7', '2', '01'), m and m.groups())
    m1, m2 = R.LANDCAP_COVER_RE.search(LINES['landcap cover']), R.LANDCAP_COVER_RE.search(LINES['landcap cover own'])
    check('LANDCAP_COVER_RE: the side, the division, the airfield, and what the game alone would have tasked',
          m1 and m1.groups() == ('1', 'France Battle Division 8', 'Naval air station Brest', 'Naval air station Plymouth',
                                 '0') and m2 and m2.groups()[3] is None, (m1 and m1.groups(), m2 and m2.groups()))
    m = re.search(R.NIGHTCAP_RE, LINES['nightcap'])
    check('NIGHTCAP_RE: forces on, of all, put back', m and m.groups() == ('2', '4', '2'), m and m.groups())
    m = R.LAUNCH_RE.search(LINES['launch'])
    check('LAUNCH_RE: the source, the new AFId (side 1\'s range), name, aircraft, mission, ordered state',
          m and m.groups() == ('42', '1100001', 'N strike #58', '3', '3', '2')
          and R.UI_AFID[1][0] <= int(m[2]) < R.UI_AFID[1][1], m and m.groups())
    m = R.REPLAY_RE.search(LINES['replay'])
    check('REPLAY_RE: the replayed launch', m and m.groups() == ('41', '1100000', 'None #57'), m and m.groups())
    m = re.search(R.REJOIN_HOST_RE, LINES['rejoin host'])
    check('REJOIN_HOST_RE: the player, the nation, the battle', m and m.groups() == ('Captain', '1', '100000'),
          m and m.groups())
    m = re.search(R.REJOINABLE_RE, LINES['rejoinable'])
    check('REJOINABLE_RE: battle, epoch, player, nation', m and m.groups() == ('100000', '1', 'Captain', 'Germany'),
          m and m.groups())
    check('STRIKE_PAUSE_LINE: the refusal of the strike buttons while a minute runs',
          R.STRIKE_PAUSE_LINE.endswith('aircraft are launched, readied and stood down between minutes.'))
    lines = ['e0080000', 'bf529a47', 'f0090000', '565c1c', '58d80d', 'ff529a47', 'f0090000', '80529a47', 'f009000a',
             '62a1df', 'bf000001', '6aa000', 'e0120001', '6aa001', 'ff000001', 'c0529a47']
    e, b = R.trace_entries(lines)
    check('trace_entries: display markers (e...) left out; the shared sections (bf.../ff...: idx 0x3f) hold the call '
          'sites between their ENTER and EXIT', len(e) == 14 and b == [['565c1c', '58d80d'], ['6aa000', '6aa001']], (e, b))
    check('expected_ls_level: this source tree\'s MP_LS_LEVEL (11 or later: the live-battle limits)',
          (R.expected_ls_level() or 0) >= 11, R.expected_ls_level())
    scan = ("22:41:07.512 [4120] data files: 1415 files of Data (but Data\\Graphics and the game's temporary files tmp*: "
            "Data\\tmpMapData.dat, Data\\tmpShip.ttp) hashed in 2531 ms: 7d1c0e5a9b3f2468\n"
            "22:41:09.001 [4120] data files: 1417 files of Data (but Data\\Graphics and the game's temporary files tmp*: "
            "none) hashed in 812 ms: 00000000000000ff\n")
    p1, p2 = R.parse_datafiles_scan(scan.split('\n')[0]), R.parse_datafiles_scan(scan)
    check('parse_datafiles_scan (engine level 18): the file count, the temporary files left out (lower case) and the '
          'hash; the last scan line of a log counts ("none": no temporary file); a log without one: None',
          p1 == dict(files=1415, temporary=['data\\tmpmapdata.dat', 'data\\tmpship.ttp'], hash='7d1c0e5a9b3f2468')
          and p2 == dict(files=1417, temporary=[], hash='00000000000000ff') and R.parse_datafiles_scan('x') is None,
          (p1, p2))
    m = re.search(R.AUTO_OFF_RE, 'lockstep: side 1 back to its player as in single player: the lead divisions of 2 forces; 1 '
                                 'forces stay with the computer as before auto-run; 9 divisions stay with the computer (31 '
                                 'ships) at t=05:40')
    check('AUTO_OFF_RE (engine level 12): the side, the lead divisions given back, the forces and divisions kept by the '
          'computer', m and m.groups() == ('1', '2', '1', '9', '31'), m and m.groups())
    box = ("12:00:00.000 [1] lockstep: the result box shows this player's side: 'German minor victory\r\rOwn VP: 120\r"
           "Enemy VP: 340' (the game's: 'Enemy minor victory\r\rBritish prestige +2\rOwn VP: 340\rEnemy VP: 120')\r\n"
           "12:00:00.001 [1] lockstep: next line\r\n")
    saved = R.log_since
    try:
        R.log_since = lambda port, mark=0: box
        r1 = R.result_box_side(0, 0)
        R.log_since = lambda port, mark=0: box.replace('German minor', 'Enemy minor', 1)
        r2 = R.result_box_side(0, 0)
        R.log_since = lambda port, mark=0: 'nothing\r\n'
        r3 = R.result_box_side(0, 0)
    finally:
        R.log_since = saved
    import tempfile
    with tempfile.TemporaryDirectory(dir='.') as td:
        dll = os.path.join(td, 'RTW3MP.dll')
        with open(dll, 'wb') as f:
            f.write(b'\x00MZ\x00mod 0.4.0\x00both mod 0.4.0, but different builds\x001.2\x0010.0.19041\x00x')
        v1 = R.build_version(dll)
        with open(dll, 'wb') as f:
            f.write(b'\x00mod 0.4.0\x00mod 0.3.3\x00')
        v2 = R.build_version(dll)
        with open(dll, 'wb') as f:
            f.write(b'\x00version 0.3.2\x00')
        v3 = R.build_version(dll)
    check('build_version: the one x.y.z after "mod " (another x.y.z such as an SDK version ignored), two: None; without '
          '"mod ": the one token', (v1, v2, v3) == ('0.4.0', None, '0.3.2'), (v1, v2, v3))
    real = {n: R.build_version(os.path.join(SESSION_FILES, n, 'RTW3MP.dll')) for n in ('dist_ll_15', 'dist_ll_16', 'dist_033')
            if os.path.exists(os.path.join(SESSION_FILES, n, 'RTW3MP.dll'))}
    if real:
        check('build_version on the snapshots at hand: dist_ll_15 0.6.0 (the branch), dist_ll_16 0.4.0, dist_033 0.3.3',
              all(v == {'dist_ll_15': '0.6.0', 'dist_ll_16': '0.4.0', 'dist_033': '0.3.3'}[n] for n, v in real.items()),
              real)
    check('expected_ctx_hooks: the context hooks of this source tree, hex stack sizes counted too (LaunchSomeAircraft '
          '0x14): 22', R.expected_ctx_hooks() == 22, R.expected_ctx_hooks())
    lg = ("10:00:00.000 [1] lockstep: DESYNC report ls_desync_A_100000_2_4.txt written (1051 kB; compare it with the "
          "other PC's: tools/desync_diff.py)\n10:00:01.000 [1] lockstep: DESYNC report ls_desync_B_100000_2_4.txt NOT "
          "written (3 kB; ...)\n")
    saved = R.log_since
    try:
        R.log_since = lambda port, mark=0: lg
        dr = R.desync_reports(0, 0)
        R.log_since = lambda port, mark=0: (
            "1 lockstep: the simulation's ship identification as the battle save leaves it (type 22, no class); this PC's "
            "display had identified 27 ship(s) during the load and keeps that\n2 lockstep: battle loaded from slot 1 "
            "(seed 1, minute 0, t=07:18) in 2968 ms\n3 lockstep: battle loaded from slot 1 (seed 2, minute 9, t=07:27) in "
            "2000 ms\n")
        il = R.ident_loads(0, 0)
        R.log_since = lambda port, mark=0: ("air units: 232 from before the load dropped; the campaign's 232 kept\n"
                                            "air units: 116 repeated units of the save dropped (232 -> 116; a save of an "
                                            "earlier version)\n")
        al = R.air_lines(0, 0)
    finally:
        R.log_since = saved
    check('desync_reports: the DESYNC report lines (file, A|B, battle, epoch, minute, written)', dr == [
        ('ls_desync_A_100000_2_4.txt', 'A', '100000', '2', '4', True),
        ('ls_desync_B_100000_2_4.txt', 'B', '100000', '2', '4', False)], dr)
    check('ident_loads: two live loads, the first with the identification line before it (type 22, 27 shown)',
          il == dict(loads=2, with_line=1, types=['22'], shown=[27]), il)
    check('air_lines: the load lines (N, M) only, not the repair line; AIR_REPAIR_RE its numbers',
          al == [(232, 232)] and re.search(R.AIR_REPAIR_RE, 'air units: 116 repeated units of the save dropped (232 -> '
                                                         '116; a save of an earlier version)').groups()
          == ('116', '232', '116'), al)
    race = ("20:59:39.313 [ 3764] battle 34: the nation's invasions after it: plan -/0, enemy plan -/-1, 0 invasion(s)\r\n"
            "20:59:39.313 [ 3764] battle 34: fought\r\n20:59:39.549 [ 3764] LoadCampaign slot 77 player 1 -> ok\r\n"
            "20:59:39.787 [ 3764] player copy in slot 87: Germany is the player nation, Great Britain is nation 1 (AI)\r\n")
    drv = type('Drv', (), {'cmark': 0})()
    saved = R.log_since
    try:
        R.log_since = lambda port, mark=0: race
        br1, br2 = R.battle_slot_race(drv, 34), R.battle_slot_race(drv, 35)
        R.log_since = lambda port, mark=0: race.replace('20:59:39.787', '20:59:44.787')
        br3 = R.battle_slot_race(drv, 34)
    finally:
        R.log_since = saved
    check('battle_slot_race (0.7.1 RC): the next battle\'s copy into the battle slot within 3 s of "battle N: fought" '
          '(the times named); no "fought" line or a later copy: None',
          br1 and '"fought" at 20:59:39.313' in br1 and 'slot 87 at 20:59:39.787' in br1 and br2 is None and br3 is None,
          (br1, br2, br3))
    check('GAME_ERROR_RE: the game\'s own error boxes, not its ordinary ones', R.GAME_ERROR_RE.search(
        'Error in SaveBuildCampaign procedure! Access violation at address 00B3F2A1') and R.GAME_ERROR_RE.search(
        'Out of memory') and not R.GAME_ERROR_RE.search('CV Furious aircraft capacity exceeded!'))
    check('result_box_side: the second player\'s result box (engine level 12): heads and points of both texts across the '
          'box\'s line breaks; ok for the victory from its side with the points swapped, not for a head left as the game '
          'wrote it; None without the line', r1 and r1['ok'] and (r1['head'], r1['own'], r1['enemy'], r1['g_head'],
                                                                 r1['g_own'], r1['g_enemy']) == (
              'German minor victory', '120', '340', 'Enemy minor victory', '340', '120') and r2 and not r2['ok']
          and r3 is None, (r1, r2, r3))


def twopc_samples():
    """twopc (0.4.1): the probes' replies, the mod's log lines of engine level 15 (the lines of the live proofs nb4-nb8),
    the map check's rules, the load cover's report, the proxy's log."""
    keys = R.battle_prefs_keys()
    check('battle_prefs_keys: the 14 battle preferences a live battle takes from A (kPrefs of this source tree), in order',
          keys and len(keys) == 14 and keys[0] == 'AIControl' and keys[-1] == 'BSSMHC' and 'RealismSetting' in keys
          and all(k in R.TWOPC_ALT for k in keys), keys)
    pa = R.parse_prefs(PREFS_A)
    pb = R.parse_prefs(PREFS_B)
    check('parse_prefs: the battle preferences, the display ones and B\'s own kept for after the battle (A -> B)',
          pa['sim'].get('AIControl') == 2 and pa['sim'].get('BSSMHC') == 40 and pa['disp'] == dict(
              DarkMode=0, Scale=0, SoundVolume=25, MainFormState=0) and pa['after'] == {} and pb['disp']['DarkMode'] == 1
          and pb['after'] == {'AIControl': (2, 0), 'RealismSetting': (1, -1), 'BSSMHC': (40, 35)}
          and R.parse_prefs('error no record') == {}, (pa, pb))
    m = re.search(R.PREFS_LINE_RE, LINES2['prefs'])
    b = re.search(R.PREFS_BACK_RE, LINES2['prefs back'])
    d = re.search(R.PREFS_DLG_RE, LINES2['prefs dialog'])
    check('PREFS_LINE_RE, PREFS_BACK_RE, PREFS_DLG_RE: A\'s name and each preference\'s values (B\'s -> A\'s, A\'s -> '
          'B\'s, the dialog\'s change)', m and m[1] == 'Admiral' and R.parse_pref_changes(m[2]) == {
              'AIControl': (0, 2), 'RealismSetting': (2, 1), 'BSSMHC': (35, 40)} and b
          and R.parse_pref_changes(b[1]) == {'AIControl': (2, 0), 'BSSMHC': (40, 35)} and d
          and R.parse_pref_changes(d[1]) == {'RealismSetting': (1, -1)}, (m and m.groups(), b and b.groups()))
    check('ENSIGN_LINE, LOCALE_RE, QUEUED_RE, LOADED_RE: the lines of nb6/nb8', R.ENSIGN_LINE in LINES2['ensign']
          and re.search(R.LOCALE_RE, LINES2['locale']) and re.search(R.QUEUED_RE, LINES2['queued'])
          and R.LOADED_RE in LINES2['loaded'])
    mb, ma = R.parse_mapview(MAPVIEW_B), R.parse_mapview(MAPVIEW_A)
    check('parse_mapview: view, the pass, PlayerSide and per side the ships shown/afloat, wakes, flags shown/divisions',
          mb == dict(view=1, on=1, passes=25, playerSide=1, shown=[7, 31], ships=[15, 31], wake=[1, 31], flags=[0, 14],
                     divs=[8, 14]) and ma['flags'] == [8, 0] and R.parse_mapview('error no battle') == {}, mb)
    check('map_problems: B\'s map of nb6 minute 12 and A\'s mirror are right; with the simulation\'s state the other side\'s '
          'ships shown must be exactly those this side sights, and the ships afloat the same',
          R.map_problems(mb, 1) == [] and R.map_problems(ma, 0) == []
          and R.map_problems(mb, 1, dict(afloat=[15, 31], sighted=7), load=True) == []
          and len(R.map_problems(mb, 1, dict(afloat=[15, 31], sighted=6), load=True)) == 1
          and len(R.map_problems(mb, 1, dict(afloat=[16, 31], sighted=7), load=True)) == 1,
          R.map_problems(mb, 1, dict(afloat=[15, 31], sighted=6), True))
    p040 = R.map_problems(R.parse_mapview(MAPVIEW_040), 1)
    check('map_problems: 0.4.0\'s picture on B (the first side\'s: B\'s own ships hidden under their flags, A\'s ships and '
          'flags shown, as nb1 captured it) is found: own ships hidden, A\'s flags shown, A\'s ships beyond those '
          'sighted', any('own ships shown 4 of 31' in x for x in p040) and any('8 of the other side' in x for x in p040)
          and R.map_problems(R.parse_mapview(MAPVIEW_040), 1, dict(afloat=[15, 31], sighted=2), load=True)[-1].startswith(
              'the other side\'s ships shown 15'), p040)
    ships = R.parse_state_ships(STATE_DUMP)
    check('parse_state_ships: a DESYNC report\'s state section by division side (names with spaces, the division index, '
          'the sighting fields little-endian, sunk ships kept, other sides and the raw boundary\'s lines left out)',
          [s['name'] for s in ships[0]] == ['Revenge', 'Royal Sovereign', 'Hood'] and [s['name'] for s in ships[1]] == [
              'Wörth', 'S 22'] and [(s['div'], s['sunk'], s['sight'], s['radar'], s['seen']) for s in ships[0]] == [
              (0, 0, 2, 0, 1), (0, 0, 0, 1, 0), (1, 1, 3, 0, 1)] and ships[1][0]['sight'] == 0
          and [s['div'] for s in ships[1]] == [2, 2], ships)
    check('map_ref: the ships afloat per side, the own ships of human forces, the other side\'s ships this side sights: '
          'the levels and SeenByHumanControlledShips between minutes, the levels alone right after a load',
          R.map_ref(ships, 1, False) == dict(afloat=[2, 2], own=2, sighted=1) and R.map_ref(ships, 1, True) == dict(
              afloat=[2, 2], own=2, sighted=2) and R.map_ref(ships, 0, False) == dict(afloat=[2, 2], own=2, sighted=1)
          and R.map_ref(ships, 0, False, {0: True}) == dict(afloat=[2, 2], own=0, sighted=1),
          (R.map_ref(ships, 1, False), R.map_ref(ships, 1, True), R.map_ref(ships, 0, False)))
    a_cmp = R.parse_mapview(MAPVIEW_A.replace('side0=15/15', 'side0=12/15'))
    check('map_problems: own ships of a computer force hidden until spotted (the game\'s rule) are no problem when ref '
          'says so; without ref every own ship must show', R.map_problems(a_cmp, 0, dict(afloat=[15, 31], own=11,
                                                                                         sighted=1)) == []
          and R.map_problems(a_cmp, 0, dict(afloat=[15, 31], own=13, sighted=1)) and R.map_problems(a_cmp, 0),
          R.map_problems(a_cmp, 0, dict(afloat=[15, 31], own=11, sighted=1)))
    check('hexint: memory-order hex as a little-endian signed integer', R.hexint('02000000') == 2
          and R.hexint('ffffffff') == -1 and R.hexint('01') == 1 and R.hexint('') is None and R.hexint('zz') is None)
    m = re.search(R.TWOPC_DESYNC_RE, 'lockstep: DESYNC report ls_desync_B_100000_1_4.txt written (1051 kB; ...)')
    check('TWOPC_DESYNC_RE: a DESYNC and the loaded battles differ, not the report line of `ls desyncreport`', m is None
          and re.search(R.TWOPC_DESYNC_RE, 'lockstep: DESYNC at minute 7: here a, Captain b')
          and re.search(R.TWOPC_DESYNC_RE, 'lockstep: the loaded battles differ: here x, Captain y'))
    check('map_problems: flags of the other side, more wakes than ships, another PlayerSide, an unreadable reply',
          R.map_problems(R.parse_mapview(MAPVIEW_B.replace('flags0=0/8', 'flags0=1/8')), 1)
          and R.map_problems(R.parse_mapview(MAPVIEW_B.replace('wake0=1', 'wake0=9')), 1)
          and R.map_problems(R.parse_mapview(MAPVIEW_B.replace('playerSide=1', 'playerSide=0')), 1)
          and R.map_problems({}, 1))
    q, e = 100.0, 103.0
    good = [(99.0, None), (100.1, 'hidden'), (102.885, 'clear'), (103.002, 'shown')]  # run 2's first load
    resync = [(90.0, 'shown'), (100.2, 'hidden'), (102.958, 'clear'), (103.003, 'shown')]  # the old picture until reset
    bad1 = [(99.0, None), (100.1, 'hidden'), (101.5, 'shown')]  # the load's picture shown (0.4.0: no cover)
    bad2 = [(90.0, 'shown')]  # a resync drawn into the old window, never hidden or clear
    bad3 = [(99.0, None), (100.1, 'hidden'), (102.9, 'shown')]  # no cover, shown 0.1 s before the load's end
    r = [R.cover_report(t, [(q, e)])[0] for t in (good, resync, bad1, bad2, bad3)]
    check('cover_report: a load whose window is hidden or clear until its end is not exposed (the old picture before the '
          'reset is not the load\'s); a window shown opaque in the load (also 0.1 s before its end), or never hidden '
          'nor clear, is exposed; cover seen and shown after reported', [(x['exposed'], x['cover'], x['after'])
                                                                         for x in r] == [
              (False, True, True), (False, True, True), (True, False, True), (True, False, True), (True, False, True)]
          and r[0]['states'][0] == (-3.1, None), [(x['exposed'], x['cover'], x['after'], x['states']) for x in r])
    nothing = R.cover_report([(99.0, None)], [(q, e)])[0]
    check('cover_report: a watch that never saw the window: not exposed, not shown after (the check fails on that)',
          not nothing['exposed'] and not nothing['after'], nothing)
    lw = R.load_windows('\n'.join((LINES2['queued'], LINES2['ensign'], LINES2['loaded'], LINES2['loaded'])))
    check('load_windows: "queued for loading" -> the next "battle loaded from slot" (seconds of the day), one per queue',
          lw == [(R.log_secs(LINES2['queued']), R.log_secs(LINES2['loaded']))], lw)
    px = R.parse_proxy_log(PROXY_LOG)
    check('parse_proxy_log: the head line, each connection\'s latest statistics (bytes and the bandwidth cap\'s wait per '
          'direction; closed)', px['head'].endswith('bandwidth 4000 kbit/s per direction') and px['conns'] == {
              1: dict(up=2234, down=6000000, up_wait=0.01, down_wait=3.9, closed=True),
              2: dict(up=10, down=900000, up_wait=0.0, down_wait=1.75, closed=False)}, px)
    check('as_int / parse_pref_changes: numbers and negatives; text and None give None', R.as_int('-1') == -1
          and R.as_int('x') is None and R.as_int(None) is None and R.parse_pref_changes('') == {})


def intel_samples():
    """The politics panel on a joined player's PC (0.4.1): `intelinfo`'s reply, IntelToString's labels and the slot ->
    nation rule of the relations view and the player's own intelligence together (intel_panel_bad)."""
    r = ('ok fields=0:2/1,1:0/0,2:0/0,3:1/1,4:0/0,5:3/3,6:0/0,7:0/0,8:0/0 cost=360 budget=360 balance=-1,234 '
         'labels=Med *|None|Low|None|High|None|None|None reports=4 client=1 month1=intel:-360/balance:100')
    ii = R.parse_intelinfo(r)
    check('parse_intelinfo: the nations\' spending/effort, the cost, the 8 labels (with " *"), reports, client; an error '
          'reply -> None', ii and ii['fields'][0] == (2, 1) and ii['fields'][5] == (3, 3) and len(ii['fields']) == 9
          and ii['cost'] == 360 and ii['labels'][0] == 'Med *' and len(ii['labels']) == 8 and ii['reports'] == 4
          and ii['client'] and R.parse_intelinfo('error unavailable') is None, ii)
    check('intel_label_ok: None/Low/Med/High by the spending, " *" only while the effort is below it',
          R.intel_label_ok('Med *', 2, 1) and R.intel_label_ok('High', 3, 3) and R.intel_label_ok('None', 0, 0)
          and not R.intel_label_ok('Med', 2, 1) and not R.intel_label_ok('Low', 2, 2)
          and not R.intel_label_ok('None *', 0, 0) and not R.intel_label_ok('High', 4, 4))
    names = {0: 'Great Britain', 1: 'Germany', 2: 'France', 3: 'Russia', 4: 'Spain', 5: 'Japan', 6: 'United States',
             7: 'Italy', 8: 'Austria-Hungary'}

    def view(me):
        return dict(slots={i: dict(flag=names[0 if i == me else i], hint='', war=False, ally=False, block=False)
                           for i in range(1, 9)})
    rv = view(1)
    own_label = dict(ii, labels=['None'] + ii['labels'][1:])  # slot 1 showing nation 1's own pair, not the host's
    own_flag = dict(slots={**rv['slots'], 1: dict(rv['slots'][1], flag='Germany')})
    host_ii = R.parse_intelinfo(r.replace('labels=Med *|None|', 'labels=None|None|').replace('client=1', 'client=0'))
    check('intel_panel_bad: a joined player\'s panel (slot 1 = the host\'s flag with the player\'s own spending on the '
          'host) passes; its own nation\'s label under the host\'s flag, its own flag in slot 1, client=0, a missing '
          'reply fail; the host\'s panel (slot i = nation i, client=0) passes and fails as a player\'s',
          R.intel_panel_bad(1, names, rv, ii) == [] and R.intel_panel_bad(1, names, rv, own_label)
          and R.intel_panel_bad(1, names, own_flag, ii) and R.intel_panel_bad(1, names, rv, dict(ii, client=False))
          and R.intel_panel_bad(1, names, None, ii) and R.intel_panel_bad(0, names, view(0), host_ii) == []
          and R.intel_panel_bad(1, names, view(0), host_ii),
          (R.intel_panel_bad(1, names, rv, ii), R.intel_panel_bad(0, names, view(0), host_ii)))
    # the game's gun-comparison repeat (0.6.0 RC1: SpySuccess's calibre loop) in a save's [IntelReports]
    gun = 'Jun 1901  France - Our 7 in guns are superior to French guns of the same calibre.'
    other = 'Jun 1901  Italy - New ship under construction of class CL Iride is rumoured to carry 7 in main guns.'
    better = 'Jul 1901  Russia - Russian 9 in guns have better performance than ours.'

    def sec(*v):
        return f'[General]\nX=1\n[IntelReports]\nReportNo={len(v)}\n' + ''.join(
            f'Intel{i}={x}\n' for i, x in enumerate(v)) + '[Other]\nY=2\n'
    s_gun, s_oth, s_bet = (R.intel_section_stats(sec(*v)) for v in ((gun, other, gun), (gun, other, other),
                                                                     (better, better, gun)))
    s_clean, s_empty = R.intel_section_stats(sec(gun, other)), R.intel_section_stats(sec(gun, '', other))
    check('intel_section_stats / saved_clean_game: a repeated gun-comparison report ("Our N in guns are superior to X '
          'guns of the same calibre." / "X N in guns have better performance than ours.") is the game\'s own repeat '
          '(gun=1: allowed), any other repeat or an empty entry is not; a clean list passes both',
          s_gun['repeated'] == s_gun['gun'] == 1 and R.saved_clean_game(s_gun) and not R.saved_clean(s_gun)
          and s_oth['repeated'] == 1 and s_oth['gun'] == 0 and not R.saved_clean_game(s_oth)
          and s_bet['gun'] == 1 and R.saved_clean_game(s_bet)
          and R.saved_clean(s_clean) and R.saved_clean_game(s_clean) and not R.saved_clean_game(s_empty)
          and R.intel_section_stats('[General]\nX=1\n') is None, (s_gun, s_oth, s_bet, s_empty))
    # the client's save repeats what its own monthly check repeated the month before (0.6.0 RC2, dist_av_4 arms_treaty M2)
    s_own2 = R.intel_section_stats(sec(other, other))
    check('saved_own_repeat: the client\'s save may repeat as many entries as its own monthly check repeated in its list '
          'the month before (the host sends the player\'s reports back), never more, never with an empty entry or '
          'without such a repeat',
          R.saved_own_repeat(s_oth, 1) and R.saved_own_repeat(s_own2, 1) and R.saved_own_repeat(s_own2, 2)
          and not R.saved_own_repeat(s_own2, 0) and not R.saved_own_repeat(s_own2, None)
          and not R.saved_own_repeat(R.intel_section_stats(sec(other, other, other)), 1)
          and not R.saved_own_repeat(R.intel_section_stats(sec(other, '', other)), 1)
          and not R.saved_own_repeat(s_clean, 1) and not R.saved_own_repeat(None, 1),
          (s_oth, s_own2))
    sk = ("13:34:33.569 [37012] relations v2: peace of nation 1: ship 495 (Imperator Aleksandr III) skipped: not among "
          "nation 1's ships\n13:34:33.570 [37012] relations v2: peace of nation 1: ship 12 (X (Y)) skipped: not among "
          "nation 2's ships\n")
    check('PEACE_SHIP_SKIPPED: the host\'s "ship N (name) skipped: not among nation K\'s ships" for nation K only',
          [int(m[1]) for m in re.finditer(R.PEACE_SHIP_SKIPPED.format(K=1), sk)] == [495])
    cl = ('14:09:20.322 [38852] victory points shown: own 5548, enemy 673\n'
          '14:09:34.970 [38852] victory points shown: own 5730, enemy 675\n')
    peace = ("14:09:38.799 [38852] relations v2: peace: DoPeace (nation 1's wars with the computer's nations end)\n")
    cv0 = dict(vp={1: 0, 7: 675}, enemy=0)
    check('vp_shown_ok: the latest shown line equals vpinfo; after nation 1\'s own peace (DoPeace in its relation turn) '
          'its VP and Enemy VP 0 pass; 0/0 without that peace, or the peace before the last load, fail',
          R.vp_shown_ok(cl, dict(vp={1: 5730}, enemy=675)) == (True, False)
          and R.vp_shown_ok(cl + peace, cv0) == (True, True) and R.vp_shown_ok(cl, cv0) == (False, False)
          and R.vp_shown_ok(peace + cl, cv0) == (False, False) and R.vp_shown_ok(cl + peace, None) == (False, False)
          and R.vp_shown_ok(cl + peace, dict(vp={1: 10}, enemy=0))[0] is False)
    # lockstep_pvp f): the host's CreditWarVP lines of the players' war count (dist_rc060_2's default run: B5 blockade)
    req = '18:00:40.100 [45452] battle request 32: nation 1 vs 2 in North Sea (5:5 ships), fought live by Captain and Bot\n'
    blk = ('18:00:16.874 [45452] victory points: nation 2 +100 (blockade, in its war with nation 1) -> 291, war score 1-2 '
           '55:291\n18:00:16.874 [45452] victory points: nation 2 +140 (blockade, in its war with nation 1) -> 431, war '
           'score 1-2 55:431\n18:00:16.900 [45452] victory points: nation 3 +50 (blockade, in its war with nation 1) -> '
           '90\n')
    tail = ('18:00:53.595 [45452] battle 32 applied: 4 ships changed (24 fields), 0 removed, VP 0/0, prestige 0 (player: '
            'VP 0/0, prestige 0, morale 0)\n18:00:54.238 [45452] war score 1-2: +2/+0 -> 57:431\n')
    ws = dict(before=dict(a=55, b=191), after=dict(a=57, b=431), war='1')
    check('pvp_war_score_check: the host\'s CreditWarVP credits of the war 1-2 ("victory points: nation G +v (blockade, '
          'in its war with nation L) -> t, war score 1-2 x:y") count with the battle hooks; a credit of another pair '
          'does not; without the credit lines the same score fails',
          R.pvp_war_score_check(dict(ws, log=blk + tail), req) is True
          and R.pvp_war_score_check(dict(ws, log=tail), req) is False
          and bool(re.search(R.WAR_VP_CREDIT_RE, blk.splitlines()[0])) and not re.search(R.WAR_VP_CREDIT_RE,
                                                                                         blk.splitlines()[2]))
    # treaty k): the war score 1-2 steps in log order (dist_rc060_2's --stress run: blockade points before the battle line)
    tk = ('18:18:13.525 [9180] victory points: nation 1 +100 (blockade, in its war with nation 2) -> 100, war score 1-2 '
          '1000:100\n18:18:13.525 [9180] victory points: nation 1 +90 (blockade, in its war with nation 2) -> 190, war '
          'score 1-2 1090:100\n18:18:14.000 [9180] victory points: nation 3 +40 (blockade, in its war with nation 1) -> 40'
          '\n18:18:17.934 [9180] war score 1-2: +2/+0 -> 1092:100\n')
    st = R.war_score_steps(tk, 1, 2)
    check('war_score_steps: the host\'s battle lines and CreditWarVP lines of the war 1-2 in log order (another pair\'s '
          'credit left out)', st == [(100, 0, 1000, 100, 'blockade'), (90, 0, 1090, 100, 'blockade'),
                                     (2, 0, 1092, 100, 'battle')], st)
    # battles b): the area of a fought battle from its request line (dist_rc060_2 --stress: the player's generator's)
    rq = ("18:14:34.842 [ 9180] battle request 3 (the player's generator): nation 1 vs 7 in Northern Europe, 4 "
          "attempt(s), fought by Captain\n18:14:30.000 [ 9180] battle request 1 injected: nation 1 vs 7 in The Baltic\n"
          "18:14:31.000 [ 9180] battle request 5: nation 1 vs 2 in North Sea (5:6 ships), fought live by Captain and Bot\n")
    check('battle_area: the sea area of a request line (the player\'s generator\'s ", N attempt(s), fought by X", an '
          'injected one, a live one with its ship counts; the default without a line)',
          (R.battle_area(rq, 3, 'x'), R.battle_area(rq, 1, 'x'), R.battle_area(rq, 5, 'x'), R.battle_area(rq, 9, 'x'))
          == ('Northern Europe', 'The Baltic', 'North Sea', 'x'),
          (R.battle_area(rq, 3, 'x'), R.battle_area(rq, 1, 'x'), R.battle_area(rq, 5, 'x')))


def relations_samples():
    """Relations v2 (0.5.0): the joined player's own events of the new month change nation 1's pairs after the host's
    month (event_tension_changes, read by relrules_month_check), in log order, both pair orders, other nations' events
    left out."""
    hl = ("22:25:56.596 [47160] event of Captain (nation 1, #-1 'Intelligence'): tension 1-4 3->4 intel: 0 reports\n"
          "22:26:43.264 [47160] event of Captain (nation 1, #34 'A new Government (wants) to reduce: naval'): tension "
          "1-2 10->11 tension 1-3 3->4 tension 4-1 4->5\n"
          "22:26:44.000 [47160] event of Bot (nation 2, #5 'XX appears'): tension 2-3 7->9\n"
          "22:26:45.000 [47160] second event of Captain in Aug 1900 ignored (one per month)\n")
    got = R.event_tension_changes(hl)
    check('event_tension_changes: nation 1\'s event tension changes in log order (the intelligence result, a random event '
          'with "(" and ":" in its caption, j-1 as 1-j), another nation\'s event and an ignored second event left out',
          got == [(4, 3, 4), (2, 10, 11), (3, 3, 4), (4, 4, 5)] and R.event_tension_changes(hl, 2) == [(3, 7, 9)], got)
    el = ("22:27:19.165 [47160] economy: nation 1 (Germany) 1901-01: funds 10505 -> 13780 (balance +3275 at the player's "
          "prices, maintenance +0); withheld: subsidy 150; the player's money rules (war counter -72): debt none; army "
          "offensive 0 month(s) (budget -0), ParliamentDelay 0; month's end funds 13780 (other changes +0); left out for "
          "the player: escorts/ASW 0\n"
          "22:28:06.358 [47160] economy: nation 1 (Germany) 1901-01: no ComputeExpenses; withheld: subsidy 0; the player's "
          "rules: preeminence 0 (+0), war start +1 (computer's +2); month's end funds unknown; left out for the player: 0\n")
    pat = R.ECON_LINE_RE.format(n=1, date=re.escape('1901-01'))
    lines = R.log_lines(el, pat)
    m = re.search(pat, lines[-1]) if lines else None
    check('ECON_LINE_RE: the month\'s own economy line of nation 1 (funds a -> b, the month\'s end funds and other '
          'changes), not the "no ComputeExpenses" line of a war that started after the month\'s end, not nation 2',
          len(lines) == 1 and m and m.groups() == ('10505', '13780', '13780', '+0')
          and not R.log_lines(el, R.ECON_LINE_RE.format(n=2, date=re.escape('1901-01'))), (lines, m and m.groups()))

    def copy_war(war, k_at_war, kwar):
        src = {'General': {'War': str(war), 'EnemyVP': '0'}, 'Nation0': {'Tension': '0'},
               'Nation1': {'Tension': '0', 'AITension2': '50' if k_at_war else '3', 'VP': '0'},
               'Nation2': {'Tension': '3', 'AITension1': '50' if k_at_war else '3', 'VP': '7'}}
        return R.expected_copy(src, 1, None, kwar)[0]['General']['War']
    got = [copy_war(-1, False, 0), copy_war(-1, False, None), copy_war(5, False, -3), copy_war(-1, True, 4),
           copy_war(-1, True, -2), copy_war(5, False, 2)]
    check('expected_copy (0.5.0): the copy\'s [General] War = K\'s own war counter when it agrees with K\'s wars (0 at '
          'peace after the last war, -3, 4 at war), else the host\'s guess (-1; -12 after the host\'s war; 1 at war)',
          got == ['0', '-1', '-3', '4', '1', '-12'], got)
    rep = ('Unrest is reduced as there is room in the government budget for some modest welfare programmes. | USA has laid '
           'down a CA of Indianapolis class! | Intelligence report France: Our 7 in guns are superior to French guns of the '
           'same calibre. | Research breakthrough: Submarines, Early coastal submarine, Enables building of coastal '
           'submarines')
    got = (R.sea_war_box_lines(rep), R.sea_war_box_lines('Our scientists report progress on submarines.'),
           R.sea_war_box_lines(rep + ' | German raider B Wittelsbach sinks 2 Russian merchant ships in The Baltic'))
    check('sea_war_box_lines (war_effects M2 (c)): a host turn report with only its own research line on submarines (a '
          'run of 2026-10-23) and a research box have none; a raider\'s line in the report is found',
          got == ([], [], ['German raider B Wittelsbach sinks 2 Russian merchant ships in The Baltic']), got)


def divisions_samples():
    """Campaign divisions (0.6.1): the bridge's `divs` / `divstore <K>` replies (parse_divs) and an .off's
    [CampaignDivisions] (off_divisions)."""
    got = R.parse_divs('ok n=2 nation=1 list=5:RT K Div 1:2:174.175|6:RT K Div 2R_x:1:150')
    check('parse_divs: `divs` with two divisions (nation, ids, a name with ":" made "_")',
          got and got['n'] == 2 and got['nation'] == 1 and [(d['id'], d['name'], d['ships'], d['ids']) for d in got['divs']]
          == [('5', 'RT K Div 1', 2, ['174', '175']), ('6', 'RT K Div 2R_x', 1, ['150'])], got)
    got = [R.parse_divs(x) for x in ('ok n=0 nation=0 list=', 'ok n=0 list=', 'ok n=1 list=990001:Host Div A:0:',
                                     'ok n=2 nation=0 list=nil|7:A:2:nil.12')]
    check('parse_divs: none (`divs`, `divstore` without nation), a division without ships, a nil division and a nil ship',
          got[0] and got[0]['n'] == 0 and got[0]['nation'] == 0 and got[0]['divs'] == []
          and got[1] and got[1]['nation'] is None and got[1]['divs'] == []
          and got[2] and got[2]['divs'] == [dict(id='990001', name='Host Div A', ships=0, ids=[])]
          and got[3] and got[3]['divs'][0]['id'] is None and got[3]['divs'][1]['ids'] == ['nil', '12'], got)
    bad = [R.parse_divs(x) for x in ('error unavailable', 'error the joined nations\' records are not loaded (host only)',
                                     'ok n=2 nation=1 list=5:A:2:1.2', 'ok n=1 nation=1 list=5:A:x:1', 'ok 3', None)]
    check('parse_divs: an error reply, a count that disagrees with the list, a bad ship count -> None', bad == [None] * 6,
          bad)
    off = R.parse_save('[General]\r\nYear=1900\r\n[CampaignDivisions]\r\nCampDivNo=2\r\nCampDiv0CDivId=990001\r\n'
                       'CampDiv0Name=Host Div A\r\nCampDiv0ShipCount=2\r\nCampDiv0ShipId0=122\r\nCampDiv0ShipId1=123\r\n'
                       'CampDiv1CDivId=990002\r\nCampDiv1Name=Host Div B\r\nCampDiv1ShipCount=0\r\n'.encode())
    got = R.off_divisions(off.get('CampaignDivisions'))
    check('off_divisions: CampDivNo and each division\'s id, name, ship ids; none for a missing section',
          got == dict(no=2, divs=[dict(id='990001', name='Host Div A', ids=['122', '123']),
                                  dict(id='990002', name='Host Div B', ids=[])])
          and R.off_divisions(None) == dict(no=None, divs=[])
          and R.div_summary(got) == 'Host Div A[122.123]; Host Div B[]', got)


def tracefiles(sandbox):
    d = os.path.join(sandbox, 'traces')
    os.makedirs(d)
    a = ['e0080000', 'bf529a47', 'f0090000', '565c1c', 'ff529a47', '80aaaaaa', '62a1df', 'c0aaaaaa']
    b = ['e0080001', 'bf529a47', 'f0090000', '565c1c', 'ff529a47', '80bbbbbb', '62a1df', 'c0bbbbbb', 'e0120000']
    for side, lines in (('A', a), ('B', b)):
        with open(os.path.join(d, f'ls_trace_{side}_100000_1_5.txt'), 'w') as f:
            f.write('\n'.join(lines) + '\n')
    with open(os.path.join(d, 'ls_trace_A_100000_1_6.txt'), 'w') as f:
        f.write('\n'.join(a) + '\n')
    with open(os.path.join(d, 'ls_trace_B_100000_1_6.txt'), 'w') as f:
        f.write('\n'.join(a[:3] + ['565c1d'] + a[4:]) + '\n')
    with open(os.path.join(d, 'ls_dump_A_100000_1_6.txt'), 'w') as f:
        f.write('x')
    with open(os.path.join(d, 'ls_trace_A_200000_1_6.txt'), 'w') as f:
        f.write('x')
    t5 = R.trace_compare(100000, 1, 5, folder=d)
    check('trace_compare: equal call sites and marker kinds (the frame markers\' hash bits counted apart: hash_diff 2)',
          t5['found'] and t5['first_diff'] is None and t5['hash_diff'] == 2 and t5['blocks_equal'], t5)
    t6 = R.trace_compare(100000, 1, 6, folder=d)
    check('trace_compare: a different call site is found (its index)', t6['first_diff'] == 2 and not t6['blocks_equal'],
          t6)
    check('trace_compare: a missing minute', not R.trace_compare(100000, 1, 7, folder=d)['found'])
    n = R.trace_cleanup({100000}, folder=d)
    check('trace_cleanup: this run\'s battles\' trace and dump files deleted, another battle\'s kept',
          n == 5 and os.listdir(d) == ['ls_trace_A_200000_1_6.txt'], (n, os.listdir(d)))


# ---------------------------------------------------------------- the runner's plan in a sandbox

def write(p, text):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, 'w', encoding='utf-8') as f:
        f.write(text)


def digest(d):
    return R.tree_digest(d)


@contextlib.contextmanager
def sandboxed(sandbox):
    """The runner's paths pointed into the sandbox (saves, logs, Preferences, its backups, the game folder)."""
    names = ('SAVE', 'LOG_DIR', 'PREFS_FILE', 'PREFS_BACKUP', 'BACKUP', 'F1935_BACKUP', 'SNAP_DIR', 'SNAPSHOT_DIRS')
    saved = {k: getattr(R, k) for k in names}
    saved_game = R.ctl.GAME
    root = os.path.join(sandbox, 'Rule the Waves 3')
    R.SAVE = os.path.join(root, 'Save')
    R.LOG_DIR = os.path.join(root, 'RTW3MP')
    R.PREFS_FILE = os.path.join(root, 'Preferences')
    R.PREFS_BACKUP = os.path.join(sandbox, 'tmp', 'rtw3mp_regression_backup_Preferences')
    R.BACKUP = os.path.join(sandbox, 'tmp', 'rtw3mp_regression_backup_Game1')
    R.F1935_BACKUP = os.path.join(sandbox, 'tmp', 'rtw3mp_regression_backup_Game9')
    R.SNAP_DIR = os.path.join(sandbox, 'tmp', 'rtw3mp_regression_snapshot_Game1')
    R.SNAPSHOT_DIRS = (R.SNAP_DIR,)
    R.ctl.GAME = os.path.join(sandbox, 'game')
    tmp_saved = R.tempfile.tempdir
    R.tempfile.tempdir = os.path.join(sandbox, 'tmp')
    try:
        yield root
    finally:
        for k, v in saved.items():
            setattr(R, k, v)
        R.ctl.GAME = saved_game
        R.tempfile.tempdir = tmp_saved
        R.MISSILE.update(path=None, slot=None, nations=None, mission=0, why=None)


def run_main(args):
    out = io.StringIO()
    code = None
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
        try:
            code = R.main(args)
        except SystemExit as e:
            code = ('exit', e.code)
    return code, out.getvalue()


def runner(sandbox):
    with sandboxed(sandbox) as root:
        os.makedirs(os.path.join(sandbox, 'tmp'))
        os.makedirs(os.path.join(sandbox, 'game'))
        write(os.path.join(R.SAVE, 'Game1', 'RTWGame1.bcs'), '[General]\nYear=1900\nMonth=8\nDay=1\nScenarioInProgress=-1\n'
                                                            '[Nation0]\nName=Great Britain\n[Nation1]\nName=Germany\n')
        write(os.path.join(R.SAVE, 'Game9', 'RTWGame9.bcs'), '[General]\nCampaignStartYear=1935\nYear=1935\nMonth=1\nDay=1\n'
                                                            'ScenarioInProgress=-1\n[Nation0]\nName=Great Britain\n'
                                                            '[Nation2]\nName=France\n')
        write(R.PREFS_FILE, 'MapZoom=3\nRealismSetting=1\nSound=1\n')
        fix = os.path.join(sandbox, 'fixture_late', 'Game5')
        write(os.path.join(fix, 'RTWGame5.bcs'), '[General]\nCampaignStartYear=1935\nYear=1958\nMonth=4\nDay=1\n'
                                                 'ScenarioInProgress=-1\n[Nation0]\nName=Italy\n[Nation3]\n'
                                                 'Name=Soviet Union\n[AirUnits]\nAirUnitNo=2\nAU0Id=5\nAU0Name=A\n'
                                                 'AU1Id=7\nAU1Name=B\n')
        write(os.path.join(fix, 'MapData5.dat'), '[MapAreas]\n')
        # --list and the groups
        code, out = run_main(['--list'])
        check('--list: exit 0, every scenario with its game time and where it runs, the groups', code == 0
              and all(re.search(rf'^{s[0]} ', out, re.M) for s in R.SCENARIOS) and 'groups: limits:' in out
              and re.search(r'^lockstep_limits +0 +\d+ +default, limits', out, re.M), out[-400:])
        check('GROUPS, STRESS_ONLY and EST name only scenarios of the suite; every scenario has a time estimate',
              all(n in [s[0] for s in R.SCENARIOS] for v in R.GROUPS.values() for n in v)
              and all(n in [s[0] for s in R.SCENARIOS] for n in R.STRESS_ONLY)
              and all(s[0] in R.EST for s in R.SCENARIOS), (R.GROUPS, R.STRESS_ONLY))
        dflt = [s[0] for s in R.SCENARIOS if s[0] not in R.STRESS_ONLY]
        check('the default run: lockstep_ui, lockstep_limits, lockstep_air and twopc in it; rejoin, resume, torpedo, '
              'strike and missiles not', {'lockstep_ui', 'lockstep_limits', 'lockstep_air', 'twopc'} <= set(dflt)
              and not ({'lockstep_rejoin', 'lockstep_resume', 'lockstep_torpedo', 'lockstep_strike', 'lockstep_missiles'}
                       & set(dflt)) and R.GROUPS.get('twopc') == ('twopc',) and 'twopc' in R.EST_STRESS, dflt)
        order = [s[0] for s in R.SCENARIOS]
        check('the order: the slot-1 scenarios before lockstep_air, lockstep_strike and lockstep_missiles (they move the '
              'session to other campaigns); twopc right after lockstep_month', all(order.index(x) < order.index(
                  'lockstep_air') for x in ('lockstep_limits', 'lockstep_rejoin', 'lockstep_resume', 'lockstep_torpedo',
                                            'lockstep_stress', 'twopc'))
              and order.index('lockstep_air') < order.index('lockstep_strike') < order.index('lockstep_missiles')
              and order.index('twopc') == order.index('lockstep_month') + 1, order)
        code, out = run_main(['--group', 'nosuch'])
        check('--group with an unknown group: an argument error', code == ('exit', 2) and 'unknown group' in out, out[-300:])
        # --dry-run: nothing written, nothing deleted
        before = digest(sandbox)
        code, out = run_main(['--dry-run'])
        check('--dry-run (default): the plan (the scenarios with their game time, slot 1, the 1935 campaign), the game '
              'files missing in the sandbox reported, exit 2; the sandbox unchanged', code == 2 and 'lockstep_limits' in out
              and 'lockstep_rejoin' not in out and 'RTW3.exe not found in the game folder' in out
              and 'the 1935 test campaign (slot 9): ok' in out and digest(sandbox) == before, out[-1500:])
        code, out = run_main(['--dry-run', '--stress'])
        check('--dry-run --stress: the stress-only scenarios and the long variants\' times', code == 2
              and 'lockstep_resume' in out and 'lockstep_stress' in out and '(long variants: --stress)' in out
              and 'SKIP lockstep_missiles' in out and digest(sandbox) == before, out[-1500:])
        code, out = run_main(['--dry-run', '--group', 'torpedo'])
        check('--dry-run --group torpedo: session and lockstep_torpedo; RealismSetting 1 would be set to 2 for the run, '
              'the Preferences unchanged now', code == 2 and 'lockstep_torpedo' in out and 'lockstep_ui' not in out
              and 'would set RealismSetting=2' in out and R.realism_pref() == 1 and digest(sandbox) == before,
              out[-1200:])
        code, out = run_main(['--dry-run', '--group', 'missiles', '--missile-fixture', fix, '--missile-nations', '0,3'])
        check('--dry-run --group missiles with a fixture folder: copied into the free slot 5 for the run (not now), the '
              'fixture read', code == 2 and 'copied into the free save slot 5' in out and 'ok {' in out
              and not os.path.exists(R.slot_dir(5)) and digest(sandbox) == before, out[-1200:])
        code, out = run_main(['--dry-run', '--only', 'lockstep_rejoin,lockstep_air'])
        check('--dry-run --only: session first, the suite\'s order, the third copy\'s slots for lockstep_rejoin',
              code == 2 and out.index('session') < out.index('lockstep_rejoin') < out.index('lockstep_air')
              and '76, 86' in out, out[-1200:])
        # a leftover backup of an interrupted run
        os.makedirs(os.path.dirname(R.PREFS_BACKUP), exist_ok=True)
        write(R.PREFS_BACKUP, 'RealismSetting=3\n')
        p = R.preflight(os.path.join(R.SAVE, 'Game1'), dry=True)
        check('preflight: a Preferences backup that differs from the file is a problem (the user\'s settings)',
              any('Preferences from an interrupted run' in x for x in p) and os.path.exists(R.PREFS_BACKUP), p)
        shutil.copyfile(R.PREFS_FILE, R.PREFS_BACKUP)
        p = R.preflight(os.path.join(R.SAVE, 'Game1'), dry=True)
        check('preflight (dry): an identical leftover backup is kept, only reported', os.path.exists(R.PREFS_BACKUP)
              and not any('Preferences' in x for x in p), p)
        with contextlib.redirect_stdout(io.StringIO()):
            R.preflight(os.path.join(R.SAVE, 'Game1'))
        check('preflight (a run): an identical leftover backup is removed', not os.path.exists(R.PREFS_BACKUP))
        # Preferences for lockstep_torpedo
        raw0 = open(R.PREFS_FILE, 'rb').read()
        h, note = R.prefs_realism_for_run(2)
        check('prefs_realism_for_run: RealismSetting 1 -> 2, the rest of the file as it was, a backup',
              h and R.realism_pref() == 2 and open(R.PREFS_FILE, 'rb').read() == raw0.replace(b'RealismSetting=1',
                                                                                             b'RealismSetting=2')
              and os.path.exists(R.PREFS_BACKUP), note)
        check('prefs_restore: the original back (verified by its hash), the backup deleted', R.prefs_restore(h)
              and open(R.PREFS_FILE, 'rb').read() == raw0 and not os.path.exists(R.PREFS_BACKUP))
        write(R.PREFS_FILE, 'RealismSetting=3\n')
        h, note = R.prefs_realism_for_run(2)
        check('prefs_realism_for_run: RealismSetting 3 is left as it is (no backup)', h is None and R.realism_pref() == 3
              and not os.path.exists(R.PREFS_BACKUP), note)
        write(R.PREFS_FILE, 'MapZoom=3\n')
        h, note = R.prefs_realism_for_run(2)
        check('prefs_realism_for_run: no RealismSetting line: nothing changed', h is None and R.realism_pref() is None,
              note)
        p = R.preflight(os.path.join(R.SAVE, 'Game1'), dry=True, prefs=True)
        check('preflight: lockstep_torpedo without a RealismSetting line is a problem', any('RealismSetting' in x
                                                                                            for x in p), p)
        # Preferences for lockstep_strike (search floatplanes: FloatplaneLaunch=0, Automatic)
        write(R.PREFS_FILE, 'MapZoom=3\nFloatplaneLaunch=1\nRealismSetting=1\n')
        raw0 = open(R.PREFS_FILE, 'rb').read()
        h, note = R.prefs_for_run(2, float_auto=True)
        check('prefs_for_run: RealismSetting 1 -> 2 and FloatplaneLaunch 1 -> 0 (lockstep_torpedo and lockstep_strike), '
              'the rest of the file as it was, a backup', h and open(R.PREFS_FILE, 'rb').read() == raw0.replace(
                  b'FloatplaneLaunch=1', b'FloatplaneLaunch=0').replace(b'RealismSetting=1', b'RealismSetting=2')
              and os.path.exists(R.PREFS_BACKUP), note)
        check('prefs_restore after prefs_for_run: the original back, the backup deleted', R.prefs_restore(h)
              and open(R.PREFS_FILE, 'rb').read() == raw0 and not os.path.exists(R.PREFS_BACKUP))
        write(R.PREFS_FILE, 'FloatplaneLaunch=0\nRealismSetting=1\n')
        h, note = R.prefs_for_run(None, float_auto=True)
        check('prefs_for_run: FloatplaneLaunch 0 (Automatic) is left as it is (no backup)', h is None
              and not os.path.exists(R.PREFS_BACKUP), note)
        write(R.PREFS_FILE, 'MapZoom=3\nRealismSetting=1\nSound=1\n')
        # the missile-era fixture
        names = ['session', 'lockstep_missiles']
        R.MISSILE.update(path=None, slot=None, nations=None, mission=0, why=None)
        pl = R.missile_plan(None, None, None, None, names)
        ok, why, _ = R.missile_fixture()
        check('missile_plan: nothing given: no problem, lockstep_missiles SKIPs with the reason (the options named)',
              not pl['problems'] and not ok and '--missile-fixture' in why, (pl, why))
        pl = R.missile_plan(fix, None, '0,3', '2', names)
        ok, why, info = R.missile_fixture(os.path.join(fix, 'RTWGame5.bcs'))
        check('missile_plan: a fixture folder Game5 with slot 5 free: copied in for the run; MISSILE slot 5, nations '
              '(0, 3), mission 2, the 1958 campaign\'s area 1; the fixture the 1958 missile test campaign', not pl['problems']
              and pl['copy'] == (fix, 5) and R.MISSILE['slot'] == 5 and R.MISSILE['nations'] == (0, 3)
              and R.MISSILE['mission'] == 2 and R.MISSILE['area'] == 1 and ok
              and info['names'] == ['Italy', 'Soviet Union'], (pl, why, info))
        R.MISSILE.update(path=None, slot=None, nations=None, mission=0, area=None, why=None)
        pl = R.missile_plan(fix, None, None, None, names)
        check('missile_plan: without --missile-nations/--missile-mission the 1958 campaign\'s 0,3, mission 1, area 1',
              not pl['problems'] and R.MISSILE['nations'] == (0, 3) and R.MISSILE['mission'] == 1
              and R.MISSILE['area'] == 1, R.MISSILE)
        R.MISSILE.update(path=None, slot=None, nations=None, mission=0, area=None, why=None)
        pl = R.missile_plan(fix, None, '0,2', None, names)
        ok2, why2, _ = R.missile_fixture(os.path.join(fix, 'RTWGame5.bcs'))
        check('missile_plan: other nations: no default area; a fixture without that nation SKIPs', R.MISSILE['area'] is None
              and not ok2 and 'missile-era' in why2, (R.MISSILE, why2))
        R.MISSILE.update(path=None, slot=None, nations=None, mission=0, area=None, why=None)
        pl = R.missile_plan(fix, None, '0,3', '2', names)
        shutil.copytree(fix, R.slot_dir(5))
        pl = R.missile_plan(fix, None, '0,3', None, names)
        check('missile_plan: slot 5 holds the same files: used as it is, backed up and put back', not pl['problems']
              and pl['copy'] is None and pl['backup'][0] == 5, pl)
        write(os.path.join(R.slot_dir(5), 'RTWGame5.bcs'), '[General]\nYear=1901\n')
        pl = R.missile_plan(fix, None, '0,3', None, names)
        check('missile_plan: slot 5 holds another campaign: a problem (never overwritten)', any(
            'differs from the missile fixture' in x for x in pl['problems']), pl)
        R.MISSILE.update(why=None)
        pl = R.missile_plan(None, '5', '0,3', None, names)
        ok, why, _ = R.missile_fixture()
        check('missile_plan: --missile-slot 5 alone: backed up and put back; a 1901 campaign there SKIPs (not a '
              'missile-era campaign)', not pl['problems'] and pl['backup'][0] == 5 and not ok and 'missile-era' in why,
              (pl, why))
        shutil.rmtree(R.slot_dir(5))
        for args, want in (((fix, None, '0', None), '--missile-nations'), ((fix, None, '3,3', None), '--missile-nations'),
                           ((fix, '6', '0,3', None), 'belongs in save slot 5'),
                           ((None, '77', '0,3', None), 'one the suite writes itself'),
                           ((None, '9', '0,3', None), '1935 test campaign')):
            R.MISSILE.update(path=None, slot=None, nations=None, mission=0, why=None)
            pl = R.missile_plan(*args, names + ['lockstep_air'])
            ok, why, _ = R.missile_fixture()
            shown = tuple('<the fixture Game5>' if x == fix else x for x in args[:3])
            check(f'missile_plan {shown}: refused ({want}); lockstep_missiles SKIPs with that reason',
                  any(want in x for x in pl['problems']) and not ok and want in why, (pl, why))
        R.MISSILE.update(path=None, slot=None, nations=None, mission=0, why=None)
        check('the 1935 fixture check reads the sandbox slot 9', R.fixture1935() == (True, ''), R.fixture1935())
        bk9 = os.path.join(sandbox, 'tmp', 'fx', 'Game9')
        shutil.copytree(R.slot_dir(R.F1935_SLOT), bk9)
        sv = os.path.join(bk9, 'RTWGame9.bcs')
        ok_bk = R.fixture1935(bk9)
        with open(sv, 'rb') as f:
            raw9 = f.read()
        with open(sv, 'wb') as f:  # (lockstep_air's battle snapshot: a battle in progress)
            f.write(raw9.replace(b'ScenarioInProgress=-1', b'ScenarioInProgress=2'))
        bad_bk = R.fixture1935(bk9)
        check('fixture1935(folder): the run\'s backup checked like slot 9 (lockstep_strike runs after lockstep_air on the '
              'backup); a battle in progress refused', ok_bk == (True, '') and not bad_bk[0]
              and "'2'" in bad_bk[1] and b'ScenarioInProgress=-1' in raw9, (ok_bk, bad_bk))
        shutil.rmtree(os.path.dirname(bk9))
        src = os.path.join(sandbox, 'tmp', 'bk', 'Game9')
        os.makedirs(src)
        with open(os.path.join(src, 'RTWGame9.bcs'), 'wb') as f:
            f.write(b'\xef\xbb\xbf' + '[General]\r\nYear=1935\r\n[AirUnits]\r\nAirUnitNo=2\r\nAU0Id=59\r\nAU0Name=59 '
                    'Squadron\r\nAU1Id=60\r\nAU1Name=60 Squadron\r\n[EUT]\r\nX=1\r\n'.encode('utf-8'))
        write(os.path.join(src, 'MapData9.dat'), '[MapAreas]\n')
        write(os.path.join(src, 'DesignFiles0.des'), 'x')
        n = R.air_copy(R.F1935_SLOT, R.AIR_SLOT, src)
        raw = open(R.save_path(R.AIR_SLOT), 'rb').read()
        check('air_copy (from the run\'s backup of slot 9): the slot\'s files renamed (RTWGame49.bcs, MapData49.dat), every '
              '[AirUnits] entry twice with the same ids (AirUnitNo 4, 4 units, 2 ids), the BOM, the line ends and the '
              'other sections kept', n == 2
              and sorted(os.listdir(R.slot_dir(R.AIR_SLOT))) == ['DesignFiles0.des', 'MapData49.dat', 'RTWGame49.bcs']
              and R.saved_airunits(R.AIR_SLOT) == dict(no=4, units=4, ids=2) and raw.startswith(b'\xef\xbb\xbf')
              and b'AU3Id=60\r\n' in raw and b'[EUT]\r\nX=1' in raw, raw[:200])
        check('saved_airunits: a save without [AirUnits] counts 0 (the 1900 campaign)',
              R.saved_airunits(1) in (dict(no=0, units=0, ids=0), None), R.saved_airunits(1))
        p = R.preflight(os.path.join(R.SAVE, 'Game1'), dry=True, air_slots=(R.AIR_SLOT,))
        check('preflight: air_units refuses an existing slot 49', any('save slot 49 exists' in x for x in p), p)
        shutil.rmtree(R.slot_dir(R.AIR_SLOT))
        # legacy_fleet (0.7.0): the legacy test campaign's check and its copy into the throwaway slot 46
        lg = os.path.join(sandbox, 'tmp', 'lg', 'Game3')
        os.makedirs(lg)
        bcs3 = '[General]\r\nYear=1900\r\nMonth=1\r\nCampaignStartYear=1900\r\nFleetSize=4\r\n[Nation1]\r\nName=Germany\r\n'
        with open(os.path.join(lg, 'RTWGame3.bcs'), 'wb') as f:
            f.write(b'\xef\xbb\xbf' + bcs3.encode('utf-8'))
        write(os.path.join(lg, 'MapData3.dat'), '[MapAreas]\n')
        write(os.path.join(lg, 'DesignFiles1.des'), 'v10139\n0\n')
        good = R.legacy_fixture(lg)
        with open(os.path.join(lg, 'RTWGame3.bcs'), 'wb') as f:
            f.write(bcs3.replace('Month=1', 'Month=2').encode('utf-8'))
        bad = R.legacy_fixture(lg)
        check('legacy_fixture: a January 1900 campaign with nation 1 Germany accepted (slot 3), a February one refused; '
              'none given: SKIP reason', good[:2] == (True, '') and good[3] == 3 and not bad[0] and 'January 1900' in bad[1]
              and not R.legacy_fixture(os.path.join(sandbox, 'nowhere'))[0], (good, bad))
        R.legacy_copy(lg, 3, R.LEGACY_SLOT)
        check('legacy_copy: the files renamed into slot 46 (RTWGame46.bcs, MapData46.dat, the design files as they are)',
              sorted(os.listdir(R.slot_dir(R.LEGACY_SLOT))) == ['DesignFiles1.des', 'MapData46.dat', 'RTWGame46.bcs'],
              os.listdir(R.slot_dir(R.LEGACY_SLOT)))
        sec = R.save_sections(open(R.save_path(R.LEGACY_SLOT), 'rb').read())
        check('save_sections: the sections\' lines in order (BOM dropped)', sec.get('General', [])[:2] ==
              ['Year=1900', 'Month=2'] and sec.get('Nation1') == ['Name=Germany'], sec)
        p = R.preflight(os.path.join(R.SAVE, 'Game1'), dry=True, air_slots=(R.LEGACY_SLOT,))
        check('preflight: legacy_fleet refuses an existing slot 46', any('save slot 46 exists' in x for x in p), p)
        shutil.rmtree(R.slot_dir(R.LEGACY_SLOT))
        shutil.rmtree(os.path.dirname(lg))
        check('no_doubling: the kept count within 25 % of the dropped one (0.3.2: x2)', R.no_doubling(232, 232)
              and R.no_doubling(232, 240) and not R.no_doubling(232, 464) and not R.no_doubling(0, 5))
        check('KEPT_LOG_RE: the kept logs of every restart tag, not the .prev log or the current one',
              all(re.match(R.KEPT_LOG_RE, f'rtw3mp_47702.{t}3.log') for t in (
                  'kill', 'hostkill', 'restart', 'k2restart', 'level', 'afterend', 'relay', 'vanilla', 'clean', 'twopc'))
              and not re.match(R.KEPT_LOG_RE, 'rtw3mp_47702.prev.log')
              and not re.match(R.KEPT_LOG_RE, 'rtw3mp_47702.log'))
        twopc_sandbox(sandbox)
        _ = root


def twopc_sandbox(sandbox):
    """twopc's file helpers in the sandbox (inside runner's sandboxed()): the Preferences backup for the run, B's
    variant, the battle file's sighting counts, the PNG writer, the dry run."""
    prefs = ('[Break]\r\nBreakOnHit=1\r\nBreakOnNewSighting=1\r\nAirApproachMessage=1\r\nLetAIControlForce=0\r\n'
             '[Settings]\r\nFloatWarning=2\r\nReadyPopup=0\r\nFOW=1\r\nAIControl=2\r\nDarkMode=0\r\nLOScalc=1\r\n'
             'FloatplaneLaunch=1\r\nSoundVolume=25\r\nScale=0\r\nRealismSetting=1\r\nAutosave=60\r\n'
             'NoFriendlyTorpHits=0\r\nSoundOption=1\r\nBSSMHC=40\r\nAirSurfaceMessageLvl=0\r\n[MainForm]\r\n'
             'MainFormWidth=1000\r\nMainFormHeight=719\r\nMainFormState=0\r\n[BCMainForm]\r\nBCMainFormWidth=1288\r\n')
    with open(R.PREFS_FILE, 'wb') as f:
        f.write(prefs.encode())
    raw0 = open(R.PREFS_FILE, 'rb').read()
    h, note = R.prefs_for_run(None, keep=True)
    check('prefs_for_run(keep=True) (twopc): the Preferences backed up although nothing changes now, the file as it was',
          h == R.file_hash(R.PREFS_FILE) and os.path.exists(R.PREFS_BACKUP) and open(R.PREFS_FILE, 'rb').read() == raw0
          and 'twopc' in note, note)
    keys = R.battle_prefs_keys()
    a_sim = R.parse_prefs(PREFS_A)['sim']
    var, diff = R.twopc_variant(raw0, a_sim, keys)
    vals, before = R.prefs_values(var), R.prefs_values(raw0)
    check('twopc_variant: every key of TWOPC_ALT differs from A\'s (the battle preferences from A\'s `ls prefs`, the '
          'others from the file); the keys the file lacks added to their sections (BreakOnSunk to [Break], ShipFlags to '
          '[Settings]); MainFormWidth set, BCMainFormWidth kept; CRLF kept', all(a != b for a, b in diff.values())
          and set(diff) == set(R.TWOPC_ALT) and all(str(diff[k][1]) == vals.get(k) for k in diff)
          and vals.get('BCMainFormWidth') == '1288' and vals.get('MainFormWidth') == '1400'
          and b'[Break]\r\nBreakOnSunk=0\r\n' in var and b'\n' not in var.replace(b'\r\n', b'')
          and before['RealismSetting'] == '1' and vals['RealismSetting'] == '2', (diff, var[:200]))
    os.remove(R.PREFS_FILE)
    check('prefs_restore after prefs_for_run(keep=True): the file as it was again (here: it was deleted meanwhile), '
          'verified, the backup deleted', R.prefs_restore(h) and open(R.PREFS_FILE, 'rb').read() == raw0
          and not os.path.exists(R.PREFS_BACKUP))
    sac = os.path.join(sandbox, 'tmp', 'RTWGame87.sac')
    write(sac, '\ufeff[Environment]\nYear=1900\n[Division1]\nSide=0\nShipCount=3\nShip0SightingLevel=2\n'
               'Ship0RadarSightingLevel=0\nShip0Sunk=0\nShip1SightingLevel=0\nShip1RadarSightingLevel=1\nShip1Sunk=0\n'
               'Ship2SightingLevel=3\nShip2Sunk=1\n[Division2]\nSide=1\nShipCount=2\nShip0SightingLevel=0\nShip0Sunk=0\n'
               'Ship1SightingLevel=0\nShip1RadarSightingLevel=0\nShip1Sunk=0\n[Division3]\nSide=3\nShipCount=4\n')
    check('sac_sighting: per side the ships afloat and those the other side sights (sighting or radar level > 0); sunk '
          'ships and side 3 left out', R.sac_sighting(sac) == {0: (2, 2), 1: (2, 0)}
          and R.sac_sighting(sac + '.none') is None, R.sac_sighting(sac))
    png = os.path.join(sandbox, 'tmp', 'x.png')
    R.write_png(png, 2, 2, bytes([0, 0, 255, 255, 0, 255, 0, 255, 255, 0, 0, 255, 9, 9, 9, 255]))
    data = open(png, 'rb').read()
    i = data.index(b'IDAT')
    pix = R.zlib.decompress(data[i + 4:i + 4 + R.struct.unpack('>I', data[i - 4:i])[0]])
    check('write_png: a PNG (signature, IHDR 2x2 RGB), the BGRA pixels as RGB rows with filter 0',
          data[:8] == b'\x89PNG\r\n\x1a\n' and data[12:16] == b'IHDR' and pix == bytes(
              [0, 255, 0, 0, 0, 255, 0, 0, 0, 0, 255, 9, 9, 9]), pix)
    with open(R.PREFS_FILE, 'wb') as f:
        f.write(b'MapZoom=3\r\nRealismSetting=1\r\nSound=1\r\n')
    before = digest(sandbox)
    code, out = run_main(['--dry-run', '--group', 'twopc'])
    check('--dry-run --group twopc: session and twopc (short), the client\'s comma locale and Preferences variant, the '
          'delay proxy\'s port and network; nothing written', code == 2 and 'twopc (short)' in out
          and 'RTW3MP_TEST_LOCALE=comma' in out and f'127.0.0.1:{R.TWOPC_PROXY_PORT}' in out and 'lockstep_ui' not in out
          and digest(sandbox) == before, out[-1200:])
    code, out = run_main(['--dry-run', '--group', 'twopc', '--stress'])
    check('--dry-run --group twopc --stress: the full variant (its network, its game time)', code == 2
          and 'twopc (full)' in out and f'{R.TWOPC_NET["full"]["kbps"]} kbit/s' in out
          and f'~{R.EST_STRESS["twopc"]:>5} s' in out, out[-1200:])
    os.remove(R.PREFS_FILE)
    code, out = run_main(['--dry-run', '--group', 'twopc'])
    check('--dry-run --group twopc without a Preferences file: twopc would SKIP', 'MISSING: SKIP twopc' in out, out[-600:])
    write(R.PREFS_FILE, 'MapZoom=3\nRealismSetting=1\nSound=1\n')


# ---------------------------------------------------------------- recorded logs (a report)

def recorded(folder):
    print(f'--- recorded logs in {folder}')
    logs = sorted(f for f in os.listdir(folder) if re.match(r'^rtw3mp_\d+(\..+)?\.log$', f))
    texts = {}
    for fn in logs:
        with open(os.path.join(folder, fn), encoding='utf-8', errors='replace') as f:
            texts[fn] = f.read()
    for fn, t in texts.items():
        hg = R.hash_groups(t)
        n = {k: len(re.findall(p, t)) for k, p in (
            ('rejoinable', R.REJOINABLE_RE), ('rejoin host', R.REJOIN_HOST_RE), ('ident', R.IDENT_LOAD_RE),
            ('fastest', R.FASTEST_REQ_RE), ('autorun q', R.AUTORUN_Q_RE), ('autorun rel', R.AUTORUN_REL_RE),
            ('nightcap', R.NIGHTCAP_RE))}
        n.update(cover=len(R.LANDCAP_COVER_RE.findall(t)), launch=len(R.LAUNCH_RE.findall(t)),
                 replay=len(R.REPLAY_RE.findall(t)))
        print(f'  {fn}: {len(hg)} hash minutes, deferred {len(R.deferred_lines(t))}, quiet boxes {len(R.quiet_boxes(t))}, '
              f'load dates {len(R.load_dates(t))}, ' + ', '.join(f'{k} {v}' for k, v in n.items() if v))
        bad = [ln for ln in t.splitlines() if 'lockstep: deferred ' in ln and ' of side ' in ln
               and ('carried out at minute' in ln) and not R.DEFERRED_RE.search(ln) and 'FAILED' not in ln]
        check(f'{fn}: every deferred release line parsed', not bad, bad[:2])
        lines = [ln for ln in t.splitlines() if 'campaign date' in ln and ('kept (the battle save' in ln or
                                                                          '(as the battle save:' in ln)]
        check(f'{fn}: every load-date line parsed', len(R.load_dates(t)) == len(lines), lines[:2])
    host = next((f for f in logs if re.match(r'^rtw3mp_47701\.log$', f)), None)
    client = next((f for f in logs if re.match(r'^rtw3mp_47702\.log$', f)), None)
    if host and client:
        cmp_ = R.groups_compare(R.hash_groups(texts[host]), R.hash_groups(texts[client]))
        print(f'  host/client hash groups: {cmp_["minutes"]} minutes, differ '
              f'{ {g: v[:6] for g, v in cmp_["differ"].items()} }, dates {cmp_["values"]["date"][:4]}')
    traces = sorted(f for f in os.listdir(folder) if re.match(r'^ls_trace_A_\d+_\d+_\d+\.txt$', f))
    eq = 0
    for fn in traces:
        _, _, _, bid, ep, mn = fn[:-4].split('_')
        tc = R.trace_compare(bid, ep, mn, folder=folder)
        eq += tc.get('found') and tc.get('first_diff') is None and tc.get('blocks_equal')
    if traces:
        check(f'the trace files: {eq} of {len(traces)} minutes with the same draws on both copies', eq == len(traces))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--logs', action='append', default=[], help='a folder of a recorded run\'s mod logs (repeatable)')
    a = ap.parse_args()
    samples()
    twopc_samples()
    intel_samples()
    relations_samples()
    divisions_samples()
    sandbox = os.path.abspath('regression_selftest_sandbox')
    if os.path.exists(sandbox):
        shutil.rmtree(sandbox)
    os.makedirs(sandbox)
    try:
        tracefiles(sandbox)
        runner(sandbox)
    finally:
        shutil.rmtree(sandbox, ignore_errors=True)
    for folder in a.logs:
        recorded(folder)
    bad = [n for n, ok in RESULTS if not ok]
    print(f'{len(RESULTS) - len(bad)}/{len(RESULTS)} self-test checks passed')
    return 0 if not bad else 1


if __name__ == '__main__':
    sys.exit(main())
