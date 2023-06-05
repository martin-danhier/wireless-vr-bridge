# Config initially inspired by https://github.com/krux02/minimal_cmake_example

cmake_minimum_required(VERSION 3.16)
project(wireless_vr_bridge VERSION 0.1.0)

message("Configuring ${PROJECT_NAME} ${PROJECT_VERSION}")

##################################################################
###                          SETTINGS                          ###
##################################################################

# Add settings flags here
set(settings
        #WVB_VIDEO_SOCKET_USE_RTP
        #WVB_VIDEO_SOCKET_USE_UDP
        )

set(debug_settings
        USE_OPENXR_VALIDATION_LAYERS
        XR_USE_GRAPHICS_API_OPENGL_ES
        )

# Enable/disable sanitizers
set(enable_asan 0)
set(enable_ubsan 0)

# Enable/disable interactivity
# Setting this to 0 will add timers to ensure that no test is blocked in a loop, waiting for user input.
# Useful for CI, where the value is always overridden to 0 automatically.
set(interactive 1)

# Define C and C++ versions
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

# Enable tests
set(enable_tests 1)

##################################################################
###                       APPLY SETTINGS                       ###
##################################################################

# If the project is included as a cmake subdirectory
if (NOT CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    # Disable tests in this scope
    message(STATUS "WVB included as a subdirectory, disabling tests")
    set(enable_tests 0)
endif ()

# Add all settings as definitions
foreach (setting ${settings})
    add_compile_definitions(${setting})
endforeach ()

# If debug
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Debug build detected, enabling debug settings")
    foreach (setting ${debug_settings})
        add_compile_definitions(${setting} DEBUG)
    endforeach ()
else ()
    add_compile_definitions(NDEBUG)
endif ()

# On a CI, override the value of interactive with 0
# We know that we are on a CI if the CI environment variable is set
if (DEFINED ENV{CI})
    message(STATUS "Continuous integration detected, disabling interactive mode")
    set(interactive 0)
    # Also disable sanitizers
    set(enable_asan 0)
    set(enable_ubsan 0)
endif ()

# Windows must be compiled with MSVC
if (WIN32 AND NOT MSVC)
    message(FATAL_ERROR "On Windows, only MSVC is supported due to a SteamVR requirement.")
endif ()

# Enable sanitizers if variable is 1
if (${enable_asan} STREQUAL "1")
    message(STATUS "Enabling address sanitizer")

    # MSVC syntax is a bit different
    if (MSVC)
        set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} /fsanitize=address")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /fsanitize=address")
    else ()
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
    endif ()
endif ()

if (${enable_ubsan} STREQUAL "1" AND NOT MSVC)
    message(STATUS "Enabling undefined behavior sanitizer")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
endif ()

# Enable or not interactivity
if (${interactive} STREQUAL "1")
    add_compile_definitions(
            INTERACTIVE
    )
else ()
    add_compile_definitions(
            NO_INTERACTIVE
    )
endif ()

# Add -fPIC flag for shared libraries on Linux
if (UNIX AND NOT APPLE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
endif ()

# Set unused parameters as warnings instead of errors
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wno-error=unused-parameter)
endif ()

# On MSVC
if (MSVC)
    add_compile_options(/Zo)


endif()

##################################################################
###                        DEPENDENCIES                        ###
##################################################################

# Load external script
add_subdirectory(external)

##################################################################
###                         LIBRARIES                          ###
##################################################################

# Common library
add_subdirectory(common)

# WVB driver
add_subdirectory(wvb_driver)

# WVB server
add_subdirectory(wvb_server)

# WVB client
add_subdirectory(wvb_client)

##################################################################
###                           TESTING                          ###
##################################################################

if (${enable_tests} STREQUAL "1")

    # Define files that should be copied into build directory to be relatively accessible from tests
    set(test_resources
            # Example:
            tests/resources/frame_1000_rgba.png
            tests/resources/frame_1000_yuv444.png
            tests/resources/av_packet.h264
            tests/resources/full_stream.h264
            )

    # Macro inspired by https://bertvandenbroucke.netlify.app/2019/12/12/unit-testing-with-ctest/

    # Enable CTest
    enable_testing()


    # Add tests modules
    # For each module, compile it as standalone dll
    file(GLOB_RECURSE test_modules
            "tests/modules/*.cpp"
            )

    foreach (test_module ${test_modules})

        # Get local path (remove prefix project source)
        string(REGEX REPLACE "^${PROJECT_SOURCE_DIR}/tests/modules/" "" test_module ${test_module})

        # Get module name
        string(REGEX REPLACE ".(c|cpp)$" "" test_module_name ${test_module})
        string(REGEX REPLACE "/" "-" test_module_name ${test_module_name})
        # Add wvb_module_ prefix
        set(test_module_name "wvb_module_${test_module_name}")

        # Compile module
        message(STATUS "Generating test module \"${test_module_name}\"")
        add_library(${test_module_name} SHARED EXCLUDE_FROM_ALL tests/modules/${test_module})
        target_sources(${test_module_name} PRIVATE tests/modules/${test_module})

        # Set directory for lib
        set_target_properties(${test_module_name} PROPERTIES
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
                )


        # Link project lib(s) and testing framework
        target_link_libraries(${test_module_name}
                wvb_common
                )

        # Add module to list
        set(TEST_MODULES ${TEST_MODULES} ${test_module_name})
    endforeach (test_module)

    # Create custom target to build all test modules
    add_custom_target(all_test_modules DEPENDS ${TEST_MODULES})

    # Add a new unit test
    # A new target with the test sources is constructed, and a CTest test with the
    # same name is created. The new test is also added to the global list of test
    # contained in the check target
    macro(add_unit_test)
        # Define macro arguments
        set(options PARALLEL)
        set(oneValueArgs FILE)
        cmake_parse_arguments(TEST "${options}" "${oneValueArgs}" "" ${ARGN})

        # Remove extension of TEST_FILE.
        set(TEST_NAME ${TEST_FILE})
        string(REGEX REPLACE ".(c|cpp)$" "" TEST_NAME ${TEST_NAME})
        # At the beginning of the last part, add test_
        string(REGEX REPLACE "([^/]+)$" "test_\\1" TEST_NAME ${TEST_NAME})
        # Also replace / by -
        string(REGEX REPLACE "/" "-" TEST_NAME ${TEST_NAME})

        # Compile test
        message(STATUS "Generating test \"${TEST_NAME}\"")
        add_executable(${TEST_NAME} EXCLUDE_FROM_ALL tests/${TEST_FILE})
        target_sources(${TEST_NAME} PRIVATE tests/${TEST_FILE})

        # Set directory for executable
        set_target_properties(${TEST_NAME} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests"
                COMPILE_DEFINITIONS UNIT_TESTS
                )

        # Link project lib(s) and testing framework
        target_link_libraries(${TEST_NAME}
                wvb_common
                wvb_driver
                testing_framework
                ffmpeg_lib
                stb_image_write
                stb_image)

        if (WIN32)
            target_link_libraries(${TEST_NAME}
                    wvb_server_lib)
        endif ()

        if (ANDROID)
            target_link_libraries(${TEST_NAME}
                    wvb_client_lib)
        endif ()


        # Register test
        add_test(NAME ${TEST_NAME}
                WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/tests
                COMMAND ${TEST_NAME})

        # Add test to list
        set(TEST_NAMES ${TEST_NAMES} ${TEST_NAME})
        set_tests_properties(${TEST_NAME} PROPERTIES
                ENVIRONMENT "TEST_FILE=tests/${TEST_FILE};TEST_LINE=0"
                )
    endmacro(add_unit_test)


    # Get all c++ files in the tests directory, recursively
    file(GLOB_RECURSE test_files
            "tests/*.cpp"
            )
    # Remove tests/modules/* because they are not tests
    list(FILTER test_files EXCLUDE REGEX "tests/modules/.*")

    # For each one, add it
    foreach (test_file ${test_files})
        # Get local path (remove prefix project source)
        string(REGEX REPLACE "^${PROJECT_SOURCE_DIR}/tests/" "" test_file ${test_file})

        add_unit_test(FILE ${test_file})
    endforeach (test_file)
    add_dependencies(test_modules all_test_modules)

    # Copy ffmpeg dlls
    if (WIN32)
        file(COPY ${ffmpeg_dlls} DESTINATION ${CMAKE_BINARY_DIR}/tests)
    endif ()

    # Save target
    add_custom_target(
            tests
            DEPENDS ${TEST_NAMES}
    )

    # Copy test resources
    foreach (test_resource ${test_resources})
        # Get local path (remove prefix project source)
        get_filename_component(file_path ${test_resource} PATH)
        file(COPY ${test_resource} DESTINATION ${PROJECT_BINARY_DIR}/${file_path})
    endforeach ()
endif()