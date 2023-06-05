#include "wvb_common/benchmark.h"

#include <wvb_common/macros.h>
#include <wvb_common/rtp.h>
namespace wvb
{
    int64_t to_us(const rtp::RTPClock &clock, uint32_t timestamp)
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(clock.from_rtp_timestamp(timestamp).time_since_epoch()).count();
    }

    std::string to_string(SocketId socket_id)
    {
        switch (socket_id)
        {
            case SocketId::VIDEO_SOCKET: return "VIDEO";
            case SocketId::VRCP_BCAST_SOCKET: return "VRCP_BCAST";
            case SocketId::VRCP_TCP_SOCKET: return "VRCP_TCP";
            case SocketId::VRCP_UDP_SOCKET: return "VRCP_UDP";
            default: return "UNKNOWN";
        }
    }

    std::string to_string(SocketType socket_type)
    {
        switch (socket_type)
        {
            case SocketType::SOCKET_TYPE_UDP: return "UDP";
            case SocketType::SOCKET_TYPE_TCP: return "TCP";
            default: return "INVALID";
        }
    }

    uint32_t SocketMeasurementBucket::register_socket(SocketId socket_id, SocketType socket_type)
    {
        m_socket_measurements.push_back(SocketMeasurements {
            .socket_id   = socket_id,
            .socket_type = socket_type,
        });
        return m_socket_measurements.size() - 1;
    }

    void ClientMeasurementBucket::get_rtt_stats(uint32_t &min_rtt, uint32_t &max_rtt, uint32_t &avg_rtt, uint32_t &med_rtt) const
    {
        min_rtt = 0;
        max_rtt = 0;
        avg_rtt = 0;
        med_rtt = 0; // median
        if (m_network_measurements.empty())
        {
            return;
        }
        min_rtt = m_network_measurements[0].rtt_us;
        max_rtt = m_network_measurements[0].rtt_us;
        for (auto &measurement : m_network_measurements)
        {
            min_rtt = std::min(min_rtt, measurement.rtt_us);
            max_rtt = std::max(max_rtt, measurement.rtt_us);
            avg_rtt += measurement.rtt_us;
        }
        avg_rtt /= m_network_measurements.size();

        // Get median rtt
        med_rtt = compute_median(&m_network_measurements[0].rtt_us, m_network_measurements.size(), sizeof(NetworkMeasurements));
    }

    void ClientMeasurementBucket::get_clock_error_stats(uint32_t &min_clock_error,
                                                        uint32_t &max_clock_error,
                                                        uint32_t &med_clock_error) const
    {
        min_clock_error = 0;
        max_clock_error = 0;
        med_clock_error = 0; // median
        if (m_network_measurements.empty())
        {
            return;
        }
        // Get min absolute clock error
        min_clock_error = static_cast<uint32_t>(std::abs(m_network_measurements[0].clock_error_us));
        max_clock_error = static_cast<uint32_t>(std::abs(m_network_measurements[0].clock_error_us));
        for (auto &measurement : m_network_measurements)
        {
            min_clock_error = std::min(min_clock_error, static_cast<uint32_t>(std::abs(measurement.clock_error_us)));
            max_clock_error = std::max(max_clock_error, static_cast<uint32_t>(std::abs(measurement.clock_error_us)));
        }

        // Get median clock error
        std::vector<uint32_t> abs_errors;
        abs_errors.reserve(m_network_measurements.size());
        for (auto &measurement : m_network_measurements)
        {
            abs_errors.push_back(static_cast<uint32_t>(std::abs(measurement.clock_error_us)));
        }
        med_clock_error = compute_median(abs_errors);
    }

    DriverMeasurementBucket::DriverMeasurementBucket(const DriverMeasurementBucket &other)
    {
        // Deep copy measurements
        m_frame_measurements.reserve(other.m_frame_measurements.size());
        for (auto &measurement : other.m_frame_measurements)
        {
            m_frame_measurements.push_back(measurement);
        }

        m_tracking_measurements.reserve(other.m_tracking_measurements.size());
        for (auto &measurement : other.m_tracking_measurements)
        {
            m_tracking_measurements.push_back(measurement);
        }

        m_pose_accesses_measurements.reserve(other.m_pose_accesses_measurements.size());
        for (auto &measurement : other.m_pose_accesses_measurements)
        {
            m_pose_accesses_measurements.push_back(measurement);
        }

        m_rtp_clock = other.m_rtp_clock;
        m_window    = other.m_window;
        m_mode      = other.m_mode;
    }

    DriverMeasurementBucket &DriverMeasurementBucket::operator=(const DriverMeasurementBucket &other)
    {
        if (this == &other)
        {
            return *this;
        }

        // this->reset();
        m_frame_measurements.clear();
        m_tracking_measurements.clear();
        m_pose_accesses_measurements.clear();
        m_tracking_measurements.clear();

        // Deep copy measurements
        m_frame_measurements.reserve(other.m_frame_measurements.size());
        for (auto &measurement : other.m_frame_measurements)
        {
            m_frame_measurements.push_back(measurement);
        }

        m_tracking_measurements.reserve(other.m_tracking_measurements.size());
        for (auto &measurement : other.m_tracking_measurements)
        {
            m_tracking_measurements.push_back(measurement);
        }

        m_pose_accesses_measurements.reserve(other.m_pose_accesses_measurements.size());
        for (auto &measurement : other.m_pose_accesses_measurements)
        {
            m_pose_accesses_measurements.push_back(measurement);
        }

        // m_rtp_clock = other.m_rtp_clock;
        // m_window    = other.m_window;
        m_mode = other.m_mode;

        return *this;
    }

    // --- Export ---

    void SocketMeasurements::export_csv_header(std::ofstream &file)
    {
        if (!file.is_open())
        {
            LOGE("File not open\n");
            return;
        }

        // Write header
        file << "component,socket_id,socket_type,bytes_sent,bytes_received,packets_sent,packets_received\n";
    }

    void SocketMeasurements::export_csv_body(std::ofstream                         &file,
                                             const std::vector<SocketMeasurements> &measurements,
                                             const char                            *component)
    {
        if (!file.is_open())
        {
            LOGE("File not open\n");
            return;
        }

        for (const auto &measurement : measurements)
        {
            file << component << ',' << wvb::to_string(measurement.socket_id) << ',' << wvb::to_string(measurement.socket_type) << ','
                 << measurement.bytes_sent << ',' << measurement.bytes_received << ',' << measurement.packets_sent << ','
                 << measurement.packets_received << '\n';
        }
    }

    void DriverFrameTimeMeasurements::export_csv(std::ofstream                                  &file,
                                                 const rtp::RTPClock                            &clock,
                                                 const std::vector<DriverFrameTimeMeasurements> &measurements)
    {
        if (!file.is_open())
        {
            LOGE("File not open\n");
            return;
        }

        // Write header
        file << "frame_id,present_called,vsync,frame_sent,wait_for_present_called,server_finished,pose_updated_event\n";

        // Write body
        for (const auto &measurement : measurements)
        {
            file << measurement.frame_id << ',' << to_us(clock, measurement.present_called_timestamp) << ','
                 << to_us(clock, measurement.vsync_timestamp) << ',' << to_us(clock, measurement.frame_sent_timestamp) << ','
                 << to_us(clock, measurement.wait_for_present_called_timestamp) << ','
                 << to_us(clock, measurement.server_finished_timestamp) << ','
                 << to_us(clock, measurement.pose_updated_event_timestamp) << '\n';
        }
    }

    void TrackingTimeMeasurements::export_csv(std::ofstream                               &file,
                                              const rtp::RTPClock                         &clock,
                                              const std::vector<TrackingTimeMeasurements> &measurements)
    {
        if (!file.is_open())
        {
            LOGE("File not open\n");
            return;
        }

        // Write header
        file << "pose_timestamp,tracking_received,tracking_processed\n";

        // Write body
        for (const auto &measurement : measurements)
        {
            file << to_us(clock, measurement.pose_timestamp) << ',' << to_us(clock, measurement.tracking_received_timestamp) << ','
                 << to_us(clock, measurement.tracking_processed_timestamp) << '\n';
        }
    }

    void PoseAccessTimeMeasurements::export_csv(std::ofstream                                 &file,
                                                const rtp::RTPClock                           &clock,
                                                const std::vector<PoseAccessTimeMeasurements> &measurements)
    {
        if (!file.is_open())
        {
            LOGE("File not open\n");
            return;
        }

        // Write header
        file << "pose_timestamp,pose_accessed\n";

        // Write body
        for (const auto &measurement : measurements)
        {
            file << to_us(clock, measurement.pose_timestamp) << ',' << to_us(clock, measurement.pose_accessed_timestamp) << '\n';
        }
    }

    void ServerFrameTimeMeasurements::export_csv(std::ofstream                                  &file,
                                                 const rtp::RTPClock                            &clock,
                                                 const std::vector<ServerFrameTimeMeasurements> &measurements)
    {
        if (!file.is_open())
        {
            LOGE("File not open\n");
            return;
        }

        // Write header
        file << "frame_id,dropped,frame_event_received,present_info_received,shared_texture_opened,shared_texture_acquired,staging_"
                "texture_mapped,encoder_frame_pushed,encoder_frame_pulled,before_last_get_next_packet,after_last_get_next_packet,before_last_send_packet,after_"
                "last_send_packet,finished_signal_sent\n";

        // Write body
        for (const auto &measurement : measurements)
        {
            file << measurement.frame_id << ',' << measurement.dropped << ','
                 << to_us(clock, measurement.frame_event_received_timestamp) << ','
                 << to_us(clock, measurement.present_info_received_timestamp) << ','
                 << to_us(clock, measurement.shared_texture_opened_timestamp) << ','
                 << to_us(clock, measurement.shared_texture_acquired_timestamp) << ','
                 << to_us(clock, measurement.staging_texture_mapped_timestamp) << ','
                 << to_us(clock, measurement.frame_pushed_timestamp) << ','
                 << to_us(clock, measurement.frame_pulled_timestamp) << ','
                 << to_us(clock, measurement.before_last_get_next_packet_timestamp) << ','
                 << to_us(clock, measurement.after_last_get_next_packet_timestamp) << ','
                 << to_us(clock, measurement.before_last_send_packet_timestamp) << ','
                 << to_us(clock, measurement.after_last_send_packet_timestamp) << ','
                 << to_us(clock, measurement.finished_signal_sent_timestamp) << '\n';
        }
    }

    void ImageQualityMeasurements::export_csv(std::ofstream &file, const std::vector<ImageQualityMeasurements> &measurements)
    {
        if (!file.is_open())
        {
            LOGE("File not open\n");
            return;
        }

        // Write header
        file << "frame_id,codestream_size,raw_size,psnr\n";

        // Write body
        for (const auto &measurement : measurements)
        {
            file << measurement.frame_id << ',' << measurement.codestream_size << ',' << measurement.raw_size << ','
                 << measurement.psnr << '\n';
        }
    }

    void ClientFrameTimeMeasurements::export_csv(std::ofstream                                  &file,
                                                 const rtp::RTPClock                            &clock,
                                                 const std::vector<ClientFrameTimeMeasurements> &measurements)
    {
        if (!file.is_open())
        {
            LOGE("File not open\n");
            return;
        }

        // Write header
        file << "frame_index,frame_id,frame_delay,tracking_sampled,last_packet_received,pushed_to_decoder,begin_wait_frame,begin_"
                "frame,after_wait_swapchain,after_render,end_frame,predicted_present_time,pose_timestamp\n";

        // Write body
        for (const auto &measurement : measurements)
        {
            file << measurement.frame_index << ',' << measurement.frame_id << ',' << measurement.frame_delay << ','
                 << to_us(clock, measurement.tracking_timestamp) << ',' << to_us(clock, measurement.last_packet_received_timestamp)
                 << ',' << to_us(clock, measurement.pushed_to_decoder_timestamp) << ','
                 << to_us(clock, measurement.begin_wait_frame_timestamp) << ',' << to_us(clock, measurement.begin_frame_timestamp)
                 << ',' << to_us(clock, measurement.after_wait_swapchain_timestamp) << ','
                 << to_us(clock, measurement.after_render_timestamp) << ',' << to_us(clock, measurement.end_frame_timestamp) << ','
                 << to_us(clock, measurement.predicted_present_timestamp) << ',' << to_us(clock, measurement.pose_timestamp) << '\n';
        }
    }

    void NetworkMeasurements::export_csv(std::ofstream &file, const std::vector<NetworkMeasurements> &measurements)
    {
        if (!file.is_open())
        {
            LOGE("File not open\n");
            return;
        }

        // Write header
        file << "rtt,clock_error\n";

        // Write body
        for (const auto &measurement : measurements)
        {
            file << measurement.rtt_us << ',' << measurement.clock_error_us << '\n';
        }
    }

    void export_misc_measurements_csv(std::ofstream &file,
                                      uint32_t       nb_dropped_frames_server,
                                      uint32_t       nb_dropped_frames_client,
                                      uint32_t       nb_catched_up_frames_client,
                                      uint32_t       encoder_delay,
                                      uint32_t       decoder_delay)
    {
        if (!file.is_open())
        {
            LOGE("File not open\n");
            return;
        }

        // Write header
        file << "nb_dropped_frames_server,nb_dropped_frames_client,nb_catched_up_frames_client,encoder_delay,decoder_delay\n";

        // Write body
        file << nb_dropped_frames_server << ',' << nb_dropped_frames_client << ',' << nb_catched_up_frames_client << ','
             << encoder_delay << ',' << decoder_delay << '\n';
    }

    // void BenchmarkContext::print_stats(const rtp::RTPClock &clock) const
    // {
    //     int32_t i = frame_times_index - frame_times_count;
    //     if (frame_times_count == WVB_TIMING_PHASE_FRAME_CAPACITY)
    //     {
    //         i += 1;
    //     }
    //     if (i < 0)
    //     {
    //         i += WVB_TIMING_PHASE_FRAME_CAPACITY;
    //     }

    //     while (i != frame_times_index)
    //     {
    //         auto &frame_time = frame_times[i];

    //         if (frame_time.tracking_timestamp == 0)
    //         {
    //             i = (i + 1) % WVB_TIMING_PHASE_FRAME_CAPACITY;
    //             continue;
    //         }

    //         ClientFrameExecutionTimeRelative frame_time_relative(frame_time, clock);

    //         LOG("%u | tracking %lld wait %lld begin %lld swap %lld render %lld end %lld predicted %lld pose %lld delta %lld\n",
    //             frame_time_relative.frame_index,
    //             std::chrono::duration_cast<std::chrono::microseconds>(frame_time_relative.tracking_timestamp.time_since_epoch())
    //                 .count(),
    //             frame_time_relative.begin_wait_frame.count(),
    //             frame_time_relative.begin_frame.count(),
    //             frame_time_relative.after_wait_swapchain.count(),
    //             frame_time_relative.after_render.count(),
    //             frame_time_relative.end_frame.count(),
    //             frame_time_relative.predicted_present.count(),
    //             frame_time_relative.pose.count(),
    //             (frame_time_relative.pose - frame_time_relative.predicted_present).count());

    //         i = (i + 1) % WVB_TIMING_PHASE_FRAME_CAPACITY;
    //     }
    // }

    // void BenchmarkContext::add_frame_time(const ClientFrameExecutionTime &frame_time)
    // {
    //     frame_times[frame_times_index] = frame_time;
    //     frame_times_index              = (frame_times_index + 1) % WVB_TIMING_PHASE_FRAME_CAPACITY;
    //     frame_times_count              = std::min(frame_times_count + 1, WVB_TIMING_PHASE_FRAME_CAPACITY);
    // }

    // ClientFrameExecutionTimeRelative::ClientFrameExecutionTimeRelative(const ClientFrameExecutionTime &frame_time,
    //                                                                    const rtp::RTPClock            &clock)
    //     : frame_index(frame_time.frame_index),
    //       tracking_timestamp(std::chrono::time_point_cast<std::chrono::steady_clock::duration>(
    //           clock.from_rtp_timestamp(frame_time.tracking_timestamp))),
    //       begin_wait_frame(
    //           rtp::rtp_timestamps_distance_us(frame_time.tracking_timestamp, frame_time.begin_wait_frame_timestamp, clock)),
    //       begin_frame(rtp::rtp_timestamps_distance_us(frame_time.begin_wait_frame_timestamp, frame_time.begin_frame_timestamp,
    //       clock)), after_wait_swapchain(
    //           rtp::rtp_timestamps_distance_us(frame_time.begin_frame_timestamp, frame_time.after_wait_swapchain_timestamp, clock)),
    //       after_render(
    //           rtp::rtp_timestamps_distance_us(frame_time.after_wait_swapchain_timestamp, frame_time.after_render_timestamp, clock)),
    //       end_frame(rtp::rtp_timestamps_distance_us(frame_time.after_render_timestamp, frame_time.end_frame_timestamp, clock)),
    //       predicted_present(
    //           rtp::rtp_timestamps_distance_us(frame_time.tracking_timestamp, frame_time.predicted_present_timestamp, clock)),
    //       pose(rtp::rtp_timestamps_distance_us(frame_time.tracking_timestamp, frame_time.pose_timestamp, clock))
    // {
    // }
} // namespace wvb