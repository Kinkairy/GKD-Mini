#!/usr/bin/env python3
import argparse,base64,hashlib,json,struct,subprocess,tempfile
from pathlib import Path
MAGIC=b'GKDUV1\0\0'
def canon(x):return json.dumps(x,sort_keys=True,separators=(',',':')).encode()
def sha(x):return hashlib.sha256(x).hexdigest()
def sign(data,key):
 with tempfile.TemporaryDirectory() as d:
  a=Path(d,'m');b=Path(d,'s');a.write_bytes(data);subprocess.run(['openssl','dgst','-sha256','-sign',str(key),'-out',str(b),str(a)],check=True);return b.read_bytes()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);ap.add_argument('--disk',type=Path,required=True);ap.add_argument('--key',type=Path,default=Path('/opt/gkd-build/private-state/gkd-card-writer-dev/test-rsa-private.pem'));a=ap.parse_args()
 bs=512;mbr=bytes(range(256))*2;before=b'A'*bs+b'B'*bs;after=b'C'*bs+b'D'*bs;a.disk.write_bytes(mbr+before+b'Z'*bs*5)
 comps=[]
 for ident,kind,idx in [('data','data',0),('launcher','launcher',1)]:
  block={'offset':str(512+idx*bs),'pack_index':str(idx),'before_sha256':sha(before[idx*bs:(idx+1)*bs]),'after_sha256':sha(after[idx*bs:(idx+1)*bs])}
  comps.append({'id':ident,'kind':kind,'mapped_blocks':[block],'source_sha256':sha(before[idx*bs:(idx+1)*bs]),'target_sha256':sha(after[idx*bs:(idx+1)*bs]),'preserve':{'meaning':'synthetic','raw_sha256':''}})
 s={'schema':'gkd-mini-generic-update-v1','version':1,'disk':{'identity':{'disk_number':'1','disk_bytes':str(a.disk.stat().st_size),'mbr_sha256':sha(mbr),'p2_uuid':'synthetic'},'partition_geometry':{'p2_offset':'512','p2_bytes':str(a.disk.stat().st_size-512),'logical_block_bytes':'512'},'mbr_sha256':sha(mbr)},'components':comps,'packs':{'block_size':'512','before':{'bytes':str(len(before)),'sha256':sha(before)},'after':{'bytes':str(len(after)),'sha256':sha(after)}},'protocol':{'apply_order':['data'],'launcher_last':True,'rollback_order':['launcher','data'],'max_run_bytes':'1024','runs':[]}}
 m=canon(s);sig=sign(m,a.key);assert len(sig)==256;a.output.write_bytes(MAGIC+struct.pack('<I',len(m))+m+sig+before+after)
if __name__=='__main__':main()
