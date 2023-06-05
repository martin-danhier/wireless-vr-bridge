cmake_minimum_required(VERSION 3.16)
project(testing_framework)

# Create library
add_library(testing_framework STATIC)
target_sources(testing_framework PRIVATE
        test_framework/test_framework.hpp
        src/test_framework.cpp
        test_framework/test_framework.h
        src/test_framework.c
        )
target_include_directories(testing_framework PUBLIC test_framework)

# Check if the testing_framework is being loaded as a subproject or not
if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)

        message(STATUS "Building tests for testing_framework")
        
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
        target_link_libraries(${TEST_NAME} testing_framework)

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

endif()