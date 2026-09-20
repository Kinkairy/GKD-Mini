#!/bin/sh
set -eu

for tool in \
  gcc g++ clang cmake ninja meson gdb ccache git \
  bc bison flex dtc mkimage openssl python3 pip3 rsync
do
  command -v "$tool" >/dev/null
done

echo "native-gcc: $(gcc -dumpfullversion -dumpversion)"
echo "native-clang: $(clang --version | head -n 1)"
echo "C builder toolchains: PASS"
