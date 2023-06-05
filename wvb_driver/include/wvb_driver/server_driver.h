#pragma once

#include <memory>
#include <openvr_driver.h>

// Maximum time that the driver will wait for a response from the server
// Shouldn't be too large: the server is either there or not. If it is, it will answer very quickly.
// If the server times out, users will have to restart SteamVR for it to work correctly.
// Ideally, the server should thus already be ready when SteamVR is launched (and the server can launch Steam by itself)
#define WVB_DRIVER_SESSION_DATA_TIMEOUT_MS 250
#define WVB_DRIVER_SERVER_STATE_CHECK_INTERVAL_MS 1000

namespace wvb::driver
{
    std::shared_ptr<vr::IServerTrackedDeviceProvider> server_driver();
} // namespace wvb::driver