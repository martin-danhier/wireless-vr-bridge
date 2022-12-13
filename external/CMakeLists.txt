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
        "/usr/lib"
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

# Setup and link OpenVR
add_library(openvr INTERFACE)
target_include_directories(openvr INTERFACE ${openvr_include_dir})
target_link_directories(openvr INTERFACE ${openvr_lib_dir})
target_link_libraries(openvr INTERFACE openvr_api)

##################################################################
###                      TESTING FRAMEWORK                     ###
##################################################################

add_subdirectory(testing_framework)

##################################################################
###                        LIBDATACHANNEL                      ###
##################################################################

# Set options for subdirectory
set(NO_EXAMPLES ON CACHE BOOL "Disable examples" FORCE)
set(NO_TESTS ON CACHE BOOL "Disable tests builds" FORCE)

add_subdirectory(libdatachannel)

# Fix MSVC warning C5105 (macro expansion producing 'defined' has undefined behavior) in WinBase.h
if (MSVC)
    target_compile_definitions(usrsctp PRIVATE MICROSOFT_WINDOWS_WINBASE_H_DEFINE_INTERLOCKED_CPLUSPLUS_OVERLOADS=0)
    target_compile_definitions(juice PRIVATE MICROSOFT_WINDOWS_WINBASE_H_DEFINE_INTERLOCKED_CPLUSPLUS_OVERLOADS=0)
endif ()

##################################################################
###                           UVGRTP                           ###
##################################################################

# add_library(uvgRTP)
# target_sources(uvgRTP PRIVATE
#         uvgRTP/src/clock.cc
#         uvgRTP/src/crypto.cc
#         uvgRTP/src/frame.cc
#         uvgRTP/src/hostname.cc
#         uvgRTP/src/context.cc
#         uvgRTP/src/media_stream.cc
#         uvgRTP/src/mingw_inet.cc
#         uvgRTP/src/reception_flow.cc
#         uvgRTP/src/poll.cc
#         uvgRTP/src/frame_queue.cc
#         uvgRTP/src/random.cc
#         uvgRTP/src/rtcp.cc
#         uvgRTP/src/rtcp_packets.cc
#         uvgRTP/src/rtp.cc
#         uvgRTP/src/session.cc
#         uvgRTP/src/socket.cc
#         uvgRTP/src/zrtp.cc
#         uvgRTP/src/holepuncher.cc

#         uvgRTP/src/formats/media.cc
#         uvgRTP/src/formats/h26x.cc
#         uvgRTP/src/formats/h264.cc
#         uvgRTP/src/formats/h265.cc
#         uvgRTP/src/formats/h266.cc

#         uvgRTP/src/zrtp/zrtp_receiver.cc
#         uvgRTP/src/zrtp/hello.cc
#         uvgRTP/src/zrtp/hello_ack.cc
#         uvgRTP/src/zrtp/commit.cc
#         uvgRTP/src/zrtp/dh_kxchng.cc
#         uvgRTP/src/zrtp/confirm.cc
#         uvgRTP/src/zrtp/confack.cc
#         uvgRTP/src/zrtp/error.cc
#         uvgRTP/src/zrtp/zrtp_message.cc
#         uvgRTP/src/srtp/base.cc
#         uvgRTP/src/srtp/srtp.cc
#         uvgRTP/src/srtp/srtcp.cc
#         uvgRTP/src/wrapper_c.cc
# )
# target_include_directories(uvgRTP
#         PUBLIC uvgRTP/include
#         PRIVATE uvgRTP/src
#         )

##################################################################
###                             RE                             ###
##################################################################

# Dependency of RawRTC DC



##################################################################
###                          RAWRTCDC                          ###
##################################################################

# It needs to be built with Meson

# Find meson cli
# find_program(meson_cli meson)

# # Error if not found
# if (meson_cli STREQUAL "meson_cli-NOTFOUND")
#     message(FATAL_ERROR "Meson CLI not found. Please install it.")
# endif ()

# # Create build directory
# file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/rawrtcdc)

# # Run meson to generate build files
# execute_process(
#         COMMAND ${meson_cli} setup ${CMAKE_CURRENT_BINARY_DIR}/rawrtcdc
#         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/rawrtc-data-channel
# )

# # Link library
# add_library(rawrtcdc INTERFACE)
# target_include_directories(rawrtcdc INTERFACE
#         ${CMAKE_CURRENT_SOURCE_DIR}/rawrtc-data-channel/include
#         ${CMAKE_CURRENT_BINARY_DIR}/rawrtcdc/include
#         ${CMAKE_CURRENT_SOURCE_DIR}/rawrtc-data-channel/subprojects/re/include
#         ${CMAKE_CURRENT_SOURCE_DIR}/rawrtc-data-channel/subprojects/usrsctp
#         ${CMAKE_CURRENT_SOURCE_DIR}/rawrtc-data-channel/subprojects/rawrtcc/include
#         ${CMAKE_CURRENT_BINARY_DIR}/rawrtcdc/subprojects/rawrtcc/include
#         )
# target_link_directories(rawrtcdc INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/rawrtcdc)
# target_link_libraries(rawrtcdc INTERFACE rawrtcdc)


