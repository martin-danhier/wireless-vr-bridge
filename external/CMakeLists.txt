##################################################################
###                           OPEN VR                          ###
##################################################################

if (DEFINED CACHE{openvr_include_dir} AND DEFINED CACHE{openvr_lib_dir})
    set(openvr_already_in_cache "true")
endif ()

# Find the header directory
find_path(openvr_include_dir
        "openvr_driver.h"
        PATHS "$ENV{OPENVR_SDK_PATH}/headers"
        "/usr/include"
        )

# Find the OpenVR platform name
# win64
if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(openvr_platform_name "win64")
endif ()
# win32
if (WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(openvr_platform_name "win32")
endif ()
# linux64
if (UNIX AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    # ARM
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|ARM)")
        set(openvr_platform_name "linuxarm64")
    else ()
        set(openvr_platform_name "linux64")
    endif()
endif ()
# linux32
if (UNIX AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(openvr_platform_name "linux32")
endif ()
# osx32
if (APPLE AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(openvr_platform_name "osx32")
endif ()
# android arm64
if (ANDROID AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(openvr_platform_name "androidarm64")
endif ()
# If the platform name is not set, the platform is not supported
if (NOT DEFINED openvr_platform_name)
    message(FATAL_ERROR "OpenVR is not supported on this platform")
endif ()

# Place it in the parent scope
set(openvr_platform_name ${openvr_platform_name} PARENT_SCOPE)

# Find the lib directory
find_path(openvr_lib_dir
        NAMES "openvr_api.lib" "libopenvr_api.so" "libopenvr_api.dylib"
        PATHS "$ENV{OPENVR_SDK_PATH}/lib/${openvr_platform_name}"
        "/usr/lib/x86_64-linux-gnu/"
        "/usr/lib64"
        PATH_SUFFIXES ".lib" ".so" ".dll" ".dylib"
        )

# Not found
if ((openvr_include_dir STREQUAL "openvr_include_dir-NOTFOUND") OR (openvr_lib_dir STREQUAL "openvr_lib_dir-NOTFOUND"))
    # Clear them from cache
    unset(openvr_include_dir CACHE)
    unset(openvr_lib_dir CACHE)
    message(FATAL_ERROR "If the OpenVR SDK is not located at a conventional place, the root directory needs to be stored in the env variable OPENVR_SDK_PATH.")
endif ()

# Found
if (NOT openvr_already_in_cache STREQUAL "true")
    message("-- Found OpenVR SDK")
    message("       Include dir: ${openvr_include_dir}")
    message("       Lib dir:     ${openvr_lib_dir}")
endif ()

# Setup and link SDL
add_library(openvr INTERFACE)
target_include_directories(openvr INTERFACE ${openvr_include_dir})
target_link_directories(openvr INTERFACE ${openvr_lib_dir})
target_link_libraries(openvr INTERFACE openvr_api)
