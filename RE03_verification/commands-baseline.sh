#!/bin/bash
set -e
cd /mnt/data/usm_re03/OpenAndroidUSM
trap 'echo $? > RE03_verification/baseline.exit' EXIT
git init -q
git config user.email 'reconstruction@local.invalid'
git config user.name 'Reconstruction'
git add .
git commit -qm 'RE02 supplied source baseline'
mkdir -p build/re03-linux/_deps/{powervr-decompress,stb-vorbis}
cp build/windows-msvc/_deps/powervr-decompress/* build/re03-linux/_deps/powervr-decompress/
cp build/windows-msvc/_deps/stb-vorbis/stb_vorbis.c build/re03-linux/_deps/stb-vorbis/
cmake -S . -B build/re03-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DFETCHCONTENT_SOURCE_DIR_ZLIB="$PWD/build/windows-msvc/_deps/zlib-src" -DFETCHCONTENT_SOURCE_DIR_PUGIXML="$PWD/build/windows-msvc/_deps/pugixml-src" -DFETCHCONTENT_FULLY_DISCONNECTED=ON > RE03_verification/baseline-configure.log 2>&1
cmake --build build/re03-linux --parallel 4 > RE03_verification/baseline-build.log 2>&1
ctest --test-dir build/re03-linux --output-on-failure -V > RE03_verification/baseline-tests.log 2>&1
