#include "wvb_driver/server_driver.h"

#include <wvb_common/benchmark.h>
#include <wvb_common/server_shared_state.h>
#include <wvb_driver/device_drivers.h>
#include <wvb_driver/driver_logger.h>
#include <wvb_driver/global_state.h>

#include <thread>

namespace wvb::driver
{
    // =======================================================================================
    // =                                  Class definition                                   =
    // =======================================================================================

    /** The server driver is the main entry point for the driver. It is loaded by
     *  the VR server and is responsible for initializing the driver. */
    class ServerDriver : public vr::IServerTrackedDeviceProvider
    {
      private:
        // Thread to listen for server events (async init)
        std::shared_ptr<DriverLogger> m_logger = nullptr;

        // Server communication
        std::shared_ptr<ServerDriverSharedMemory> m_shared_memory = nullptr;
        std::shared_ptr<DriverEvents>             m_driver_events = nullptr;
        std::shared_ptr<ServerEvents>             m_server_events = nullptr;

        // Device drivers
        std::shared_ptr<VirtualHMDDriver>     m_device_driver          = nullptr;
        std::shared_ptr<ShutdownDeviceDriver> m_shutdown_device_driver = nullptr;

        // Benchmark
        std::shared_ptr<DriverMeasurementBucket> m_measurement_bucket = nullptr;

        std::chrono::steady_clock::time_point m_last_server_state_check = std::chrono::steady_clock::now();

      public:
        vr::EVRInitError Init(vr::IVRDriverContext *driver_context) override;
        void             Cleanup() override;

        const char *const *GetInterfaceVersions() override { return vr::k_InterfaceVersions; }
        void               RunFrame() override;
        bool               ShouldBlockStandbyMode() override { return false; }
        void               EnterStandby() override;
        void               LeaveStandby() override;
        void               HandleServerStateChange();
    };

    std::shared_ptr<ServerDriver> g_server_driver = nullptr;

    std::shared_ptr<vr::IServerTrackedDeviceProvider> server_driver()
    {
        if (g_server_driver == nullptr)
        {
            g_server_driver = std::make_shared<ServerDriver>();
        }
        return g_server_driver;
    }

    vr::EVRInitError ServerDriver::Init(vr::IVRDriverContext *driver_context)
    {
        VR_INIT_SERVER_DRIVER_CONTEXT(driver_context);

        m_logger             = std::make_shared<DriverLogger>(vr::VRDriverLog());
        m_measurement_bucket = std::make_shared<DriverMeasurementBucket>();

        // Init shared memory and change driver state
        m_logger->log("server driver loaded");

        m_shared_memory = std::make_shared<ServerDriverSharedMemory>(WVB_SERVER_DRIVER_MUTEX_NAME, WVB_SERVER_DRIVER_MEMORY_NAME);
        m_server_events = std::make_shared<ServerEvents>(false);
        m_driver_events = std::make_shared<DriverEvents>(true);

        {
            auto data          = m_shared_memory->lock();
            data->driver_state = DriverState::AWAITING_CLIENT_SPEC;
        }
        m_driver_events->driver_state_changed.signal();

        // Steam VR doesn't support async init (it is not possible to return then add an HMD later from another thread).
        // Thus, we need to block until we find a valid client.
        // However, we do not want our driver to freeze steamVR if clients are not found.
        auto start_time = std::chrono::high_resolution_clock::now();
        if (!m_server_events->new_system_specs.wait(WVB_DRIVER_SESSION_DATA_TIMEOUT_MS))
        {
            {
                auto data = m_shared_memory->lock();
                if (data->server_state == ServerState::AWAITING_CONNECTION)
                {
                    // Server is started, but no client connected
                    // Shutdown driver. For that to work, we need a valid HMD, so create a fake one just to shutdown the driver
                    m_shutdown_device_driver = std::make_shared<ShutdownDeviceDriver>();
                    vr::VRServerDriverHost()->TrackedDeviceAdded(WVB_VIRTUAL_DEVICE_SERIAL_NUMBER,
                                                                 vr::TrackedDeviceClass_HMD,
                                                                 m_shutdown_device_driver.get());

                    return vr::VRInitError_None;
                }
            }

            // Server isn't even started, simply tell that no headset was found so that steam can be used with other headsets like a
            // Rift
            auto end_time = std::chrono::high_resolution_clock::now();
            m_logger->log(
                "No client found after %lld ms. Driver will be unloaded to avoid blocking SteamVR. Refresh driver to try again.",
                std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count());
            return vr::VRInitError_Driver_Failed;
        }

        // We have a session
        VRSystemSpecs specs;

        {
            auto data = m_shared_memory->lock();
            specs     = data->vr_system_specs;
        }

        // Filter out invalid specs
        if (specs.manufacturer_name.empty() || specs.system_name.empty() || specs.eye_resolution.width == 0
            || specs.eye_resolution.height == 0 || specs.eye_resolution.width > 4096 || specs.eye_resolution.height > 4096
            || specs.refresh_rate.denominator == 0 || specs.refresh_rate.numerator == 0)
        {
            m_logger->log("Received invalid client specs.");
            return vr::VRInitError_Driver_Failed;
        }

        m_logger->log("Wireless client \"%s %s\" connected with resolution %dx%d per eye",
                      specs.manufacturer_name.c_str(),
                      specs.system_name.c_str(),
                      specs.eye_resolution.width,
                      specs.eye_resolution.height);

        // Create and register device driver
        m_device_driver = std::make_shared<VirtualHMDDriver>(std::move(specs),
                                                             m_logger,
                                                             m_shared_memory,
                                                             m_server_events,
                                                             m_driver_events,
                                                             m_measurement_bucket);
        vr::VRServerDriverHost()->TrackedDeviceAdded(WVB_VIRTUAL_DEVICE_SERIAL_NUMBER,
                                                     vr::TrackedDeviceClass_HMD,
                                                     m_device_driver.get());
        {
            auto data          = m_shared_memory->lock();
            data->driver_state = DriverState::READY;
        }
        m_driver_events->driver_state_changed.signal();

        return vr::VRInitError_None;
    }

    void ServerDriver::Cleanup()
    {
        // Cleanup server
        m_logger->log("Server driver unloaded");

        // Disconnect from server and reset driver state
        {
            auto data                                 = m_shared_memory->lock();
            data->driver_state                        = DriverState::NOT_RUNNING;
            data->frame_time_measurements_count       = 0;
            data->tracking_time_measurements_count    = 0;
            data->pose_access_time_measurements_count = 0;
            data->latest_present_info                 = {};
        }
        m_driver_events->driver_state_changed.signal();
        m_device_driver.reset();
    }

    void ServerDriver::RunFrame()
    {
        if (m_device_driver != nullptr)
        {
            m_device_driver->RunFrame();
        }

        // Set window as soon as we receive it so we can take the measurements
        if (!m_measurement_bucket->has_window() && m_server_events->new_benchmark_data.is_signaled())
        {
            m_server_events->new_benchmark_data.reset();

            auto lock = m_shared_memory->lock();

            // Set window
            if (lock.is_valid() && lock->measurement_window.is_valid() && lock->ntp_epoch > UNIX_EPOCH_NTP)
            {
                m_measurement_bucket->set_clock(std::make_shared<rtp::RTPClock>(lock->ntp_epoch));
                m_measurement_bucket->set_window(lock->measurement_window);
            }
        }

        // Handle state change
        if (m_server_events->server_state_changed.is_signaled())
        {
            m_server_events->server_state_changed.reset();

            HandleServerStateChange();
        }
        else if (m_last_server_state_check + std::chrono::milliseconds(WVB_DRIVER_SERVER_STATE_CHECK_INTERVAL_MS)
            < std::chrono::steady_clock::now())
        {
            // Also check regularly
            HandleServerStateChange();
        }
    }

    void ServerDriver::HandleServerStateChange()
    {
        ServerState new_state;
        {
            auto data = m_shared_memory->lock();
            new_state = data->server_state;
        }

        m_last_server_state_check = std::chrono::steady_clock::now();

        bool should_exit = false;

        if (new_state == ServerState::AWAITING_DRIVER_MEASUREMENTS)
        {
            m_logger->log("Sending measurements to server");

            // Server finished its window and is collecting measurements
            // We need to stop measuring if not already done, and send the measurements to the server
            m_measurement_bucket->reset_window();
            {
                auto data = m_shared_memory->lock();
                if (!data.is_valid())
                {
                    m_logger->debug_log("Can't lock shared memory");
                    return;
                }

                // Fill in the measurements
                const auto &frame_time_measurements = m_measurement_bucket->get_frame_time_measurements();
                data->frame_time_measurements_count = std::min(static_cast<uint32_t>(frame_time_measurements.size()),
                                                               static_cast<uint32_t>(WVB_BENCHMARK_TIMING_PHASE_CAPACITY));
                for (uint32_t i = 0; i < data->frame_time_measurements_count; i++)
                {
                    data->frame_time_measurements[i] = frame_time_measurements[i];
                }

                const auto &tracking_measurements      = m_measurement_bucket->get_tracking_measurements();
                data->tracking_time_measurements_count = std::min(static_cast<uint32_t>(tracking_measurements.size()),
                                                                  static_cast<uint32_t>(WVB_BENCHMARK_TIMING_PHASE_CAPACITY));
                for (uint32_t i = 0; i < data->tracking_time_measurements_count; i++)
                {
                    data->tracking_time_measurements[i] = tracking_measurements[i];
                }

                const auto &pose_access_measurements      = m_measurement_bucket->get_pose_access_measurements();
                data->pose_access_time_measurements_count = std::min(static_cast<uint32_t>(pose_access_measurements.size()),
                                                                     static_cast<uint32_t>(WVB_BENCHMARK_TIMING_PHASE_CAPACITY));
                for (uint32_t i = 0; i < data->pose_access_time_measurements_count; i++)
                {
                    data->pose_access_time_measurements[i] = pose_access_measurements[i];
                }

                data->driver_state = DriverState::READY; // Stop spamming server with frame it will not use
            }

            m_driver_events->new_measurements.signal();
            m_driver_events->driver_state_changed.signal();
        }

        // Check if the server is still alive
        else if (new_state == ServerState::NOT_RUNNING)
        {
            m_logger->debug_log("Server is not running, exiting");
            should_exit = true;
        }
        else if (new_state == ServerState::AWAITING_CONNECTION)
        {
            m_logger->debug_log("Driver should be started only when a client is connected, exiting");
            should_exit = true;
        }
        else if (new_state == ServerState::PROCESSING_MEASUREMENTS)
        {
            m_logger->debug_log("Server received measurements and doesn't need the driver anymore, exiting");
            should_exit = true;
        }

        if (should_exit)
        {
            // Send exit event

            if (m_device_driver != nullptr)
            {
                m_logger->debug_log("Sending stop signal to device driver");
                m_device_driver->SendStopSignal();
            }
            else if (m_shutdown_device_driver != nullptr)
            {
                m_logger->debug_log("Sending stop signal to shutdown device driver");
                m_shutdown_device_driver->SendStopSignal();
            }
        }
    }

    void ServerDriver::EnterStandby()
    {
        m_logger->log("Server driver entered standby");
    }

    void ServerDriver::LeaveStandby()
    {
        m_logger->log("Server driver left standby");
    }
} // namespace wvb::driver
  // namespace wvb::driver