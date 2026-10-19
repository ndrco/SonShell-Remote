#!/usr/bin/env python3
"""SDK-free protocol fixture. No network or camera access."""
import json
import os
from pathlib import Path
import select
import signal
import sys
import time

args = sys.argv[1:]
record = os.environ.get('REMOTE_TEST_RECORD')
if record:
    with open(record, 'a', encoding='utf-8') as output:
        output.write(json.dumps({'argv': args, 'has_password': bool(os.environ.get('SONSHELL_CAMERA_PASSWORD')), 'private_loader_path': os.environ.get('LD_LIBRARY_PATH', '').startswith(str(Path(sys.argv[0]).resolve().parent) + ':')}) + '\n')
mode = os.environ.get('REMOTE_TEST_MODE', 'normal')
if mode == 'hang':
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    while True:
        time.sleep(1)
if mode == 'fail':
    sys.exit(2)
# Deliberately fragment events, exactly as a process pipe can do.
sys.stdout.write('INFO  | [INFO] REMOTE/1 con')
sys.stdout.flush()
time.sleep(0.03)
print('nected', flush=True)
if mode == 'disconnect':
    time.sleep(0.04)
    print('INFO  | [INFO] REMOTE/1 disconnected', flush=True)
for line in sys.stdin:
    command = line.strip()
    if record:
        with open(record, 'a', encoding='utf-8') as output:
            output.write(json.dumps({'command': command}) + '\n')
    if command == 'quit':
        break
    if command == 'monitor start':
        if '--liveview-file' in args and os.environ.get('REMOTE_TEST_JPEG'):
            target = Path(args[args.index('--liveview-file') + 1])
            target.write_bytes(Path(os.environ['REMOTE_TEST_JPEG']).read_bytes())
        print('INFO  | [INFO] REMOTE/1 liveview-started', flush=True)
    elif command == 'monitor stop':
        print('INFO  | [INFO] REMOTE/1 liveview-stopped', flush=True)
