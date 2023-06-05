#pragma once

#include <wvb_common/rtp_clock.h>

#include <chrono>
#include <fstream>
#include <vector>

namespace wvb
{
#define WVB_BENCHMARK_TIMING_PHASE_CAPACITY        2000
#define WVB_BENCHMARK_IMAGE_QUALITY_PHASE_CAPACITY 500

    template<typename T>
    T compute_median(std::vector<T> &array)
    {
        if (array.empty())
        {
            return 0;
        }
        if (array.size() == 1)
        {
            return array[0];
        }
        std::sort(array.begin(), array.end());
        if (array.size() % 2 == 0)
        {
            return (array[array.size() / 2 - 1] + array[array.size() / 2]) / 2;
        }
        else
        {
            return array[array.size() / 2];
        }
    }

    /** Returns the median of the given array.
     * @param array pointer to the first element of the array
     * @param count number of elements in the array
     * @param byte_stride number of bytes between two elements in the array
     * @return the median of the array
     */
    template<typename T>
    T compute_median(const T *array, size_t count, size_t byte_stride)
    {
        // Get a sorted array of the elements
        std::vector<T> sorted_array;
        sorted_array.reserve(count);
        for (size_t i = 0; i < count; i++)
        {
            sorted_array.push_back(*reinterpret_cast<const T *>(reinterpret_cast<const uint8_t *>(array) + i * byte_stride));
        }

        return compute_median(sorted_array);
    }

    struct MeasurementWindow
    {
        rtp::RTPClock::time_point start_timing_phase;
        rtp::RTPClock::time_point start_image_quality_phase;
        rtp::RTPClock::time_point end_measurements;
        // There is a margin after the end of measurements before sending them to the server
        // This is to prevent overlapping due to sync imprecision
        rtp::RTPClock::time_point end;

        [[nodiscard]] constexpr bool is_valid()
        {
            return start_timing_phase < start_image_quality_phase && start_image_quality_phase < end_measurements
                   && end_measurements <= end;
        }

        [[nodiscard]] constexpr bool is_in_timing_phase(rtp::RTPClock::time_point time)
        {
            return time >= start_timing_phase && time <= start_image_quality_phase;
        }

        [[nodiscard]] constexpr bool is_in_image_quality_phase(rtp::RTPClock::time_point time)
        {
            return time >= start_image_quality_phase && time <= end_measurements;
        }

        [[nodiscard]] constexpr bool is_in_window(rtp::RTPClock::time_point time) { return time >= start_timing_phase && time <= end; }

        /** If true, measurements are over and can be sent back to the server. */
        [[nodiscard]] constexpr bool is_after_window(rtp::RTPClock::time_point time) { return time > end; }
    };

    enum class SocketId : uint8_t
    {
        UNKNOWN_SOCKET    = 0,
        VIDEO_SOCKET      = 1,
        VRCP_TCP_SOCKET   = 2,
        VRCP_UDP_SOCKET   = 3,
        VRCP_BCAST_SOCKET = 4,
    };

    std::string to_string(SocketId socket_id);

    enum class SocketType : uint8_t
    {
        SOCKET_TYPE_INVALID = 0,
        SOCKET_TYPE_TCP     = 1,
        SOCKET_TYPE_UDP     = 2,
    };

    std::string to_string(SocketType socket_type);

    /**
     * Storage for socket measurements (bitrate, packet loss, etc.)
     */
    struct SocketMeasurements
    {
        SocketId   socket_id        = SocketId::UNKNOWN_SOCKET;
        SocketType socket_type      = SocketType::SOCKET_TYPE_INVALID;
        uint32_t   bytes_sent       = 0;
        uint32_t   bytes_received   = 0;
        uint32_t   packets_sent     = 0;
        uint32_t   packets_received = 0;

        [[nodiscard]] constexpr bool is_valid() const { return socket_type != SocketType::SOCKET_TYPE_INVALID; }

        static void export_csv_header(std::ofstream &file);

        static void export_csv_body(std::ofstream &file, const std::vector<SocketMeasurements> &measurements, const char *component);
    };

    struct ServerFrameTimeMeasurements
    {
        bool     dropped  = false;
        uint32_t frame_id = 0;
        // NEW_PRESENT_INFO event received from driver
        uint32_t frame_event_received_timestamp = 0;
        // After shared memory is locked, opened and read
        uint32_t present_info_received_timestamp       = 0;
        uint32_t shared_texture_opened_timestamp       = 0; // optional
        uint32_t shared_texture_acquired_timestamp     = 0; // optional
        uint32_t staging_texture_mapped_timestamp      = 0; // optional
        uint32_t frame_pushed_timestamp                = 0;
        uint32_t frame_pulled_timestamp                = 0;
        uint32_t before_last_get_next_packet_timestamp = 0;
        uint32_t after_last_get_next_packet_timestamp  = 0;
        uint32_t before_last_send_packet_timestamp     = 0;
        uint32_t after_last_send_packet_timestamp      = 0;
        uint32_t finished_signal_sent_timestamp        = 0;

        static void
            export_csv(std::ofstream &file, const rtp::RTPClock &clock, const std::vector<ServerFrameTimeMeasurements> &measurements);
    };

    struct ClientFrameTimeMeasurements
    {
        uint32_t frame_index                    = 0;
        uint32_t frame_id                       = 0;
        uint32_t tracking_timestamp             = 0;
        uint32_t last_packet_received_timestamp = 0;
        uint32_t pushed_to_decoder_timestamp    = 0;
        uint32_t begin_wait_frame_timestamp     = 0;
        uint32_t begin_frame_timestamp          = 0;
        uint32_t after_wait_swapchain_timestamp = 0;
        uint32_t after_render_timestamp         = 0;
        uint32_t end_frame_timestamp            = 0;
        uint32_t predicted_present_timestamp    = 0;
        uint32_t pose_timestamp                 = 0;
        uint32_t frame_delay                    = 0;

        static void
            export_csv(std::ofstream &file, const rtp::RTPClock &clock, const std::vector<ClientFrameTimeMeasurements> &measurements);
    };

    struct DriverFrameTimeMeasurements
    {
        uint32_t frame_id                          = 0;
        uint32_t present_called_timestamp          = 0;
        uint32_t vsync_timestamp                   = 0;
        uint32_t frame_sent_timestamp              = 0;
        uint32_t wait_for_present_called_timestamp = 0;
        uint32_t server_finished_timestamp         = 0;
        uint32_t pose_updated_event_timestamp      = 0;

        static void
            export_csv(std::ofstream &file, const rtp::RTPClock &clock, const std::vector<DriverFrameTimeMeasurements> &measurements);
    };

    struct TrackingTimeMeasurements
    {
        uint32_t pose_timestamp               = 0;
        uint32_t tracking_received_timestamp  = 0;
        uint32_t tracking_processed_timestamp = 0;

        static void
            export_csv(std::ofstream &file, const rtp::RTPClock &clock, const std::vector<TrackingTimeMeasurements> &measurements);
    };

    struct PoseAccessTimeMeasurements
    {
        uint32_t pose_timestamp          = 0;
        uint32_t pose_accessed_timestamp = 0;

        static void
            export_csv(std::ofstream &file, const rtp::RTPClock &clock, const std::vector<PoseAccessTimeMeasurements> &measurements);
    };

    struct ImageQualityMeasurements
    {
        uint32_t frame_id        = 0;
        uint32_t codestream_size = 0;
        uint32_t raw_size        = 0;
        float    psnr            = 0;

        static void export_csv(std::ofstream &file, const std::vector<ImageQualityMeasurements> &measurements);
    };

    struct NetworkMeasurements
    {
        uint32_t rtt_us         = 0;
        int32_t  clock_error_us = 0;

        static void export_csv(std::ofstream &file, const std::vector<NetworkMeasurements> &measurements);
    };

    void export_misc_measurements_csv(std::ofstream &file,
                                      uint32_t       nb_dropped_frames_server,
                                      uint32_t       nb_dropped_frames_client,
                                      uint32_t       nb_catched_up_frames_client,
                                      uint32_t       encoder_delay,
                                      uint32_t       decoder_delay);

    // ---- Buckets ----

    /**
     * A measurement bucket accumulates measurements from all over the app.
     *
     * When ready, the bucket can be given a measurement time window, and it will start saving
     * measurements until the window ends. Save methods will not do anything outside of a window.
     *
     * Once the window is over, measurements can be exported to a VRCP packet in order to be sent to the
     * server.
     */
    class MeasurementBucket
    {
      protected:
        enum class BucketMode
        {
            /** Default mode: only accept measurements when they occur in the appropriate window. */
            WINDOW = 0,
            /** Accept all measurements without looking at the window. */
            ACCEPT_ALL,
            /** Refuse all measurements, and consider the measurement period over. */
            FINISHED,
        };

        // RTP clock is used to have synchronized time between devices.
        // On the client, its epoch can change while pings are still being sent,
        // so the measurements shouldn't start until the epoch adjustments are over.
        std::shared_ptr<rtp::RTPClock> m_rtp_clock = nullptr;
        MeasurementWindow              m_window;
        BucketMode                     m_mode = BucketMode::WINDOW;

      public:
        virtual ~MeasurementBucket() = default;

        virtual void reset()
        {
            m_mode   = BucketMode::WINDOW;
            m_window = {};
        };

        [[nodiscard]] bool measurements_complete()
        {
            return m_rtp_clock != nullptr
                   && ((m_mode == BucketMode::WINDOW && m_window.is_valid() && m_window.is_after_window(m_rtp_clock->now()))
                       || m_mode == BucketMode::FINISHED);
        }

        [[nodiscard]] inline bool is_in_timing_phase()
        {
            return m_rtp_clock != nullptr
                   && ((m_mode == BucketMode::WINDOW && m_window.is_valid() && m_window.is_in_timing_phase(m_rtp_clock->now()))
                       || m_mode == BucketMode::ACCEPT_ALL);
        }

        [[nodiscard]] inline bool is_in_image_quality_phase()
        {
            return m_rtp_clock != nullptr
                   && ((m_mode == BucketMode::WINDOW && m_window.is_valid() && m_window.is_in_image_quality_phase(m_rtp_clock->now()))
                       || m_mode == BucketMode::ACCEPT_ALL);
        }

        [[nodiscard]] constexpr bool has_window() { return m_window.is_valid(); }

        /** Resets the window so that measurements stop early if they were in progress. */
        void reset_window() { m_window = MeasurementWindow(); }

        /** Disable window checks - all measurements are now accepted. */
        inline void set_as_accept_all() { m_mode = BucketMode::ACCEPT_ALL; }

        inline void set_as_finished() { m_mode = BucketMode::FINISHED; }

        inline void set_clock(std::shared_ptr<rtp::RTPClock> rtp_clock) { m_rtp_clock = std::move(rtp_clock); }

        constexpr void set_window(MeasurementWindow window) { m_window = window; }
    };

    class SocketMeasurementBucket : public MeasurementBucket
    {
        std::vector<SocketMeasurements> m_socket_measurements;

      public:
        ~SocketMeasurementBucket() override = default;

        void reset() override
        {
            MeasurementBucket::reset();

            for (auto &socket : m_socket_measurements)
            {
                socket.bytes_received   = 0;
                socket.bytes_sent       = 0;
                socket.packets_received = 0;
                socket.packets_sent     = 0;
            }
        }

        /** Adds a new socket measurements storage for the given socket. Returns the storage id, that can be used to
         * update the measurements. */
        [[nodiscard]] uint32_t register_socket(SocketId socket_id, SocketType socket_type);

        inline void add_bytes_sent(uint32_t storage_id, uint32_t bytes_sent)
        {
            if (is_in_timing_phase())
            {
                m_socket_measurements[storage_id].bytes_sent += bytes_sent;
            }
        }

        inline void add_bytes_received(uint32_t storage_id, uint32_t bytes_received)
        {
            if (is_in_timing_phase())
            {
                m_socket_measurements[storage_id].bytes_received += bytes_received;
            }
        }

        inline void add_packets_sent(uint32_t storage_id, uint32_t packets_sent)
        {
            if (is_in_timing_phase())
            {
                m_socket_measurements[storage_id].packets_sent += packets_sent;
            }
        }

        inline void add_packets_received(uint32_t storage_id, uint32_t packets_received)
        {
            if (is_in_timing_phase())
            {
                m_socket_measurements[storage_id].packets_received += packets_received;
            }
        }

        void add_socket_measurements(const SocketMeasurements &measurements)
        {
            if (is_in_timing_phase())
            {
                m_socket_measurements.push_back(measurements);
            }
        }

        const std::vector<SocketMeasurements> &get_socket_measurements() const { return m_socket_measurements; }
    };

    class ServerMeasurementBucket : public SocketMeasurementBucket
    {
      private:
        uint32_t m_pass_id = 0;
        uint32_t m_run_id  = 0;

        std::vector<ServerFrameTimeMeasurements> m_frame_measurements;
        std::vector<TrackingTimeMeasurements>    m_tracking_measurements;
        std::vector<ImageQualityMeasurements>    m_image_quality_measurements;
        uint32_t                                 m_dropped_frames  = 0; // Dropped frames because of delay
        uint32_t                                 m_nb_saved_frames = 0; // Whether a frame was saved for image quality measurements

      public:
        ServerMeasurementBucket() : SocketMeasurementBucket()
        {
            m_frame_measurements.reserve(WVB_BENCHMARK_TIMING_PHASE_CAPACITY);
            m_tracking_measurements.reserve(WVB_BENCHMARK_TIMING_PHASE_CAPACITY);
            m_image_quality_measurements.reserve(WVB_BENCHMARK_IMAGE_QUALITY_PHASE_CAPACITY);
        }

        ~ServerMeasurementBucket() override = default;

        void reset() override
        {
            SocketMeasurementBucket::reset();

            m_frame_measurements.clear();
            m_tracking_measurements.clear();
            m_image_quality_measurements.clear();
            m_dropped_frames  = 0;
            m_nb_saved_frames = 0;
        }

        inline void add_frame_time_measurement(const ServerFrameTimeMeasurements &measurement)
        {
            if (is_in_timing_phase())
            {
                m_frame_measurements.push_back(measurement);
            }
        }

        inline void add_tracking_time_measurement(TrackingTimeMeasurements &&measurement)
        {
            if (is_in_timing_phase())
            {
                m_tracking_measurements.push_back(measurement);
            }
        }

        inline void add_image_quality_measurement(ImageQualityMeasurements &&measurement)
        {
            if (is_in_image_quality_phase())
            {
                m_image_quality_measurements.push_back(measurement);
            }
        }

        inline void add_dropped_frame()
        {
            if (is_in_timing_phase())
            {
                m_dropped_frames++;
            }
        }

        inline void add_saved_frame() { m_nb_saved_frames++; }

        inline bool has_saved_frames() const { return m_nb_saved_frames == 10; }

        inline uint32_t get_nb_saved_frames() const { return m_nb_saved_frames; }

        inline void set_pass_id(uint32_t pass_id) { m_pass_id = pass_id; }

        inline void set_run_id(uint32_t run_id) { m_run_id = run_id; }

        inline uint32_t get_pass_id() const { return m_pass_id; }

        inline uint32_t get_run_id() const { return m_run_id; }

        inline const std::vector<ServerFrameTimeMeasurements> &get_frame_time_measurements() const { return m_frame_measurements; }

        inline const std::vector<TrackingTimeMeasurements> &get_tracking_time_measurements() const { return m_tracking_measurements; }

        inline const std::vector<ImageQualityMeasurements> &get_image_quality_measurements() const
        {
            return m_image_quality_measurements;
        }

        inline uint32_t get_dropped_frames() const { return m_dropped_frames; }
    };

    class ClientMeasurementBucket : public SocketMeasurementBucket
    {
      private:
        std::vector<ClientFrameTimeMeasurements> m_frame_measurements;
        std::vector<TrackingTimeMeasurements>    m_tracking_measurements;
        std::vector<ImageQualityMeasurements>    m_image_quality_measurements;
        std::vector<NetworkMeasurements>         m_network_measurements;
        uint32_t                                 m_decoder_nb_pushed_frames = 0;
        uint32_t                                 m_decoder_nb_pulled_frames = 0;
        uint32_t                                 m_nb_saved_frames   = 0; // Whether a frame was saved for image quality measurements
        uint32_t                                 m_nb_dropped_frames = 0;
        uint32_t m_nb_catched_up_frames = 0; // Number of times we successfully pulled two frames at once to catch up with delay

      public:
        void reset() override
        {
            SocketMeasurementBucket::reset();

            m_frame_measurements.clear();
            m_tracking_measurements.clear();
            m_image_quality_measurements.clear();
            m_network_measurements.clear();
            m_decoder_nb_pushed_frames = 0;
            m_decoder_nb_pulled_frames = 0;
            m_nb_saved_frames          = 0;
            m_nb_dropped_frames        = 0;
        }

        ClientMeasurementBucket() : SocketMeasurementBucket()
        {
            m_frame_measurements.reserve(WVB_BENCHMARK_TIMING_PHASE_CAPACITY);
            m_tracking_measurements.reserve(WVB_BENCHMARK_TIMING_PHASE_CAPACITY);
            m_image_quality_measurements.reserve(WVB_BENCHMARK_IMAGE_QUALITY_PHASE_CAPACITY);
            m_network_measurements.reserve(20);
        }

        ~ClientMeasurementBucket() override = default;

        inline void add_frame_time_measurement(const ClientFrameTimeMeasurements &measurement)
        {
            if (is_in_timing_phase())
            {
                m_frame_measurements.push_back(measurement);
            }
        }

        inline void add_tracking_time_measurement(TrackingTimeMeasurements &&measurement)
        {
            if (is_in_timing_phase())
            {
                m_tracking_measurements.push_back(measurement);
            }
        }

        inline void add_image_quality_measurement(ImageQualityMeasurements &&measurement)
        {
            if (is_in_image_quality_phase())
            {
                m_image_quality_measurements.push_back(measurement);
            }
        }

        inline void add_network_measurement(NetworkMeasurements &&measurement) { m_network_measurements.push_back(measurement); }
        inline void add_decoder_pushed_frame() { m_decoder_nb_pushed_frames++; }
        inline void add_decoder_pulled_frame() { m_decoder_nb_pulled_frames++; }
        inline void set_decoder_frame_delay(uint32_t delay) { m_decoder_nb_pushed_frames = delay; }
        inline void add_saved_frame() { m_nb_saved_frames++; }
        inline void add_dropped_frames(uint32_t nb_dropped = 1)
        {
            if (is_in_timing_phase())
            {
                m_nb_dropped_frames += nb_dropped;
            }
        }
        inline void add_catched_up_frame()
        {
            if (is_in_timing_phase())
            {
                m_nb_catched_up_frames++;
            }
        }
        inline bool     has_saved_frames() const { return m_nb_saved_frames == 10; }
        inline uint32_t get_nb_saved_frames() const { return m_nb_saved_frames; }
        inline uint32_t get_nb_dropped_frames() const { return m_nb_dropped_frames; }
        inline uint32_t get_nb_catched_up_frames() const { return m_nb_catched_up_frames; }
        inline void     set_nb_dropped_frames(uint32_t nb_dropped_frames) { m_nb_dropped_frames = nb_dropped_frames; }
        inline void     set_nb_catched_up_frames(uint32_t nb_catched_up_frames) { m_nb_catched_up_frames = nb_catched_up_frames; }
        void            get_rtt_stats(uint32_t &min_rtt, uint32_t &max_rtt, uint32_t &avg_rtt, uint32_t &med_rtt) const;
        void            get_clock_error_stats(uint32_t &min_clock_error, uint32_t &max_clock_error, uint32_t &med_clock_error) const;
        const std::vector<NetworkMeasurements>         &get_network_measurements() const { return m_network_measurements; }
        const std::vector<ClientFrameTimeMeasurements> &get_frame_time_measurements() const { return m_frame_measurements; }
        const std::vector<TrackingTimeMeasurements>    &get_tracking_measurements() const { return m_tracking_measurements; }
        const std::vector<ImageQualityMeasurements>    &get_image_quality_measurements() const { return m_image_quality_measurements; }
        uint32_t get_decoder_frame_delay() const { return m_decoder_nb_pushed_frames - m_decoder_nb_pulled_frames; }
    };

    class DriverMeasurementBucket : public MeasurementBucket
    {
      private:
        std::vector<DriverFrameTimeMeasurements> m_frame_measurements;
        std::vector<TrackingTimeMeasurements>    m_tracking_measurements;
        std::vector<PoseAccessTimeMeasurements>  m_pose_accesses_measurements;

      public:
        void reset() override
        {
            MeasurementBucket::reset();

            m_frame_measurements.clear();
            m_tracking_measurements.clear();
            m_pose_accesses_measurements.clear();
        }

        DriverMeasurementBucket() : MeasurementBucket()
        {
            m_frame_measurements.reserve(WVB_BENCHMARK_TIMING_PHASE_CAPACITY);
            m_tracking_measurements.reserve(WVB_BENCHMARK_TIMING_PHASE_CAPACITY);
            m_pose_accesses_measurements.reserve(WVB_BENCHMARK_TIMING_PHASE_CAPACITY);
        }

        DriverMeasurementBucket(const DriverMeasurementBucket &other);

        ~DriverMeasurementBucket() override = default;

        DriverMeasurementBucket &operator=(const DriverMeasurementBucket &other);

        inline void add_frame_time_measurement(const DriverFrameTimeMeasurements &measurement)
        {
            if (is_in_timing_phase())
            {
                m_frame_measurements.push_back(measurement);
            }
        }

        inline void add_tracking_time_measurement(const TrackingTimeMeasurements &measurement)
        {
            if (is_in_timing_phase())
            {
                m_tracking_measurements.push_back(measurement);
            }
        }

        inline void add_pose_access_measurement(const PoseAccessTimeMeasurements &measurement)
        {
            if (is_in_timing_phase())
            {
                m_pose_accesses_measurements.push_back(measurement);
            }
        }

        inline const std::vector<DriverFrameTimeMeasurements> &get_frame_time_measurements() const { return m_frame_measurements; }

        inline const std::vector<TrackingTimeMeasurements> &get_tracking_measurements() const { return m_tracking_measurements; }

        inline const std::vector<PoseAccessTimeMeasurements> &get_pose_access_measurements() const
        {
            return m_pose_accesses_measurements;
        }
    };

} // namespace wvb