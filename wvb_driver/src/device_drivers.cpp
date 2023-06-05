#include "wvb_driver/device_drivers.h"

#include <wvb_common/rtp_clock.h>
#include <wvb_driver/global_state.h>

#include <cstring>
#include <fstream>
// #include <wvb_driver/room_setup.h>
#include <thread>
#include <utility>

#ifdef __linux__
#include <cmath>
#include <cstring>
#endif

#define PI                    3.14159265358979323846f
#define ACTIVE_WAIT_MARGIN_US 2000
#define FPS_MARGIN            0.00f
#define WAIT_MARGIN_OFFSET_US 3000

namespace wvb::driver
{
    ShutdownDeviceDriver::ShutdownDeviceDriver() = default;
    vr::EVRInitError ShutdownDeviceDriver::Activate(uint32_t object_id)
    {
        m_object_id = object_id;

        return vr::VRInitError_None;
    }
    void ShutdownDeviceDriver::SendStopSignal()
    {
        vr::VRServerDriverHost()->VendorSpecificEvent(m_object_id, vr::VREvent_DriverRequestedQuit, {0, 0}, 0);
    }

    VirtualHMDDriver::VirtualHMDDriver(VRSystemSpecs                           &&specs,
                                       std::shared_ptr<DriverLogger>             logger,
                                       std::shared_ptr<ServerDriverSharedMemory> shared_memory,
                                       std::shared_ptr<ServerEvents>             server_events,
                                       std::shared_ptr<DriverEvents>             driver_events,
                                       std::shared_ptr<DriverMeasurementBucket>  measurement_bucket)
        : m_specs(std::move(specs)),
          m_logger(std::move(logger)),
          // We can use a shared pointer to the shared memory used by the server driver because they run in the same thread. Each
          // thread should have its own instance of the shared memory.
          m_shared_memory(std::move(shared_memory)),
          m_server_events(std::move(server_events)),
          m_driver_events(std::move(driver_events)),
          m_measurement_bucket(std::move(measurement_bucket))
    {
        m_logger->debug_log("Initializing Virtual HMD Driver");

        {
            auto lock = m_shared_memory->lock();
            m_rtp_clock.set_epoch(lock->ntp_epoch);
        }
    }

    vr::EVRInitError VirtualHMDDriver::Activate(uint32_t object_id)
    {
        m_logger->debug_log("Activating Virtual HMD Driver");

        // Save object id
        m_object_id = object_id;

        // Setup properties
        auto prop_container = vr::VRProperties()->TrackedDeviceToPropertyContainer(object_id);

        // Name information
        vr::VRProperties()->SetStringProperty(prop_container, vr::Prop_ModelNumber_String, WVB_VIRTUAL_DEVICE_MODEL_NUMBER);
        vr::VRProperties()->SetStringProperty(prop_container, vr::Prop_SerialNumber_String, WVB_VIRTUAL_DEVICE_SERIAL_NUMBER);
        vr::VRProperties()->SetStringProperty(prop_container, vr::Prop_RenderModelName_String, WVB_VIRTUAL_DEVICE_RENDER_MODEL_NAME);
        vr::VRProperties()->SetStringProperty(prop_container, vr::Prop_ManufacturerName_String, m_specs.manufacturer_name.c_str());

        // General settings
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_WillDriftInYaw_Bool, false);
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_DeviceIsWireless_Bool, true);
        // TODO possible to get this data from headset
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_DeviceIsCharging_Bool, false);
        vr::VRProperties()->SetFloatProperty(prop_container, vr::Prop_DeviceBatteryPercentage_Float, 1.0f);
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_DeviceProvidesBatteryStatus_Bool, false);
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_DeviceCanPowerOff_Bool, true);
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_Firmware_UpdateAvailable_Bool, false);
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_Firmware_ManualUpdate_Bool, false);
        vr::VRProperties()->SetStringProperty(prop_container, vr::Prop_Firmware_ManualUpdateURL_String, "");
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_HasCamera_Bool, false);
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_Firmware_ForceUpdateRequired_Bool, false);
        vr::VRProperties()->SetUint64Property(prop_container, vr::Prop_CurrentUniverseId_Uint64, WVB_UNIVERSE_ID);
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_IsOnDesktop_Bool, false);
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_DisplaySuppressed_Bool, false);
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_DisplayAllowNightMode_Bool, false);
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_ReportsTimeSinceVSync_Bool, true);
        vr::VRProperties()->SetStringProperty(prop_container, vr::Prop_DriverVersion_String, WVB_VERSION);
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_ContainsProximitySensor_Bool, false);
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_BlockServerShutdown_Bool, false);
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_CanUnifyCoordinateSystemWithHmd_Bool, true);
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_DriverProvidedChaperoneVisibility_Bool, true);

        // Tracking settings
        vr::VRProperties()->SetFloatProperty(prop_container, vr::Prop_UserIpdMeters_Float, m_specs.ipd);
        vr::VRProperties()->SetFloatProperty(prop_container, vr::Prop_UserHeadToEyeDepthMeters_Float, m_specs.eye_to_head_distance);
        vr::VRProperties()->SetFloatProperty(prop_container, vr::Prop_DisplayFrequency_Float, m_specs.refresh_rate.to_float());
        vr::VRProperties()->SetFloatProperty(prop_container,
                                             vr::Prop_SecondsFromVsyncToPhotons_Float,
                                             0.01f); // TODO possible to get this data from headset

        // Disable motion smoothing (already done in headset driver)
        vr::VRSettings()->SetBool(vr::k_pch_SteamVR_Section, vr::k_pch_SteamVR_MotionSmoothing_Bool, false);

        // We handle our room setup, don't let SteamVR do it
        vr::VRProperties()->SetBoolProperty(prop_container, vr::Prop_HasSpatialAnchorsSupport_Bool, false);

        // The reference space is a STAGE space, with the origin at the center of the play area, at the floor level.
        // The bounds of the rectangle are specs.world_bounds
        // Save chaperone data
        // room_setup(m_specs.world_bounds);

        // Init pose
        m_pose                   = {0};
        m_pose.poseIsValid       = true;
        m_pose.result            = vr::TrackingResult_Running_OK;
        m_pose.deviceIsConnected = true;

        m_pose.vecPosition[0]             = 0;
        m_pose.vecPosition[1]             = 1.7;
        m_pose.vecPosition[2]             = 0;
        m_pose.qWorldFromDriverRotation.w = 1;
        m_pose.qDriverFromHeadRotation.w  = 1;

        // Vsync init
        m_start_time = std::chrono::high_resolution_clock::now();
        // Remove interval from start time to get last vsync
        m_last_vsync_time = m_start_time - std::chrono::microseconds(GetFrameIntervalUs());

        m_event_thread_running = true;
        m_logger->log("Starting event thread");
        m_event_thread = std::thread(&VirtualHMDDriver::EventThread, this);

        // Signal that the app is running
        {
            auto lock = m_shared_memory->lock();
            if (lock.is_valid())
            {
                lock->driver_state = DriverState::RUNNING;
            }
        }
        m_driver_events->driver_state_changed.signal();

        return vr::VRInitError_None;
    }

    void VirtualHMDDriver::Deactivate()
    {
        m_event_thread_running = false;
        m_event_thread.join();

        m_logger->debug_log("Deactivating Virtual HMD Driver");

        m_object_id = vr::k_unTrackedDeviceIndexInvalid;

        // Signal that the app is not running
        {
            auto lock = m_shared_memory->lock();
            if (lock.is_valid())
            {
                lock->driver_state = DriverState::READY;
            }
        }
        m_driver_events->driver_state_changed.signal();
    }

    void VirtualHMDDriver::EnterStandby()
    {
        m_logger->debug_log("Entering standby mode for Virtual HMD Driver");
    }

    vr::DriverPose_t VirtualHMDDriver::GetPose()
    {
        std::lock_guard<std::mutex> lock(m_pose_mutex);

        m_latest_accessed_pose_timestamp = m_latest_pose_timestamp;
        m_measurement_bucket->add_pose_access_measurement({
            .pose_timestamp          = m_latest_pose_timestamp,
            .pose_accessed_timestamp = m_rtp_clock.now_rtp_timestamp(),
        });

        return m_pose;
    }

    void *VirtualHMDDriver::GetComponent(const char *component_name_and_version)
    {
        // SteamVR might try to call this method with component names. Respond to those we support.
        if (strcmp(component_name_and_version, vr::IVRDisplayComponent_Version) == 0)
        {
            return static_cast<vr::IVRDisplayComponent *>(this);
        }
        if (strcmp(component_name_and_version, vr::IVRVirtualDisplay_Version) == 0)
        {
            return static_cast<vr::IVRVirtualDisplay *>(this);
        }
        return nullptr;
    }

    void VirtualHMDDriver::Present(const vr::PresentInfo_t *present_info, uint32_t present_info_size)
    {
        if (present_info == nullptr)
        {
            return;
        }

        m_frame_number                        = present_info->nFrameId;
        m_frame_logged                        = false;
        m_current_frame_measurements.frame_id = m_frame_number;

        const auto expected_next_vsync_time = m_last_vsync_time + std::chrono::microseconds(GetFrameIntervalUs());
        const auto now                      = std::chrono::high_resolution_clock::now();

        m_current_frame_measurements.present_called_timestamp = m_rtp_clock.now_rtp_timestamp();

        // Try to have guess the time the frame took to render, so we can wait less and try to get the Present call closer to the vsync
        if (m_frame_number > 1)
        {
            m_wait_margin_us = std::chrono::duration_cast<std::chrono::microseconds>(now - m_last_wait_time).count() + WAIT_MARGIN_OFFSET_US;
            if (m_frame_number % 100 == 0)
            {
                m_logger->debug_log("Frame %lu: margin %u", m_frame_number, m_wait_margin_us);
            }
        }

        //        if (now < expected_next_vsync_time)
        //        {
        // We are too early, wait until the next vsync
        // If the render time doesn't vary much, the wait time shouldn't be too long
        WaitForVsync(0);
        //        }
        m_last_vsync_time                            = std::chrono::high_resolution_clock::now();
        m_current_frame_measurements.vsync_timestamp = m_rtp_clock.now_rtp_timestamp();

        if (m_frame_number % 100 == 0)
        {
            int64_t delay =
                std::chrono::duration_cast<std::chrono::microseconds>(m_last_vsync_time.time_since_epoch()).count()
                - std::chrono::duration_cast<std::chrono::microseconds>(expected_next_vsync_time.time_since_epoch()).count();
            m_logger->debug_log(
                "Frame %lu: %d us after expected Vsync, waited %d us in Present()",
                m_frame_number,
                static_cast<int>(delay),
                static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(m_last_vsync_time - now).count()));
        }

        // Forward info to server
        {
            auto                        lock = m_shared_memory->lock();
            std::lock_guard<std::mutex> pose_lock(m_pose_mutex);

            lock->latest_present_info = OpenVRPresentInfo {
                .backbuffer_texture_handle = present_info->backbufferTextureHandle,
                .frame_id                  = present_info->nFrameId,
                .vsync_time_in_seconds     = present_info->flVSyncTimeInSeconds,
                .sample_rtp_timestamp      = m_rtp_clock.now_rtp_timestamp(),
                .pose_rtp_timestamp        = m_latest_accessed_pose_timestamp,
            };
        }
        m_driver_events->new_present_info.signal();

        m_current_frame_measurements.frame_sent_timestamp = m_rtp_clock.now_rtp_timestamp();
    }

    void VirtualHMDDriver::WaitForPresent()
    {
        m_current_frame_measurements.wait_for_present_called_timestamp = m_rtp_clock.now_rtp_timestamp();

        // Wait until the server has finished rendering the frame, or until we are told to exit
        uint8_t wait_count = 0;
        while (!m_server_events->frame_finished.wait(WVB_WAIT_TIMEOUT_MS) && !m_should_exit && wait_count < WVB_MAX_WAIT_COUNT)
        {
            wait_count++;
        }

        m_current_frame_measurements.server_finished_timestamp = m_rtp_clock.now_rtp_timestamp();

        //        WaitForVsync(m_wait_margin_us);

        // Compute time left before next vsync
        const auto expected_next_vsync_time = m_last_vsync_time + std::chrono::microseconds(GetFrameIntervalUs());
        const auto now                      = std::chrono::high_resolution_clock::now();
        double     time_left_sec =
            static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(expected_next_vsync_time - now).count())
            / 1000000.0;
        // Add multiples of the frame interval until we are in the future
        while (time_left_sec < 0.0)
        {
            time_left_sec += static_cast<double>(GetFrameIntervalUs()) / 1000000.0;
        }
        // Send VSYNC event
        vr::VRServerDriverHost()->VsyncEvent(time_left_sec);

        // Update pose
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_object_id, GetPose(), sizeof(vr::DriverPose_t));
        // vr::VRServerDriverHost()->SetDisplayProjectionRaw(m_object_id,
        //                                                   {
        //                                                       {tanf(-m_fov[EYE_LEFT].down), tanf(m_fov[EYE_LEFT].right)},
        //                                                       {tanf(-m_fov[EYE_LEFT].up), tanf(m_fov[EYE_LEFT].left)},
        //                                                   },
        //                                                   {
        //                                                       {tanf(-m_fov[EYE_RIGHT].down), tanf(m_fov[EYE_RIGHT].right)},
        //                                                       {tanf(-m_fov[EYE_RIGHT].up), tanf(m_fov[EYE_RIGHT].left)},
        //                                                   });

        m_current_frame_measurements.pose_updated_event_timestamp = m_rtp_clock.now_rtp_timestamp();

        // Save frame time
        m_measurement_bucket->add_frame_time_measurement(m_current_frame_measurements);
    }

    bool VirtualHMDDriver::GetTimeSinceLastVsync(float *seconds_since_last_vsync, uint64_t *frame_counter)
    {
        const auto now = std::chrono::high_resolution_clock::now();

        // Calculate time since last vsync
        *seconds_since_last_vsync =
            static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(now - m_last_vsync_time).count()) / 1000000.0f;
        *frame_counter = m_frame_number;

        return true;
    }

    vr::DistortionCoordinates_t VirtualHMDDriver::ComputeDistortion(vr::EVREye eye, float u, float v)
    {
        // fU and fV are in the range [0,1] and represent the normalized screen coordinates

        vr::DistortionCoordinates_t coordinates = {0};
        // No distortion, remote headset will take care of that
        coordinates.rfBlue[0]  = u;
        coordinates.rfBlue[1]  = v;
        coordinates.rfGreen[0] = u;
        coordinates.rfGreen[1] = v;
        coordinates.rfRed[0]   = u;
        coordinates.rfRed[1]   = v;
        return coordinates;
    }

    void VirtualHMDDriver::GetRecommendedRenderTargetSize(uint32_t *width, uint32_t *height)
    {
        *width  = m_specs.eye_resolution.width * 2;
        *height = m_specs.eye_resolution.height;
    }

    void VirtualHMDDriver::GetEyeOutputViewport(vr::EVREye eye, uint32_t *x, uint32_t *y, uint32_t *width, uint32_t *height)
    {
        *x      = eye == vr::Eye_Left ? 0 : m_specs.eye_resolution.width;
        *y      = 0;
        *width  = m_specs.eye_resolution.width;
        *height = m_specs.eye_resolution.height;
    }

    void VirtualHMDDriver::GetProjectionRaw(vr::EVREye eye, float *left, float *right, float *top, float *bottom)
    {
        *left   = tanf(m_fov[eye].left);
        *right  = tanf(m_fov[eye].right);
        *top    = tanf(-m_fov[eye].up);
        *bottom = tanf(-m_fov[eye].down);
    }

    void VirtualHMDDriver::GetWindowBounds(int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight)
    {
        *pnX      = 0;
        *pnY      = 0;
        *pnWidth  = m_specs.eye_resolution.width * 2;
        *pnHeight = m_specs.eye_resolution.height;
    }

    void VirtualHMDDriver::DebugRequest(const char *request, char *response_buffer, uint32_t response_buffer_size)
    {
        if (response_buffer_size >= 1)
        {
            response_buffer[0] = 0;
        }
    }

    void VirtualHMDDriver::RunFrame()
    {
        if (m_object_id == vr::k_unTrackedDeviceIndexInvalid)
        {
            return;
        }
    }

    uint32_t VirtualHMDDriver::GetFrameIntervalUs() const
    {
        // Add a margin, so that we don't overshoot the vsync
        // Its better to have slightly too long frames than too short
        // Because if there are too many frames, the client won't be able to keep up
        return static_cast<uint32_t>(1000000.f / (m_specs.refresh_rate.to_float() - FPS_MARGIN));
    }

    void VirtualHMDDriver::EventThread()
    {
        TrackingTimeMeasurements tracking_time_measurements {};

        while (m_event_thread_running)
        {
            // Wait for new tracking
            if (m_server_events->new_tracking_data.wait(WVB_WAIT_TIMEOUT_MS))
            {
                tracking_time_measurements.tracking_received_timestamp = m_rtp_clock.now_rtp_timestamp();

                // Read tracking
                auto lock = m_shared_memory->lock();
                if (!lock.is_valid())
                {
                    continue;
                }

                {
                    // Get pose mutex
                    std::lock_guard<std::mutex> pose_lock(m_pose_mutex);

                    // Convert OpenXR pose to OpenVR pose
                    // In OpenXR, each eye is tracked independently, but in OpenVR, the pose is the same for both eyes
                    const auto &orientation = lock->tracking_state.pose.orientation;
                    m_pose.qRotation        = {orientation.w, orientation.x, orientation.y, orientation.z};

                    const auto &position  = lock->tracking_state.pose.position;
                    m_pose.vecPosition[0] = position.x;
                    m_pose.vecPosition[1] = position.y;
                    m_pose.vecPosition[2] = position.z;
                    m_fov[EYE_LEFT]       = lock->tracking_state.fov_left;
                    m_fov[EYE_RIGHT]      = lock->tracking_state.fov_right;

                    m_pose.poseTimeOffset       = 0;
                    m_pose.poseIsValid          = true;
                    m_pose.deviceIsConnected    = true;
                    m_pose.willDriftInYaw       = false;
                    m_pose.shouldApplyHeadModel = false;
                    m_pose.result               = vr::TrackingResult_Running_OK;
                    m_latest_pose_timestamp     = lock->tracking_state.pose_timestamp;

                    // Update tracking time measurements
                    tracking_time_measurements.pose_timestamp               = m_latest_pose_timestamp;
                    tracking_time_measurements.tracking_processed_timestamp = m_rtp_clock.now_rtp_timestamp();
                }
            }

            // Save measurements
            m_measurement_bucket->add_tracking_time_measurement(tracking_time_measurements);
        }
    }

    void VirtualHMDDriver::SendStopSignal()
    {
        m_should_exit          = true;
        m_event_thread_running = false;
        vr::VRServerDriverHost()->VendorSpecificEvent(m_object_id, vr::VREvent_DriverRequestedQuit, {0, 0}, 0);
    }

    void VirtualHMDDriver::SendEnterStandbySignal()
    {
        // Pause the app
        vr::VRServerDriverHost()->VendorSpecificEvent(m_object_id, vr::VREvent_EnterStandbyMode, {0, 0}, 0);
    }

    void VirtualHMDDriver::SendLeaveStandbySignal()
    {
        // Resume the app
        vr::VRServerDriverHost()->VendorSpecificEvent(m_object_id, vr::VREvent_LeaveStandbyMode, {0, 0}, 0);
    }

    void VirtualHMDDriver::WaitForVsync(uint32_t margin_us)
    {
        // Wait for VSync
        auto       now      = std::chrono::high_resolution_clock::now();
        auto       elapsed  = std::chrono::duration_cast<std::chrono::microseconds>(now - m_last_vsync_time).count();
        const auto interval = GetFrameIntervalUs();

        // Wait until the next VSync
        const auto should_wait = interval - elapsed - margin_us;
        if (should_wait <= 0)
        {
            return;
        }

        // Sleep for the major part of the wait to save resources, but wake up isn't very precise
        if (should_wait > ACTIVE_WAIT_MARGIN_US)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(should_wait - ACTIVE_WAIT_MARGIN_US));
        }
        // Active wait for the last 2ms to end on the exact desired time
        while (elapsed < interval)
        {
            now     = std::chrono::high_resolution_clock::now();
            elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - m_last_vsync_time).count();
        }

        m_last_wait_time = now;
    }
} // namespace wvb::driver