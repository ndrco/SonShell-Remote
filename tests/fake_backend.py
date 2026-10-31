#!/usr/bin/env python3
"""SDK-free protocol fixture. No network or camera access."""
import json
import hashlib
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
                 files=[dict(id=1, format=0x3801, size=str(Path(captured).stat().st_size), path='DCIM/100MSDCF/\u0441\u043d\u0438\u043c\u043e\u043a 1.jpg', source=captured)])]
epoch = ''
pending = None
movie = None
capture_sequence = 0
recording_specs = [
    ('file_type', '3', [('1', 'JPEG'), ('2', 'RAW'), ('3', 'RAW+JPEG')]),
    ('compression_format', '1', [('1', 'JPEG'), ('2', 'HEIF 4:2:2')]),
    ('image_quality', '3', [('3', 'Fine'), ('4', 'Extra fine')]),
    ('image_size', '1', [('1', 'L'), ('2', 'M')]),
    ('aspect_ratio', '1', [('1', '3:2'), ('2', '16:9')]),
    ('raw_compression', '2', [('2', 'Lossless'), ('1', 'Compressed')]),
    ('steady_still', '1', [('1', 'On'), ('0', 'Off')]),
    ('movie_format', '5', [('5', 'XAVC HS 4K'), ('2', 'XAVC S 4K')]),
    ('movie_framerate', '4', [('4', '50p'), ('6', '25p')]),
    ('movie_setting', '1', [('1', '100 Mbps 4:2:2 10-bit'), ('2', '50 Mbps 4:2:0 10-bit')]),
    ('steady_movie', '2', [('2', 'Active'), ('1', 'Standard')]),
]
recording_values = {name: current for name, current, _ in recording_specs}
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
            for n, (name, current, values) in enumerate((
                ('iso', '16777215', [('16777215', 'AUTO'), ('100', '100'), ('200', '200'), ('400', '400')]),
                ('shutter', '65661', [('65596', '1/60'), ('65661', '1/125'), ('65786', '1/250')]),
                ('aperture', '280', [('280', 'f/2.8'), ('400', 'f/4'), ('560', 'f/5.6')]),
                ('ev', '0', [('65203', '-0.3'), ('0', '+0.0'), ('333', '+0.3')]))):
                properties.append(dict(name=name, code=110 + n, readable=True,
                    writable=not bool(os.environ.get('REMOTE_TEST_CONTROLS_READONLY')),
                    current=current, values=[dict(value=v, label=label) for v, label in values]))
            if not os.environ.get('REMOTE_TEST_RECORDING_UNSUPPORTED'):
                for n, (name, default, values) in enumerate(recording_specs):
                    current = recording_values[name]
                    if name == 'file_type' and recording_values['compression_format'] == '2':
                        values = [('4', 'RAW+HEIF'), ('5', 'HEIF'), ('2', 'RAW')]
                    writable = not bool(os.environ.get('REMOTE_TEST_CONTROLS_READONLY')) and not (name == 'raw_compression' and recording_values['file_type'] == '1')
                    if name == 'image_quality' and os.environ.get('REMOTE_TEST_RECORDING_QUALITY_READONLY'):
                        current = '9'; writable = False
                    properties.append(dict(name=name, code=120 + n, readable=True, writable=writable, current=current,
                        current_label='Camera quality' if current == '9' else next((label for value, label in values if value == current), current),
                        values=[dict(value=value, label=label) for value, label in values]))
            event('snapshot', request, channel='controls', properties=properties,
                  af=not bool(os.environ.get('REMOTE_TEST_CONTROLS_READONLY') or os.environ.get('REMOTE_TEST_AF_UNAVAILABLE')))
        else:
            if operation == 'af' and os.environ.get('REMOTE_TEST_AF_FAIL'):
                event('error', request, channel='controls', message='af-press:32768')
                continue
            if operation == 'set' and len(parts) == 5 and not os.environ.get('REMOTE_TEST_CONTROLS_FAIL'):
                index = int(parts[3]) - 120
                if 0 <= index < len(recording_specs):
                    name = recording_specs[index][0]; recording_values[name] = parts[4]
                    if name == 'compression_format': recording_values['file_type'] = '4' if parts[4] == '2' else '3'
            event('error' if os.environ.get('REMOTE_TEST_CONTROLS_FAIL') else 'applied', request,
                  channel='controls', message='read-only')
    if command == 'shoot':
        # Capturing never copies an original, even with a legacy Auto Sync setting.
        if os.environ.get('REMOTE_TEST_ADVANCE_CAPTURE') and card:
            capture_sequence += 1
            card[0]['identity'] = 'test-camera:new-' + str(capture_sequence)
            card[0]['stamp'] = '2026-10-24T12:00:%02d' % capture_sequence
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
            event('error', pending, message='Transfer canceled')
            pending = None
    elif len(parts) >= 3 and parts[1] in ('movie-play', 'movie-pause', 'movie-seek'):
        if movie and parts[2] == movie['request']:
            movie['pause'] = parts[1] == 'movie-pause'
            event('movie-state', parts[2], state='paused' if movie['pause'] else 'playing')
    elif len(parts) >= 6 and parts[0] == 'card':
        kind, request, revision, slot, content = parts[1:6]
        item = next((item for item in card if item['slot'] == int(slot) and item['id'] == int(content)), None)
        if revision != epoch or not item:
            event('error', request, message='Catalog is out of date')
            continue
        if kind == 'delete':
            if os.environ.get('REMOTE_TEST_DELETE_FAIL') or item['protected'] or not item['deletable']:
                event('error', request, message='Camera does not permit deletion')
            else:
                card.remove(item)
                event('deleted', request)
            continue
        file = next((f for f in item['files'] if f['id'] == int(parts[6])), None)
        if not file:
            event('error', request, message='File not found')
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
            if parts[7] == 'jpeg' and file['format'] != 0x3801:
                event('error', request, message='JPEG required')
                continue
            original = parts[7] in ('jpeg', 'original')
            if parts[7] == 'original':
                event('progress', request, percent=50)
                if os.environ.get('REMOTE_TEST_SLOW_ORIGINAL'):
                    pending = request
                    continue
            source = file['source'] if original else os.environ.get('REMOTE_TEST_CAPTURE') or os.environ.get('REMOTE_TEST_JPEG') or file['source']
            extension = Path(file['path']).suffix.lower() if parts[7] == 'original' else '.jpg'
            target = Path(args[args.index('--camera-cache') + 1]) / (request + extension)
            target.write_bytes(Path(source).read_bytes())
            event('preview', request, path=str(target))
        elif kind == 'copy':
            if os.environ.get('REMOTE_TEST_COPY_FAIL'):
                event('error', request, message='Unsafe camera card path')
                continue
            event('progress', request, percent=50)
            if os.environ.get('REMOTE_TEST_SLOW_COPY') or (os.environ.get('REMOTE_TEST_HOLD_RAW') and file['format'] == 0xb101):
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
            event('progress', request, phase='hash', percent=100)
            event('copied', request, path=str(target), size=str(target.stat().st_size), sha256=hashlib.sha256(target.read_bytes()).hexdigest())
    if command == 'monitor start':
        if '--liveview-file' in args and os.environ.get('REMOTE_TEST_JPEG'):
            target = Path(args[args.index('--liveview-file') + 1])
            target.write_bytes(Path(os.environ['REMOTE_TEST_JPEG']).read_bytes())
        print('INFO  | [INFO] REMOTE/1 liveview-started', flush=True)
    elif command == 'monitor stop':
        print('INFO  | [INFO] REMOTE/1 liveview-stopped', flush=True)
