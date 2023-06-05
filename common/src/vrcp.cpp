#include "wvb_common/vrcp.h"

#include <wvb_common/network_utils.h>

namespace wvb::vrcp
{
    VRCPTrackingData::VRCPTrackingData(const TrackingState &state)
        : sample_timestamp(htonl(state.sample_timestamp)),
          pose_timestamp(htonl(state.pose_timestamp)),
          left_eye_orientation_x(htonf(state.pose.orientation.x)),
          left_eye_orientation_y(htonf(state.pose.orientation.y)),
          left_eye_orientation_z(htonf(state.pose.orientation.z)),
          left_eye_orientation_w(htonf(state.pose.orientation.w)),
          left_eye_position_x(htonf(state.pose.position.x)),
          left_eye_position_y(htonf(state.pose.position.y)),
          left_eye_position_z(htonf(state.pose.position.z)),
          left_eye_fov_left(htonf(state.fov_left.left)),
          left_eye_fov_right(htonf(state.fov_left.right)),
          left_eye_fov_up(htonf(state.fov_left.up)),
          left_eye_fov_down(htonf(state.fov_left.down)),
          right_eye_fov_left(htonf(state.fov_right.left)),
          right_eye_fov_right(htonf(state.fov_right.right)),
          right_eye_fov_up(htonf(state.fov_right.up)),
          right_eye_fov_down(htonf(state.fov_right.down))
    {
    }

    void VRCPTrackingData::to_tracking_state(TrackingState &state) const
    {
        state.sample_timestamp   = ntohl(sample_timestamp);
        state.pose_timestamp     = ntohl(pose_timestamp);
        state.pose.orientation.x = ntohf(left_eye_orientation_x);
        state.pose.orientation.y = ntohf(left_eye_orientation_y);
        state.pose.orientation.z = ntohf(left_eye_orientation_z);
        state.pose.orientation.w = ntohf(left_eye_orientation_w);
        state.pose.position.x    = ntohf(left_eye_position_x);
        state.pose.position.y    = ntohf(left_eye_position_y);
        state.pose.position.z    = ntohf(left_eye_position_z);
        state.fov_left.left      = ntohf(left_eye_fov_left);
        state.fov_left.right     = ntohf(left_eye_fov_right);
        state.fov_left.up        = ntohf(left_eye_fov_up);
        state.fov_left.down      = ntohf(left_eye_fov_down);
        state.fov_right.left     = ntohf(right_eye_fov_left);
        state.fov_right.right    = ntohf(right_eye_fov_right);
        state.fov_right.up       = ntohf(right_eye_fov_up);
        state.fov_right.down     = ntohf(right_eye_fov_down);
    }

    VRCPFrameTimeMeasurement::VRCPFrameTimeMeasurement(const ClientFrameTimeMeasurements &frame_time)
        : frame_index(htonl(frame_time.frame_index)),
          frame_id(htonl(frame_time.frame_id)),
          frame_delay(htonl(frame_time.frame_delay)),
          tracking_timestamp(htonl(frame_time.tracking_timestamp)),
          last_packet_received_timestamp(htonl(frame_time.last_packet_received_timestamp)),
          pushed_to_decoder_timestamp(htonl(frame_time.pushed_to_decoder_timestamp)),
          begin_wait_frame_timestamp(htonl(frame_time.begin_wait_frame_timestamp)),
          begin_frame_timestamp(htonl(frame_time.begin_frame_timestamp)),
          after_wait_swapchain_timestamp(htonl(frame_time.after_wait_swapchain_timestamp)),
          after_render_timestamp(htonl(frame_time.after_render_timestamp)),
          end_frame_timestamp(htonl(frame_time.end_frame_timestamp)),
          predicted_present_timestamp(htonl(frame_time.predicted_present_timestamp)),
          pose_timestamp(htonl(frame_time.pose_timestamp))
    {
    }

    void VRCPFrameTimeMeasurement::to_frame_time_measurements(ClientFrameTimeMeasurements &frame_time) const
    {
        frame_time.frame_index                    = ntohl(frame_index);
        frame_time.frame_id                       = ntohl(frame_id);
        frame_time.frame_delay                    = ntohl(frame_delay);
        frame_time.tracking_timestamp             = ntohl(tracking_timestamp);
        frame_time.last_packet_received_timestamp = ntohl(last_packet_received_timestamp);
        frame_time.pushed_to_decoder_timestamp    = ntohl(pushed_to_decoder_timestamp);
        frame_time.begin_wait_frame_timestamp     = ntohl(begin_wait_frame_timestamp);
        frame_time.begin_frame_timestamp          = ntohl(begin_frame_timestamp);
        frame_time.after_wait_swapchain_timestamp = ntohl(after_wait_swapchain_timestamp);
        frame_time.after_render_timestamp         = ntohl(after_render_timestamp);
        frame_time.end_frame_timestamp            = ntohl(end_frame_timestamp);
        frame_time.predicted_present_timestamp    = ntohl(predicted_present_timestamp);
        frame_time.pose_timestamp                 = ntohl(pose_timestamp);
    }

    VRCPBenchmarkInfo::VRCPBenchmarkInfo(const MeasurementWindow &window, const rtp::RTPClock &clock)
        : start_timing_phase_timestamp(htonl(clock.to_rtp_timestamp(window.start_timing_phase))),
          start_image_quality_phase_timestamp(htonl(clock.to_rtp_timestamp(window.start_image_quality_phase))),
          end_measurements_timestamp(htonl(clock.to_rtp_timestamp(window.end_measurements))),
          end_timestamp(htonl(clock.to_rtp_timestamp(window.end)))
    {
    }

    MeasurementWindow VRCPBenchmarkInfo::to_measurement_window(const rtp::RTPClock &clock) const
    {
        MeasurementWindow window;
        window.start_timing_phase        = clock.from_rtp_timestamp(ntohl(start_timing_phase_timestamp));
        window.start_image_quality_phase = clock.from_rtp_timestamp(ntohl(start_image_quality_phase_timestamp));
        window.end_measurements          = clock.from_rtp_timestamp(ntohl(end_measurements_timestamp));
        window.end                       = clock.from_rtp_timestamp(ntohl(end_timestamp));
        return window;
    }

    VRCPImageQualityMeasurement::VRCPImageQualityMeasurement(const ImageQualityMeasurements &image_quality)
        : frame_id(htonl(image_quality.frame_id)),
          codestream_size(htonl(image_quality.codestream_size)),
          raw_size(htonl(image_quality.raw_size)),
          psnr(htonf(image_quality.psnr))
    {
    }

    void VRCPImageQualityMeasurement::to_image_quality_measurements(ImageQualityMeasurements &image_quality) const
    {
        image_quality.frame_id        = ntohl(frame_id);
        image_quality.codestream_size = ntohl(codestream_size);
        image_quality.raw_size        = ntohl(raw_size);
        image_quality.psnr            = ntohf(psnr);
    }

    VRCPTrackingTimeMeasurement::VRCPTrackingTimeMeasurement(const TrackingTimeMeasurements &tracking_time)
        : pose_timestamp(htonl(tracking_time.pose_timestamp)),
          tracking_received_timestamp(htonl(tracking_time.tracking_received_timestamp)),
          tracking_processed_timestamp(htonl(tracking_time.tracking_processed_timestamp))
    {
    }

    void VRCPTrackingTimeMeasurement::to_tracking_time_measurements(TrackingTimeMeasurements &tracking_time) const
    {
        tracking_time.pose_timestamp               = ntohl(pose_timestamp);
        tracking_time.tracking_received_timestamp  = ntohl(tracking_received_timestamp);
        tracking_time.tracking_processed_timestamp = ntohl(tracking_processed_timestamp);
    }

    VRCPNetworkMeasurement::VRCPNetworkMeasurement(const NetworkMeasurements &network_measurements)
        : rtt(htonl(network_measurements.rtt_us)),
          clock_error(htonl(*reinterpret_cast<const uint32_t *>(&network_measurements.clock_error_us)))
    {
    }

    void VRCPNetworkMeasurement::to_network_measurements(NetworkMeasurements &network_measurements) const
    {
        network_measurements.rtt_us = ntohl(rtt);

        uint32_t err                        = ntohl(clock_error);
        network_measurements.clock_error_us = *reinterpret_cast<const int32_t *>(&err);
    }

    VRCPSocketMeasurement::VRCPSocketMeasurement(const SocketMeasurements &socket_measurement)
        : socket_id(static_cast<uint8_t>(socket_measurement.socket_id)),
          socket_type(static_cast<uint8_t>(socket_measurement.socket_type)),
          bytes_sent(htonl(socket_measurement.bytes_sent)),
          bytes_received(htonl(socket_measurement.bytes_received)),
          packets_sent(htonl(socket_measurement.packets_sent)),
          packets_received(htonl(socket_measurement.packets_received))
    {
    }

    void VRCPSocketMeasurement::to_socket_measurements(SocketMeasurements &socket_measurement) const
    {
        socket_measurement.socket_id        = static_cast<SocketId>(socket_id);
        socket_measurement.socket_type      = static_cast<SocketType>(socket_type);
        socket_measurement.bytes_sent       = ntohl(bytes_sent);
        socket_measurement.bytes_received   = ntohl(bytes_received);
        socket_measurement.packets_sent     = ntohl(packets_sent);
        socket_measurement.packets_received = ntohl(packets_received);
    }
} // namespace wvb::vrcp