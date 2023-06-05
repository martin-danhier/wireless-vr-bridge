#pragma once

#include "driver_logger.h"
#include <wvb_common/benchmark.h>
#include <wvb_common/rtp_clock.h>
#include <wvb_common/server_shared_state.h>
#include <wvb_common/vr_structs.h>

#include <chrono>
#include <memory>
#include <openvr_driver.h>

#include <mutex>

#define WVB_WAIT_TIMEOUT_MS 50
#define WVB_MAX_WAIT_COUNT  10

namespace wvb::driver
{
    /** Dummy device driver that can be used to request SteamVR to shutdown during initialization.
     *
     * The use of this driver is required because a shutdown event can only be sent from a device driver. Usually, if a server driver fails,
     * SteamVR will try to load another available driver. However, for benchmarks, in some cases, we want to stop SteamVR as soon as possible, so that
     * it can be started again when the server and client are ready.
    */
    class ShutdownDeviceDriver : public vr::ITrackedDeviceServerDriver
    {
      private:
        uint32_t m_object_id = vr::k_unTrackedDeviceIndexInvalid;

      public:
        ShutdownDeviceDriver();

        vr::EVRInitError Activate(uint32_t object_id) override;
        void             Deactivate() override {}
        void             EnterStandby() override {}
        void            *GetComponent(const char *component_name_and_version) override { return nullptr; }
        void             DebugRequest(const char *request, char *response_buffer, uint32_t response_buffer_size) override {}
        vr::DriverPose_t GetPose() override { return {}; }

        void SendStopSignal();
    };

    /** Simulates a virtual HMD based on the provided device specifications.
     * The presented frames are forwarded to the server process, and a separate tracking thread waits for new tracking data
     * to minimize latency. */
    class VirtualHMDDriver : public vr::ITrackedDeviceServerDriver, public vr::IVRDisplayComponent, public vr::IVRVirtualDisplay
    {
      private:
        std::shared_ptr<DriverLogger> m_logger      = nullptr;
        std::atomic<bool>             m_should_exit = false;

        // Benchmark
        std::shared_ptr<DriverMeasurementBucket> m_measurement_bucket         = nullptr;
        DriverFrameTimeMeasurements              m_current_frame_measurements = {};

        uint64_t                                       m_frame_number    = 0;
        std::chrono::high_resolution_clock::time_point m_last_vsync_time = {};
        std::chrono::high_resolution_clock::time_point m_last_wait_time  = {};
        uint32_t                                       m_wait_margin_us  = 0;

        VRSystemSpecs                                  m_specs                          = {};
        vr::DriverPose_t                               m_pose                           = {};
        Fov                                            m_fov[NB_EYES]                   = {};
        uint32_t                                       m_latest_pose_timestamp          = 0;
        uint32_t                                       m_latest_accessed_pose_timestamp = 0;
        std::chrono::high_resolution_clock::time_point m_start_time                     = {};
        bool                                           m_frame_logged                   = false;

        std::shared_ptr<ServerDriverSharedMemory> m_shared_memory = nullptr;
        std::shared_ptr<ServerEvents>             m_server_events = nullptr;
        std::shared_ptr<DriverEvents>             m_driver_events = nullptr;
        uint32_t                                  m_object_id     = vr::k_unTrackedDeviceIndexInvalid;
        wvb::rtp::RTPClock                        m_rtp_clock;

        std::atomic<bool> m_event_thread_running = false;
        std::thread       m_event_thread         = {};
        std::mutex        m_pose_mutex           = {};

        std::chrono::high_resolution_clock::time_point m_last_log         = std::chrono::high_resolution_clock::now();
        uint32_t                                       m_nb_pose_accesses = 0;

      public:
        VirtualHMDDriver(VRSystemSpecs                           &&specs,
                         std::shared_ptr<DriverLogger>             logger,
                         std::shared_ptr<ServerDriverSharedMemory> shared_memory,
                         std::shared_ptr<ServerEvents>             server_events,
                         std::shared_ptr<DriverEvents>             driver_events,
                         std::shared_ptr<DriverMeasurementBucket>  measurement_bucket);

        // ITrackedDeviceServerDriver
        vr::EVRInitError Activate(uint32_t object_id) override;
        void             Deactivate() override;
        void             EnterStandby() override;

        vr::DriverPose_t GetPose() override;
        void            *GetComponent(const char *component_name_and_version) override;
        void             DebugRequest(const char *request, char *response_buffer, uint32_t response_buffer_size) override;

        // IVRVirtualDisplay
        void Present(const vr::PresentInfo_t *present_info, uint32_t present_info_size) override;
        void WaitForPresent() override;
        bool GetTimeSinceLastVsync(float *seconds_since_last_vsync, uint64_t *frame_counter) override;

        // IVRDisplayComponent
        void GetWindowBounds(int32_t *pnX, int32_t *pnY, uint32_t *pnWidth, uint32_t *pnHeight) override;
        bool IsDisplayOnDesktop() override { return false; }
        bool IsDisplayRealDisplay() override { return false; }
        void GetRecommendedRenderTargetSize(uint32_t *width, uint32_t *height) override;
        void GetEyeOutputViewport(vr::EVREye eye, uint32_t *x, uint32_t *y, uint32_t *width, uint32_t *height) override;
        void GetProjectionRaw(vr::EVREye eye, float *left, float *right, float *top, float *bottom) override;
        vr::DistortionCoordinates_t ComputeDistortion(vr::EVREye eye, float u, float v) override;

        void RunFrame();

        // Other
        [[nodiscard]] bool     IsValid() { return true; }
        [[nodiscard]] uint32_t GetFrameIntervalUs() const;
        void                   EventThread();
        /** Set the app in quitting state. Threads will stop as soon as possible. */
        void SendStopSignal();
        void SendEnterStandbySignal();
        void SendLeaveStandbySignal();
        /** Sleep until the next vsync time - margin_us. */
        void WaitForVsync(uint32_t margin_us);
    };
} // namespace wvb::driver