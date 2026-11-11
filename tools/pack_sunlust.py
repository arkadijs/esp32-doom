#!/usr/bin/env python3
"""Pack MAP01..05 with selected textures; preserve all actor sprites and sounds.

This supports the Doom II + Sunlust WAD layout, not arbitrary
PWAD merging. Output is a normal IWAD, with no image/audio quality changes.
"""
import argparse
import hashlib
import json
import re
import struct
from collections import OrderedDict
from pathlib import Path

# PrBoom draws these outside map sectors (menus, borders and finale pages).
ENGINE_FLATS = set('F_SKY1 GRNROCK FLOOR7_2 CEIL5_1 MFLR8_4 FLOOR4_8 SFLR6_1 MFLR8_3 SLIME16 RROCK14 RROCK07 RROCK17 RROCK13 RROCK19'.split())

MAP_LUMPS = 'THINGS LINEDEFS SIDEDEFS VERTEXES SEGS SSECTORS NODES SECTORS REJECT BLOCKMAP'.split()


def name(raw):
    return raw.split(b'\0', 1)[0].decode('ascii').upper()


def read(path):
    data = path.read_bytes()
    magic, count, directory = struct.unpack_from('<4sII', data)
    assert magic in (b'IWAD', b'PWAD') and directory + count * 16 <= len(data)
    out = []
    for i in range(count):
        offset, size, key = struct.unpack_from('<II8s', data, directory + i * 16)
        assert offset + size <= len(data)
        out.append((name(key), data[offset:offset + size]))
    return out


def pnames(data):
    count, = struct.unpack_from('<I', data)
    assert len(data) == 4 + count * 8
    return [name(data[4 + i * 8:12 + i * 8]) for i in range(count)]


def textures(data):
    count, = struct.unpack_from('<I', data)
    out = OrderedDict()
    for i in range(count):
        offset, = struct.unpack_from('<I', data, 4 + i * 4)
        patches, = struct.unpack_from('<H', data, offset + 20)
        record = data[offset:offset + 22 + patches * 10]
        assert len(record) == 22 + patches * 10
        key = name(record[:8])
        # PrBoom resolves duplicate texture names to the first definition.
        out.setdefault(key, record)
    return out


def pack(args):
    inputs = {p.resolve() for p in (args.iwad, args.pwad, args.support)}
    assert args.output.resolve() not in inputs
    assert (args.output.parent / 'prboom-plus.wad').resolve() not in inputs
    resources = {ns: OrderedDict() for ns in ('global', 'sprites', 'flats')}
    maps = {}
    patch_names = set()
    for path in (args.iwad, args.pwad):
        lumps = read(path)
        local = dict(lumps)
        patch_names.update(pnames(local['PNAMES']))
        ns = 'global'
        i = 0
        while i < len(lumps):
            key, data = lumps[i]
            if re.fullmatch(r'MAP\d\d', key):
                block = lumps[i:i + 11]
                assert [k for k, _ in block[1:]] == MAP_LUMPS
                maps[key] = block
                i += 11
                continue
            if key in ('S_START', 'SS_START', 'F_START', 'FF_START'):
                ns = 'sprites' if key[0] == 'S' else 'flats'
            elif key in ('S_END', 'SS_END', 'F_END', 'FF_END'):
                ns = 'global'
            elif not re.fullmatch(r'P[P0-9]?_(START|END)', key):
                resources[ns][key] = data
            i += 1
    glob = resources['global']
    assert 'TEXTURE2' not in glob, 'multiple texture directories need explicit merging'
    tex = textures(glob['TEXTURE1'])
    pn = pnames(glob['PNAMES'])
    needed = {next(iter(tex)), 'SKY1', 'SKY2', 'SKY3'}
    selected = [f'MAP{i:02}' for i in range(1, args.maps + 1)]
    for key in selected:
        level = dict(maps[key])
        assert level['NODES'][:4] not in (b'XNOD', b'ZNOD'), 'extended nodes unsupported'
        sides = level['SIDEDEFS']
        assert len(sides) % 30 == 0
        for i in range(0, len(sides), 30):
            needed.update(name(sides[i + p:i + p + 8]) for p in (4, 12, 20))
        sectors = level['SECTORS']
        for i in range(0, len(sectors), 26):
            for p in (4, 12):
                assert name(sectors[i+p:i+p+8]) in resources['flats']
    # Close the referenced wall set over switch pairs and whole animation
    # sequences. Keep flat animations whole and in their original order.
    flats_needed = ENGINE_FLATS & resources['flats'].keys()
    for key in selected:
        sectors = dict(maps[key])['SECTORS']
        for i in range(0, len(sectors), 26):
            flats_needed.update(name(sectors[i+p:i+p+8]) for p in (4, 12))
    switches, anim = glob['SWITCHES'], glob['ANIMATED']
    order, flat_order = list(tex), list(resources['flats'])
    while True:
        before = needed.copy()
        for i in range(0, len(switches), 20):
            if not struct.unpack_from('<H', switches, i + 18)[0]:
                break
            pair = {name(switches[i:i+9]), name(switches[i+9:i+18])}
            if pair & needed:
                needed.update(pair)
        for i in range(0, len(anim), 23):
            kind = anim[i]
            if kind == 255:
                break
            end, start = name(anim[i+1:i+10]), name(anim[i+10:i+19])
            sequence_order = order if kind else flat_order
            if start not in sequence_order:
                continue
            assert end in sequence_order and sequence_order.index(end) > sequence_order.index(start)
            sequence = sequence_order[sequence_order.index(start):sequence_order.index(end)+1]
            if kind:
                if set(sequence) & needed:
                    needed.update(sequence)
            else:
                flats_needed.update(sequence)
        if before == needed:
            break
    needed.discard('-')
    needed.discard('')
    assert not needed - tex.keys(), f'missing textures: {needed - tex.keys()}'
    kept = [(k, v) for k, v in tex.items() if k in needed]
    patches = set()
    for _, record in kept:
        for i in range(22, len(record), 10):
            index, = struct.unpack_from('<H', record, i + 4)
            patches.add(pn[index])
    assert all(k in glob or k in resources['sprites'] for k in patches)
    # Compact PNAMES as well as TEXTURE1 so startup metadata follows selection.
    newpn = [k for k in pn if k in patches]
    assert len(set(newpn)) == len(newpn)
    records = []
    for _, original in kept:
        record = bytearray(original)
        for i in range(22, len(record), 10):
            old, = struct.unpack_from('<H', record, i + 4)
            struct.pack_into('<H', record, i + 4, newpn.index(pn[old]))
        records.append(bytes(record))
    offset = 4 + 4 * len(records)
    directory = []
    for record in records:
        directory.append(offset)
        offset += len(record)
    glob['TEXTURE1'] = struct.pack('<I', len(records)) + struct.pack('<'+'I'*len(records), *directory) + b''.join(records)
    glob['PNAMES'] = struct.pack('<I', len(newpn)) + b''.join(k.encode().ljust(8, b'\0') for k in newpn)
    music = set(('D_RUNNIN', 'D_STALKS', 'D_COUNTD', 'D_BETWEE', 'D_DOOM')[:args.maps]) | {'D_DM2TTL', 'D_DM2INT', 'D_READ_M'}
    out = [(k, v) for k, v in glob.items()
           if (k not in patch_names or k in patches)
           and (not k.startswith('D_') or k in music)
           and not re.fullmatch(r'DEMO\d', k)]
    for ns, prefix in [('sprites', 'S'), ('flats', 'F')]:
        out.append((prefix+'_START', b''))
        out.extend((k, v) for k, v in resources[ns].items()
                   if ns != 'flats' or k in flats_needed)
        out.append((prefix+'_END', b''))
    for key in selected:
        out.extend(maps[key])
    payload = bytearray(12)
    entries, shared = [], {}
    for key, data in out:
        if data not in shared:
            payload.extend(b'\0' * (-len(payload) % 4))
            shared[data] = len(payload)
            payload.extend(data)
        entries.append(struct.pack('<II8s', shared[data], len(data), key.encode()))
    directory_offset = len(payload)
    payload.extend(b''.join(entries))
    struct.pack_into('<4sII', payload, 0, b'IWAD', len(entries), directory_offset)
    assert len(payload) <= 14848 * 1024, f'pack exceeds IWAD partition: {len(payload)}'
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    assert read(args.output) == out
    # The port autoloads the support WAD after the IWAD. Carry Sunlust's
    # overrides into that small WAD so its animation/switch tables win.
    support = read(args.support)
    overrides = dict(read(args.pwad))
    support = [(k, overrides.get(k, v) if k in ('ANIMATED', 'SWITCHES', 'M_WEAP') else v)
               for k, v in support]
    # Space in the 448 KiB support partition is needed for demos. These lumps
    # are never read by this firmware: the trig/gamma tables are compiled in
    # (prboom-wad-tables), and the Chex/Nerve/BFG DeHackEd patches and OpenGL
    # lumps belong to modes it does not support.
    unused = {'SINETABL', 'TANGTABL', 'TANTOANG', 'GAMMATBL', 'CHEXDEH', 'BFGBEX',
              'NERVEBEX', 'GLSHADOW', 'GLFP', 'GLVP'}
    support = [(k, v) for k, v in support if k not in unused]
    support_data = bytearray(12)
    support_dir = []
    for k, v in support:
        support_dir.append(struct.pack('<II8s', len(support_data), len(v), k.encode()))
        support_data.extend(v)
    struct.pack_into('<4sII', support_data, 0, b'PWAD', len(support), len(support_data))
    support_data.extend(b''.join(support_dir))
    assert len(support_data) <= 448 * 1024
    support_path = args.output.parent / 'prboom-plus.wad'
    assert support_path != args.output
    support_path.write_bytes(support_data)
    assert read(support_path) == support
    report = dict(support_bytes=len(support_data), support_sha256=hashlib.sha256(support_data).hexdigest(), maps=selected, bytes=len(payload), textures=len(kept), patches=len(newpn),
                  lumps=len(entries), sha256=hashlib.sha256(payload).hexdigest(),
                  inputs={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (args.iwad,args.pwad,args.support)},
                  retained='all sprites and sounds; referenced flats and textures with animation/switch closure')
    args.output.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--iwad', type=Path, required=True)
    parser.add_argument('--pwad', type=Path, required=True)
    parser.add_argument('--support', type=Path, required=True)
    parser.add_argument('--maps', type=int, choices=range(1, 6), default=5)
    parser.add_argument('--output', type=Path, required=True)
    pack(parser.parse_args())
