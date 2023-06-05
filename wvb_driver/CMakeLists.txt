if (NOT ANDROID)

    ##################################################################
    ###                          SOURCES                           ###
    ##################################################################

    # Take all files in the source directory
    file(GLOB_RECURSE wvb_driver_sources
            "src/*.cpp"
            )
    # Take all files in the include directory
    file(GLOB_RECURSE wvb_driver_headers
            "include/*.h"
            "include/*.hpp"
            )
    set(wvb_driver_header_dir
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            )

    ##################################################################
    ###                         CREATE LIB                         ###
    ##################################################################

    add_library(wvb_driver SHARED ${wvb_driver_sources} ${wvb_driver_headers})
    target_sources(wvb_driver PRIVATE ${wvb_driver_sources})
    target_include_directories(wvb_driver PUBLIC ${wvb_driver_header_dir})

    # SteamVR requires both a 32-bit and 64-bit version of the driver

    ##################################################################
    ###                         LINK LIBS                          ###
    ##################################################################

    target_link_libraries(wvb_driver
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

    set_target_properties(wvb_driver PROPERTIES
            # Set prefix required by OpenVR: driver_<driver_name>
            # On Linux, there is no "lib" prefix
            PREFIX "driver_"
            # Generate it in bin/<platform> subdirectory
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/${openvr_platform_name}"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/${openvr_platform_name}"
            )

endif ()
