/**
 * Shared state between the server and the driver.
 */

#pragma once

#include <wvb_common/benchmark.h>
#include <wvb_common/ipc.h>
#include <wvb_common/vr_structs.h>

namespace wvb
{

#define WVB_SERVER_DRIVER_MEMORY_NAME "WVB_SERVER_DRIVER_MEMORY"
#define WVB_SERVER_DRIVER_MUTEX_NAME  "WVB_SERVER_DRIVER_MUTEX"

#define WVB_EVENT_SERVER_STATE_CHANGED      "WVB_EVENT_SERVER_STATE_CHANGED"
#define WVB_EVENT_SERVER_SESSION_CREATED    "WVB_EVENT_SERVER_SESSION_CREATED"
#define WVB_EVENT_SERVER_FRAME_FINISHED     "WVB_EVENT_SERVER_FRAME_FINISHED"
#define WVB_EVENT_SERVER_NEW_TRACKING_DATA  "WVB_EVENT_SERVER_NEW_TRACKING_DATA"
#define WVB_EVENT_SERVER_NEW_BENCHMARK_DATA "WVB_EVENT_SERVER_NEW_BENCHMARK_DATA"

#define WVB_EVENT_DRIVER_STATE_CHANGED    "WVB_EVENT_DRIVER_STATE_CHANGED"
#define WVB_EVENT_DRIVER_NEW_PRESENT_INFO "WVB_EVENT_DRIVER_NEW_PRESENT_INFO"
#define WVB_EVENT_DRIVER_NEW_MEASUREMENTS "WVB_EVENT_DRIVER_NEW_MEASUREMENTS"

    // =======================================================================================
    // =                                     Shared state                                    =
    // =======================================================================================

    enum ServerState : uint32_t
    {
        /** The server app is entirely stopped. */
        NOT_RUNNING = 0,
        /** The server app is started and listening for a client. */
        AWAITING_CONNECTION = 1,
        /** The server app is connected to a client and has set the VR system specs in the shared memory. It is ready for the driver to
           start. */
        READY = 2,
        /** The server is running */
        RUNNING = 3,
        /** Measurements are finished and the server is awaiting the driver's measurements. */
        AWAITING_DRIVER_MEASUREMENTS = 4,
        /** The server received the driver measurements, but is still busy before the next run. The driver can quit. */
        PROCESSING_MEASUREMENTS = 5,
    };

    enum class DriverState : uint32_t
    {
        /** The driver is entirely stopped. */
        NOT_RUNNING = 0,
        /** The driver is started and waiting for the server to set the VR system specs in the shared memory. */
        AWAITING_CLIENT_SPEC = 2,
        /** The driver is set up, but no VR content is currently running. */
        READY = 3,
        /** The driver is running, actively receiving frames from SteamVR */
        RUNNING = 4,
        /** The driver is running, but the VR content is paused. */
        STANDBY = 5,
    };

    // Frame data passed by SteamVR. Synchronization with the driver is required, in order to prevent SteamVR from using the handle
    // while it is still in use.
    struct OpenVRPresentInfo
    {
        SharedTextureHandle backbuffer_texture_handle = 0;
        uint64_t            frame_id                  = 0;
        double              vsync_time_in_seconds     = 0;
        /** Nb of 90KHz ticks since RTP epoch */
        uint32_t sample_rtp_timestamp = 0;
        uint32_t pose_rtp_timestamp   = 0;
    };

    struct ServerDriverSharedData
    {
        // Set by driver, read by server
        DriverState                 driver_state = DriverState::NOT_RUNNING;
        OpenVRPresentInfo           latest_present_info {};
        uint32_t                    frame_time_measurements_count                                      = 0;
        uint32_t                    tracking_time_measurements_count                                   = 0;
        uint32_t                    pose_access_time_measurements_count                                = 0;
        DriverFrameTimeMeasurements frame_time_measurements[WVB_BENCHMARK_TIMING_PHASE_CAPACITY]       = {0};
        TrackingTimeMeasurements    tracking_time_measurements[WVB_BENCHMARK_TIMING_PHASE_CAPACITY]    = {0};
        PoseAccessTimeMeasurements  pose_access_time_measurements[WVB_BENCHMARK_TIMING_PHASE_CAPACITY] = {0};

        // Set by server, read by driver
        ServerState server_state = ServerState::NOT_RUNNING;
        /** Offset of ticks applied to the RTP timestamp */
        uint32_t rtp_offset = 0;
        /** NTP timestamp of RTP epoch (nb of seconds since 1/1/1900) */
        uint64_t          ntp_epoch = 0;
        VRSystemSpecs     vr_system_specs {};
        TrackingState     tracking_state {};
        MeasurementWindow measurement_window {};
    };

    typedef SharedMemory<ServerDriverSharedData> ServerDriverSharedMemory;

    // =======================================================================================
    // =                                       Events                                        =
    // =======================================================================================

    // Driver events

    enum class DriverEvent
    {
        NO_EVENT             = 0,
        DRIVER_STATE_CHANGED = 1,
        NEW_PRESENT_INFO     = 2,
        NEW_MEASUREMENTS     = 3,
    };

    struct DriverEvents
    {
        InterProcessEvent driver_state_changed;
        InterProcessEvent new_present_info;
        InterProcessEvent new_measurements;

        explicit DriverEvents(bool is_driver);

        /** Return true if an event was triggered. Said event is set in the event parameter. */
        [[nodiscard]] bool poll(DriverEvent &event) const;
    };

    // Server events

    enum class ServerEvent
    {
        NO_EVENT             = 0,
        SERVER_STATE_CHANGED = 1,
        NEW_SYSTEM_SPECS     = 2,
        FRAME_FINISHED       = 3,
        NEW_TRACKING_DATA    = 4,
        NEW_BENCHMARK_DATA   = 5,
    };

    struct ServerEvents
    {
        InterProcessEvent server_state_changed;
        InterProcessEvent new_system_specs;
        InterProcessEvent frame_finished;
        InterProcessEvent new_tracking_data;
        InterProcessEvent new_benchmark_data;

        explicit ServerEvents(bool is_server);

        /** Return true if an event was triggered. Said event is set in the event parameter. */
        [[nodiscard]] bool poll(ServerEvent &event) const;
    };
} // namespace wvb