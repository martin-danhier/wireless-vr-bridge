# Wireless VR Bridge

Bridge for low-latency wireless transmission of realtime virtual reality content over WiFi.

## Planned Features

- **PC Server**
    - OpenVR driver for use in SteamVR
    - Compression of downlink data (images, sound, haptics)
    - Reception of uplink data (input, tracking, errors)
    - Benchmarking module to evaluate performances
- **VR Client**
    - OpenXR application
    - Reception of downlink data
    - Compression of uplink data
    - Benchmarking module
    - Later, if required: reprojection (ASW, ATW...)

Initially, the VR client will be developed for standalone headsets only (Quest 2, Pico 4).

## How to setup development environment

- Clone this repository with ``--recurse-submodules`` flag
    - If you forgot the flag, you can run `git submodule update --init` instead.
- Install a C++ compiler (clang and Ninja are recommended)
- Install the [OpenVR SDK](https://github.com/ValveSoftware/openvr/releases)
  - If it is not at a standard location, you can extract the zip and set the ``OPENVR_SDK_PATH`` environment variable to the root directory.
  - On Linux, you can also extract the headers in `/usr/include` and the corresponding lib in `/usr/lib` or `/usr/lib64`.
