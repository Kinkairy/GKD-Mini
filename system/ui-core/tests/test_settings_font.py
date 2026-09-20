#!/usr/bin/env python3
"""Decode both PSF Unicode maps and verify exact native source/catalog coverage."""
import struct, sys
from pathlib import Path
root=Path(__file__).resolve().parent.parent
assert len(sys.argv)==3
for path,px in zip(sys.argv[1:],(14,12)):
    blob=Path(path).read_bytes()
    magic,version,header,flags,count,size,height,width=struct.unpack('<8I',blob[:32])
    assert (magic,version,header,flags,size,height,width)==(0x864ab572,0,32,1,2*px,px,px)
    entries=blob[header+count*size:].split(bytes([255]))
    assert entries[-1]==b'' and len(entries)==count+1
    mapping={}
    for i,raw in enumerate(entries[:-1]):
        chars=raw.decode('utf-8')
        assert len(chars)==1 and chars not in mapping
        mapping[chars]=blob[header+i*size:header+(i+1)*size]
    expected={}
    for line in (root/f'third_party/wenquanyi/native-cn-{px}.hex').read_text().splitlines():
        cp,bits=line.split(':');expected[chr(int(cp,16))]=bytes.fromhex(bits)
    assert mapping==expected
    needed={c for c in (root/'include/gkd-ui-language.def').read_text() if ord(c)>255}
    assert needed==mapping.keys()
    assert all(any(mapping[c]) for c in needed)
    print('SETTINGS_FONT_PASS native=%d psf-map/source-parity/catalog-coverage glyphs=%d bytes=%d'%(px,count,len(blob)))
