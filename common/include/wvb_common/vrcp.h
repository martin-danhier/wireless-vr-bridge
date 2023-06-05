/**
 * Data types for VR control protocol used to transmit data message in binary format.
 */

#pragma once

#include "benchmark.h"
#include <wvb_common/vr_structs.h>

#include <cstdint>

namespace wvb::vrcp
{

    // =============================================================
    // =                       Data types                          =
    // =============================================================

#define VRCP_VERSION                    1
#define VRCP_MAGIC                      0x4D
#define VRCP_DEFAULT_ADVERTISEMENT_PORT 7672
#define VRCP_ROW_SIZE                   4

    /** Field type for the VR Control Protocol */
    enum class VRCPFieldType : uint8_t
    {
        // MSB set to 0: built-in static length field

        INVALID = 0b00000000,
        // Client starts by sending connection request with device specs
        CONN_REQ = 0b00000001,
        // Server can either accept of reject the connection
        CONN_ACCEPT = 0b00000010,
        CONN_REJECT = 0b00000011,

        // Input, no value
        INPUT_DATA    = 0b00000100,
        TRACKING_DATA = 0x05,

        // TLV subfields for CONN_REQ
        MANUFACTURER_NAME_TLV      = 0x09,
        SYSTEM_NAME_TLV            = 0x0A,
        SUPPORTED_VIDEO_CODECS_TLV = 0x0B,
        CHOSEN_VIDEO_CODEC_TLV     = 0x0C,

        // Synchronization
        PING          = 0x10,
        PING_REPLY    = 0x11,
        SYNC_FINISHED = 0x12, // Sync finished

        // Benchmarking
        BENCHMARK_INFO                = 0x20,
        MEASUREMENT_TRANSFER_FINISHED = 0x21,
        FRAME_TIME_MEASUREMENT        = 0x22,
        IMAGE_QUALITY_MEASUREMENT     = 0x23,
        TRACKING_TIME_MEASUREMENT     = 0x24,
        NETWORK_MEASUREMENT           = 0x25,
        SOCKET_MEASUREMENT            = 0x26,
        NEXT_PASS                     = 0x27,
        FRAME_CAPTURE_FRAGMENT        = 0x28,

        // Server advertisement broadcasted when no one is connected
        SERVER_ADVERTISEMENT = 0x70,

        // MSB set to 1: user TLV field, m_packet extracted then given to user
        USER_DATA = 0b10000000,
    };

    enum class VRCPVideoMode
    {
        UDP = 0,
        TCP = 1,
    };

    // Helper functions for type
    inline bool is_user_field(VRCPFieldType type)
    {
        return (static_cast<uint8_t>(type) & 0b10000000) != 0;
    }
    constexpr VRCPFieldType to_ft(uint8_t value)
    {
        return static_cast<VRCPFieldType>(value);
    }
    constexpr uint8_t to_u8(VRCPFieldType value)
    {
        return static_cast<uint8_t>(value);
    }
    constexpr uint8_t to_u8(VRCPVideoMode value)
    {
        return static_cast<uint8_t>(value);
    }

    // Input id
    enum class VRCPInputId : uint8_t
    {
        INVALID = 0,
        // 1-7 reserved for built-in inputs

        // 8-127 reserved for user inputs
    };

    // Reject reason
    enum class VRCPRejectReason : uint8_t
    {
        NONE               = 0,
        GENERIC_ERROR      = 1,
        VERSION_MISMATCH   = 2,
        INVALID_VRCP_PORT  = 3,
        INVALID_VIDEO_PORT = 4,
        // Invalid specs details
        INVALID_EYE_SIZE          = 5,
        INVALID_REFRESH_RATE      = 6,
        INVALID_MANUFACTURER_NAME = 7,
        INVALID_SYSTEM_NAME       = 8,
        INVALID_VIDEO_CODECS      = 9,
        NO_SUPPORTED_VIDEO_CODEC  = 10,
        VIDEO_MODE_MISMATCH       = 11,
        INVALID_NTP_TIMESTAMP     = 12,
    };

#pragma pack(push, 1)
    /** Can be casted on all VRCP packets to read common fields, such as the type. */
    struct VRCPBaseHeader
    {
        VRCPFieldType ftype = VRCPFieldType::INVALID;
        // Number of 32 bits rows. Must be at least 1.
        uint8_t                  n_rows = 1;
        [[maybe_unused]] uint8_t _reserved[2] {0, 0};
    };
    static_assert(sizeof(VRCPBaseHeader) == 4 * VRCPBaseHeader {}.n_rows, "Size must be 4 * n_rows");

    struct VRCPAdditionalField
    {
        VRCPFieldType type;
        uint8_t       length;
        uint8_t       value[];
    };

    /**
     * Sent by the client to the server at the beginning of the TCP connection.
     * Used to share device specifications and other settings that will be used
     * during the connection.
     * */
    struct VRCPConnectionRequest
    {
        VRCPFieldType ftype         = VRCPFieldType::CONN_REQ;
        uint8_t       n_rows        = 10;
        uint8_t       version       = VRCP_VERSION;
        uint8_t       video_mode    = to_u8(VRCPVideoMode::UDP);
        uint16_t      udp_vrcp_port = 0; // UDP port to use for real-time control and input data
        uint16_t      video_port    = 0; // Port to use for real-time video data
        // Device specs
        uint16_t eye_width                = 0;
        uint16_t eye_height               = 0;
        uint16_t refresh_rate_numerator   = 0;
        uint16_t refresh_rate_denominator = 0;
        uint32_t ipd                      = 0;
        uint32_t eye_to_head_distance     = 0;
        uint32_t world_bounds_width       = 0;
        uint32_t world_bounds_height      = 0;
        // NTP timestamp of the server
        uint64_t ntp_timestamp = 0;
    };
    static_assert(sizeof(VRCPConnectionRequest) == VRCP_ROW_SIZE *VRCPConnectionRequest {}.n_rows, "Size must be 4 * n_rows");

    /**
     * Replied by the server if it is able to start the connection with the user using the specified
     * specifications.
     * Also used to share some encodings settings (codecs, parameters, etc.)
     */
    struct VRCPConnectionAccept
    {
        VRCPFieldType            ftype         = VRCPFieldType::CONN_ACCEPT;
        uint8_t                  n_rows        = 2;
        [[maybe_unused]] uint8_t _reserved[2]  = {0};
        uint16_t                 udp_vrcp_port = 0; // UDP port to use for real-time control and input data
        uint16_t                 video_port    = 0; // Port to use for real-time data
    };
    static_assert(sizeof(VRCPConnectionAccept) == VRCP_ROW_SIZE *VRCPConnectionAccept {}.n_rows, "Size must be 4 * n_rows");

    /** Sent by the server if it is unable to start a connection.
     * Contains the reason of this reject, as well as an optional additional info. */
    struct VRCPConnectionReject
    {
        VRCPFieldType    ftype  = VRCPFieldType::CONN_REJECT;
        uint8_t          n_rows = 1;
        VRCPRejectReason reason = VRCPRejectReason::NONE;
        uint8_t          data   = 0; // Optional value for additional info
    };
    static_assert(sizeof(VRCPConnectionReject) == VRCP_ROW_SIZE *VRCPConnectionReject {}.n_rows, "Size must be 4 * n_rows");

    /** Sent by the client when it wants to send input data to the server, such as button presses.
     *
     * Variants of this m_packet type are available depending on the payload size
     * (some events are simply enum fields, some other require parameters).
     * */
    struct VRCPInputData
    {
        VRCPFieldType            ftype     = VRCPFieldType::INPUT_DATA;
        uint8_t                  n_rows    = 2;
        VRCPInputId              id        = VRCPInputId::INVALID;
        [[maybe_unused]] uint8_t _reserved = 0;
        uint32_t                 timestamp = 0; // Timestamp of the input)
    };
    static_assert(sizeof(VRCPInputData) == VRCP_ROW_SIZE *VRCPInputData {}.n_rows, "Size must be 4 * n_rows");

    /**
     * Sent by the server to advertise itself on the network.
     */
    struct VRCPServerAdvertisement
    {
        VRCPFieldType ftype   = VRCPFieldType::SERVER_ADVERTISEMENT;
        uint8_t       n_rows  = 3;
        uint8_t       magic   = VRCP_MAGIC;
        uint8_t       version = VRCP_VERSION;

        // TCP port waiting for connection
        uint16_t tcp_port = 0;
        // Interval between two advertisements, in seconds
        uint8_t                  interval  = 0;
        [[maybe_unused]] uint8_t _reserved = 0;

        // When the advert was sent. Combined with interval, allows to see if advert is still valid.
        uint32_t timestamp = 0; // unix timestamp in seconds
    };
    static_assert(sizeof(VRCPServerAdvertisement) == VRCP_ROW_SIZE *VRCPServerAdvertisement {}.n_rows, "Size must be 4 * n_rows");

    /** If the user wants to tunnel data in a VRCP m_packet, this header is added on top of the message.
     * The user can give anything as type field between 0x80 and 0xFF.
     *
     * The n_rows field must be equal to (size/4)+1, the total number of rows, including the header.
     * */
    struct VRCPUserDataHeader
    {
        VRCPFieldType ftype  = VRCPFieldType::USER_DATA;
        uint8_t       n_rows = 1;
        uint16_t      size   = 0; // Size of the data in bytes
    };
    static_assert(sizeof(VRCPUserDataHeader) == VRCP_ROW_SIZE, "Size must be 4");

    struct VRCPTrackingData
    {
        VRCPFieldType            ftype        = VRCPFieldType::TRACKING_DATA;
        uint8_t                  n_rows       = 18;
        [[maybe_unused]] uint8_t _reserved[2] = {0, 0};

        uint32_t sample_timestamp = 0; // Timestamp of the input sample (when measured)
        uint32_t pose_timestamp   = 0; // Timestamp of the input

        // Left eye
        uint32_t left_eye_orientation_x = 0;
        uint32_t left_eye_orientation_y = 0;
        uint32_t left_eye_orientation_z = 0;
        uint32_t left_eye_orientation_w = 0;
        uint32_t left_eye_position_x    = 0;
        uint32_t left_eye_position_y    = 0;
        uint32_t left_eye_position_z    = 0;
        uint32_t left_eye_fov_left      = 0;
        uint32_t left_eye_fov_right     = 0;
        uint32_t left_eye_fov_up        = 0;
        uint32_t left_eye_fov_down      = 0;
        uint32_t right_eye_fov_left     = 0;
        uint32_t right_eye_fov_right    = 0;
        uint32_t right_eye_fov_up       = 0;
        uint32_t right_eye_fov_down     = 0;

        // Helpers
        VRCPTrackingData() = default;
        VRCPTrackingData(const TrackingState &state);

        void to_tracking_state(TrackingState &state) const;
    };
    static_assert(sizeof(VRCPTrackingData) == VRCP_ROW_SIZE *VRCPTrackingData {}.n_rows, "Size must be 4 * n_rows");

    struct VRCPFrameTimeMeasurement
    {
        VRCPFieldType            ftype                          = VRCPFieldType::FRAME_TIME_MEASUREMENT;
        uint8_t                  n_rows                         = 14;
        [[maybe_unused]] uint8_t _reserved[2]                   = {0, 0};
        uint32_t                 frame_index                    = 0;
        uint32_t                 frame_id                       = 0;
        uint32_t                 frame_delay                    = 0;
        uint32_t                 tracking_timestamp             = 0;
        uint32_t                 last_packet_received_timestamp = 0;
        uint32_t                 pushed_to_decoder_timestamp    = 0;
        uint32_t                 begin_wait_frame_timestamp     = 0;
        uint32_t                 begin_frame_timestamp          = 0;
        uint32_t                 after_wait_swapchain_timestamp = 0;
        uint32_t                 after_render_timestamp         = 0;
        uint32_t                 end_frame_timestamp            = 0;
        uint32_t                 predicted_present_timestamp    = 0;
        uint32_t                 pose_timestamp                 = 0;

        // Helpers
        VRCPFrameTimeMeasurement() = default;
        VRCPFrameTimeMeasurement(const ClientFrameTimeMeasurements &frame_time);

        void to_frame_time_measurements(ClientFrameTimeMeasurements &frame_time) const;
    };
    static_assert(sizeof(VRCPFrameTimeMeasurement) == VRCP_ROW_SIZE *VRCPFrameTimeMeasurement {}.n_rows, "Size must be 4 * n_rows");

    struct VRCPImageQualityMeasurement
    {
        VRCPFieldType            ftype           = VRCPFieldType::IMAGE_QUALITY_MEASUREMENT;
        uint8_t                  n_rows          = 5;
        [[maybe_unused]] uint8_t _reserved[2]    = {0, 0};
        uint32_t                 frame_id        = 0;
        uint32_t                 codestream_size = 0;
        uint32_t                 raw_size        = 0;
        uint32_t                 psnr            = 0;

        // Helpers
        VRCPImageQualityMeasurement() = default;
        VRCPImageQualityMeasurement(const ImageQualityMeasurements &image_quality);

        void to_image_quality_measurements(ImageQualityMeasurements &image_quality) const;
    };
    static_assert(sizeof(VRCPImageQualityMeasurement) == VRCP_ROW_SIZE *VRCPImageQualityMeasurement {}.n_rows,
                  "Size must be 4 * n_rows");

    struct VRCPTrackingTimeMeasurement
    {
        VRCPFieldType            ftype                        = VRCPFieldType::TRACKING_TIME_MEASUREMENT;
        uint8_t                  n_rows                       = 4;
        [[maybe_unused]] uint8_t _reserved[2]                 = {0, 0};
        uint32_t                 pose_timestamp               = 0;
        uint32_t                 tracking_received_timestamp  = 0;
        uint32_t                 tracking_processed_timestamp = 0;

        // Helpers
        VRCPTrackingTimeMeasurement() = default;
        VRCPTrackingTimeMeasurement(const TrackingTimeMeasurements &tracking_time);

        void to_tracking_time_measurements(TrackingTimeMeasurements &tracking_time) const;
    };
    static_assert(sizeof(VRCPTrackingTimeMeasurement) == VRCP_ROW_SIZE *VRCPTrackingTimeMeasurement {}.n_rows,
                  "Size must be 4 * n_rows");

    struct VRCPNetworkMeasurement
    {
        VRCPFieldType            ftype        = VRCPFieldType::NETWORK_MEASUREMENT;
        uint8_t                  n_rows       = 3;
        [[maybe_unused]] uint8_t _reserved[2] = {0, 0};
        uint32_t                 rtt          = 0;
        uint32_t                 clock_error  = 0;

        // Helpers
        VRCPNetworkMeasurement() = default;
        VRCPNetworkMeasurement(const NetworkMeasurements &network_measurements);

        void to_network_measurements(NetworkMeasurements &network_measurements) const;
    };
    static_assert(sizeof(VRCPNetworkMeasurement) == VRCP_ROW_SIZE *VRCPNetworkMeasurement {}.n_rows, "Size must be 4 * n_rows");

    struct VRCPSocketMeasurement
    {
        VRCPFieldType ftype            = VRCPFieldType::SOCKET_MEASUREMENT;
        uint8_t       n_rows           = 5;
        uint8_t       socket_id        = 0;
        uint8_t       socket_type      = 0;
        uint32_t      bytes_sent       = 0;
        uint32_t      bytes_received   = 0;
        uint32_t      packets_sent     = 0;
        uint32_t      packets_received = 0;

        // Helpers
        VRCPSocketMeasurement() = default;
        VRCPSocketMeasurement(const SocketMeasurements &socket_measurement);

        void to_socket_measurements(SocketMeasurements &socket_measurement) const;
    };
    static_assert(sizeof(VRCPSocketMeasurement) == VRCP_ROW_SIZE *VRCPSocketMeasurement {}.n_rows, "Size must be 4 * n_rows");

    /** Part of the clock synchronization algorithm.
     * Client regularly send this packet to the server.
     * A unique ping_id is used to identify the ping.
     */
    struct VRCPPing
    {
        VRCPFieldType ftype   = VRCPFieldType::PING;
        uint8_t       n_rows  = 1;
        uint16_t      ping_id = 0;
    };
    static_assert(sizeof(VRCPPing) == VRCP_ROW_SIZE *VRCPPing {}.n_rows, "Size must be 4 * n_rows");

    /** Part of the clock synchronization algorithm.
     * Upon receiving a ping, the server replies with this packet as soon as possible with the same ping_id.
     * This will allow computation of the round trip time between the client and the server.
     * Using the reply timestamp, the client can synchronize its clock with the server.
     *
     * Such packets are sent unreliably (to have a more accurate RTT measurement), hence
     * the presence of a ping_id to filter out old packets that didn't receive a reply.
     */
    struct VRCPPingReply
    {
        VRCPFieldType ftype           = VRCPFieldType::PING_REPLY;
        uint8_t       n_rows          = 2;
        uint16_t      ping_id         = 0;
        uint32_t      reply_timestamp = 0;
    };
    static_assert(sizeof(VRCPPingReply) == VRCP_ROW_SIZE *VRCPPingReply {}.n_rows, "Size must be 4 * n_rows");

    struct VRCPSyncFinished
    {
        VRCPFieldType            ftype        = VRCPFieldType::SYNC_FINISHED;
        uint8_t                  n_rows       = 1;
        [[maybe_unused]] uint8_t _reserved[2] = {0, 0};
    };
    static_assert(sizeof(VRCPSyncFinished) == VRCP_ROW_SIZE *VRCPSyncFinished {}.n_rows, "Size must be 4 * n_rows");

    struct VRCPBenchmarkInfo
    {
        VRCPFieldType            ftype                               = VRCPFieldType::BENCHMARK_INFO;
        uint8_t                  n_rows                              = 5;
        [[maybe_unused]] uint8_t _reserved[2]                        = {0, 0};
        uint32_t                 start_timing_phase_timestamp        = 0;
        uint32_t                 start_image_quality_phase_timestamp = 0;
        uint32_t                 end_measurements_timestamp          = 0;
        uint32_t                 end_timestamp                       = 0;

        // Helpers
        VRCPBenchmarkInfo() = default;
        VRCPBenchmarkInfo(const MeasurementWindow &window, const rtp::RTPClock &clock);
        MeasurementWindow to_measurement_window(const rtp::RTPClock &clock) const;
    };
    static_assert(sizeof(VRCPBenchmarkInfo) == VRCP_ROW_SIZE *VRCPBenchmarkInfo {}.n_rows, "Size must be 4 * n_rows");

    struct VRCPMeasurementTransferFinished
    {
        VRCPFieldType ftype  = VRCPFieldType::MEASUREMENT_TRANSFER_FINISHED;
        uint8_t       n_rows = 3;
        // Other standalone misc measurements
        uint8_t                  decoder_frame_delay  = 0;
        [[maybe_unused]] uint8_t _reserved            = 0;
        uint32_t                 nb_dropped_frames    = 0;
        uint32_t                 nb_catched_up_frames = 0;
    };
    static_assert(sizeof(VRCPMeasurementTransferFinished) == VRCP_ROW_SIZE *VRCPMeasurementTransferFinished {}.n_rows,
                  "Size must be 4 * n_rows");

    struct VRCPNextPass
    {
        VRCPFieldType ftype  = VRCPFieldType::NEXT_PASS;
        uint8_t       n_rows = 1;
        uint8_t       pass   = 0;
        uint8_t       run    = 0;
        // TLV field for new codec id
    };
    static_assert(sizeof(VRCPNextPass) == VRCP_ROW_SIZE *VRCPNextPass {}.n_rows, "Size must be 4 * n_rows");

    struct VRCPFrameCaptureFragment
    {
        VRCPFieldType            ftype     = VRCPFieldType::FRAME_CAPTURE_FRAGMENT;
        uint8_t                  n_rows    = 4;
        uint8_t                  last      = 0;
        [[maybe_unused]] uint8_t _reserved = 0;
        uint32_t                 full_size = 0; // Full size of frame
        uint32_t                 offset    = 0; // Start index in the frame
        uint32_t                 size      = 0; // Size in this packet
    };
    static_assert(sizeof(VRCPFrameCaptureFragment) == VRCP_ROW_SIZE *VRCPFrameCaptureFragment {}.n_rows, "Size must be 4 * n_rows");

#pragma pack(pop)

} // namespace wvb::vrcp