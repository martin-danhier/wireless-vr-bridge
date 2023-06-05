##################################################################
###                          SOURCES                           ###
##################################################################

# Take all files in the source directory
file(GLOB_RECURSE wvb_common_sources
        "src/*.cpp"
        )
# Take all files in the include directory
file(GLOB_RECURSE wvb_common_headers
        "include/*.h"
        "include/*.hpp"
        )
set(wvb_common_header_dir
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        )

##################################################################
###                         CREATE LIB                         ###
##################################################################

add_library(wvb_common STATIC ${wvb_common_sources} ${wvb_common_headers})
target_sources(wvb_common PRIVATE ${wvb_common_sources})
target_include_directories(wvb_common PUBLIC ${wvb_common_header_dir})

set_target_properties(wvb_common PROPERTIES LINKER_LANGUAGE CXX)

##################################################################
###                         LINK LIBS                          ###
##################################################################

target_link_libraries(wvb_common
        ffmpeg_lib
        stb_image_write
        )

if (WIN32)
    # Link winsock2
    target_link_libraries(wvb_common
            ws2_32
            d3d11
            d3dcompiler
            dxguid
            iphlpapi)
endif ()
if (ANDROID)
    # Link android
    target_link_libraries(wvb_common
            android
            mediandk
            log)
endif ()

# On Linux, link to pthread
if (UNIX AND NOT APPLE AND NOT ANDROID)
    target_link_libraries(wvb_common pthread rt)
endif ()