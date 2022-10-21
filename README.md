# Wireless VR Bridge

Bridge for low-latency wireless transmission of realtime VR content over WiFi.

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

