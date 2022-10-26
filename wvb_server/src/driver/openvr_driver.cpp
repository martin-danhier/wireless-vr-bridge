#include <wvb_server/driver_logger.h>

#include <cstring>
#include <openvr_driver.h>
#include <system_error>
#include <thread>

// Inspired by official driver sample
// https://github.com/ValveSoftware/openvr/blob/master/samples/driver_sample/driver_sample.cpp

#if defined(_WIN32)
#define HMD_DLL_EXPORT extern "C" [[maybe_unused]] __declspec(dllexport)
#define HMD_DLL_IMPORT extern "C" [[maybe_unused]] __declspec(dllimport)
#elif defined(__GNUC__) || defined(COMPILER_GCC) || defined(__APPLE__)
#define HMD_DLL_EXPORT extern "C" [[maybe_unused]] __attribute__((visibility("default")))
#define HMD_DLL_IMPORT extern "C" [[maybe_unused]]
#else
#error "Unsupported Platform."
#endif

namespace wvb::server
{
    // =======================================================================================
    // =                                Define driver classes                                =
    // =======================================================================================

    /**
     * The watchdog driver is a special driver that is loaded by the VR server. It
     *  is responsible for loading and unloading the actual driver for the HMD.
     * */
    class WatchdogDriver : public vr::IVRWatchdogProvider
    {
      private:
        std::thread *m_watchdog_thread = nullptr;
        DriverLogger m_logger {nullptr};

      public:
        vr::EVRInitError Init(vr::IVRDriverContext *driver_context) override;
        void             Cleanup() override;
    };

    /** The virtual device driver is the main driver for the HMD. It is responsible
     *  for providing all the information to the VR server that it needs to know
     *  about the HMD. */
    class VirtualDeviceDriver : public vr::ITrackedDeviceServerDriver, public vr::IVRVirtualDisplay
    {
    };

    /** The server driver is the main entry point for the driver. It is loaded by
     *  the VR server and is responsible for initializing the driver. */
    class ServerDriver : public vr::IServerTrackedDeviceProvider
    {
      private:
        DriverLogger m_logger {nullptr};

      public:
        vr::EVRInitError Init(vr::IVRDriverContext *driver_context) override;
        void             Cleanup() override;

        const char *const *GetInterfaceVersions() override { return vr::k_InterfaceVersions; }
        void               RunFrame() override;
        bool               ShouldBlockStandbyMode() override { return false; }
        void               EnterStandby() override {}
        void               LeaveStandby() override {}
    };

    // =======================================================================================
    // =                            Global variables and parameter                           =
    // =======================================================================================

#define WATCHDOG_WAKEUP_INTERVAL_MS 5000

    /** If true, the driver is exiting and threads should stop. */
    bool EXITING = false;

    WatchdogDriver WATCHDOG_DRIVER;
    ServerDriver   SERVER_DRIVER;

    // =======================================================================================
    // =                                    Implementation                                   =
    // =======================================================================================

    // ---=== Watchdog ===---

    void watchdog_thread_function()
    {
        while (!EXITING)
        {
            // Wait for
            std::this_thread::sleep_for(std::chrono::milliseconds(WATCHDOG_WAKEUP_INTERVAL_MS));
            vr::VRWatchdogHost()->WatchdogWakeUp(vr::TrackedDeviceClass_HMD);
        }
    }

    vr::EVRInitError WatchdogDriver::Init(vr::IVRDriverContext *driver_context)
    {
        VR_INIT_WATCHDOG_DRIVER_CONTEXT(driver_context);
        m_logger = DriverLogger(vr::VRDriverLog());

        EXITING = false;

        // Start the watchdog thread
        try
        {
            m_watchdog_thread = new std::thread(watchdog_thread_function);
        }
        catch (std::system_error &e)
        {
            m_logger.log("Failed to start watchdog thread: %s", e.what());
            return vr::VRInitError_Driver_Failed;
        }

        m_logger.log("Watchdog driver initialized");
        return vr::VRInitError_None;
    }

    void WatchdogDriver::Cleanup()
    {
        // Signal the watchdog thread to stop
        EXITING = true;

        if (m_watchdog_thread != nullptr)
        {
            // Wait for it to stop
            m_watchdog_thread->join();

            // And delete it
            delete m_watchdog_thread;
            m_watchdog_thread = nullptr;
        }
    }

    // ---=== Server ===---

    vr::EVRInitError ServerDriver::Init(vr::IVRDriverContext *driver_context)
    {
        VR_INIT_SERVER_DRIVER_CONTEXT(driver_context);
        m_logger = DriverLogger(vr::VRDriverLog());

        // No device for now

        m_logger.log("Server driver initialized");
        return vr::VRInitError_None;
    }

    void ServerDriver::Cleanup()
    {
        // No device for now
    }

    void ServerDriver::RunFrame()
    {
        // No device for now

        vr::VREvent_t vr_event {};
        while (vr::VRServerDriverHost()->PollNextEvent(&vr_event, sizeof(vr_event)))
        {
            m_logger.log("Received event: %d", vr_event.eventType);
        }
    }

} // namespace wvb::server

// =======================================================================================
// =                                     Driver Factory                                  =
// =======================================================================================

/**
 * Entry point of the driver: called by the runtime to get the driver interfaces.
 */
HMD_DLL_EXPORT void *HmdDriverFactory(const char *pInterfaceName, int *pReturnCode)
{
    if (strcmp(vr::IVRWatchdogProvider_Version, pInterfaceName) == 0)
    {
        return &wvb::server::WATCHDOG_DRIVER;
    }
    if (strcmp(vr::IServerTrackedDeviceProvider_Version, pInterfaceName) == 0)
    {
        return &wvb::server::SERVER_DRIVER;
    }

    if (pReturnCode)
    {
        *pReturnCode = vr::VRInitError_Init_InterfaceNotFound;
    }
    return nullptr;
}