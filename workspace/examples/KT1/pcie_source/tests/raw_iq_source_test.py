#!/usr/bin/env python3
"""Real SDK/process stop regression, no hardware or SignalSink endpoint touched."""
import os
from pathlib import Path
import select
import subprocess
import sys
import time

source, checker = map(str, map(Path.resolve, map(Path, sys.argv[1:3])))
name = f'/rawiq-stop-test-{os.getpid()}'
env = dict(os.environ, UESTCRADAR_DOWNSTREAM_SHM_NAME=name)
holder = subprocess.Popen([checker, '--hold-ring', name], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
producer = None
try:
    assert select.select([holder.stdout], [], [], 5)[0], 'Ring fixture startup timed out'
    assert holder.stdout.readline().strip() == 'ready'
    producer = subprocess.Popen([source, '--frames', '0', '--sample-rate', '0'], env=env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    deadline = time.monotonic() + 5
    while True:
        holder.stdin.write('used\n')
        holder.stdin.flush()
        assert select.select([holder.stdout], [], [], 2)[0]
        used = holder.stdout.readline().strip()
        assert producer.poll() is None, 'producer exited before stop'
        if used == '2':
            break
        assert time.monotonic() < deadline, 'producer did not fill Ring'
        time.sleep(.01)
    producer.terminate()
    out, error = producer.communicate(timeout=2)
    assert producer.returncode == 0 and 'frames=2 ' in out, (producer.returncode, out, error)
finally:
    if producer is not None and producer.poll() is None:
        producer.kill()
        producer.wait()
    holder.stdin.close()
    holder.wait(timeout=3)

start = time.monotonic()
missing = subprocess.run([source, '--frames', '1'], env=env, capture_output=True, text=True, timeout=5)
assert missing.returncode != 0 and 'ring open timed out' in missing.stderr
assert time.monotonic() - start < 4
print('full Ring SIGTERM and missing-port timeout passed')
