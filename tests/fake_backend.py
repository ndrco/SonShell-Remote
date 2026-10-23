#!/usr/bin/env python3
"""SDK-free protocol fixture. No network or camera access."""
import json
import os
from pathlib import Path
import select
import signal
import sys
import time
import socket
import threading
import datetime

args = sys.argv[1:]
record = os.environ.get('REMOTE_TEST_RECORD')
def record_event(event):
    if record:
        with open(record, 'a', encoding='utf-8') as output:
            output.write(json.dumps(event) + '\n')
def event(kind, request='', channel='card', **fields):
    data = json.dumps(dict(type=kind, request=request, **fields), ensure_ascii=False).encode().hex()
    print('INFO  | [INFO] REMOTE/1 ' + channel + ' ' + data, flush=True)
record_event({'argv': args, 'has_password': bool(os.environ.get('SONSHELL_CAMERA_PASSWORD')), 'private_loader_path': os.environ.get('LD_LIBRARY_PATH', '').startswith(str(Path(sys.argv[0]).resolve().parent) + ':')})
mode = os.environ.get('REMOTE_TEST_MODE', 'normal')
if mode == 'hang':
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    while True:
        time.sleep(1)
if mode == 'fail':
    sys.exit(2)
sys.stdout.write('INFO  | [INFO] REMOTE/1 con')
sys.stdout.flush()
time.sleep(0.03)
print('nected', flush=True)
if mode == 'disconnect':
    time.sleep(0.04)
    print('INFO  | [INFO] REMOTE/1 disconnected', flush=True)
fixture = os.environ.get('REMOTE_TEST_CARD')
card = json.loads(Path(fixture).read_text()) if fixture else []
captured = os.environ.get('REMOTE_TEST_CAPTURE')
if captured and not fixture:
    card = [dict(slot=1, id=1, stamp='2026-10-21T12:00:00', identity='test-camera:one', protected=False, deletable=True,
                 files=[dict(id=1, format=0x3801, size=str(Path(captured).stat().st_size), path='DCIM/100MSDCF/снимок 1.jpg', source=captured)])]
epoch = ''
pending = None
movie = None
for line in sys.stdin:
    command = line.strip()
    record_event({'command': command})
    parts = command.split()
    if command == 'quit':
        break
    if parts[:1] == ['controls'] and len(parts) >= 3:
        operation, request = parts[1:3]
        if operation == 'list' and os.environ.get('REMOTE_TEST_CONTROLS_LIST_FAIL'):
            event('error', request, channel='controls', message='read-failed')
        elif operation == 'list':
            properties = []
            for n, name in enumerate(('focus_mode', 'focus_area', 'white_balance', 'drive_mode')):
                properties.append(dict(name=name, code=100 + n, readable=True,
                    writable=not bool(os.environ.get('REMOTE_TEST_CONTROLS_READONLY')),
                    current='1', values=[dict(value='1', label='Single'), dict(value='2', label='Continuous')]))
            event('snapshot', request, channel='controls', properties=properties,
                  af=not bool(os.environ.get('REMOTE_TEST_CONTROLS_READONLY')))
        else:
            event('error' if os.environ.get('REMOTE_TEST_CONTROLS_FAIL') else 'applied', request,
                  channel='controls', message='read-only')
    if command == 'shoot':
        # Capturing never copies an original, even with a legacy Auto Sync setting.
        event('changed')
    if len(parts) >= 3 and parts[:2] == ['card', 'list']:
        epoch = parts[2]
        event('begin', epoch, epoch=epoch)
        for item in card:
            clean = dict(item)
            clean['files'] = [{k: v for k, v in f.items() if k != 'source'} for f in item['files']]
            event('item', epoch, epoch=epoch, **clean)
        event('end', epoch, epoch=epoch, stale=False)
    elif command == 'card cancel':
        if movie:
            movie['stop'].set()
            event('movie-stopped', movie['request'])
            movie = None
        if pending:
            event('error', pending, message='Передача отменена')
            pending = None
    elif len(parts) >= 3 and parts[1] in ('movie-play', 'movie-pause', 'movie-seek'):
        if movie and parts[2] == movie['request']:
            movie['pause'] = parts[1] == 'movie-pause'
            event('movie-state', parts[2], state='paused' if movie['pause'] else 'playing')
    elif len(parts) >= 6 and parts[0] == 'card':
        kind, request, revision, slot, content = parts[1:6]
        item = next((item for item in card if item['slot'] == int(slot) and item['id'] == int(content)), None)
        if revision != epoch or not item:
            event('error', request, message='Каталог устарел')
            continue
        if kind == 'delete':
            if os.environ.get('REMOTE_TEST_DELETE_FAIL') or item['protected'] or not item['deletable']:
                event('error', request, message='Камера не разрешает удаление')
            else:
                card.remove(item)
                event('deleted', request)
            continue
        file = next((f for f in item['files'] if f['id'] == int(parts[6])), None)
        if not file:
            event('error', request, message='Нет файла')
            continue
        if kind == 'movie':
            print('INFO  | [INFO] REMOTE/1 liveview-stopped', flush=True)
            connection = socket.socket(socket.AF_UNIX); connection.settimeout(0.2)
            connection.connect(bytes.fromhex(parts[7]).decode())
            movie = dict(request=request, stop=threading.Event(), pause=False)
            def stream(state=movie, client=connection):
                try:
                    source = Path(os.environ['REMOTE_TEST_HEVC']).read_bytes() * 20
                    for offset in range(0, len(source), 1024):
                        while state['pause'] and not state['stop'].is_set(): time.sleep(0.01)
                        if state['stop'].is_set(): break
                        client.sendall(source[offset:offset+1024]); time.sleep(0.02)
                except (OSError, KeyError): pass
                finally:
                    client.close()
                    if not state['stop'].is_set(): event('movie-stopped', state['request'])
            threading.Thread(target=stream, daemon=True).start()
            event('movie-state', request, state='playing')
        elif kind == 'preview':
            if os.environ.get('REMOTE_TEST_PREVIEW_DELAY'):
                time.sleep(float(os.environ['REMOTE_TEST_PREVIEW_DELAY']))
            source = os.environ.get('REMOTE_TEST_CAPTURE') or os.environ.get('REMOTE_TEST_JPEG') or file['source']
            target = Path(args[args.index('--camera-cache') + 1]) / (request + '.jpg')
            target.write_bytes(Path(source).read_bytes())
            event('preview', request, path=str(target))
        elif kind == 'copy':
            if os.environ.get('REMOTE_TEST_COPY_FAIL'):
                event('error', request, message='Небезопасный путь на карте')
                continue
            event('progress', request, percent=50)
            if os.environ.get('REMOTE_TEST_SLOW_COPY'):
                pending = request
                continue
            root = Path(bytes.fromhex(parts[7]).decode())
            capture_date = item.get('capture_date') or item.get('stamp', '')[:10]
            try: datetime.date.fromisoformat(capture_date)
            except ValueError: capture_date = 'Unknown-Date'
            target = root / capture_date / ('Slot_' + slot) / file['path']
            target.parent.mkdir(parents=True, exist_ok=True)
            base = target
            n = 1
            while target.exists():
                target = base.with_stem(base.stem + '_' + str(n))
                n += 1
            target.write_bytes(Path(file['source']).read_bytes())
            event('copied', request, path=str(target), size=str(target.stat().st_size))
    if command == 'monitor start':
        if '--liveview-file' in args and os.environ.get('REMOTE_TEST_JPEG'):
            target = Path(args[args.index('--liveview-file') + 1])
            target.write_bytes(Path(os.environ['REMOTE_TEST_JPEG']).read_bytes())
        print('INFO  | [INFO] REMOTE/1 liveview-started', flush=True)
    elif command == 'monitor stop':
        print('INFO  | [INFO] REMOTE/1 liveview-stopped', flush=True)
