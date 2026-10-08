#!/usr/bin/env python3
"""Isolated SDK -> SignalSink real-filesystem test; not a Sidecar/RDMA acceptance."""
import argparse
import json
import math
import os
from pathlib import Path
import platform
import select
import signal
import subprocess
import sys
import time
import uuid
sys.dont_write_bytecode = True
from check_capture import check

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--worker', required=True)
parser.add_argument('--producer', required=True)
parser.add_argument('--capture-root', type=Path, required=True)
parser.add_argument('--seconds', type=float, default=30)
parser.add_argument('--functional-smoke', action='store_true', help='allow a small non-ARM test; never counts as throughput acceptance')
args = parser.parse_args()
if not (0 < args.seconds <= 120):
    parser.error('seconds must be in (0, 120]')
if not args.functional_smoke and platform.machine() != 'aarch64':
    parser.error('performance validation requires native aarch64')
if args.functional_smoke and args.seconds > 1:
    parser.error('functional smoke is limited to one second')
root = args.capture_root.resolve(strict=True)
mounts = []
for line in Path('/proc/self/mountinfo').read_text().splitlines():
    left, right = line.split(' - ', 1)
    mount = left.split()[4]
    if str(root) == mount or str(root).startswith(mount.rstrip('/') + '/'):
        mounts.append((len(mount), mount, right.split()[0], right.split()[1]))
_, mount, filesystem, device = max(mounts)
if not args.functional_smoke and filesystem in {'tmpfs', 'overlay', 'ramfs'}:
    parser.error('capture-root must be a real filesystem, not tmpfs/overlay/ramfs')
frames = math.ceil(args.seconds * 468.75)
space = os.statvfs(root)
required = frames * (1048576 + 2136 + 64 + 8) + (32 * 1024**2 if args.functional_smoke else 2 * 1024**3)
if space.f_frsize * space.f_bavail < required:
    parser.error(f'insufficient space: need {required} bytes including reserve')
identifier = uuid.uuid4().hex
ring = '/signalsink-benchmark-' + identifier
producer = worker = None
samples = []
started = None
finished = None
result = dict(kind='functional smoke' if args.functional_smoke else 'native ARM isolated SDK/disk benchmark',
              architecture=platform.machine(), filesystem=filesystem, device=device, mount=mount,
              seconds_requested=args.seconds, target_payload_bytes_per_second=491520000, expected_frames=frames,
              source_is_synthetic=True, payload_pattern='xorshift32-12345678-with-frame-marker',
              sidecar_rdma_test=False, sample_continuity='unverified', success=False)
report = root / ('benchmark-' + identifier + '.json')


def control(*command):
    if command[0] != 'status' and (worker.poll() is not None or control('status').get('process_id') != str(worker.pid)):
        raise RuntimeError('control endpoint does not belong to this benchmark Worker')
    value = subprocess.run([args.worker, 'control', *command], capture_output=True, text=True, check=True, timeout=6)
    status = json.loads(value.stdout)
    if not status['ok']:
        raise RuntimeError(status)
    return status


try:
    producer = subprocess.Popen([args.producer, ring, str(frames)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    if not select.select([producer.stdout], [], [], 10)[0] or producer.stdout.readline().strip() != 'ready':
        raise RuntimeError('benchmark producer failed to initialize')
    tuning = ['--queue-bytes', str(16 * 1024**2), '--min-free-bytes', str(32 * 1024**2)] if args.functional_smoke else []
    worker = subprocess.Popen([args.worker, '--input', '1:3', '--capture-root', str(root), *tuning],
                              env=dict(os.environ, UESTCRADAR_UPSTREAM_SHM_NAME=ring))
    deadline = time.monotonic() + 10
    while True:
        if worker.poll() is not None or time.monotonic() >= deadline:
            raise RuntimeError('benchmark Worker failed to initialize; no control mutation sent')
        try:
            status = control('status')
            if status.get('process_id') != str(worker.pid):
                raise RuntimeError('control endpoint belongs to a different process')
            if status['state'] == 'idle':
                break
        except (subprocess.SubprocessError, RuntimeError):
            if time.monotonic() >= deadline:
                raise
        time.sleep(0.05)
    started = control('start', '--directory', 'benchmark-' + identifier)
    result['recording_id'] = started['recording_id']
    begin = time.monotonic()
    producer.stdin.write('go\n')
    producer.stdin.flush()
    deadline = begin + args.seconds + 30
    while not select.select([producer.stdout], [], [], 0.25)[0]:
        status = control('status')
        samples.append(status)
        if status['state'] != 'recording':
            raise RuntimeError('recording failed: ' + json.dumps(status))
        if time.monotonic() > deadline:
            raise RuntimeError('producer did not finish within deadline')
    result['producer'] = json.loads(producer.stdout.readline())
    # Producer commit may precede consumer acceptance. Do not stop before every frame is accepted.
    deadline = time.monotonic() + 10
    while True:
        status = control('status')
        samples.append(status)
        if status['state'] != 'recording':
            raise RuntimeError('recording failed: ' + json.dumps(status))
        if int(status['accepted_frames']) == frames:
            break
        if time.monotonic() > deadline:
            raise RuntimeError('consumer did not accept every produced frame')
        time.sleep(0.02)
    control('stop', '--recording-id', started['recording_id'])
    deadline = time.monotonic() + 60
    while True:
        finished = control('status')
        if finished['state'] == 'idle':
            break
        if finished['state'] != 'stopping' or time.monotonic() >= deadline:
            raise RuntimeError('finalization failed: ' + json.dumps(finished))
        time.sleep(0.1)
    result['capture_and_sync_seconds'] = time.monotonic() - begin
    result['payload_bytes_per_second_including_sync'] = frames * 1048576 / result['capture_and_sync_seconds']
    result['final'] = finished
    capture = root / ('benchmark-' + identifier) / (started['recording_id'] + '.sink')
    result['capture'] = str(capture)
    result['file_validation'] = check(capture, iq_benchmark=True)
    if not result['file_validation']['complete'] or result['file_validation']['frames'] != frames or int(finished['written_frames']) != frames:
        raise RuntimeError('capture counts differ from producer')
    result['capture_integrity_passed'] = True
    result['throughput_tolerance_fraction'] = 0.01
    result['throughput_target_met'] = result['payload_bytes_per_second_including_sync'] >= 491520000 * 0.99
    result['success'] = args.functional_smoke or result['throughput_target_met']
    if not result['success']:
        result['error'] = 'capture intact, but durable throughput is below target (1% scheduling tolerance)'
except Exception as error:
    result['error'] = str(error)
finally:
    result['samples'] = samples
    result['observed_queue_peak_bytes'] = max((int(sample['queue_used_bytes']) for sample in samples), default=0)
    if worker is not None and worker.poll() is None:
        worker.send_signal(signal.SIGTERM)
        try:
            worker.wait(timeout=10)
        except subprocess.TimeoutExpired:
            worker.kill(); worker.wait()
            result['success'] = False
            result['shutdown_error'] = 'forced Worker termination; file may be incomplete'
    if producer is not None:
        if producer.poll() is None:
            producer.send_signal(signal.SIGTERM)
            producer.stdin.close()
            try:
                producer.wait(timeout=10)
            except subprocess.TimeoutExpired:
                producer.kill(); producer.wait()
        if producer.returncode:
            result['success'] = False
            result['producer_exit'] = producer.returncode
    report.write_text(json.dumps(result, indent=2))
    print(json.dumps(dict(report=str(report), **result), indent=2))
if not result['success']:
    raise SystemExit(1)
