#!/usr/bin/env python3
"""Small fixture/checker regression; never a throughput acceptance."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
sys.dont_write_bytecode = True
from check_capture import check

with tempfile.TemporaryDirectory(prefix='signalsink-throughput-smoke-') as root:
    run = subprocess.run([sys.executable, str(Path(__file__).with_name('benchmark.py')),
                          '--worker', sys.argv[1], '--producer', sys.argv[2],
                          '--capture-root', root, '--seconds', '0.02', '--functional-smoke'],
                         capture_output=True, text=True, timeout=20)
    assert run.returncode == 0, run.stdout + run.stderr
    result = json.loads(run.stdout)
    assert result['capture_integrity_passed'] and result['file_validation']['frames'] == 10
    capture = Path(result['capture'])
    with capture.open('r+b') as file:
        file.seek(16 + 64 + 2136 + 123)
        old = file.read(1)
        file.seek(-1, 1)
        file.write(bytes([old[0] ^ 1]))
    try:
        check(capture, iq_benchmark=True)
    except ValueError as error:
        assert 'samples differ' in str(error), error
    else:
        raise AssertionError('altered IQ sample accepted')
print('PASS: deterministic high-entropy IQ capture and corruption rejection; no performance claim')
