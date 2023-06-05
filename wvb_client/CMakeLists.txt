if (ANDROID)
##################################################################
###                          SOURCES                           ###
##################################################################


# Source files
set(wvb_client_sources
        "src/client.cpp"
        "src/vr_system.cpp"
        )
set(wvb_client_header_dir
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        )

##################################################################
###                      CREATE EXECUTABLE                     ###
##################################################################

add_library(wvb_client_lib STATIC ${wvb_client_sources} ${wvb_client_headers})
target_sources(wvb_client_lib PRIVATE ${wvb_client_sources})
target_include_directories(wvb_client_lib PUBLIC ${wvb_client_header_dir})

##################################################################
###                         LINK LIBS                          ###
##################################################################

target_link_libraries(wvb_client_lib
        wvb_common
        openxr
        ffmpeg_lib
        stb_image_write
        )

##################################################################
###                        EXECUTABLE                          ###
##################################################################

add_executable(wvb_client src/main.cpp)
target_link_libraries(wvb_client
        wvb_client_lib
        )

##################################################################
###                         QUEST LIB                          ###
##################################################################

# Build for Android
endif()