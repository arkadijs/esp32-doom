#!/usr/bin/env python3
"""Validate generated packs against user-supplied WADs (no game data in Git)."""
import argparse
import struct
from pathlib import Path
from pack_sunlust import read, textures, pnames, name, MAP_LUMPS, ENGINE_FLATS

p = argparse.ArgumentParser()
p.add_argument('pack', type=Path)
p.add_argument('sunlust', type=Path)
p.add_argument('--iwad', type=Path, help='Also verify retained flats, sprites and patch bytes')
a = p.parse_args()
original_lumps = read(a.sunlust)
packed_lumps = read(a.pack)
original, packed = dict(original_lumps), dict(packed_lumps)
oldtex, newtex = textures(original['TEXTURE1']), textures(packed['TEXTURE1'])
oldpn, newpn = pnames(original['PNAMES']), pnames(packed['PNAMES'])
if a.iwad:
    merged = dict(read(a.iwad)) | original
    assert not (ENGINE_FLATS & merged.keys()) - packed.keys(), 'UI/finale flat missing'
    for key, data in packed_lumps:
        if key in merged and key not in ('TEXTURE1', 'PNAMES', *MAP_LUMPS):
            assert data == merged[key], f'resource changed: {key}'

for key, new in newtex.items():
    old = oldtex[key]
    assert old[:22] == new[:22], key
    for i in range(22, len(new), 10):
        oi, = struct.unpack_from('<H', old, i+4)
        ni, = struct.unpack_from('<H', new, i+4)
        assert oldpn[oi] == newpn[ni], key
        assert old[i:i+4] == new[i:i+4] and old[i+6:i+10] == new[i+6:i+10], key
        assert newpn[ni] in packed
# Map bytes and lump order must survive byte-for-byte, including shared payloads.
maps = []
for i, (key, data) in enumerate(packed_lumps):
    if key.startswith('MAP') and len(key) == 5:
        maps.append(key)
        j = next(j for j, (k, _) in enumerate(original_lumps) if k == key)
        assert packed_lumps[i:i+11] == original_lumps[j:j+11]
        assert [k for k, _ in packed_lumps[i+1:i+11]] == MAP_LUMPS
        level = dict(packed_lumps[i:i+11])
        for n in range(0, len(level['SIDEDEFS']), 30):
            for offset in (4, 12, 20):
                texture = name(level['SIDEDEFS'][n+offset:n+offset+8])
                assert texture in newtex or texture in ('', '-')
        for n in range(0, len(level['SECTORS']), 26):
            for offset in (4, 12):
                assert name(level['SECTORS'][n+offset:n+offset+8]) in packed

# Preserve entire animation ranges, not only their endpoint names.
anims = packed['ANIMATED']
for i in range(0, len(anims), 23):
    kind = anims[i]
    if kind == 255:
        break
    if kind:
        end, start = name(anims[i+1:i+10]), name(anims[i+10:i+19])
        if start in newtex:
            before, after = list(oldtex), list(newtex)
            assert before[before.index(start):before.index(end)+1] == after[after.index(start):after.index(end)+1]
# Every included flat animation must retain the whole original sequence.
def flats(lumps):
    active = False
    out = []
    for key, data in lumps:
        if key in ('F_START', 'FF_START'):
            active = True
        elif key in ('F_END', 'FF_END'):
            active = False
        elif active:
            out.append(key)
    return out
before = list(dict.fromkeys(flats(read(a.iwad)) + flats(original_lumps))) if a.iwad else []
after = flats(packed_lumps)
if a.iwad:
    assert not (ENGINE_FLATS & set(before)) - set(after), 'UI/finale flat outside flat namespace'
for i in range(0, len(anims), 23):
    if anims[i] == 255:
        break
    if anims[i] == 0:
        end, start = name(anims[i+1:i+10]), name(anims[i+10:i+19])
        if start in after:
            assert end in after
            if before:
                assert before[before.index(start):before.index(end)+1] == after[after.index(start):after.index(end)+1]
for key in ('DEHACKED', 'SWITCHES', 'ANIMATED'):
    assert packed[key] == original[key]
support = dict(read(a.pack.parent / 'prboom-plus.wad'))
for key in ('SWITCHES', 'ANIMATED', 'M_WEAP'):
    assert support[key] == original[key]
print(f'PASS: {len(maps)} maps unchanged; {len(newtex)} texture definitions, patch references, animation ranges and Sunlust overrides verified')
