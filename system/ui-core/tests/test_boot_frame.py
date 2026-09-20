#!/usr/bin/env python3
from pathlib import Path
import struct
import subprocess
import sys

root = Path(sys.argv[1])
source = root / "boot-test.c"
source.write_text('''#include <stdio.h>
#include <stdlib.h>
#include "gkd-boot-frame.h"
int main(int argc, char **argv) {
    unsigned short pixels[328 * 241];
    unsigned i;
    if (argc != 3) return 2;
    for (i = 0; i < 328 * 241; ++i) pixels[i] = 0x1234;
    gkd_boot_frame(pixels, 328, (unsigned)strtoul(argv[1], 0, 10), atoi(argv[2]));
    return fwrite(pixels, sizeof(pixels), 1, stdout) == 1 ? 0 : 2;
}
''')
subprocess.run(["cc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                "-I/project/system/ui-core/include", "-I" + str(root),
                str(source), "-o", str(root / "boot-test")], check=True)


def render(stage, failure=0):
    raw = subprocess.run([str(root / "boot-test"), str(stage), str(failure)],
                         capture_output=True, check=True).stdout
    pixels = struct.unpack("<79048H", raw)
    assert all(pixels[y * 328 + x] == 0x1234 for y in range(240) for x in range(320, 328))
    assert all(pixel == 0x1234 for pixel in pixels[240 * 328:])
    return [pixels[y * 328 + x] for y in range(240) for x in range(320)]


base = render(0)
previous = -1
for stage in range(8):
    frame = render(stage)
    assert all(frame[y * 320 + x] == base[y * 320 + x]
               for y in range(240) for x in range(320)
               if not (111 <= x < 209 and 153 <= y < 162))
    filled = sum(frame[y * 320 + x] == 0x77f1
                 for y in range(156, 159) for x in range(113, 206))
    assert filled == 3 * (93 * stage // 7) and filled > previous
    previous = filled
    (root / f"boot-{stage}.rgb565").write_bytes(struct.pack("<76800H", *frame))
assert render(255) == render(7)
failure = render(4, 1)
assert failure[154 * 320 + 111] == 0xfacc
assert base[154 * 320 + 111] == 0x3569
print("GKD_BOOT_FRAME=PASS stages=8 bounds=guarded real_progress=1 failure=red")
if len(sys.argv) > 2:
    encoded = (root / "gkd-boot-art.generated.h").read_text().split(
        "gkd_boot_pixels[38400] = {", 1)[1].split("};", 1)[0]
    packed = bytes(int(value) for value in encoded.replace("\n", "").split(",") if value)
    assert len(packed) == 38400 and packed in Path(sys.argv[2]).read_bytes()
    print("GKD_BOOT_EMBEDDED=PASS exact_compiled_art=1")
