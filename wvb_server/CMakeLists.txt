if (WIN32)

##################################################################
###                          SOURCES                           ###
##################################################################

# Source files
set(wvb_server_sources
        "src/server.cpp"
        "src/video_pipeline_windows.cpp"
        "src/arg_parser.cpp"
        )

# Take all files in the include directory
file(GLOB_RECURSE wvb_server_headers
        "include/*.h"
        "include/*.hpp"
        )
set(wvb_server_header_dir
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        )

# Copy ffmpeg DLLs (only dlls) to build directory
if (WIN32)
file(COPY ${ffmpeg_dlls} DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
endif()

##################################################################
###                      CREATE EXECUTABLE                     ###
##################################################################

add_library(wvb_server_lib STATIC ${wvb_server_sources} ${wvb_server_headers})
target_sources(wvb_server_lib PRIVATE ${wvb_server_sources})
target_include_directories(wvb_server_lib PUBLIC ${wvb_server_header_dir})

##################################################################
###                         SHADERS                            ###
##################################################################

add_custom_target(wvb_server_shaders)

# Find fxc.exe
find_program(fxc_exe fxc.exe
        PATHS 
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64"
        "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22000.0/x64"
        NO_DEFAULT_PATH
)
if (fxc_exe STREQUAL "fxc_exe-NOTFOUND")
    message(FATAL_ERROR "Could not find fxc.exe")
else()
    message(STATUS "Found fxc.exe at ${fxc_exe}")
endif()

# Find shader files
file(GLOB_RECURSE wvb_server_shader_files
        "shaders/*.hlsl"
        )

# Make output directory if it doesn't exist
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shaders)

# Project conventions: *.comp.hlsl is a compute shader, *.vert.hlsl is a vertex shader, etc.
# Inspired by https://stackoverflow.com/a/71317698
foreach(shader_file ${wvb_server_shader_files})
    get_filename_component(shader_file_name ${shader_file} NAME)
    get_filename_component(shader_file_name_we ${shader_file} NAME_WLE)
    get_filename_component(shader_file_dir ${shader_file} DIRECTORY)

    message(STATUS "Found shader ${shader_file_name_we}")

    # Determine shader type
    if(${shader_file_name} MATCHES "comp.hlsl")
        set(shader_type "cs_5_0")
    elseif(${shader_file_name} MATCHES "vert.hlsl")
        set(shader_type "vs_5_0")
    elseif(${shader_file_name} MATCHES "frag.hlsl")
        set(shader_type "ps_5_0")
    else()
        message(FATAL_ERROR "Unknown shader type for file ${shader_file}")
    endif()

    # Compile shader
    add_custom_command(
            TARGET wvb_server_shaders
            COMMAND ${fxc_exe} /nologo /Emain /T${shader_type} $<IF:$<CONFIG:DEBUG>,/Od,/O1> /Zi /Fo ${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader_file_name_we}.cso /Fd ${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader_file_name_we}.pdb ${shader_file}
            MAIN_DEPENDENCY ${FILE}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Compiling shader ${shader_file_name}"
    )
endforeach()

##################################################################
###                         LINK LIBS                          ###
##################################################################

target_link_libraries(wvb_server_lib
        wvb_common
        d3d11
        d3dcompiler
        dxguid
        stb_image_write
        ffmpeg_lib
        plugins
        )
add_dependencies(wvb_server_lib wvb_server_shaders)

if (${has_plugins} STREQUAL "true")
    add_custom_command(TARGET wvb_server_lib POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${plugin_dlls} $<TARGET_FILE_DIR:wvb_server_lib>)
else()
    message(STATUS "No plugin DLLs found")
endif()



##################################################################
###                         EXECUTABLE                         ###
##################################################################

add_executable(wvb_server "src/main.cpp")
target_link_libraries(wvb_server wvb_server_lib)


endif()