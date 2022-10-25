##################################################################
###                          SOURCES                           ###
##################################################################

# Take all files in the source directory
file(GLOB_RECURSE wvb_server_lib_sources
        "src/*.cpp"
        )
# Take all files in the include directory
file(GLOB_RECURSE wvb_server_lib_headers
        "include/*.h"
        "include/*.hpp"
        )
set(server_lib_header_dir
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        )

##################################################################
###                         CREATE LIB                         ###
##################################################################

add_library(wvb_server_lib ${server_lib_sources} ${server_lib_headers})
target_sources(wvb_server_lib PRIVATE ${wvb_server_lib_sources})
target_include_directories(wvb_server_lib PUBLIC ${server_lib_header_dir})