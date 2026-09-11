#!/usr/bin/env bash
#
# Build a pinned SDL3 from source into $PREFIX.
#
# SDL3 >= 3.4 is required. It is not guaranteed to be present (or recent enough)
# in the Steam Runtime SDK or on a plain build host, so we always build it
# ourselves for the packaged/Steam builds.
#
# Works on Linux and inside the MSYS2/MinGW shell used by the Windows CI job.
#
# Usage: PREFIX=/path/to/prefix [JOBS=n] [SDL3_VERSION=3.4.0] \
#            scripts/ci/build-sdl3.sh
set -euo pipefail

PREFIX="${PREFIX:-}"
if [ -z "$PREFIX" ]; then
	echo "error: PREFIX must be set" >&2
	exit 2
fi

SDL3_VERSION="${SDL3_VERSION:-3.4.0}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
SRC="${SRC:-$PREFIX/src}"
CMAKE="${CMAKE:-cmake}"

if [ -f "$PREFIX/.stamp-sdl3-$SDL3_VERSION" ]; then
	echo "SDL3 $SDL3_VERSION is already installed in $PREFIX"
	exit 0
fi

mkdir -p "$SRC"
cd "$SRC"

tarball="SDL3-$SDL3_VERSION.tar.gz"
url="https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VERSION/$tarball"
if [ ! -f "$tarball" ]; then
	curl -fL --retry 3 --retry-delay 2 -o "$tarball" "$url"
fi

rm -rf "SDL3-$SDL3_VERSION" build-sdl3
tar -xzf "$tarball"

# Static linking is deliberately not used: the shared SDL3 is copied into the
# package by the CMake install step together with its own dependencies.
"$CMAKE" -S "SDL3-$SDL3_VERSION" -B build-sdl3 -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$PREFIX" \
	-DCMAKE_INSTALL_LIBDIR=lib \
	-DSDL_SHARED=ON \
	-DSDL_STATIC=OFF \
	-DSDL_TESTS=OFF \
	-DSDL_EXAMPLES=OFF \
	-DSDL_INSTALL_TESTS=OFF

"$CMAKE" --build build-sdl3 -j "$JOBS"
"$CMAKE" --install build-sdl3

touch "$PREFIX/.stamp-sdl3-$SDL3_VERSION"
echo "SDL3 $SDL3_VERSION installed into $PREFIX"
