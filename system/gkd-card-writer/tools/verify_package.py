#!/usr/bin/env python3
"""Verifier for the fixed-layout, non-extracting .gkdupdate container."""
from __future__ import annotations
import hashlib,json,struct,subprocess,sys,tempfile
from pathlib import Path
MAGIC=b'GKDUV1\0\0'; SIG=256; SCHEMA='gkd-mini-generic-update-v1'; HEAD=12
class PackageError(RuntimeError):pass
def sha(b):return hashlib.sha256(b).hexdigest()
def strict(d,k,what):
 if not isinstance(d,dict) or set(d)!=set(k):raise PackageError(what+' keys')
def load(p):
 raw=Path(p).read_bytes()
 if len(raw)<HEAD+SIG or raw[:8]!=MAGIC:raise PackageError('magic')
 n=struct.unpack('<I',raw[8:12])[0]
 if n<2 or n>4*1024*1024 or len(raw)<HEAD+n+SIG:raise PackageError('manifest bound')
 mb=raw[HEAD:HEAD+n]; sig=raw[HEAD+n:HEAD+n+SIG]
 try:m=json.loads(mb.decode('utf-8'))
 except:raise PackageError('manifest json')
 strict(m,['components','disk','packs','protocol','schema','version'],'manifest')
 if m['schema']!=SCHEMA:raise PackageError('schema')
 strict(m['packs'],['after','before','block_size'],'packs');
 if not isinstance(m['packs']['block_size'],str) or not m['packs']['block_size'].isdigit():raise PackageError('block size type')
 bs=int(m['packs']['block_size'])
 if bs not in (512,1024,2048,4096):raise PackageError('block size')
 for x in ('before','after'):
  strict(m['packs'][x],['bytes','sha256'],x); 
  if not isinstance(m['packs'][x]['bytes'],str) or not m['packs'][x]['bytes'].isdigit():raise PackageError('u64 decimal')
 a=int(m['packs']['before']['bytes']);b=int(m['packs']['after']['bytes'])
 if a>512*1024*1024 or b>512*1024*1024 or len(raw)!=HEAD+n+SIG+a+b:raise PackageError('exact container length')
 before=raw[HEAD+n+SIG:HEAD+n+SIG+a];after=raw[-b:] if b else b''
 if sha(before)!=m['packs']['before']['sha256'] or sha(after)!=m['packs']['after']['sha256']:raise PackageError('pack hash')
 return m,mb,sig,before,after
def verify_signature(p,pub):
 m,mb,sig,b,a=load(p)
 with tempfile.TemporaryDirectory() as d:
  x=Path(d,'m'); y=Path(d,'s');x.write_bytes(mb);y.write_bytes(sig)
  if subprocess.run(['openssl','dgst','-sha256','-verify',str(pub),'-signature',str(y),str(x)],capture_output=True).returncode:raise PackageError('signature')
 return m
if __name__=='__main__':verify_signature(sys.argv[1],Path(sys.argv[2]) if len(sys.argv)>2 else Path(__file__).parents[1]/'trust-anchor.pem');print('PACKAGE_PASS')
