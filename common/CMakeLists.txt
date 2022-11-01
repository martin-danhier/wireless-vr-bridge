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

add_library(wvb_common INTERFACE ${wvb_common_sources} ${wvb_common_headers})
target_sources(wvb_common PRIVATE ${wvb_common_sources})
target_include_directories(wvb_common INTERFACE ${wvb_common_header_dir})

set_target_properties(wvb_common PROPERTIES LINKER_LANGUAGE CXX)