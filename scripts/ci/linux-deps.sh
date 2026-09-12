#!/usr/bin/env bash
#
# Build the Linux runtime dependencies for the packaged/Steam build.
#
# This script is intended to run inside Valve's official Steam Runtime SDK
# container (see STEAM_BASE_IMAGE in .github/workflows/steam-beta.yml), which
# gives the build exactly the glibc and library stack of the Steam Linux Runtime
# the game targets. It also works on a plain Debian/Ubuntu image.
# Building against a newer glibc than the target runtime would make the binaries
# refuse to start there.
#
# Everything is installed into $PREFIX, which the CI workflows cache, so only
# the first run pays for compiling the dependencies. Re-running is a no-op.
#
# Usage: [PREFIX=/path] scripts/ci/linux-deps.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PREFIX="${PREFIX:-$REPO_ROOT/.ci/linux-prefix}"
JOBS="${JOBS:-$(nproc)}"

SDL3_VERSION="${SDL3_VERSION:-3.4.0}"
ZLIB_NG_VERSION="${ZLIB_NG_VERSION:-2.3.3}"
SPNG_VERSION="${SPNG_VERSION:-0.7.4}"
CMAKE_VERSION="${CMAKE_VERSION:-3.31.6}"

SRC="$PREFIX/src"

log() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }

export DEBIAN_FRONTEND=noninteractive
mkdir -p "$PREFIX"

REQUIRED_PACKAGES="
build-essential ninja-build git curl ca-certificates pkg-config file patchelf python3
libgl1-mesa-dev libglu1-mesa-dev
libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev
libxinerama-dev libxss-dev libxkbcommon-dev libwayland-dev
libdrm-dev libgbm-dev libasound2-dev libpulse-dev libudev-dev libdbus-1-dev
libfreetype6-dev libfreetype-dev libssl-dev libcurl4-openssl-dev
libopus-dev libopusfile-dev libogg-dev
zlib1g-dev libzstd-dev
"

# GLVND splits its development files into several packages, and which of them
# exist differs between distributions: Debian 13 / the steamrt4 SDK need
# libopengl-dev to get libOpenGL.so, while older Ubuntu only ships libglvnd-dev
# (which pulls all of them). The client links libOpenGL.so - CMakeLists.txt uses
# OPENGL_opengl_LIBRARY - so one of these has to install.
OPTIONAL_PACKAGES="
libglvnd-dev libopengl-dev libglx-dev libgl-dev libegl-dev
libdecor-0-dev libxkbcommon-x11-dev
"

log "Installing distribution packages"
apt-get update -qq || echo "warning: apt-get update failed, using existing lists"

# Install only the names this image actually knows. The Steam Runtime SDK
# (Debian 13) and older Ubuntu disagree on a few package names, and a single
# unknown name would otherwise abort the whole batch install.
available=""
for pkg in $REQUIRED_PACKAGES $OPTIONAL_PACKAGES; do
	if apt-cache show "$pkg" > /dev/null 2>&1; then
		available="$available $pkg"
	else
		echo "warning: package '$pkg' is not available in this image, skipping"
	fi
done

# shellcheck disable=SC2086
if ! apt-get install -y -qq --no-install-recommends $available; then
	echo "warning: batch install failed, retrying packages individually"
	for pkg in $available; do
		apt-get install -y -qq --no-install-recommends "$pkg" \
			|| echo "warning: could not install $pkg"
	done
fi

# Fail early and clearly if the GLVND development files could not be installed;
# otherwise the configure step fails later with "OPENGL_opengl_LIBRARY NOTFOUND".
opengl_found=0
for candidate in /usr/lib/*/libOpenGL.so /usr/lib/libOpenGL.so; do
	if [ -e "$candidate" ]; then
		opengl_found=1
	fi
done
if [ "$opengl_found" -eq 0 ]; then
	echo "warning: libOpenGL.so is missing (needs libopengl-dev or libglvnd-dev)" \
		"- the client will not configure unless the versioned object is present"
fi

# --- CMake -----------------------------------------------------------------
# Debian/Ubuntu base images (and the Steam Runtime SDK) ship an older CMake; the
# project requires >= 3.21. The official binary tarball is built against an old
# glibc and runs fine on all of them.
if [ ! -x "$PREFIX/bin/cmake" ]; then
	log "Installing CMake $CMAKE_VERSION"
	curl -fL --retry 3 --retry-delay 2 -o /tmp/cmake.tar.gz \
		"https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION-linux-x86_64.tar.gz"
	rm -rf /tmp/cmake-extract
	mkdir -p /tmp/cmake-extract
	tar -xzf /tmp/cmake.tar.gz -C /tmp/cmake-extract --strip-components=1
	cp -a /tmp/cmake-extract/. "$PREFIX/"
	rm -rf /tmp/cmake-extract /tmp/cmake.tar.gz
fi
export PATH="$PREFIX/bin:$PATH"
cmake --version | head -1

mkdir -p "$SRC"

# --- zlib-ng ---------------------------------------------------------------
if [ ! -f "$PREFIX/.stamp-zlib-ng-$ZLIB_NG_VERSION" ]; then
	log "Building zlib-ng $ZLIB_NG_VERSION"
	cd "$SRC"
	curl -fL --retry 3 --retry-delay 2 -o zlib-ng.tar.gz \
		"https://github.com/zlib-ng/zlib-ng/archive/refs/tags/$ZLIB_NG_VERSION.tar.gz"
	rm -rf "zlib-ng-$ZLIB_NG_VERSION" build-zlib-ng
	tar -xzf zlib-ng.tar.gz
	cmake -S "zlib-ng-$ZLIB_NG_VERSION" -B build-zlib-ng -G Ninja \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX="$PREFIX" \
		-DBUILD_TESTING=OFF -DZLIB_ENABLE_TESTS=OFF -DWITH_GTEST=OFF \
		-DZLIB_COMPAT=OFF -DWITH_GZFILEOP=ON
	cmake --build build-zlib-ng -j "$JOBS"
	cmake --install build-zlib-ng
	touch "$PREFIX/.stamp-zlib-ng-$ZLIB_NG_VERSION"
fi

# --- libspng ---------------------------------------------------------------
if [ ! -f "$PREFIX/.stamp-spng-$SPNG_VERSION" ]; then
	log "Building libspng $SPNG_VERSION"
	cd "$SRC"
	curl -fL --retry 3 --retry-delay 2 -o libspng.tar.gz \
		"https://github.com/randy408/libspng/archive/refs/tags/v$SPNG_VERSION.tar.gz"
	rm -rf "libspng-$SPNG_VERSION" build-spng
	tar -xzf libspng.tar.gz
	cmake -S "libspng-$SPNG_VERSION" -B build-spng -G Ninja \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX="$PREFIX" \
		-DBUILD_SHARED_LIBS=ON \
		-DSPNG_USE_MINIZLIB=OFF \
		-DSPNG_TESTS=OFF -DSPNG_EXAMPLES=OFF
	cmake --build build-spng -j "$JOBS"
	cmake --install build-spng
	touch "$PREFIX/.stamp-spng-$SPNG_VERSION"
fi

# --- SDL3 ------------------------------------------------------------------
log "Building SDL3 $SDL3_VERSION"
PREFIX="$PREFIX" SDL3_VERSION="$SDL3_VERSION" JOBS="$JOBS" \
	bash "$REPO_ROOT/scripts/ci/build-sdl3.sh"

log "Dependencies are ready in $PREFIX"
