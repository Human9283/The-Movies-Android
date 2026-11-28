#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <install-prefix>" >&2
    exit 2
fi
: "\${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to the pinned Android NDK}"
OUT="$1"
API="\${ANDROID_API:-24}"
VERSION="9.1"
FINGERPRINT="FCF986EA15E6E293A5644F10B4322F04D67658D8"
TOOLCHAIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64"
SYSROOT="$TOOLCHAIN/sysroot"
CC="$TOOLCHAIN/bin/aarch64-linux-android\${API}-clang"
CXX="$TOOLCHAIN/bin/aarch64-linux-android\${API}-clang++"
AR="$TOOLCHAIN/bin/llvm-ar"
RANLIB="$TOOLCHAIN/bin/llvm-ranlib"
NM="$TOOLCHAIN/bin/llvm-nm"
STRIP="$TOOLCHAIN/bin/llvm-strip"
for tool in curl gpg tar make; do command -v "$tool" >/dev/null 2>&1 || { echo "Missing host tool: $tool" >&2; exit 1; }; done
for tool in "$CC" "$CXX" "$AR" "$RANLIB" "$NM" "$STRIP"; do test -x "$tool" || { echo "Missing NDK tool: $tool" >&2; exit 1; }; done
case "$OUT" in ""|"/"|"/tmp"|"/home"|"/root") echo "Unsafe install prefix: $OUT" >&2; exit 2;; esac
WORK="$(mktemp -d "\${TMPDIR:-/tmp}/tm-ffmpeg-\${VERSION}.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
TARBALL="$WORK/ffmpeg-\${VERSION}.tar.xz"
curl --fail --location --retry 3 "https://ffmpeg.org/releases/ffmpeg-\${VERSION}.tar.xz" --output "$TARBALL"
curl --fail --location --retry 3 "https://ffmpeg.org/releases/ffmpeg-\${VERSION}.tar.xz.asc" --output "$TARBALL.asc"
curl --fail --location --retry 3 https://ffmpeg.org/ffmpeg-devel.asc --output "$WORK/ffmpeg-devel.asc"
export GNUPGHOME="$WORK/gnupg"
mkdir -m 700 "$GNUPGHOME"
gpg --batch --import "$WORK/ffmpeg-devel.asc"
ACTUAL_FINGERPRINT="$(gpg --batch --with-colons --fingerprint "$FINGERPRINT" | awk -F: '$1 == "fpr" { print $10; exit }')"
test "$ACTUAL_FINGERPRINT" = "$FINGERPRINT"
gpg --batch --verify "$TARBALL.asc" "$TARBALL"
tar -xJf "$TARBALL" -C "$WORK"
cd "$WORK/ffmpeg-\${VERSION}"
mkdir -p "$OUT"
./configure \
  --prefix="$OUT" \
  --target-os=android --arch=aarch64 --cpu=armv8-a \
  --sysroot="$SYSROOT" --cc="$CC" --cxx="$CXX" \
  --ar="$AR" --ranlib="$RANLIB" --nm="$NM" --strip="$STRIP" \
  --enable-cross-compile --enable-pic --enable-shared --disable-static \
  --disable-programs --disable-doc --disable-debug --disable-network \
  --disable-autodetect --disable-everything \
  --enable-avutil --enable-avcodec --enable-avformat --enable-swscale \
  --enable-demuxer=asf --enable-decoder=wmv1,wmv2,wmv3,vc1 \
  --enable-parser=vc1 --enable-protocol=file \
  --extra-cflags="-O2 -fPIC" \
  --extra-ldflags="-Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384"
make -j2
make install
for library in avutil avcodec avformat swscale; do test -s "$OUT/lib/lib\${library}.so"; done
test -f "$OUT/include/libavcodec/avcodec.h"
test -f "$OUT/include/libavformat/avformat.h"
test -f "$OUT/include/libswscale/swscale.h"
cp COPYING.LGPLv2.1 "$OUT/FFmpeg-COPYING.LGPLv2.1"
echo "Built FFmpeg \${VERSION} for aarch64-linux-android\${API}: ASF, WMV1/2/3, VC-1, file protocol only."
echo "Install prefix: $OUT"
