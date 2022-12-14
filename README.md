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

![Overview](resources/schema-overview.png)

## How to setup development environment

- Clone this repository with ``--recurse-submodules`` flag
    - If you forgot the flag, you can run `git submodule update --init --recursive` instead.
- Install the [OpenVR SDK](https://github.com/ValveSoftware/openvr/releases)
  - If it is not at a standard location, you can extract the zip and set the ``OPENVR_SDK_PATH`` environment variable to the root directory.
  - On Linux, you can also extract the headers in `/usr/include` and the corresponding lib in `/usr/lib` or `/usr/lib64`.
- Install the OpenSSL library
- Install SteamVR
- Run CMake a first time
- To allow SteamVR to find the driver, run
  - ``python ./wvb_driver/wvb_driver_control.py enable --driver_dir ./<build dir>/wvb_driver``
  - Run ``python ./wvb_driver/wvb_driver_control.py status`` to check if it worked
  - Run ``python ./wvb_driver/wvb_driver_control.py disable`` to remove the driver

## Notes

- On Windows, the driver **must** be compiled using MSVC, otherwise SteamVR won't be able to load it.
- To access SteamVR logs:
  - Right click on SteamVR window
  - Developer > Web console

## Architecture

The project is constituted of four main components:

- **Common library**: contains the logic that is used in multiple other components
- **WVB Driver**: OpenVR interfacing with SteamVR. Watches for the server application and connects to it.
- **WVB Server**: Application handling the communication between the driver and the client.
- **WVB Client**: Application communicating with the server and displaying the content on the VR headset.