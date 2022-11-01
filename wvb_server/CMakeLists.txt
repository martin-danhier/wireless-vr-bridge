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
###                         CREATE LIB                         ###
##################################################################

add_library(wvb_server SHARED ${wvb_server_sources} ${wvb_server_headers})
target_sources(wvb_server PRIVATE ${wvb_server_sources})
target_include_directories(wvb_server PUBLIC ${wvb_server_header_dir})

##################################################################
###                         LINK LIBS                          ###
##################################################################

target_link_libraries(wvb_server
        openvr
        wvb_common
        )

##################################################################
###                    DRIVER REQUIREMENTS                     ###
##################################################################

# Copy the driver manifest to the build directory
configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/driver.vrdrivermanifest
        ${CMAKE_CURRENT_BINARY_DIR}/driver.vrdrivermanifest
        COPYONLY
)

set_target_properties(wvb_server PROPERTIES
        # Set prefix required by OpenVR: driver_<driver_name>
        # On Linux, there is no "lib" prefix
        PREFIX "driver_"
        # Generate it in bin/<platform> subdirectory
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/${openvr_platform_name}"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/${openvr_platform_name}"
        )
