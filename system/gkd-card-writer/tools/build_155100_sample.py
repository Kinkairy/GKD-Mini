#!/usr/bin/env python3
"""Adapt a sealed hotfix into the generic data-only package format.

This is a sample adapter, not part of the stable writer.  It reads the sealed
155100 transaction as input and emits two independently materialized generic
packages.  No disk path is opened.
"""
from __future__ import annotations
import argparse, base64, hashlib, json, shutil, subprocess, tempfile, struct
from pathlib import Path
from verify_package import verify_signature, MAGIC
def canon(v): return json.dumps(v, sort_keys=True, separators=(',',':'), ensure_ascii=True).encode()

def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''): h.update(b)
 return h.hexdigest()
def dump(p,o): p.write_bytes(canon(o))
def sign(data,key):
 with tempfile.TemporaryDirectory() as d:
  a=Path(d,'data'); b=Path(d,'sig'); a.write_bytes(data)
  subprocess.run(['openssl','dgst','-sha256','-sign',str(key),'-out',str(b),str(a)],check=True)
  return base64.b64encode(b.read_bytes()).decode('ascii')
def build(src,out,key):
 m=json.loads((src/'manifest.json').read_text()); lock=m['disk_lock']; bs=m['block_size']
 blocks=m['blocks']; fm=m['file_maps']; before=src/'before-blocks.bin'; after=src/'after-blocks.bin'
 if len(blocks)*bs != before.stat().st_size or before.stat().st_size != after.stat().st_size: raise RuntimeError('sealed pack length')
 groups=[('opk','data',fm['opk']),('launcher','launcher',fm['launcher'])]
 by_block={b['p2_block']:b for b in blocks}; components=[]
 before_data=before.read_bytes(); after_data=after.read_bytes()
 for ident,kind,mapping in groups:
  mapped=[]
  for p2 in mapping:
   b=by_block.get(p2)
   if b is not None:
    mapped.append({'offset':str(b['absolute_offset']),'pack_index':str(b['index']),'before_sha256':b['before_sha256'],'after_sha256':b['after_sha256']})
  if not mapped: raise RuntimeError('component map missing '+ident)
  source=hashlib.sha256(b''.join(before_data[b['pack_index']*bs:(b['pack_index']+1)*bs] for b in mapped)).hexdigest()
  target=hashlib.sha256(b''.join(after_data[b['pack_index']*bs:(b['pack_index']+1)*bs] for b in mapped)).hexdigest()
  components.append({'id':ident,'kind':kind,'mapped_blocks':mapped,'source_sha256':source,'target_sha256':target,
    'preserve': {'raw_sha256':m['source'].get('sidekeys_sha256',''), 'meaning':'unmapped component data must remain unchanged'}})
 disk={'identity':{'disk_number':str(lock['disk_number']),'disk_bytes':str(lock['disk_bytes']),'mbr_sha256':lock['mbr_sha256'],'p2_uuid':lock['p2_uuid']},
       'partition_geometry':{'p2_offset':str(lock['p2_offset']),'p2_bytes':str(lock['p2_bytes']),'logical_block_bytes':str(bs)},
       'mbr_sha256':lock['mbr_sha256']}
 runs=[{k:(str(v) if k in ('bytes','count','first_index','start_block') else v) for k,v in r.items()} for r in m['opk_write_plan']['runs']]
 signed={'schema':'gkd-mini-generic-update-v1','version':1,'disk':disk,'components':components,
  'packs':{'block_size':str(bs),'before':{'bytes':str(before.stat().st_size),'sha256':sha(before)},'after':{'bytes':str(after.stat().st_size),'sha256':sha(after)}},
  'protocol':{'apply_order':['opk'],'launcher_last':True,'rollback_order':['launcher','opk'],'max_run_bytes':str(m['opk_write_plan']['max_run_bytes']),'runs':runs}}
 if out.exists(): out.unlink()
 manifest=canon(signed); signature=base64.b64decode(sign(manifest,key))
 if len(signature)!=256: raise RuntimeError('RSA-2048 required')
 out.parent.mkdir(parents=True,exist_ok=True); out.write_bytes(MAGIC+struct.pack('<I',len(manifest))+manifest+signature+before_data+after_data); out.chmod(0o444)
 return sha(out)
def main():
 ap=argparse.ArgumentParser(); ap.add_argument('--source',type=Path,required=True); ap.add_argument('--output',type=Path,required=True); ap.add_argument('--private-key',type=Path,default=Path('/opt/gkd-build/private-state/gkd-card-writer-dev/test-rsa-private.pem')); ap.add_argument('--public-key',type=Path,default=Path(__file__).parents[1]/'trust-anchor.pem'); a=ap.parse_args()
 a.output.mkdir(parents=True,exist_ok=True); hashes=[]
 for n in ('155100-a.gkdupdate','155100-b.gkdupdate'):
  p=a.output/n; hashes.append(build(a.source,p,a.private_key)); verify_signature(p,a.public_key)
 if hashes[0]!=hashes[1]: raise RuntimeError('A/B deterministic tree hash differs')
 print(json.dumps({'status':'PASS','a':hashes[0],'b':hashes[1],'equal':True},sort_keys=True))
if __name__=='__main__': main()
