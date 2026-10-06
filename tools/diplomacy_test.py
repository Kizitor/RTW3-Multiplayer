"""Live test of player diplomacy with two game instances (host + client), driven by the test bridge and posted
window messages (the user's mouse and keyboard are never used).

  python diplomacy_test.py

Same as `python regression.py --only session,diplomacy`: the scenario lives in regression.py, which also backs up
save slot 1, launches and closes the copies, and restores the saves. Nation 2 is reserved for a stand-in player
("Bot") so diplomacy between two joined nations can be tested with one client.
"""
import sys

import regression

if __name__ == '__main__':
    sys.exit(regression.main(['--only', 'session,diplomacy'] + sys.argv[1:]))
