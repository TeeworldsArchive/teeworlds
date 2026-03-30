set(PNGLITE_SRC_DIR src/engine/external/pnglite)
set_src(PNGLITE_SRC GLOB ${PNGLITE_SRC_DIR} pnglite.c pnglite.h)
add_library(pnglite EXCLUDE_FROM_ALL OBJECT ${PNGLITE_SRC})
list(APPEND TARGETS_DEP pnglite)

set(PNGLITE_INCLUDEDIR ${PNGLITE_SRC_DIR})
target_include_directories(pnglite PRIVATE ${ZLIB_INCLUDE_DIRS})

set(PNGLITE_DEP $<TARGET_OBJECTS:pnglite>)
set(PNGLITE_INCLUDE_DIRS ${PNGLITE_INCLUDEDIR})
set(PNGLITE_LIBRARIES)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Pnglite DEFAULT_MSG PNGLITE_INCLUDEDIR)
set(PNGLITE_BUNDLED ON)
