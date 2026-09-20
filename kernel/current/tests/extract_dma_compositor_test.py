#!/usr/bin/env python3
"""Extract the DMA helper from the actual 0001/0002/0004/0006 stack."""
from pathlib import Path
import re, subprocess, sys, tempfile

root = Path(__file__).resolve().parents[3]
out = Path(sys.argv[1])
patches = root / "kernel/current/patches"
stack = [patches / x for x in ("0001-rc34-accepted-kernel.patch",
    "0002-application-user-plane.patch", "0004-application-debug-freeze.patch",
    "0006-application-dma-compositor.patch")]
assert all(x.is_file() for x in stack)
driver_name = "drivers/video/fbdev/ingenic-x1830-dpu-fb.c"
header_name = "drivers/video/fbdev/gkd-x1830-dma.h"

def extract(text, name):
    found = list(re.finditer(r"^static [^;{]*\b" + re.escape(name) +
        r"\([^;{]*\)\s*\{", text, re.M))
    assert len(found) == 1, (name, len(found))
    end, depth = found[0].end(), 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[found[0].start():end]

with tempfile.TemporaryDirectory(prefix="gkd-dma-extract-") as temp:
    for patch in stack:
        subprocess.run(["git", "apply", "--include=" + driver_name,
            "--include=" + header_name, str(patch)], cwd=temp, check=True)
    driver = (Path(temp) / driver_name).read_text()
    header = (Path(temp) / header_name).read_text()

names = ["x1830_dma_completed", "x1830_dma_stage_cpu", "x1830_dma_stage_device", "x1830_dma_ready",
    "x1830_dma_transfer", "x1830_dma_copy_output", "x1830_dma_prepare", "x1830_dma_prepare_cached",
    "x1830_dma_finish"]
init = header.index("static int x1830_dma_init")
init_end = header.index("static int x1830_dma_ready", init)
assert header.index("dma_alloc_noncoherent", init) < header.index(
    "copy->stage_cpu = true;", init) < init_end
assert "copy->fault = ret;" in header
comp = driver.index("static int x1830_software_composite_locked")
direct = driver.index("ret = x1830_dma_copy_output", comp)
direct_front = driver.index("fb->composite_front = next", direct)
finish = driver.index("ret = x1830_dma_finish", comp)
cached_front = driver.index("fb->composite_front = next", finish)
assert direct < driver.index("x1830_dma_stage_cpu", direct) < direct_front
assert finish < cached_front
suspend = driver.index("static int x1830_fb_dma_suspend")
assert driver.index("fb->dma.paused = true;", suspend) < driver.index(
    "cancel_delayed_work_sync", suspend) < driver.index(
    "dmaengine_terminate_sync", suspend)
remove = driver.index("static int x1830_fb_remove")
assert driver.index("cancel_delayed_work_sync(&fb->composite_work)", remove) < driver.index(
    "x1830_dma_release(&fb->dma)", remove) < driver.index(
    "dma_free_coherent(fb->dev, X1830_COMPOSITE_SIZE", remove)
fixture = Path(__file__).with_name("dma_compositor_fixture.c").read_text()
assert fixture.count("/* DRIVER_FRAGMENT */") == 1
out.write_text(fixture.replace("/* DRIVER_FRAGMENT */",
    "\n\n".join(extract(header, x) for x in names)))
print("actual DMA helper and lifecycle contracts extracted")
