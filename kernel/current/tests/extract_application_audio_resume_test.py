#!/usr/bin/env python3
"""Compile-test actual codec functions from the applied production patch."""
from pathlib import Path
import argparse, re, subprocess, tempfile, hashlib
ap=argparse.ArgumentParser()
ap.add_argument("--output", type=Path, required=True)
ap.add_argument("--baseline", action="store_true")
ap.add_argument("--config-output", type=Path)
ap.add_argument("--regcache-source", type=Path)
ap.add_argument("--regcache-output", type=Path)
a=ap.parse_args()
assert bool(a.regcache_source) == bool(a.regcache_output)
project=Path(__file__).resolve().parents[3]
folded=project/"kernel/current/patches/0001-rc34-accepted-kernel.patch"
assert hashlib.sha256(folded.read_bytes()).hexdigest()=="9740d17d106046163fd3e1aca0a32b1b02a85b29a44ea14392670bcf199d8045"
section=folded.read_text().split("diff --git a/sound/soc/codecs/es8323.c b/sound/soc/codecs/es8323.c\n",1)[1].split("\ndiff --git ",1)[0]
baseline="\n".join(x[1:] for x in section.splitlines() if x.startswith("+") and not x.startswith("+++"))+"\n"
source=baseline
if not a.baseline:
    with tempfile.TemporaryDirectory(prefix="gkd-audio-applied-") as tmp:
        f=Path(tmp)/"sound/soc/codecs/es8323.c"
        f.parent.mkdir(parents=True); f.write_text(baseline)
        patch=project/"kernel/current/patches/0005-application-audio-resume.patch"
        subprocess.run(["git","apply","--check",str(patch)],cwd=tmp,check=True)
        subprocess.run(["git","apply",str(patch)],cwd=tmp,check=True)
        source=f.read_text()
def declaration(text, name):
    m=re.search(r"^.*\b"+re.escape(name)+r"(?:\[\])?\s*(?:=\s*)?\{",text,re.M)
    assert m,name
    end=text.index("\n};",m.start())+3
    return text[m.start():end]
def function(text, name):
    m=re.search(r"^(?:static )?[^\n]*\b"+re.escape(name)+r"\([^;]*?\n\{",text,re.M)
    assert m,name
    depth=0
    for pos in range(m.end()-1,len(text)):
        if text[pos]=="{": depth+=1
        elif text[pos]=="}":
            depth-=1
            if depth==0:
                return text[m.start():pos+1]
    raise AssertionError(name)
parts=["/* Extracted production declarations and function bodies; do not hand-edit. */"]
parts += [x for x in source.splitlines() if x.startswith("#define ES8323_")]
for name in ["es8323_coeff","es8323_coeffs","es8323_reg_defaults","es8323_priv","es8323_reg_sequence","es8323_init_sequence"]:
    parts.append(declaration(source,name))
parts.append(declaration(baseline,"es8323_reg_defaults").replace("es8323_reg_defaults","test_por_defaults"))
for name in ["es8323_write_sequence","es8323_update_speaker","es8323_find_coeff","es8323_set_fmt","es8323_hw_params","es8323_mute_stream","es8323_component_probe","es8323_component_suspend","es8323_component_resume"]:
    parts.append(function(source,name))
a.output.write_text("\n\n".join(parts)+"\n")
if a.config_output:
    a.config_output.write_text(declaration(source,"es8323_regmap_config")+"\n")
if a.regcache_source:
    assert (a.regcache_source/".git").exists()
    got=subprocess.check_output(["git","-C",str(a.regcache_source),"rev-parse","HEAD"],text=True).strip()
    assert got=="91fe78280ac7dd0dae0f58cb271e821bd39ba97e",got
    regcache_path=a.regcache_source/"drivers/base/regmap/regcache.c"
    regcache_bytes=regcache_path.read_bytes()
    pristine=subprocess.check_output(["git","-C",str(a.regcache_source),"show",got+":drivers/base/regmap/regcache.c"])
    assert regcache_bytes==pristine
    regcache=regcache_bytes.decode()
    regcache_parts=["/* Extracted pinned production regcache sync functions; do not hand-edit. */"]
    for name in ["regcache_reg_present","regcache_sync_block_single","regcache_sync_block_raw_flush","regcache_sync_block_raw","regcache_sync_block"]:
        regcache_parts.append(function(regcache,name))
    a.regcache_output.write_text("\n\n".join(regcache_parts)+"\n")
print("GKD_AUDIO_ACTUAL_EXTRACTION="+("baseline" if a.baseline else "patched")+" sha256="+hashlib.sha256(source.encode()).hexdigest())
