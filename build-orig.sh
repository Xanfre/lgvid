#!/bin/sh
# Build with the NewDark-provided FFmpeg snapshot.

cd $(dirname $0)

mkdir -p build
cd build
7z x -y ../ffmpeg-src.7z >/dev/null 2>&1
chmod +x configure
chmod +x version.sh
cp ../lgvid* .
cp ../ffmpeg-dll-config .
cp ../ffmpeg-lgvid.def .
cp ../Makefile_lgvid .
cp ../Makefile_ffdll .

patch -Nsp1 < ../patches/lgvid-ffmpeg-orig.patch

source ../set-env-vars.sh

./ffmpeg-dll-config
make -j4

make -f Makefile_lgvid -j4
i686-w64-mingw32-strip lgvid.dll

make -f Makefile_ffdll -j4
i686-w64-mingw32-strip ffmpeg.dll

