#!/usr/bin/env python3
"""Local SDK/Worker/control/file test. Not ARM, Sidecar or Web acceptance."""
import hashlib
import json
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time
sys.dont_write_bytecode = True
from check_capture import check

binary, fixture_binary = sys.argv[1:]
ring = f'/signalsink-integration-{os.getpid()}'
fixture = subprocess.Popen([fixture_binary, ring], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
worker = None


def source(command):
    fixture.stdin.write(command + '\n')
    fixture.stdin.flush()
    return fixture.stdout.readline().strip()


def control(*args):
    if args[0] != 'status' and (worker.poll() is not None or control('status').get('process_id') != str(worker.pid)):
        raise RuntimeError('control endpoint does not belong to this test Worker')
    result = subprocess.run([binary, 'control', *args], capture_output=True, text=True, timeout=5)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return json.loads(result.stdout)


def wait_for(predicate):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        try:
            result = predicate()
            if result:
                return result
        except (RuntimeError, json.JSONDecodeError):
            pass
        time.sleep(0.02)
    raise AssertionError('condition timed out')


try:
    assert fixture.stdout.readline().strip() == 'ready'
    with tempfile.TemporaryDirectory(prefix='signalsink-integration-') as root:
        worker = subprocess.Popen([binary, '--input', '999:1', '--capture-root', root,
                                   '--queue-bytes', '1048576', '--min-free-bytes', '1048576'],
                                  env=dict(os.environ, UESTCRADAR_UPSTREAM_SHM_NAME=ring))
        wait_for(lambda: control('status').get('process_id') == str(worker.pid))
        assert control('status')['state'] == 'idle'
        assert source('push') == 'pushed'
        wait_for(lambda: source('used') == '0')
        assert not list(Path(root).rglob('*.partial'))
        started = control('start', '--directory', 'a/b')
        assert started['ok'] and started['state'] == 'recording'
        identifier = started['recording_id']
        for _ in range(4):
            assert source('push') == 'pushed'
        wait_for(lambda: control('status')['written_frames'] == '4')
        assert control('stop', '--recording-id', 'f' * 32)['ok'] is False
        assert control('stop', '--recording-id', identifier)['ok']
        wait_for(lambda: control('status')['state'] == 'idle')
        path = Path(root, 'a/b', identifier + '.sink')
        result = check(path)
        expected = hashlib.sha256()
        for index in range(1, 5):
            expected.update(struct.pack('<QQQIII28s', index, 1000 + index, 999, 1, 13, 0x1234, b'\xab' * 28))
            expected.update(bytes([index]) * 13)
        assert result['complete'] and result['frames'] == 4
        assert result['raw_sha256'] == expected.hexdigest(), result
        incomplete = Path(root, 'interrupted.partial')
        incomplete.write_bytes(path.read_bytes()[:-30])
        assert not check(incomplete, allow_partial=True)['complete']
        try:
            check(incomplete)
            raise AssertionError('truncated capture accepted')
        except ValueError:
            pass
        # No source data follows this start/stop; neither stop nor process exit may wait for a frame.
        started = control('start', '--directory', 'empty')
        assert control('stop', '--recording-id', started['recording_id'])['ok']
        wait_for(lambda: control('status')['state'] == 'idle')
        worker.send_signal(signal.SIGTERM)
        assert worker.wait(timeout=5) == 0
        worker = None
        print('PASS: default-off, full bytes/order, CLI, stale ID, partial file, empty stop, empty-input exit')
finally:
    if worker is not None:
        worker.terminate()
        try:
            worker.wait(timeout=5)
        except subprocess.TimeoutExpired:
            worker.kill()
            worker.wait()
    fixture.stdin.close()
    try:
        fixture.wait(timeout=5)
    except subprocess.TimeoutExpired:
        fixture.kill()
        fixture.wait()
