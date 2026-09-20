#!/usr/bin/env python3
"""Exercise actual patched Ingenic fixup and upstream generic system-name reader."""
from pathlib import Path
import re, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[3]
kernel=Path(sys.argv[1]).resolve()
out=Path(sys.argv[2]).resolve()
assert out.is_relative_to(Path('/tmp/gkd-mini-public'))
rel='arch/mips/ingenic/board-ingenic.c'
with tempfile.TemporaryDirectory(prefix='gkd-board-',dir=out.parent) as work:
    dst=Path(work)/rel; dst.parent.mkdir(parents=True); dst.write_bytes((kernel/rel).read_bytes())
    for name in ['0001-rc34-accepted-kernel.patch','0007-application-board-identity.patch']:
        subprocess.run(['git','apply','--include='+rel,str(root/'kernel/current/patches'/name)],cwd=work,check=True)
    board=dst.read_text()
def function(source,name):
    matches=list(re.finditer(r'^(?:static )?[^;{}\n]*\b'+name+r'\([^;{}]*\)\s*\{',source,re.M))
    assert len(matches)==1,(name,len(matches))
    m=matches[0];end=m.end();depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[m.start():end]
generic=(kernel/'arch/mips/generic/proc.c').read_text()
fragment='\n'.join([function(board,'ingenic_get_system_type'),function(board,'ingenic_fixup_fdt'),function(generic,'get_system_type')])
constants=re.search(r'enum ingenic_machine_type \{[^}]+\};',(kernel/'arch/mips/include/asm/bootinfo.h').read_text()).group(0)
model=re.search(r'^\+\s*model = "([^"]+)";', (root/'kernel/current/patches/0001-rc34-accepted-kernel.patch').read_text(),re.M).group(1)
fixture=Path(__file__).with_name('board_identity_fixture.c').read_text()
fixture=fixture.replace('/* CONSTANTS */',constants)
fixture=fixture.replace('/* ACTUAL_FUNCTIONS */',fragment).replace('/* ACTUAL_MODEL */','"'+model+'"')
out.write_text(fixture)
print('ACTUAL_BOARD_IDENTITY_EXTRACT=PASS model='+model)
