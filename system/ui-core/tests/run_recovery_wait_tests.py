#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='gkd-recovery-wait-',dir='/tmp/gkd-mini-public') as tmp:
 for kind,flags in [('normal',[]),('asan',['-fsanitize=address,undefined','-fno-omit-frame-pointer','-no-pie'])]:
  out=Path(tmp)/kind
  command=['cc','-std=gnu99','-O1','-g','-Wall','-Wextra','-Werror','-DGKD_DEDICATED_RECOVERY=1','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(r/'include'),str(r/'tests/recovery_wait_fixture.c'),str(r/'source/gkd-ui.c'),'-o',str(out)]+flags
  command+=['-Wl,--wrap='+x for x in ['gkd_ui_render_loading','gkd_ui_render_status','gkd_ui_draw_osd','fsync','execve','sync','reboot','gkd_input_owner_next_key']]
  subprocess.run(command,check=True);subprocess.run([str(out)],check=True,timeout=20)
  deadline=Path(tmp)/(kind+'-input-timeout')
  subprocess.run(['cc','-std=gnu99','-O1','-g','-Wall','-Wextra','-Werror','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I'+str(r/'include'),str(r/'tests/input_owner_timeout_fixture.c'),str(r/'source/gkd-input-owner.c'),'-o',str(deadline)]+flags,check=True)
  subprocess.run([str(deadline)],check=True,timeout=10)
print('GKD_RECOVERY_WAIT_TESTS=PASS normal/ASan/UBSan')
