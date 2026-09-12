# Exclusion lists used by cmake/InstallRuntimeDeps.cmake.in.
#
# `file(GET_RUNTIME_DEPENDENCIES)` resolves the full transitive closure of the
# shared libraries our executables link against. For a portable (and especially
# for a Steam) package we copy the libraries that the Steam Linux Runtime or a
# plain desktop does not reliably provide, while never shipping the ones that
# MUST come from the runtime/host:
#
#   * the dynamic loader and glibc          (wrong version = the game won't start)
#   * the C++/GCC runtime                    (provided by every Steam runtime)
#   * OpenGL/Vulkan/DRM loaders              (must match the host GPU driver)
#   * X11/Wayland/input/audio/session libs   (must match the host session)
#
# Two filters are handed to GET_RUNTIME_DEPENDENCIES:
#   PRE_EXCLUDE_REGEXES   match the dependency *name* (soname) before resolution
#   POST_EXCLUDE_REGEXES  match the *resolved absolute path*
#
# Anything that is *not* excluded here (SDL3, libspng, zlib-ng, opus, the
# OpenSSL/curl stack, ...) is bundled. Adjust these lists if a new dependency
# must, or must not, be shipped.

if(TARGET_OS STREQUAL "windows")
  # Only Windows system DLLs are excluded. Everything else (the MinGW runtime
  # and the third-party DLLs from MSYS2/the dependency prefix) is bundled next
  # to the executables.
  set(RUNTIME_DEPS_PRE_EXCLUDE_REGEXES
    "api-ms-.*"
    "ext-ms-.*"
  )
  set(RUNTIME_DEPS_POST_EXCLUDE_REGEXES
    # GET_RUNTIME_DEPENDENCIES normalizes paths, so match with forward slashes.
    ".*[Ss]ystem32.*"
    ".*[Ww]in[Ss][Xx][Ss].*"
  )
elseif(TARGET_OS STREQUAL "linux")
  set(RUNTIME_DEPS_PRE_EXCLUDE_REGEXES
    # Dynamic loader + glibc
    "ld-linux.*"
    "libc\\.so.*"
    "libm\\.so.*"
    "libmvec\\.so.*"
    "libdl\\.so.*"
    "libpthread\\.so.*"
    "librt\\.so.*"
    "libresolv\\.so.*"
    "libnsl\\.so.*"
    "libutil\\.so.*"
    "libanl\\.so.*"
    "libcrypt\\.so.*"
    # C++ / GCC runtime
    "libstdc\\+\\+\\.so.*"
    "libgcc_s\\.so.*"
    "libatomic\\.so.*"
    "libgomp\\.so.*"
    # OpenGL / Vulkan / DRM (host driver)
    "libGL\\.so.*"
    "libOpenGL\\.so.*"
    "libGLX.*"
    "libGLdispatch.*"
    "libGLU\\.so.*"
    "libEGL\\.so.*"
    "libGLESv2\\.so.*"
    "libdrm\\.so.*"
    "libgbm\\.so.*"
    "libvulkan\\.so.*"
    # X11 / Wayland / input
    "libX11.*"
    "libxcb.*"
    "libXext.*"
    "libXcursor.*"
    "libXrandr.*"
    "libXi\\.so.*"
    "libXfixes.*"
    "libXinerama.*"
    "libXss.*"
    "libXtst.*"
    "libXau.*"
    "libXdmcp.*"
    "libwayland-.*"
    "libxkbcommon.*"
    "libdecor.*"
    # Audio / session / device access
    "libasound\\.so.*"
    "libpulse.*"
    "libudev\\.so.*"
    "libdbus-1\\.so.*"
    "libsystemd.*"
    "libpipewire.*"
    "libjack.*"
    # Vendor drivers (host-provided). Note that libsteam_api is deliberately NOT
    # excluded: the Steam client does not inject it, so a client built with
    # STEAM=ON has to ship it (see STEAM_RUNTIME_DIR in CMakeLists.txt).
    "libnvidia.*"
    "libcuda.*"
  )
  set(RUNTIME_DEPS_POST_EXCLUDE_REGEXES
    # Never pick up libraries from the host's private/vendor directories.
    ".*/dri/.*"
    ".*/nvidia/.*"
  )
else()
  set(RUNTIME_DEPS_PRE_EXCLUDE_REGEXES "")
  set(RUNTIME_DEPS_POST_EXCLUDE_REGEXES "")
endif()
