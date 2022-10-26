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

# WVB server
add_subdirectory(wvb_server)