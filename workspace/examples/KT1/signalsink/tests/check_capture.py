#!/usr/bin/env python3
"""Check framing and bytes of USINK001. Does not prove ADC continuity/durability."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def check(path, allow_partial=False, iq_benchmark=False):
    frames = total = 0
    digest = hashlib.sha256()
    contract = None
    expected = None
    if iq_benchmark:
        expected = bytearray(1048576)
        random = 0x12345678
        for offset in range(0, len(expected), 4):
            random ^= (random << 13) & 0xffffffff
            random ^= random >> 17
            random ^= (random << 5) & 0xffffffff
            struct.pack_into('<I', expected, offset, random)
    with open(path, 'rb') as stream:
        if stream.read(8) != b'USINK001':
            raise ValueError('invalid capture magic')
        while True:
            prefix = stream.read(8)
            if len(prefix) != 8:
                if allow_partial:
                    return dict(complete=False, frames=frames, raw_bytes=total, raw_sha256=digest.hexdigest())
                raise ValueError('missing record/footer: incomplete capture')
            length, = struct.unpack('<Q', prefix)
            if length == 0:
                footer = stream.read(16)
                if len(footer) != 16 or struct.unpack('<QQ', footer) != (frames, total) or stream.read(1):
                    raise ValueError('invalid footer counts or trailing bytes')
                return dict(complete=True, frames=frames, raw_bytes=total, raw_sha256=digest.hexdigest(),
                            sample_continuity='unverified')
            if length < 65 or length > 64 + 0xffffffff:
                raise ValueError('invalid record length')
            header = stream.read(64)
            if len(header) != 64:
                if allow_partial:
                    return dict(complete=False, frames=frames, raw_bytes=total, raw_sha256=digest.hexdigest())
                raise ValueError('truncated envelope')
            type_id, version, payload = struct.unpack_from('<QII', header, 16)
            if payload != length - 64 or not type_id or not version:
                raise ValueError('invalid envelope length/type')
            if contract is not None and contract != (type_id, version):
                raise ValueError('mixed input contracts')
            contract = type_id, version
            candidate = digest.copy()
            candidate.update(header)
            remaining = payload
            if iq_benchmark:
                if (type_id, version, payload) != (1, 3, 2136 + 1048576) or struct.unpack_from('<Q', header)[0] != frames + 1:
                    raise ValueError('benchmark frame contract/sequence differs')
                metadata = stream.read(2136)
                if len(metadata) != 2136 or struct.unpack_from('<QII', metadata) != (frames, 8, 32768):
                    raise ValueError('benchmark IQ metadata differs')
                if struct.unpack_from('<d', metadata, 32)[0] != 15360000:
                    raise ValueError('benchmark sample rate differs')
                candidate.update(metadata)
                remaining -= 2136
                struct.pack_into('<hh', expected, 0, frames % 32768, -(frames % 32768))
            while remaining:
                block = stream.read(min(remaining, 1024 * 1024))
                if not block:
                    if allow_partial:
                        return dict(complete=False, frames=frames, raw_bytes=total, raw_sha256=digest.hexdigest())
                    raise ValueError('truncated frame body')
                if expected is not None and block != expected[1048576 - remaining:1048576 - remaining + len(block)]:
                    raise ValueError('benchmark IQ samples differ')
                candidate.update(block)
                remaining -= len(block)
            digest = candidate
            frames += 1
            total += length


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--allow-partial', action='store_true')
    parser.add_argument('--iq-benchmark', action='store_true', help='verify the deterministic throughput.cpp IQ fixture')
    args = parser.parse_args()
    try:
        print(json.dumps(check(args.capture, args.allow_partial, args.iq_benchmark), indent=2))
    except (ValueError, OSError) as error:
        parser.exit(1, f'capture check failed: {error}\n')
