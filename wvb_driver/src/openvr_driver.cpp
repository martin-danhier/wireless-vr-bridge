#include "wvb_driver/server_driver.h"
#include <wvb_common/server_shared_state.h>
#include <cstring>

// Inspired by official driver samples
// https://github.com/ValveSoftware/openvr/blob/master/samples/driver_sample/driver_sample.cpp
// https://github.com/ValveSoftware/virtual_display

#if defined(_WIN32)
#define HMD_DLL_EXPORT extern "C" [[maybe_unused]] __declspec(dllexport)
#define HMD_DLL_IMPORT extern "C" [[maybe_unused]] __declspec(dllimport)
#elif defined(__GNUC__) || defined(COMPILER_GCC) || defined(__APPLE__)
#define HMD_DLL_EXPORT extern "C" [[maybe_unused]] __attribute__((visibility("default")))
#define HMD_DLL_IMPORT extern "C" [[maybe_unused]]
#else
#error "Unsupported Platform."
#endif

// If windows, it must be MSVC
#if defined(_WIN32) and !defined(_MSC_VER)
#error "On Windows, the driver must be compiled with MSVC. Otherwise, SteamVR won't be able to load it."
#endif

/**
 * Entry point of the driver: called by SteamVR on startup to unsafe_get the list of available drivers.
 *
 * Depending on the connected USB devices, it will load the appropriate driver. In our case, there is no
 * USB device, so the driver is always loaded.
 *
 */
HMD_DLL_EXPORT void *HmdDriverFactory(const char *pInterfaceName, int *pReturnCode)
{

    if (strcmp(vr::IServerTrackedDeviceProvider_Version, pInterfaceName) == 0)
    {
        return wvb::driver::server_driver().get();
    }

    if (pReturnCode)
    {
        *pReturnCode = vr::VRInitError_Init_InterfaceNotFound;
    }
    return nullptr;
}