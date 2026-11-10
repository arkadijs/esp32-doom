#!/usr/bin/env python3
"""Compile PrBoom wall composites into a versioned, column-deduplicated WAD lump.

Reuses the unmodified C compositor via a native adapter. Original lumps stay intact
for reference comparison and fallback. No pixel quality or numerical changes.
"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
import sys
from pathlib import Path
from pack_sunlust import read, textures, name

ROOT = Path(__file__).resolve().parents[1]
HEADER = struct.Struct('<4s7I')
RECORD = struct.Struct('<8s4I')
COLUMN = struct.Struct('<3I')


def fnv(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xffffffff
    return h


def compositor(build, sanitize=False):
    source = (ROOT/'components/prboom/r_patch.c').read_text()
    parts = [source[source.index('static int getIsSolidAtSpot'):source.index('static void createPatch')],
             source[source.index('typedef struct {\n  unsigned short patches;'):source.index('  free(countsInColumn);\n}', source.index('static void createTextureCompositePatch'))+len('  free(countsInColumn);\n}')],
             source[source.index('const rcolumn_t *R_GetPatchColumnClamped'):source.index('//---------------------------------------------------------------------------\nconst rcolumn_t *R_GetPatchColumn(')]]
    # Retain the original compositor's GPL notice/provenance in the generated TU.
    generated = source[:source.index('#include')] + (ROOT/'tools/flash_texture_host.c').read_text().replace('/* ORIGINAL_COMPOSITOR */', '\n'.join(parts))
    build.mkdir(parents=True, exist_ok=True)
    cfile = build/'compositor.c'
    cfile.write_text(generated)
    exe = build/'compositor'
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if sanitize else []
    subprocess.run(['cc', '-std=c11', '-O2', '-g', *flags, '-I'+str(ROOT/'components/prboom/include'), str(cfile), '-o', str(exe)], check=True)
    return exe


def parse_raw(data):
    if data[:4] != b'TXR1':
        raise ValueError('bad host output')
    count, = struct.unpack_from('<I', data, 4)
    p, out = 8, []
    for i in range(count):
        name, w, h, mask = struct.unpack_from('<8sIII', data, p)
        p += 20
        columns = []
        for x in range(w):
            pixels = data[p:p+h]
            assert len(pixels) == h
            p += h
            n, = struct.unpack_from('<I', data, p)
            p += 4
            posts = data[p:p+12*n]
            assert len(posts) == 12*n
            p += 12*n
            columns.append((pixels, posts))
        out.append((name, w, h, mask, columns))
    assert p == len(data)
    return out


def encode(raw, texture1, pnames, selected=None):
    textures = parse_raw(raw)
    buf = bytearray(HEADER.size + RECORD.size*len(textures))
    pixels_pool, posts_pool, tables = {}, {}, {}
    def append(data, pool):
        if data not in pool:
            buf.extend(b'\0'*(-len(buf)%4))
            pool[data] = len(buf)
            buf.extend(data)
        return pool[data]
    for i, (name, w, h, mask, columns) in enumerate(textures):
        if selected is not None and name.split(b'\0',1)[0].decode() not in selected:
            RECORD.pack_into(buf, HEADER.size+i*RECORD.size, name, w, h, mask, 0)
            continue
        table = bytearray()
        for pixels, posts in columns:
            po = append(pixels, pixels_pool)
            so = append(posts, posts_pool) if posts else 0
            table.extend(COLUMN.pack(len(posts)//12, po, so))
        co = append(bytes(table), tables)
        RECORD.pack_into(buf, HEADER.size+i*RECORD.size, name, w, h, mask, co)
    HEADER.pack_into(buf, 0, b'TXF1', 1, len(textures), len(buf), fnv(texture1), fnv(pnames), RECORD.size, 0)
    return bytes(buf), dict(textures=len(textures), compiled_textures=sum(1 for n,*_ in textures if selected is None or n.split(b"\0",1)[0].decode() in selected), unique_pixel_columns=len(pixels_pool),
                           pixel_bytes=sum(map(len,pixels_pool)), post_bytes=sum(map(len,posts_pool)),
                           column_tables_bytes=sum(map(len,tables)), lump_bytes=len(buf))


def referenced_textures(lumps):
    """Conservative map-wall/switch/animation closure; others retain raw fallback."""
    lookup = dict(lumps)
    order = list(textures(lookup['TEXTURE1']))
    needed = {'SKY1', order[0]}
    for key, data in lumps:
        if key == 'SIDEDEFS':
            for i in range(0, len(data), 30):
                needed.update(name(data[i+p:i+p+8]) for p in (4,12,20))
    needed.discard('-'); needed.discard('')
    while True:
        old = needed.copy()
        switches = lookup['SWITCHES']
        for i in range(0, len(switches), 20):
            if not struct.unpack_from('<H', switches, i+18)[0]: break
            pair = {name(switches[i:i+9]), name(switches[i+9:i+18])}
            if pair & needed: needed |= pair
        anims = lookup['ANIMATED']
        for i in range(0, len(anims), 23):
            if anims[i] == 255: break
            if anims[i] != 1: continue
            start, end = name(anims[i+10:i+19]), name(anims[i+1:i+10])
            if start not in order: continue
            sequence = set(order[order.index(start):order.index(end)+1])
            if sequence & needed: needed |= sequence
        if needed == old: break
    if not needed <= set(order): raise ValueError('missing texture in closure')
    return needed


def write_wad(lumps, path):
    buf = bytearray(12)
    directory, shared = [], {}
    for name, data in lumps:
        if data not in shared:
            buf.extend(b'\0'*(-len(buf)%4)); shared[data] = len(buf); buf.extend(data)
        directory.append(struct.pack('<II8s', shared[data], len(data), name.encode()))
    off = len(buf)
    buf.extend(b''.join(directory))
    struct.pack_into('<4sII', buf, 0, b'IWAD', len(directory), off)
    if len(buf) > 14848*1024:
        raise ValueError(f'compiled WAD exceeds partition: {len(buf)} bytes')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(buf)
    assert read(path) == lumps
    return len(buf)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('source', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--sanitize', action='store_true')
    a = p.parse_args()
    if sys.byteorder != 'little' or a.source.resolve() == a.output.resolve():
        raise SystemExit('requires little-endian host and a distinct output path')
    lumps = read(a.source)
    assert not any(n == 'TXFLASH' for n, _ in lumps)
    build = a.output.parent/'host'
    exe = compositor(build, a.sanitize)
    raw_path = build/'composites.raw'
    subprocess.run([str(exe.resolve()), str(a.source.resolve()), str(raw_path.resolve())], check=True,
                   env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0", "UBSAN_OPTIONS": "halt_on_error=1"})
    lookup = dict(lumps)
    selected = referenced_textures(lumps)
    flash, report = encode(raw_path.read_bytes(), lookup['TEXTURE1'], lookup['PNAMES'], selected)
    report['compiled_names'] = sorted(selected)
    report['wad_bytes'] = write_wad(lumps+[('TXFLASH', flash)], a.output)
    report['compositor_source_sha256'] = hashlib.sha256((ROOT/'components/prboom/r_patch.c').read_bytes()).hexdigest()
    report['host_adapter_sha256'] = hashlib.sha256((ROOT/'tools/flash_texture_host.c').read_bytes()).hexdigest()
    report['source_sha256'] = hashlib.sha256(a.source.read_bytes()).hexdigest()
    report['wad_sha256'] = hashlib.sha256(a.output.read_bytes()).hexdigest()
    a.output.with_suffix('.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))
