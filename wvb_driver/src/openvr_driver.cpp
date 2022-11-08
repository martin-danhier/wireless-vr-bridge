#include <wvb_common/server_shared_state.h>
#include <wvb_driver/driver_logger.h>

#include <cstring>
#include <openvr_driver.h>
#include <system_error>
#include <thread>

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

namespace wvb::driver
{
    // =======================================================================================
    // =                                       Defines                                       =
    // =======================================================================================

#define WATCHDOG_WAKEUP_INTERVAL_MS 5000

#define VIRTUAL_DEVICE_SERIAL_NUMBER "WVB-VirtualHMD"
#define VIRTUAL_DEVICE_MODEL_NUMBER  "wireless_vr_bridge virtual device"

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

    /** Simulates a virtual HMD in order to receive rendered images and send device infos. */
    class VirtualHMDDriver : public vr::ITrackedDeviceServerDriver, public vr::IVRVirtualDisplay
    {
      private:
        uint64_t frame_number = 0;
        double   frame_time   = 0.0;

      public:
        VirtualHMDDriver();

        // ITrackedDeviceServerDriver
        vr::EVRInitError Activate(uint32_t object_id) override;
        void             Deactivate() override;
        void             EnterStandby() override;
        vr::DriverPose_t GetPose() override;
        void            *GetComponent(const char *component_name_and_version) override { return nullptr; }
        void             DebugRequest(const char *request, char *response_buffer, uint32_t response_buffer_size) override {}

        // IVRVirtualDisplay
        void Present(const vr::PresentInfo_t *present_info, uint32_t present_info_size) override;
        void WaitForPresent() override;
        bool GetTimeSinceLastVsync(float *seconds_since_last_vsync, uint64_t *frame_counter) override;
    };

    /** The server driver is the main entry point for the driver. It is loaded by
     *  the VR server and is responsible for initializing the driver. */
    class ServerDriver : public vr::IServerTrackedDeviceProvider
    {
      private:
        DriverLogger      m_logger {nullptr};
        VirtualHMDDriver *m_device_driver = nullptr;
        // Server communication
        ServerDriverSharedMemory m_shared_memory;
        DriverEvents             m_driver_events {true};
        ServerEvents             m_server_events {false};

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
    // =                                   Global variables                                  =
    // =======================================================================================

    /** If true, the driver is exiting and threads should stop. */
    bool EXITING = false;

    WatchdogDriver WATCHDOG_DRIVER;
    ServerDriver   SERVER_DRIVER;
    // Usually, the server driver is initialized, destroyed and initialized again.
    // Use a counter to only connect to the server the second time.
    uint8_t SERVER_INIT_COUNTER = 0;

    // =======================================================================================
    // =                                    Implementation                                   =
    // =======================================================================================

    // ---=== Watchdog ===---

    void watchdog_thread_function()
    {
        // Load shared memory
        ServerDriverSharedMemory shared_memory(WVB_SERVER_DRIVER_MUTEX_NAME, WVB_SERVER_DRIVER_MEMORY_NAME);
        InterProcessEvent        server_state_changed(WVB_EVENT_SERVER_STATE_CHANGED, false);

        // Check before starting loop
        bool server_running = false;
        {
            auto lock = shared_memory.lock();
            server_running = lock->server_state != ServerState::NOT_RUNNING;
        }
        if (server_running)
        {
            vr::VRWatchdogHost()->WatchdogWakeUp(vr::TrackedDeviceClass_HMD);
        }

        // Loop and wake up if it loads
        while (!EXITING)
        {
            // Wait for the server to start
            auto signaled = server_state_changed.wait(WATCHDOG_WAKEUP_INTERVAL_MS);

            if (signaled)
            {
                bool new_state;
                {
                    auto lock = shared_memory.lock();
                    new_state = lock->server_state != ServerState::NOT_RUNNING;
                }

                if (server_running != new_state)
                {
                    server_running = new_state;

                    if (server_running)
                    {
                        // Wake up steam vr
                        vr::VRWatchdogHost()->WatchdogWakeUp(vr::TrackedDeviceClass_HMD);
                    }
                }
            }
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

    // ---=== VirtualDevice ===---

    VirtualHMDDriver::VirtualHMDDriver()
    {
        // TODO
    }

    vr::EVRInitError VirtualHMDDriver::Activate(uint32_t object_id)
    {
        return vr::VRInitError_None;
    }

    void VirtualHMDDriver::Deactivate() {}

    void VirtualHMDDriver::EnterStandby() {}

    vr::DriverPose_t VirtualHMDDriver::GetPose()
    {
        vr::DriverPose_t pose  = {0};
        pose.poseIsValid       = true;
        pose.result            = vr::TrackingResult_Running_OK;
        pose.deviceIsConnected = true;
        return pose;
    }

    void VirtualHMDDriver::Present(const vr::PresentInfo_t *present_info, uint32_t present_info_size)
    {
        frame_number = present_info->nFrameId;
        frame_time   = present_info->flVSyncTimeInSeconds;
    }

    void VirtualHMDDriver::WaitForPresent() {}

    bool VirtualHMDDriver::GetTimeSinceLastVsync(float *seconds_since_last_vsync, uint64_t *frame_counter)
    {
        *seconds_since_last_vsync = 0.11f;
        *frame_counter            = frame_number;

        return true;
    }

    // ---=== Server ===---

    vr::EVRInitError ServerDriver::Init(vr::IVRDriverContext *driver_context)
    {
        VR_INIT_SERVER_DRIVER_CONTEXT(driver_context);

        m_logger = DriverLogger(vr::VRDriverLog());

        if (SERVER_INIT_COUNTER >= 0)
        {
            // Init shared memory and change driver state
            m_logger.log("Server driver loaded");

            m_shared_memory = ServerDriverSharedMemory(WVB_SERVER_DRIVER_MUTEX_NAME, WVB_SERVER_DRIVER_MEMORY_NAME);
            {
                auto data          = m_shared_memory.lock();
                data->driver_state = DriverState::AWAITING_CLIENT_SPEC;
            }
            m_driver_events.driver_state_changed.signal();

            //        m_device_driver = new VirtualHMDDriver();
            //        vr::VRServerDriverHost()->TrackedDeviceAdded(VIRTUAL_DEVICE_SERIAL_NUMBER, vr::TrackedDeviceClass_HMD,
            //        m_device_driver);

            SERVER_INIT_COUNTER++;
            m_logger.log("Server driver initialized");
        }
        return vr::VRInitError_None;
    }

    void ServerDriver::Cleanup()
    {
        // Cleanup server
        m_logger.log("Server driver unloaded");

        // Disconnect from server
        {
            auto data          = m_shared_memory.lock();
            data->driver_state = DriverState::NOT_RUNNING;
        }
        m_driver_events.driver_state_changed.signal();

        if (m_device_driver != nullptr)
        {
            delete m_device_driver;
            m_device_driver = nullptr;
        }
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

} // namespace wvb::driver

// =======================================================================================
// =                                     Driver Factory                                  =
// =======================================================================================

/**
 * Entry point of the driver: called by SteamVR on startup to unsafe_get the list of available drivers.
 *
 * Depending on the connected USB devices, it will load the appropriate driver. In our case, there is no
 * USB device, so the driver is always loaded.
 *
 * TODO in the future, it may be useful to seperate the main server process from the driver process, so that it can be loaded
 * only when the server application is running. This would require communication between the two processes.
 */
HMD_DLL_EXPORT void *HmdDriverFactory(const char *pInterfaceName, int *pReturnCode)
{
    if (strcmp(vr::IVRWatchdogProvider_Version, pInterfaceName) == 0)
    {
        return &wvb::driver::WATCHDOG_DRIVER;
    }
    if (strcmp(vr::IServerTrackedDeviceProvider_Version, pInterfaceName) == 0)
    {
        return &wvb::driver::SERVER_DRIVER;
    }

    if (pReturnCode)
    {
        *pReturnCode = vr::VRInitError_Init_InterfaceNotFound;
    }
    return nullptr;
}