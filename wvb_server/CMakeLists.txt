##################################################################
###                          SOURCES                           ###
##################################################################

# Take all files in the source directory
file(GLOB_RECURSE wvb_server_sources
        "src/*.cpp"
        )
# Take all files in the include directory
file(GLOB_RECURSE wvb_server_headers
        "include/*.h"
        "include/*.hpp"
        )
set(wvb_server_header_dir
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        )

##################################################################
###                      CREATE EXECUTABLE                     ###
##################################################################

add_executable(wvb_server ${wvb_server_sources} ${wvb_server_headers})
target_sources(wvb_server PRIVATE ${wvb_server_sources})
target_include_directories(wvb_server PUBLIC ${wvb_server_header_dir})

##################################################################
###                         LINK LIBS                          ###
##################################################################

target_link_libraries(wvb_server
        wvb_common
        )