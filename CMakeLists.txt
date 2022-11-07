# Config initially inspired by https://github.com/krux02/minimal_cmake_example

cmake_minimum_required(VERSION 3.16)
project(wireless_vr_bridge)

##################################################################
###                          SETTINGS                          ###
##################################################################

# Add settings flags here
set(settings
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
endif()

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

# Define files that should be copied into build directory to be relatively accessible from tests
set(test_resources
        # Example:
        # tests/resources/test.txt
        )

# Macro inspired by https://bertvandenbroucke.netlify.app/2019/12/12/unit-testing-with-ctest/

# Enable CTest
enable_testing()

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
    target_link_libraries(${TEST_NAME} wvb_common wvb_client_lib wvb_driver wvb_server_lib testing_framework)

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

# For each one, add it
foreach(test_file ${test_files})
    # Get local path (remove prefix project source)
    string(REGEX REPLACE "^${PROJECT_SOURCE_DIR}/tests/" "" test_file ${test_file})

    add_unit_test(FILE ${test_file})
endforeach(test_file)

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
endforeach()