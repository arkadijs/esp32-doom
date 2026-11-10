#!/usr/bin/env python3
"""Check mapped columns against the original C compositor and reject bad records."""
import argparse
import struct
from pathlib import Path
from pack_sunlust import read
from build_flash_textures import HEADER, RECORD, COLUMN, fnv, parse_raw, referenced_textures


def validate(blob, reference, lookup, selected):
    def require(test, message):
        if not test: raise ValueError(message)
    def region(offset, length):
        require(offset % 4 == 0 and 0 <= offset <= len(blob) and length <= len(blob)-offset, 'range')
        return blob[offset:offset+length]
    require(len(blob) >= HEADER.size, 'header length')
    magic, version, count, size, th, ph, rs, flags = HEADER.unpack_from(blob)
    require((magic,version,count,size,rs,flags) == (b'TXF1',1,len(reference),len(blob),24,0), 'header')
    require(th == fnv(lookup['TEXTURE1']) and ph == fnv(lookup['PNAMES']), 'identity')
    region(HEADER.size, count*RECORD.size)
    compiled = columns = 0
    for i, (name,w,h,mask,expected) in enumerate(reference):
        n,rw,rh,rm,co = RECORD.unpack_from(blob,HEADER.size+i*RECORD.size)
        require((n,rw,rh,rm) == (name,w,h,mask), 'texture identity')
        selected_name = name.rstrip(b'\0').decode() in selected
        require(bool(co) == selected_name, 'missing compiled texture')
        if not co: continue
        region(co,w*COLUMN.size)
        for x, (pixels, posts) in enumerate(expected):
            count,po,so = COLUMN.unpack_from(blob,co+x*COLUMN.size)
            require(region(po,h) == pixels, 'pixel mismatch')
            require(count*12 == len(posts), 'post count')
            if count: require(region(so,count*12) == posts, 'post mismatch')
            else: require(so == 0, 'empty posts')
            columns += 1
        compiled += 1
    return compiled, columns


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('source',type=Path)
    p.add_argument('compiled',type=Path)
    p.add_argument('reference_raw',type=Path)
    a=p.parse_args()
    source=read(a.source); compiled=read(a.compiled)
    assert compiled[:-1] == source and compiled[-1][0] == 'TXFLASH', 'original lumps changed'
    blob=compiled[-1][1]; lookup=dict(source)
    reference=parse_raw(a.reference_raw.read_bytes()); selected=referenced_textures(source)
    n,c=validate(blob,reference,lookup,selected)
    first=next(HEADER.size+i*RECORD.size for i in range(len(reference)) if RECORD.unpack_from(blob,HEADER.size+i*RECORD.size)[-1])
    co=struct.unpack_from('<I',blob,first+20)[0]
    pixel=struct.unpack_from('<I',blob,co+4)[0]
    cases=[blob[:10],blob[:-1]]
    for off,value in [(4,99),(first+20,0xffffffff),(co+4,len(blob)),(co,0xffffffff),(first+20,0)]:
        b=bytearray(blob);struct.pack_into('<I',b,off,value);cases.append(bytes(b))
    b=bytearray(blob);b[pixel]^=1;cases.append(bytes(b))
    for bad in cases:
        try: validate(bad,reference,lookup,selected)
        except (ValueError,struct.error): pass
        else: raise AssertionError('bad blob accepted')
    print(f'PASS: {n} mapped textures / {c} columns match C compositor; original lumps intact; {len(cases)} malformed/mismatched cases rejected')
