#!/usr/bin/env python3
"""Compare vendor output with SDK payloads and export the final RD map."""
import argparse
import json
import hashlib
import re
from pathlib import Path
import struct
import numpy as np
from PIL import Image
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def records(path, vendor):
    with path.open('rb') as stream:
        while header := stream.read(12 if vendor else 8):
            if vendor:
                magic, frequencies, ranges = struct.unpack('<Iii', header)
                assert magic == 0xAA55AA55
            else:
                ranges, frequencies = struct.unpack('<II', header)
            assert ranges > 0 and frequencies > 0
            dtype = np.dtype('<f8' if vendor else '<f4')
            length = ranges * frequencies * dtype.itemsize
            payload = stream.read(length)
            assert len(payload) == length, 'Incomplete result record'
            yield np.frombuffer(payload, dtype=dtype).reshape(ranges, frequencies)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('output', type=Path)
    parser.add_argument('--frames', type=int, default=10)
    args = parser.parse_args()
    vendor = list(records(args.output / 'algorithm_send_data.bin', True))
    sdk = list(records(args.output / 'rd_frames.bin', False))
    assert len(vendor) == len(sdk) == args.frames, (len(vendor), len(sdk))
    for index, (original, converted) in enumerate(zip(vendor, sdk)):
        assert original.shape == converted.shape
        assert np.isfinite(original).all() and np.isfinite(converted).all()
        assert np.array_equal(original.astype(np.float32), converted), f'CPI {index} differs'
    sink_log = (args.output / 'infra.log').read_text()
    digests = re.findall(r'\[PASSED\].*?sha256=([0-9a-f]{64})', sink_log)
    assert len(digests) == args.frames, 'Sink did not accept exactly the requested frames'
    for index, (frame, digest) in enumerate(zip(sdk, digests)):
        assert hashlib.sha256(frame.tobytes()).hexdigest() == digest, f'Sink CPI {index} differs'
    last = sdk[-1]
    np.save(args.output / 'rdmap_result.npy', last)
    lo, hi = float(last.min()), float(last.max())
    pixels = np.zeros(last.shape, dtype=np.uint8)
    if hi > lo:
        pixels = np.rint((last.astype(np.float64) - lo) * 255 / (hi-lo)).astype(np.uint8)
    Image.fromarray(pixels.T).save(args.output / 'rdmap_full_resolution.png')
    # Preserve peaks when reducing the range axis for a readable overview.
    step = max(1, int(np.ceil(last.shape[0] / 1600)))
    overview = np.maximum.reduceat(last, np.arange(0, last.shape[0], step), axis=0).T
    fig, ax = plt.subplots(figsize=(14, 5), constrained_layout=True)
    plot = ax.imshow(overview, origin='lower', aspect='auto', cmap='viridis',
                     extent=(-.5, last.shape[0]-.5, -.5, last.shape[1]-.5),
                     interpolation='nearest', vmin=lo, vmax=hi)
    ax.set(xlabel='Range bin', ylabel='Frequency bin',
           title=f'GFKD final CPI ({args.frames-1}) | {last.shape[0]} ranges x {last.shape[1]} frequencies\nOverview: maximum per {step} range bins; original matrix retained in NPY')
    fig.colorbar(plot, ax=ax, label='Algorithm output value')
    fig.savefig(args.output / 'rdmap_result.png', dpi=160, bbox_inches='tight')
    summary = {'frames': len(sdk), 'comparison': 'all vendor double -> SDK float32 values match exactly',
               'sink_payload_match': True, 'last_shape': list(last.shape), 'min': lo, 'max': hi,
               'peak_range_frequency': list(map(int, np.unravel_index(last.argmax(), last.shape)))}
    (args.output / 'verification.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(summary))

if __name__ == '__main__':
    main()
