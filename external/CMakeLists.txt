##################################################################
###                           OPEN VR                          ###
##################################################################

# Ignore on Android
if (NOT ANDROID)


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
        endif ()
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
            "/usr/lib"
            PATH_SUFFIXES ".lib" ".so" ".dll" ".dylib"
            )

    # Not found
    if ((openvr_include_dir STREQUAL "openvr_include_dir-NOTFOUND") OR (openvr_lib_dir STREQUAL "openvr_lib_dir-NOTFOUND"))
        # Clear them from cache
        unset(openvr_include_dir CACHE)
        unset(openvr_lib_dir CACHE)
        message(FATAL_ERROR "OpenVR SDK not found ! If the OpenVR SDK is not located at a conventional place, the root directory needs to be stored in the env variable OPENVR_SDK_PATH.")
    endif ()

    # Found
    if (NOT openvr_already_in_cache STREQUAL "true")
        message("-- Found OpenVR SDK")
        message("       Include dir: ${openvr_include_dir}")
        message("       Lib dir:     ${openvr_lib_dir}")
    endif ()

    # Setup and link OpenVR
    add_library(openvr INTERFACE)
    target_include_directories(openvr INTERFACE ${openvr_include_dir})
    target_link_directories(openvr INTERFACE ${openvr_lib_dir})
    target_link_libraries(openvr INTERFACE openvr_api)

endif ()

##################################################################
###                      TESTING FRAMEWORK                     ###
##################################################################

if (${enable_tests} STREQUAL "1")
    add_subdirectory(testing_framework)
endif ()

##################################################################
###                          STB_IMAGE                         ###
##################################################################

add_library(stb_image STATIC)
target_sources(stb_image PRIVATE
        stb_image/stb_image.h
        stb_image/stb_image.cpp
        )
target_include_directories(stb_image PUBLIC stb_image)

##################################################################
###                       STB_IMAGE_WRITE                      ###
##################################################################

add_library(stb_image_write STATIC)
target_sources(stb_image_write PRIVATE
        stb_image_write/stb_image_write.h
        stb_image_write/stb_image_write.cpp
        )
target_include_directories(stb_image_write PUBLIC stb_image_write)

##################################################################
###                            X264                            ###
##################################################################

#if (DEFINED CACHE{x264_include_dir} AND DEFINED CACHE{x264_lib_dir})
#    set(x264_already_in_cache "true")
#endif ()
#
## Find the header directory
#find_path(x264_include_dir
#        "x264.h"
#        PATHS "$ENV{X264_PATH}/include"
#        "/usr/include"
#        )
#
## Find the lib directory
#find_path(x264_lib_dir
#        NAMES "x264.lib" "libx264.lib" "libx264.so" "libx264.dylib"
#        PATHS "$ENV{X264_PATH}/lib/x64"
#        "/usr/lib/x86_64-linux-gnu/"
#        "/usr/lib64"
#        "/usr/lib"
#        PATH_SUFFIXES ".lib" ".so" ".dll" ".dylib"
#        )
#
## Not found
#if ((x264_include_dir STREQUAL "x264_include_dir-NOTFOUND") OR (x264_lib_dir STREQUAL "x264_lib_dir-NOTFOUND"))
#    # Clear them from cache
#    unset(x264_include_dir CACHE)
#    unset(x264_lib_dir CACHE)
#    message(FATAL_ERROR "x264 library not found ! If the x264 library is not located at a conventional place, the root directory needs to be stored in the env variable X264_PATH.")
#endif ()
#
## Found
#if (NOT x264_already_in_cache STREQUAL "true")
#    message("-- Found x264 library")
#    message("       Include dir: ${x264_include_dir}")
#    message("       Lib dir:     ${x264_lib_dir}")
#endif ()
#
## Setup and link X264
#add_library(x264_lib INTERFACE)
#target_include_directories(x264_lib INTERFACE ${x264_include_dir})
#target_link_directories(x264_lib INTERFACE ${x264_lib_dir})
#target_link_libraries(x264_lib INTERFACE x264)

##################################################################
###                          FFMPEG                            ###
##################################################################

if (DEFINED CACHE{ffmpeg_include_dir} AND DEFINED CACHE{ffmpeg_lib_dir} AND DEFINED CACHE{ffmpeg_bin_dir})
    set(ffmpeg_already_in_cache "true")
endif ()

# Find the header directory
find_path(ffmpeg_include_dir
        "libavcodec/avcodec.h"
        PATHS
        "${FFMPEG_PATH}/include"
        "$ENV{FFMPEG_PATH}/include"
        "/usr/include"
        "/usr/include/ffmpeg"
        NO_DEFAULT_PATH
        )

# Find the lib directory
find_path(ffmpeg_lib_dir
        NAMES "libavcodec.so" "avcodec.lib" "libavcodec.lib"
        PATHS
        "${FFMPEG_PATH}/lib"
        "$ENV{FFMPEG_PATH}/lib"
        "/usr/lib/x86_64-linux-gnu/"
        "/usr/lib64"
        "/usr/lib"
        PATH_SUFFIXES ".lib" ".so"
        )

# Find the bin directory
if (WIN32)
    find_path(ffmpeg_bin_dir
            NAMES "avcodec-59.dll" "avcodec-60.dll"
            PATHS "$ENV{FFMPEG_PATH}/bin"
            "/usr/bin"
            )
else ()
    set(ffmpeg_bin_dir "")
endif ()

# Not found
if ((ffmpeg_include_dir STREQUAL "ffmpeg_include_dir-NOTFOUND") OR (ffmpeg_lib_dir STREQUAL "ffmpeg_lib_dir-NOTFOUND") OR (ffmpeg_bin_dir STREQUAL "ffmpeg_bin_dir-NOTFOUND"))
    unset(ffmpeg_include_dir CACHE)
    unset(ffmpeg_lib_dir CACHE)
    unset(ffmpeg_bin_dir CACHE)
    message(FATAL_ERROR "FFMPEG library not found ! If the FFMPEG library is not located at a conventional place, the root directory needs to be stored in the env variable FFMPEG_PATH.")
endif ()

# Found
if (NOT ffmpeg_already_in_cache STREQUAL "true")
    message("-- Found FFMPEG library")
    message("       Include dir: \"${ffmpeg_include_dir}\"")
    message("       Lib dir:     \"${ffmpeg_lib_dir}\"")
    if (WIN32)
        message("       DLL dir:     ${ffmpeg_bin_dir}")
    endif ()
endif ()

# Setup and link ffmpeg
if (WIN32)
    set(lib_ext "lib")
    set(lib_pre "")
else ()
    set(lib_ext "so")
    set(lib_pre "lib")
endif ()


add_library(ffmpeg_avcodec_lib SHARED IMPORTED)
set_target_properties(ffmpeg_avcodec_lib PROPERTIES
        IMPORTED_LOCATION "${ffmpeg_lib_dir}/${lib_pre}avcodec.${lib_ext}"
        IMPORTED_IMPLIB "${ffmpeg_lib_dir}/${lib_pre}avcodec.${lib_ext}"
        )
add_library(ffmpeg_avformat_lib SHARED IMPORTED)
set_target_properties(ffmpeg_avformat_lib PROPERTIES
        IMPORTED_LOCATION "${ffmpeg_lib_dir}/${lib_pre}avformat.${lib_ext}"
        IMPORTED_IMPLIB "${ffmpeg_lib_dir}/${lib_pre}avformat.${lib_ext}"
        )
add_library(ffmpeg_avutil_lib SHARED IMPORTED)
set_target_properties(ffmpeg_avutil_lib PROPERTIES
        IMPORTED_LOCATION "${ffmpeg_lib_dir}/${lib_pre}avutil.${lib_ext}"
        IMPORTED_IMPLIB "${ffmpeg_lib_dir}/${lib_pre}avutil.${lib_ext}"
        )
add_library(ffmpeg_swscale_lib SHARED IMPORTED)
set_target_properties(ffmpeg_swscale_lib PROPERTIES
        IMPORTED_LOCATION "${ffmpeg_lib_dir}/${lib_pre}swscale.${lib_ext}"
        IMPORTED_IMPLIB "${ffmpeg_lib_dir}/${lib_pre}swscale.${lib_ext}"
        )
add_library(ffmpeg_swresample_lib SHARED IMPORTED)
set_target_properties(ffmpeg_swresample_lib PROPERTIES
        IMPORTED_LOCATION "${ffmpeg_lib_dir}/${lib_pre}swresample.${lib_ext}"
        IMPORTED_IMPLIB "${ffmpeg_lib_dir}/${lib_pre}swresample.${lib_ext}"
        )
add_library(ffmpeg_lib INTERFACE)
target_include_directories(ffmpeg_lib INTERFACE ${ffmpeg_include_dir})
target_link_libraries(ffmpeg_lib INTERFACE ffmpeg_avcodec_lib ffmpeg_avutil_lib ffmpeg_avformat_lib ffmpeg_swscale_lib ffmpeg_swresample_lib)


if (WIN32)
    # Set list of dlls, and make the list available to the parent scope
    file(GLOB ffmpeg_dlls "${ffmpeg_bin_dir}/*.dll")
    set(ffmpeg_dlls ${ffmpeg_dlls} PARENT_SCOPE)
endif ()


##################################################################
###                           OPEN XR                          ###
##################################################################

# On Android, use the OpenXR loader provided by the NDK

if (NOT ANDROID)
    add_library(openxr INTERFACE)

    # Debug mode
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        # Set options BUILD_TESTS to false
        set(BUILD_TESTS OFF CACHE INTERNAL "Build tests" FORCE)
        set(BUILD_API_LAYERS ON CACHE INTERNAL "Build API layers" FORCE)
        set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

        add_subdirectory(OpenXR-SDK-Source)
        # Set environment variable XR_API_LAYER_PATH  to (build)/src/api_layers
        set($ENV{XR_API_LAYER_PATH} ${CMAKE_CURRENT_BINARY_DIR}/OpenXR-SDK-Source/src/api_layers)

        target_link_libraries(openxr INTERFACE XrApiLayer_core_validation)
    else ()
        set(BUILD_TESTS OFF CACHE INTERNAL "Build tests" FORCE)
        set(BUILD_API_LAYERS OFF CACHE INTERNAL "Build API layers" FORCE)
        set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
        add_subdirectory(OpenXR-SDK)
    endif ()

    target_include_directories(openxr INTERFACE OpenXR-SDK/include)
    target_link_libraries(openxr INTERFACE openxr_loader)
endif ()

##################################################################
###                         WVB PLUGINS                        ###
##################################################################

# List WVB plugin directories next to the WVB source directory.
# A plugin directory is named "wvb_module_<name>" and contains a CMakeLists.txt file.

file(GLOB children ${CMAKE_CURRENT_SOURCE_DIR}/../../wvb_module_*)

add_library(plugins INTERFACE)

# Check if empty
if (children STREQUAL "")
    set(has_plugins "false" PARENT_SCOPE)
else()
    set(has_plugins "true" PARENT_SCOPE)
    message("Found WVB plugins: ${children}")

    foreach(child ${children})
        if(IS_DIRECTORY ${child})
            # Get the name
            get_filename_component(name ${child} NAME)
            add_subdirectory(${child} ${CMAKE_CURRENT_BINARY_DIR}/plugins/${name})

            # Copy dll
            set(plugin_dlls ${plugin_dlls} ${CMAKE_CURRENT_BINARY_DIR}/plugins/${name}/${name}.dll PARENT_SCOPE)

            target_link_libraries(plugins INTERFACE ${name})
            message("-- Registering plugin ${name}")
        endif()
    endforeach()
endif ()

