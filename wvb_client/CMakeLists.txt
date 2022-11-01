##################################################################
###                          SOURCES                           ###
##################################################################

# Take all files in the source directory
file(GLOB_RECURSE wvb_client_sources
        "src/*.cpp"
        )
# Take all files in the include directory
file(GLOB_RECURSE wvb_client_headers
        "include/*.h"
        "include/*.hpp"
        )
set(wvb_client_header_dir
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        )

##################################################################
###                      CREATE EXECUTABLE                     ###
##################################################################

add_executable(wvb_client ${wvb_client_sources} ${wvb_client_headers})
target_sources(wvb_client PRIVATE ${wvb_client_sources})
target_include_directories(wvb_client PUBLIC ${wvb_client_header_dir})

##################################################################
###                         LINK LIBS                          ###
##################################################################

target_link_libraries(wvb_client
        wvb_common
        )