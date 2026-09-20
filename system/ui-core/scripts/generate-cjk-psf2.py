#!/usr/bin/env python3
"""Build pinned native WenQuanYi Chinese PSF2; explicitly reuse separate Latin."""
import argparse, hashlib, json, struct
from pathlib import Path
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--size',type=int,choices=(12,14),default=14)
    args=parser.parse_args()
    source=Path(__file__).resolve().parent.parent/'third_party/wenquanyi'
    manifest=json.loads((source/'SOURCE.json').read_text())
    entry=manifest['subsets'][str(args.size)]
    raw=(source/entry['file']).read_bytes()
    if hashlib.sha256(raw).hexdigest()!=entry['sha256']:
        raise SystemExit('CJK_FONT=BLOCKED source-hash')
    glyphs=[]
    charsize=((args.size+7)//8)*args.size
    for line in raw.decode('ascii').splitlines():
        cp,bits=line.split(':')
        value=int(cp,16); bitmap=bytes.fromhex(bits)
        if value<256 or value>0xFFFF or len(bitmap)!=charsize:
            raise SystemExit('CJK_FONT=BLOCKED glyph')
        glyphs.append((value,bitmap))
    if len(set(cp for cp,_ in glyphs))!=len(glyphs):
        raise SystemExit('CJK_FONT=BLOCKED duplicate')
    header=struct.pack('<8I',0x864ab572,0,32,1,len(glyphs),charsize,args.size,args.size)
    blob=header+b''.join(bits for _,bits in glyphs)+b''.join(chr(cp).encode('utf-8')+bytes([255]) for cp,_ in glyphs)
    with args.output.open('xb') as f: f.write(blob)
    print('CJK_FONT=PASS size=%d glyphs=%d bytes=%d sha256=%s'%(args.size,len(glyphs),len(blob),hashlib.sha256(blob).hexdigest()))
if __name__=='__main__':main()
