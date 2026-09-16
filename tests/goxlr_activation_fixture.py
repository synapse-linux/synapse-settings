#!/usr/bin/python3 -I
# SPDX-License-Identifier: MIT
"""Finite activation-client fixture only; never launches a graphical application."""
import json
import os
from pathlib import Path
import sys
import time

root = Path(os.environ['SYNAPSE_SETTINGS_GOXLR_APP_LOG'])
mode = os.environ.get('SYNAPSE_SETTINGS_GOXLR_APP_MODE', 'valid')
with root.open('x') as output:
    json.dump({'argv': sys.argv[1:], 'session': os.environ.get('XDG_SESSION_ID'),
               'display': os.environ.get('WAYLAND_DISPLAY'), 'pid': os.getpid()}, output)
assert sys.argv[1:] == ['--open-or-activate']
receipt = b'{"schema":"synapse.goxlr.gui-activation/v1","result":"activation-requested","sceneReady":true,"focusConfirmed":false}\n'
if mode == 'delay':
    time.sleep(.2)
if mode == 'timeout':
    time.sleep(7)
if mode == 'empty':
    receipt = b''
elif mode == 'duplicate':
    receipt = receipt.replace(b'"sceneReady":true', b'"sceneReady":false,"sceneReady":true')
elif mode == 'extra':
    receipt += receipt
elif mode == 'overflow':
    receipt = b'x' * 513
elif mode == 'focus':
    receipt = receipt.replace(b'"focusConfirmed":false', b'"focusConfirmed":true')
elif mode == 'not-ready':
    receipt = receipt.replace(b'"sceneReady":true', b'"sceneReady":false')
sys.stdout.buffer.write(receipt)
sys.stdout.buffer.flush()
sys.exit(1 if mode == 'failed' else 0)
