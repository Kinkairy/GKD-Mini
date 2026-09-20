#!/usr/bin/env python3
"""Execute patched MIPS branch paths against bounded RAM to verify load/menu guards."""
from pathlib import Path
import importlib.util,struct,sys
script=Path(__file__).resolve().parents[1]/"scripts/patch-watara-rom-load.py"
spec=importlib.util.spec_from_file_location("patcher",script);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
original=Path(sys.argv[1]).read_bytes();image=m.patch(original)
def memory_image():
 mem={}
 po=struct.unpack_from("<I",image,28)[0]
 for i in range(7):
  t,off,va,pa,sz,ms,flags,align=struct.unpack_from("<8I",image,po+i*32)
  if t==1:
   for n in range(0,sz,4):mem[va+n]=int.from_bytes(image[off+n:off+n+4],"little")
 return mem
def run(pc,argc,rom):
 mem=memory_image();regs=[0]*32;regs[29]=0x70000000;regs[28]=0x42d460
 mem[regs[29]+296]=argc;mem[0x425714]=rom;mem[0x425720]=0
 regs[31]=0x410818
 pending=None
 for step in range(40):
  if pc in (0x410608,0x410cb0,0x4100f8):
   return pc,mem[0x425720],regs
  word=mem[pc];op=word>>26;rs=(word>>21)&31;rt=(word>>16)&31;imm=word&65535;si=imm if imm<32768 else imm-65536
  target=None
  if op==0:assert word==0
  elif op==15:regs[rt]=imm<<16
  elif op==35:regs[rt]=mem.get((regs[rs]+si)&0xffffffff,0)
  elif op==43:mem[(regs[rs]+si)&0xffffffff]=regs[rt]
  elif op==9:regs[rt]=(regs[rs]+si)&0xffffffff
  elif op==10:regs[rt]=int((regs[rs] if regs[rs]<0x80000000 else regs[rs]-0x100000000)<si)
  elif op==5:target=pc+4+si*4 if regs[rs]!=regs[rt] else pc+8
  elif op in (2,3):
   target=((pc+4)&0xf0000000)|((word&0x3ffffff)<<2)
   if op==3:regs[31]=pc+8
  else:raise AssertionError(hex(word))
  regs[0]=0
  next_pc=pending if pending is not None else pc+4
  assert not (pending is not None and target is not None),"branch in delay slot"
  pending=target;pc=next_pc
 raise AssertionError("unbounded patch")
for argc in (0,1,2,3,20):
 dest,state,regs=run(0x4105f8,argc,0)
 assert dest==0x410608 and state==(1 if argc>=2 else 2)
for rom in (0,0x2000000):
 dest,state,regs=run(0x410810,2,rom)
 assert dest==(0x4100f8 if rom else 0x410cb0)
 if not rom:assert state==2
 else:assert regs[31]==0x410818
bad=bytearray(original);bad[0x105fc]^=1
try:m.patch(bytes(bad))
except ValueError:pass
else:raise AssertionError("wrong base accepted")
print("WATARA_PATCH=PASS argc paths; unloaded/resume guard; delay slots; exact-base refusal")
