#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
mode=${1:-baseline}
b="build/re05-$mode"; v=RE05_verification
mkdir -p "$b/_deps/powervr-decompress" "$b/_deps/stb-vorbis"
cp build/windows-msvc/_deps/powervr-decompress/PVRTDecompress.* "$b/_deps/powervr-decompress/"
cp build/windows-msvc/_deps/stb-vorbis/stb_vorbis.c "$b/_deps/stb-vorbis/"
trap 'echo $? > "$v/$mode.exit"' EXIT
args=(-DCMAKE_BUILD_TYPE=Release)
if [[ $mode == sanitize ]]; then
 args=(-DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
 '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
 '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
 '-DCMAKE_C_FLAGS_RELWITHDEBINFO=-O1 -g -DNDEBUG'
 '-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O1 -g -DNDEBUG'
 '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
fi
cmake -S . -B "$b" -G Ninja "${args[@]}" \
 -DFETCHCONTENT_SOURCE_DIR_ZLIB="$PWD/build/windows-msvc/_deps/zlib-src" \
 -DFETCHCONTENT_SOURCE_DIR_PUGIXML="$PWD/build/windows-msvc/_deps/pugixml-src" \
 -DFETCHCONTENT_FULLY_DISCONNECTED=ON > "$v/$mode-configure.log" 2>&1
cmake --build "$b" --parallel 3 > "$v/$mode-build.log" 2>&1
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
 ctest --test-dir "$b" --output-on-failure -V > "$v/$mode-tests.log" 2>&1
