#!/usr/bin/env bash
#
# Configure, build and stage the portable (Steam) Linux package.
#
# Intended to run inside the same Steam Runtime SDK container as
# scripts/ci/linux-deps.sh, after the dependencies have been built.
#
# Usage: [PREFIX=/path] [STAGE=/path] [BUILD_DIR=/path] scripts/ci/linux-build.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PREFIX="${PREFIX:-$REPO_ROOT/.ci/linux-prefix}"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/.ci/build/linux}"
STAGE="${STAGE:-$REPO_ROOT/.ci/staging/linux}"

export PATH="$PREFIX/bin:$PATH"
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig:${PKG_CONFIG_PATH:-}"
# Belt and braces for the install step, which resolves the dependencies of the
# already-installed binaries (the CMake install script also gets the prefix via
# CMAKE_PREFIX_PATH -> RUNTIME_DEPS_SEARCH_DIRS).
export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}"

# first_existing prints the first argument that exists on disk (globs may be
# passed literally when they do not match).
first_existing() {
	for candidate in "$@"; do
		if [ -e "$candidate" ]; then
			printf '%s\n' "$candidate"
			return 0
		fi
	done
	return 1
}

# The Steam Runtime SDK ships the GLVND runtime libraries but not always the
# "libOpenGL.so" development symlink. CMakeLists.txt links OPENGL_opengl_LIBRARY,
# so when only the versioned object exists, point CMake straight at it (linking
# records the SONAME libOpenGL.so.0, which the runtime does provide).
extra_cmake_args=()
if ! first_existing /usr/lib/*/libOpenGL.so /usr/lib/libOpenGL.so > /dev/null; then
	opengl_so="$(first_existing /usr/lib/*/libOpenGL.so.0 /usr/lib/libOpenGL.so.0 || true)"
	if [ -n "$opengl_so" ]; then
		echo "note: no libOpenGL.so symlink, using $opengl_so for OPENGL_opengl_LIBRARY"
		extra_cmake_args+=("-DOPENGL_opengl_LIBRARY=$opengl_so")
	fi
fi

cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G Ninja \
	-DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PREFIX_PATH="$PREFIX" \
	-DDEV=OFF \
	-DBUNDLE_RUNTIME_DEPS=ON \
	"${extra_cmake_args[@]}"

cmake --build "$BUILD_DIR" --target ArchiveClient ArchiveServer -j "$(nproc)"

# `--component portable` produces the self-contained layout that is shipped on
# Steam: binaries + data/ + bundled runtime libraries at the package root.
rm -rf "$STAGE"
cmake --install "$BUILD_DIR" --component portable --prefix "$STAGE"

echo "Staged Linux package in $STAGE"
find "$STAGE" -maxdepth 1 -mindepth 1 -printf '  %f\n' | sort
