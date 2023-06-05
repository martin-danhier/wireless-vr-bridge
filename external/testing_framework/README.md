# Testing Framework

Lightweight CTest testing framework, inspired by Google Test.

Compatible with C, with additional features for C++.

## Example

```cpp
#include <test_framework/test_framework.hpp>

// Tested library
#include <railguard/utils/io.h>
#include <string>

TEST
{
    std::string EXPECTED("This is a file containing test text.");

    // Test reading a file
    size_t      length   = 0;
    char       *contents = nullptr;
    ASSERT_NO_THROWS(contents = static_cast<char *>(rg::load_binary_file("resources/test.txt", &length)));
    std::string s        = std::string(contents, length);
    ASSERT_NOT_NULL(contents);
    EXPECT_EQ(s, EXPECTED);
    EXPECT_TRUE(length == EXPECTED.size());

    // Free memory
    delete[] contents;
    contents = nullptr;

    // A non-existing file throws an exception
    EXPECT_THROWS(rg::load_binary_file("resources/non-existing.txt", &length));
}
```

If all checks pass, it will show:

```

 ---> PASSED

```

If some fail, it will show:

```

 ---> FAILED
  - [Error] 
  tests/utils/io.cpp:19
    Condition failed. Expected: This is a file containing test text., got: <wrong text>.
  - [Error] 
  tests/utils/io.cpp:20
    Condition failed. Expected [true], got [false].

```

On supported terminals, the output will be colored.

Examples of test files can be found in the [tests](./tests) directory.

## How to setup

1. Add this repository as a submodule of your project
2. Create a ``tests`` directory
3. In your CMake configuration, add the following snippet. Replace `<PROJECT_LIB>` by the name of your library.
    - If you are developing an executable, it is simpler to build it as a library and only build the executable with your main file. This way, all of the library code can be imported in test executables.

```cmake
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
    target_link_libraries(${TEST_NAME} <PROJECT_LIB> testing_framework)

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

```

## How to run the tests

Tests are integrated using CTest in the CMake configuration.

To build all tests, you can build the `tests` target.

You can also build individual tests by building the target named after its relative path in the test directory, with every `/` replaced by a `-`.

For example, to build the `tests/core/server.cpp` test, build the `core-server` target.

### IDE integration

If you are using **Visual Studio Code**, the [CMake Test Explorer](https://marketplace.visualstudio.com/items?itemName=fredericbonnet.cmake-test-adapter) extension can be used to list all tests in a tree and easily run them.

You just have to add the following lines in your `.vscode/settings.json` file:

```json
"cmakeExplorer.buildDir": "${workspaceFolder}/build",
"cmakeExplorer.buildConfig": "Debug",
"cmakeExplorer.suiteDelimiter": "-"
```

Replace the first line with your actual build directory.

If you are using **CLion**, it has a built-in support for CTest. Each individual test target will appear in the target list, and a special "All CTest" target is available to run all tests at once.

## How to create a test

To create a test, simply create a new C or C++ file in one of the test directories.

```c
#include <test_framework.h>

TEST {
    // Test code
}
```

CMake will automatically find the new test.

Note: the `TEST` macro wraps the `main` function, thus only one test is allow per file.

Inside a test, you can use various macros. They exist in two categories:
- **Assertions**: check if a condition is true. If it is false, do not continue the execution.
- **Expectations**: check if a condition is true. If it is false, continue the execution.

Typically, use expectations everywhere, except if a false condition could imply segmentation faults later.
For example, if a pointer is followed in the test, assert that the pointer is not null beforehand.

The macros are the following:
- ``ASSERT_TRUE(condition)``: check if a condition is true.
- ``ASSERT_FALSE(condition)``: check if a condition is false.
- ``ASSERT_NULL(pointer)``: check if a pointer is null.
- ``ASSERT_NOT_NULL(pointer)``: check if a pointer is not null.

The equivalent exists for expectations, for example ``EXPECT_TRUE``.

There are also two macros that always throw an error, for example to report invalid branches (such as error codes).
- ``ERROR(message)``: report the error and continue execution (like an EXPECT)
- ``ERROR_AND_QUIT(message)``: report the error and stop the execution (like an ASSERT)

## C++ extension

In C++, additional macros are available. To enable them, simply include the C++ variant:

```cpp
#include <test_framework.hpp>
```

The following macros are available, on top of the basic ones:

- `ASSERT_THROWS(fn)`: check if the expression throws an exception. The parameter is the body of a lambda function.
- `ASSERT_NO_THROWS(fn)`: check if the expression does not throw an exception.
- `ASSERT_EQ(actual, expected)`: checks if two expressions are equal.
- `ASSERT_NEQ(actual, not_expected)`: checks if two expressions are not equal.

The expectations equivalents are also available.

The C++ extensions also has extended support for uncaught exceptions.
- Exceptions in the main test function will be reported as errors
- Threads can be started using the `START_THREAD(thread_var, [&]{/* code */})` macro. Similarly, uncaught exceptions will be reported. See examples.

## How to check if we are in a test from the library ?

``UNIT_TESTS`` is defined when in test mode.
