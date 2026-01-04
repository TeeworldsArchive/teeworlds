if(NOT PREFER_BUNDLED_LIBS)
  set(CMAKE_MODULE_PATH ${ORIGINAL_CMAKE_MODULE_PATH})
  find_package(SDL2)
  set(CMAKE_MODULE_PATH ${OWN_CMAKE_MODULE_PATH})
  if(SDL2_FOUND)
    set(SDL2_BUNDLED OFF)
    set(SDL2_DEP)
  endif()
endif()

if(NOT CMAKE_CROSSCOMPILING)
  find_package(PkgConfig QUIET)
  pkg_check_modules(PC_SDL2 sdl2)
endif()

find_library(SDL2_LIBRARY
  NAMES SDL2
  HINTS ${HINTS_SDL2_LIBDIR} ${SDL2_LIBDIR} ${PC_SDL2_LIBDIR} ${PC_SDL2_LIBRARY_DIRS}
  PATHS ${PATHS_SDL2_LIBDIR}
  ${CROSSCOMPILING_NO_CMAKE_SYSTEM_PATH}
)
set(CMAKE_FIND_FRAMEWORK FIRST)
# Looking for 'SDL.h' directly might accidentally find a SDL 1 instead of SDL 2
# installation. Look for a header file only present in SDL 2 instead.
find_path(SDL2_INCLUDEDIR SDL_assert.h
  PATH_SUFFIXES SDL
  HINTS ${HINTS_SDL2_INCLUDEDIR} ${SDL2_INCLUDE_DIRS} ${PC_SDL2_INCLUDEDIR} ${PC_SDL2_INCLUDE_DIRS}
  PATHS ${PATHS_SDL2_INCLUDEDIR}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2 DEFAULT_MSG SDL2_LIBRARY SDL2_INCLUDEDIR)

mark_as_advanced(SDL2_LIBRARY SDL2_INCLUDEDIR)

if(SDL2_FOUND)
  set(SDL2_LIBRARIES ${SDL2_LIBRARY})
  set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDEDIR})
endif()
