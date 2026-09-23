#!/bin/bash
set -euo pipefail
src=/mnt/data/usm_re03/OpenAndroidUSM
cd /mnt/data/usm_re03_clean/OpenAndroidUSM
v="$src/RE03_verification"
b=build/re03-clean
mkdir -p "$b/_deps/powervr-decompress" "$b/_deps/stb-vorbis"
cp "$src"/build/windows-msvc/_deps/powervr-decompress/PVRTDecompress.* "$b/_deps/powervr-decompress/"
cp "$src"/build/windows-msvc/_deps/stb-vorbis/stb_vorbis.c "$b/_deps/stb-vorbis/"
trap 'echo $? > "$v/clean.exit"' EXIT
cmake -S . -B "$b" -G Ninja -DCMAKE_BUILD_TYPE=Release \
 -DFETCHCONTENT_SOURCE_DIR_ZLIB="$src/build/windows-msvc/_deps/zlib-src" \
 -DFETCHCONTENT_SOURCE_DIR_PUGIXML="$src/build/windows-msvc/_deps/pugixml-src" \
 -DFETCHCONTENT_FULLY_DISCONNECTED=ON > "$v/clean-configure.log" 2>&1
cmake --build "$b" --parallel 2 > "$v/clean-build.log" 2>&1
ctest --test-dir "$b" --output-on-failure -V > "$v/clean-tests.log" 2>&1
