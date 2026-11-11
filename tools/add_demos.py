#!/usr/bin/env python3
"""Add demo recordings (*.lmp) to the prboom-plus.wad support PWAD.

Demos are stored as DEMO1..DEMO8 in sorted file-name order, replacing any
existing DEMOn lumps, so the title loop plays them like a regular Doom II.
Only the 448 KiB support partition at 0xF80000 needs reflashing afterwards.
"""
import argparse
import re
import struct
from pathlib import Path

LIMIT = 448 * 1024
MAX_DEMOS = 8


def read_wad(path):
    data = path.read_bytes()
    kind, count, offset = struct.unpack_from('<4sII', data)
    assert kind in (b'IWAD', b'PWAD')
    lumps = []
    for i in range(count):
        pos, size, name = struct.unpack_from('<II8s', data, offset + 16 * i)
        lumps.append((name.rstrip(b'\0').decode('ascii'), data[pos:pos + size]))
    return lumps


def clean_demo(path):
    """Return the demo bytes without the PrBoom+ trailing PWAD footer."""
    data = path.read_bytes()
    footer = data.rfind(b'\x80PWAD')
    if footer >= 0:
        data = data[:footer + 1]
    assert len(data) > 13 and data[-1] == 0x80, f'{path}: no demo end marker'
    print(f'{path.name}: demo version {data[0]}, {len(data)} bytes')
    return data


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('support', type=Path, help='input prboom-plus.wad')
    p.add_argument('output', type=Path, help='output prboom-plus.wad')
    p.add_argument('demos', type=Path, nargs='+', help='directory with *.lmp files, or the .lmp files themselves')
    args = p.parse_args()

    files = sorted(f for d in args.demos for f in (d.glob('*.lmp') if d.is_dir() else [d]))
    assert 0 < len(files) <= MAX_DEMOS, f'need 1..{MAX_DEMOS} demos, found {len(files)}'
    lumps = [(k, v) for k, v in read_wad(args.support) if not re.fullmatch(r'DEMO\d', k)]
    for n, f in enumerate(files, 1):
        lumps.append((f'DEMO{n}', clean_demo(f)))

    out = bytearray(12)
    directory = []
    for name, data in lumps:
        directory.append(struct.pack('<II8s', len(out), len(data), name.encode()))
        out.extend(data)
    struct.pack_into('<4sII', out, 0, b'PWAD', len(lumps), len(out))
    out.extend(b''.join(directory))
    assert len(out) <= LIMIT, f'{len(out)} bytes exceeds {LIMIT}'
    args.output.write_bytes(out)
    assert [k for k, _ in read_wad(args.output)] == [k for k, _ in lumps]
    print(f'{args.output}: {len(out)} bytes, {LIMIT - len(out)} free, {len(files)} demos')


if __name__ == '__main__':
    main()
