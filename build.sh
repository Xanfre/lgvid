#!/bin/sh
# Build with the latest FFmpeg release from the 0.7 series.

cd $(dirname $0)
FFMPEG="ffmpeg-0.7.17"

mkdir -p build
cd build
if [ ! -f "$FFMPEG.tar.gz" ]; then
	curl -LRO "https://ffmpeg.org/releases/$FFMPEG.tar.gz"
fi
tar -xzf "$FFMPEG.tar.gz" --strip-components=1
chmod +x configure
chmod +x version.sh
cp ../lgvid* .
cp ../ffmpeg-dll-config .
cp ../ffmpeg-lgvid.def .
cp ../Makefile_lgvid .
cp ../Makefile_ffdll .

patch -Nsp1 < "../patches/lgvid-$FFMPEG.patch"

source ../set-env-vars.sh

./ffmpeg-dll-config
make -j4

make -f Makefile_lgvid -j4
i686-w64-mingw32-strip lgvid.dll

make -f Makefile_ffdll -j4
i686-w64-mingw32-strip ffmpeg.dll

