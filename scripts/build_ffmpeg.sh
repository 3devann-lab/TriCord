#!/usr/bin/env bash
# Builds the trimmed-down FFmpeg that TriCord's video player links against.
#
# Usage: scripts/build_ffmpeg.sh
#   Needs devkitARM (DEVKITPRO / DEVKITARM set, as in the devkitpro/devkitarm image).
#   Installs static libraries and headers into library/ffmpeg/, which the Makefile
#   picks up automatically. Without it TriCord still builds, minus video playback.
set -euo pipefail

FFMPEG_VERSION="${FFMPEG_VERSION:-7.1.1}"
FFMPEG_SHA256="${FFMPEG_SHA256:-733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${ROOT}/library/ffmpeg"
WORK="${FFMPEG_WORK:-${ROOT}/build/ffmpeg}"
TARBALL="${WORK}/ffmpeg-${FFMPEG_VERSION}.tar.xz"
SRC="${WORK}/ffmpeg-${FFMPEG_VERSION}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

: "${DEVKITPRO:?Set DEVKITPRO to your devkitPro install}"
: "${DEVKITARM:=${DEVKITPRO}/devkitARM}"
export PATH="${DEVKITARM}/bin:${DEVKITPRO}/tools/bin:${PATH}"

mkdir -p "${WORK}"

if [ ! -f "${TARBALL}" ]; then
	for url in \
		"https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz" \
		"https://archive.ubuntu.com/ubuntu/pool/universe/f/ffmpeg/ffmpeg_${FFMPEG_VERSION}.orig.tar.xz"; do
		echo "Downloading ${url}"
		if curl -fL --retry 3 -o "${TARBALL}.part" "${url}"; then
			mv "${TARBALL}.part" "${TARBALL}"
			break
		fi
	done
fi
[ -f "${TARBALL}" ] || { echo "Could not download FFmpeg ${FFMPEG_VERSION}" >&2; exit 1; }
echo "${FFMPEG_SHA256}  ${TARBALL}" | sha256sum -c -

rm -rf "${SRC}"
tar -xf "${TARBALL}" -C "${WORK}"
cd "${SRC}"

# FFmpeg 7.1 forgets that h2645_sei.o calls into aom_film_grain.o when H.264 is built without HEVC.
sed -i 's/^OBJS-$(CONFIG_H264_SEI)  *+= h264_sei.o h2645_sei.o$/& aom_film_grain.o/' libavcodec/Makefile
grep -q '^OBJS-$(CONFIG_H264_SEI).*aom_film_grain.o' libavcodec/Makefile

ARCH_FLAGS="-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft"

# FFmpeg's VFP code runs the ARM11's VFP in vector mode, which the 3DS kernel's
# VFP context handling is not built for; the compiler's scalar VFP code is safe.
#
# Only what Discord videos and audio need: MP4/MOV and WebM containers, H.264/VP8
# video and the audio codecs those carry, plus Ogg (voice messages), MP3, WAV, FLAC
# and raw AAC files. HEVC and VP9 are far beyond the 3DS CPU.
./configure \
	--prefix="${PREFIX}" \
	--enable-cross-compile \
	--cross-prefix=arm-none-eabi- \
	--arch=arm \
	--cpu=armv6k \
	--target-os=none \
	--extra-cflags="${ARCH_FLAGS} -D__3DS__ -O2 -ffunction-sections -fdata-sections -Wno-incompatible-pointer-types -Wno-int-conversion -Wno-array-parameter -I${DEVKITPRO}/libctru/include" \
	--extra-ldflags="-specs=3dsx.specs ${ARCH_FLAGS} -L${DEVKITPRO}/libctru/lib" \
	--extra-libs="-lctru -lm" \
	--pkg-config=false \
	--disable-autodetect \
	--disable-runtime-cpudetect \
	--disable-programs \
	--disable-doc \
	--disable-debug \
	--disable-avdevice \
	--disable-avfilter \
	--disable-network \
	--disable-pthreads \
	--disable-w32threads \
	--disable-os2threads \
	--disable-neon \
	--disable-vfp \
	--enable-static \
	--disable-shared \
	--disable-everything \
	--enable-swscale \
	--enable-swresample \
	--enable-protocol=file \
	--enable-demuxer=mov,matroska,ogg,mp3,wav,flac,aac \
	--enable-decoder=h264,vp8,aac,mp3,opus,vorbis,flac,pcm_s16le,pcm_u8,pcm_f32le \
	--enable-parser=h264,aac,vp8,opus,vorbis,mpegaudio,flac \
	--enable-bsf=h264_mp4toannexb

make -j"${JOBS}"
rm -rf "${PREFIX}/include" "${PREFIX}/lib"
make install

echo "FFmpeg ${FFMPEG_VERSION} installed to ${PREFIX}"
