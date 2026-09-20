#!/bin/sh
set -eu

project=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd -P)
output=${1:?usage: build-selector.sh OUTPUT}
image=local/c-builder:2026.08.02-kernel
image_id=sha256:43526337ccf802bc40fac6745f099c3686a7deca335cab780b5bcd76e64d63a1
toolroot=/opt/gkd-build/.cache/gkd-mini-v1.2/ingenic-toolchain-v5.2

case "$output" in
  /tmp/gkd-mini-public/gkd-ar-selector-*|/opt/gkd-build/artifacts/gkd-mini-system-rebuild/ar-selector-*) ;;
  *) echo GKD_AR_SELECTOR=BLOCKED unsafe-output >&2; exit 2 ;;
esac
[ ! -e "$output" ]
[ "$(docker image inspect "$image" --format '{{.Id}}')" = "$image_id" ]
mkdir -p "$output"

docker run --rm --network none --user "$(id -u):$(id -g)" \
  -e HOME=/opt/gkd-build -e SOURCE_DATE_EPOCH=1785448800 \
  -v "$project:/workspace:ro" -v "$toolroot:$toolroot:ro" \
  -v "$output:$output:rw" "$image" sh -eu -c '
cross=/opt/gkd-build/.cache/gkd-mini-v1.2/ingenic-toolchain-v5.2/toolchain/bin/mips-linux-gnu-
source=/workspace/system/ar-boot-selector/source/gkd-ar-selector.S
out="$1"
"${cross}gcc" -EL -march=mips32r2 -mabi=32 -mno-abicalls -fno-pic \
  -nostdlib -Wl,-Ttext,0x80600000 -Wl,-e,_start -Wl,--build-id=none \
  "$source" -o "$out/selector.elf"
"${cross}objcopy" -O binary -j .text "$out/selector.elf" "$out/selector.bin"
"${cross}objdump" -d "$out/selector.elf" >"$out/selector.disassembly.txt"
"${cross}readelf" -h "$out/selector.elf" >"$out/selector.readelf.txt"
mkimage -A mips -O linux -T standalone -C none -a 80600000 -e 80600000 \
  -n gkd-ar-selector-v1 -d "$out/selector.bin" "$out/selector.uimg"
mkimage -l "$out/selector.uimg" >"$out/selector.mkimage.txt"
size=$(stat -c %s "$out/selector.uimg")
[ "$size" -le 4096 ]
dd if="$out/selector.uimg" of="$out/selector-block.bin" bs=4096 count=1 \
  conv=sync status=none
[ "$(stat -c %s "$out/selector-block.bin")" -eq 4096 ]
(cd "$out" && sha256sum selector.bin selector.elf selector.uimg \
  selector-block.bin selector.disassembly.txt selector.mkimage.txt \
  selector.readelf.txt >SHA256SUMS)
' sh "$output"

grep -Fq "Data:                              2's complement, little endian" \
  "$output/selector.readelf.txt"
grep -Fq 'Entry point address:               0x80600000' \
  "$output/selector.readelf.txt"
grep -Fq 'Image Type:   MIPS Linux Standalone Program (uncompressed)' \
  "$output/selector.mkimage.txt"
printf 'GKD_AR_SELECTOR=PASS output=%s\n' "$output"
