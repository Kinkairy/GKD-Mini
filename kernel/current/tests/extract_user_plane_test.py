#!/usr/bin/env python3
"""Build tests from actual pinned-and-patched driver functions, never a model."""
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tempfile
project=Path(__file__).resolve().parents[3]
output=Path(sys.argv[1])
patches=project/'kernel/current/patches'
first=patches/'0001-rc34-accepted-kernel.patch'
second=patches/'0002-application-user-plane.patch'
freeze=patches/'0004-application-debug-freeze.patch'
dma=patches/'0006-application-dma-compositor.patch'
assert hashlib.sha256(first.read_bytes()).hexdigest()=='9740d17d106046163fd3e1aca0a32b1b02a85b29a44ea14392670bcf199d8045'
relative='drivers/video/fbdev/ingenic-x1830-dpu-fb.c'
with tempfile.TemporaryDirectory(prefix='gkd-plane-driver-test-') as temp:
    for patch in (first,second,freeze,dma):
        subprocess.run(['git','apply','--include='+relative,str(patch)],cwd=temp,check=True)
    driver=(Path(temp)/relative).read_text()
names=['x1830_user_menu_release_locked','x1830_user_menu_live_locked','x1830_user_menu_rows_locked',
       'x1830_user_plane_release_locked','x1830_user_plane_live_locked','x1830_user_plane_blend_locked',
       'x1830_user_debug_freeze_release_locked',
       'x1830_user_debug_freeze_live_locked','x1830_user_plane_copy_tile','x1830_software_composite_locked',
       'x1830_overlay_active_locked','x1830_composite_refresh_work',
       'gkd_ui_menu_get_caps','gkd_ui_menu_submit_ioctl','gkd_ui_menu_clear_ioctl','gkd_ui_menu_hide_ioctl','gkd_ui_plane_get_caps','gkd_ui_plane_submit_ioctl','gkd_ui_plane_clear_ioctl','x1830_fb_ioctl','x1830_menu_delay',
       'x1830_debug_frozen_release_locked','x1830_debug_loading_hold_locked','x1830_volume_osd_clear_work']
fragments=[]
for name in names:
    matches=list(re.finditer(r'^static [^;{]*\b'+name+r'\([^;{]*\)\s*\{',driver,re.M))
    assert len(matches)==1,name
    m=matches[0]; end=m.end(); depth=1
    while depth:
        depth += (driver[end]=='{')-(driver[end]=='}'); end+=1
    fragments.append(driver[m.start():end])
comp=driver.index('static int x1830_software_composite_locked')
direct=driver.index('ret = x1830_dma_copy_output',comp)
direct_front=driver.index('fb->composite_front = next',direct)
finish=driver.index('ret = x1830_dma_finish',comp)
cached_front=driver.index('fb->composite_front = next',finish)
assert direct < driver.index('x1830_dma_stage_cpu',direct) < direct_front
assert driver.index('x1830_user_plane_copy_tile(fb->dma.stage, target)',direct) < direct_front
assert finish < cached_front
fixture=Path(__file__).with_name('user_plane_fixture.c').read_text()
assert fixture.count('/* DRIVER_FRAGMENT */')==1
output.write_text(fixture.replace('/* DRIVER_FRAGMENT */','\n\n'.join(fragments)))
print('actual patches='+','.join(hashlib.sha256(x.read_bytes()).hexdigest()
      for x in (first,second,freeze,dma)))
print('actual functions='+','.join(names))
