
set(CMAKE_MODULE_PATH ${ORIGINAL_CMAKE_MODULE_PATH})
# find_package(spng)
set(CMAKE_MODULE_PATH ${OWN_CMAKE_MODULE_PATH})
if(NOT spng_FOUND AND NOT CMAKE_CROSSCOMPILING)
    find_package(PkgConfig QUIET)
    pkg_check_modules(PC_SPNG libspng)

    add_library(spng::spng INTERFACE IMPORTED)
    set_target_properties(spng::spng PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${PC_SPNG_INCLUDE_DIRS}"
    )
    target_link_libraries(spng::spng INTERFACE ${PC_SPNG_LIBRARIES})
    set(spng_FOUND PC_SPNG_FOUND)
endif()