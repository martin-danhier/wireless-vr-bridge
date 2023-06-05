#include "wvb_client/client.h"

#include <wvb_client/vr_system.h>
#include <wvb_common/module.h>
#include <wvb_common/network_utils.h>
#include <wvb_common/rtp.h>
#include <wvb_common/socket.h>
#include <wvb_common/video_socket.h>
#include <wvb_common/vrcp_socket.h>

#include <chrono>
#include <fstream>
#include <optional>
#include <sys/prctl.h>
#include <thread>

#define CONNECT_TIMEOUT_MS  30000
#define VIDEO_PORT          PORT_AUTO
#define POLL_INTERVAL_MS    500
#define WVB_MAX_PACKET_SIZE 1500
#define PING_INTERVAL_MS    std::chrono::milliseconds(200)
#define PING_TIMEOUT_MS     std::chrono::milliseconds(500)
#define PING_COUNT          20
#define FRAGMENT_SIZE       400
#define EMPTY_DEPACKETIZER_EACH_FRAME true

namespace wvb::client
{
    // =======================================================================================
    // =                                       Structs                                       =
    // =======================================================================================

    enum class ClientState
    {
        UNINITIALIZED,
        CONNECTING,
        SYNCING,
        RUNNING,
        SHUTDOWN,
        /** Half-shutdown, waiting for the next benchmark pass with new settings. */
        SOFT_SHUTDOWN,
    };

    struct Client::Data
    {
        std::shared_ptr<ClientMeasurementBucket> measurement_bucket = std::make_shared<ClientMeasurementBucket>();

        std::shared_ptr<rtp::RTPClock> rtp_clock = std::make_shared<rtp::RTPClock>();
        std::thread                    syncing_thread;

        std::vector<Module> modules;
        Module              chosen_module;

        ClientVideoSocket video_socket {VIDEO_PORT, measurement_bucket};
        android_app      *android_app = nullptr;

        VRCPSocket vrcp_socket = VRCPSocket::create_client(PORT_AUTO, PORT_AUTO, VRCP_DEFAULT_ADVERTISEMENT_PORT, measurement_bucket);

        VRCPConnectResp           connect_resp {};
        std::optional<SocketAddr> server_addr = std::nullopt;

        bool        sent_eos = false;
        std::thread render_thread;
        VRSystem    vr_system;

        std::atomic<ClientState> state = ClientState::UNINITIALIZED;

        /** Tries to connect to the given server.
         *
         * @returns true if it worked, false if it times out.
         * @throws an exception if a REJECT is received. */
        bool connect();

        void setup_codec(const std::string &codec);

        void select_server();

        void poll_vrcp_socket();

        void send_tracking_update() const;

        void save_and_send_frame_if_needed() const;

        void handle_vrcp_packet(const vrcp::VRCPBaseHeader *packet, size_t size);

        void render_thread_main();

        void syncing_thread_main();

        [[nodiscard]] inline bool is_running() const { return state == ClientState::RUNNING; }

        [[nodiscard]] inline bool is_syncing() const { return state == ClientState::SYNCING; }

        [[nodiscard]] inline bool is_connecting() const { return state == ClientState::CONNECTING; }

        [[nodiscard]] inline bool is_soft_shutdown() const { return state == ClientState::SOFT_SHUTDOWN; }

        [[nodiscard]] inline bool should_exit() const { return state == ClientState::SHUTDOWN; }

        [[nodiscard]] inline bool should_soft_exit() const { return state == ClientState::SOFT_SHUTDOWN || should_exit(); }

        void soft_shutdown();

        void send_measurements();
    };

    // =======================================================================================
    // =                                   Implementation                                    =
    // =======================================================================================

    void Client::Data::setup_codec(const std::string &codec)
    {
        // Connected; find module
        bool found = false;
        for (auto &module : modules)
        {
            if (module.codec_id == codec)
            {
                chosen_module = module;
                found         = true;
                break;
            }
        }

        // No module found (shouldn't happen if server is correct)
        if (!found)
        {
            throw std::runtime_error("No module found for codec \"" + codec + "\"");
        }

        LOG("Video codec: \"%s\"\n", chosen_module.name.c_str());

        if (chosen_module.create_video_decoder == nullptr)
        {
            throw std::runtime_error("No video decoder found for codec \"" + connect_resp.chosen_video_codec + "\"");
        }

        // Setup video decoder
        const auto                       specs = vr_system.specs();
        IVideoDecoder::DecoderCreateInfo create_info {
            .src_size     = {specs.eye_resolution.width * 2, specs.eye_resolution.height},
            .refresh_rate = specs.refresh_rate,
            .io           = IO(android_app->activity->assetManager),
        };
        vr_system.set_decoder(chosen_module.create_video_decoder(create_info));

// Setup and connect video socket
#ifdef WVB_VIDEO_SOCKET_USE_RTP
        if (chosen_module.create_depacketizer != nullptr)
        {
            video_socket.set_depacketizer(chosen_module.create_depacketizer());
        }
        else
        {
            throw std::runtime_error("No depacketizer found for codec \"" + connect_resp.chosen_video_codec + "\"");
        }
#else
        video_socket.set_depacketizer(nullptr);
#endif
    }

    void Client::Data::select_server()
    {
        if (server_addr.has_value())
        {
            // Already selected
            return;
        }

        // Listen for servers
        const auto &server_candidates = vrcp_socket.available_servers();

        if (!server_candidates.empty())
        {
            // Select the first server
            server_addr = server_candidates[0].addr;
        }
    }

    bool Client::Data::connect()
    {
        const auto &specs = vr_system.specs();

        // Wait until we have all the required information
        if (!server_addr.has_value() || specs.ipd == 0.0f || specs.refresh_rate.numerator == 0 || specs.system_name.empty()
            || specs.manufacturer_name.empty())
        {
            return false;
        }

        // Connect VRCP first
        if (!vrcp_socket.is_connected())
        {
            VRCPClientParams params {
                .video_port    = video_socket.local_addr().port,
                .specs         = specs,
                .ntp_timestamp = vr_system.ntp_epoch(),
            };
            for (auto &module : modules)
            {
                params.supported_video_codecs.push_back(module.codec_id);
            }
            if (!vrcp_socket.connect(server_addr.value(), params, &connect_resp))
            {
                // Try again later
                return false;
            }

            setup_codec(connect_resp.chosen_video_codec);
        }

        // Then the video socket
        if (!video_socket.is_connected())
        {
            SocketAddr server_video_addr {
                .addr = server_addr->addr,
                .port = connect_resp.peer_video_port,
            };
            if (!video_socket.connect(server_video_addr))
            {
                return false;
            }
        }

        LOG("Connected to server. Syncing clocks...\n");

        // Connected, move on to syncing
        state          = ClientState::SYNCING;
        syncing_thread = std::thread(&Client::Data::syncing_thread_main, this);

        return true;
    }

    void Client::Data::handle_vrcp_packet(const vrcp::VRCPBaseHeader *packet, size_t size)
    {
        if (packet->ftype == vrcp::VRCPFieldType::BENCHMARK_INFO && size == sizeof(vrcp::VRCPBenchmarkInfo))
        {
            // Received benchmark window from server
            const auto *info = reinterpret_cast<const vrcp::VRCPBenchmarkInfo *>(packet);

            if (!measurement_bucket->has_window() && rtp_clock != nullptr)
            {
                measurement_bucket->set_window(info->to_measurement_window(*rtp_clock));
            }
        }
        else if (packet->ftype == vrcp::VRCPFieldType::NEXT_PASS)
        {
            const auto *info = reinterpret_cast<const vrcp::VRCPNextPass *>(packet);
            if (size >= sizeof(vrcp::VRCPNextPass) + sizeof(vrcp::VRCPAdditionalField))
            {
                LOG("Starting pass %u, run %u\n", info->pass, info->run);

                // Read TLV field
                const auto *data           = (const uint8_t *) (info + 1);
                size_t      remaining_size = size - sizeof(vrcp::VRCPNextPass);
                std::string chosen_video_codec;

                while (remaining_size >= 2)
                {
                    const auto *field = (const vrcp::VRCPAdditionalField *) data;
                    if (field->type == vrcp::VRCPFieldType::CHOSEN_VIDEO_CODEC_TLV)
                    {
                        if (field->length == 0 || field->length + 2 > remaining_size)
                        {
                            LOGE("Invalid TLV field length\n");
                            return;
                        }

                        // Copy string
                        chosen_video_codec = std::string((const char *) (field->value), field->length);
                    }

                    // Move to next field
                    data += field->length + 2;
                    remaining_size -= field->length + 2;
                }

                if (chosen_video_codec.empty())
                {
                    LOGE("No video codec chosen\n");
                    return;
                }

                // Cleanup
                video_socket.flush();

                // Update codec
                setup_codec(chosen_video_codec);

                state = ClientState::RUNNING;
            }
        }
    }

    void Client::Data::poll_vrcp_socket()
    {
        const vrcp::VRCPBaseHeader *buffer = nullptr;
        size_t                      size   = 0;

        if (!vrcp_socket.is_connected_refresh())
        {
            LOG("Lost connection to server\n");
            state = ClientState::SHUTDOWN;
            return;
        }

        // First reliable poll
        while (vrcp_socket.reliable_receive(&buffer, &size))
        {
            // Handle packet
            handle_vrcp_packet(buffer, size);
        }
        // Then unreliable poll
        while (vrcp_socket.unreliable_receive(&buffer, &size))
        {
            // Handle packet
            handle_vrcp_packet(buffer, size);
        }
    }

    void Client::Data::send_tracking_update() const
    {
        if (!is_running())
        {
            return;
        }

        TrackingState tracking_state;
        if (!vr_system.get_next_tracking_state(tracking_state))
        {
            return;
        }

        vrcp::VRCPTrackingData msg {tracking_state};
        vrcp_socket.unreliable_send(reinterpret_cast<vrcp::VRCPBaseHeader *>(&msg), sizeof(msg));
    }

    void Client::Data::save_and_send_frame_if_needed() const
    {
        IOBuffer buf {};
        bool     should_send_frame = vr_system.save_frame_if_needed(buf);

        if (should_send_frame && buf.data != nullptr && buf.size > 0)
        {
            // Allocate a large buffer for vrcp packet
            IOBuffer vrcp_buf {};
            vrcp_buf.data = new uint8_t[sizeof(vrcp::VRCPFrameCaptureFragment) + FRAGMENT_SIZE];
            vrcp_buf.size = sizeof(vrcp::VRCPFrameCaptureFragment) + FRAGMENT_SIZE;

            auto *packet = reinterpret_cast<vrcp::VRCPFrameCaptureFragment *>(vrcp_buf.data);
            *packet      = {
                     .last      = 0,
                     .full_size = htonl(buf.size),
            };
            auto *vrcp_data = reinterpret_cast<uint8_t *>(packet + 1);

            for (size_t offset = 0; offset < buf.size; offset += FRAGMENT_SIZE)
            {
                // Compute size
                size_t data_size = std::min(buf.size - offset, (size_t) FRAGMENT_SIZE);

                // Create packet
                memcpy(vrcp_data, buf.data + offset, data_size);

                // Pad size to the next multiple of 4
                size_t packet_data_size = (data_size + 3) & ~3;

                // Set remainder with 0
                if (data_size < packet_data_size)
                {
                    memset(vrcp_data + data_size, 0, packet_data_size - data_size);
                }

                // Set packet size
                packet->size   = htonl(data_size);
                packet->offset = htonl(offset);
                packet->n_rows = static_cast<uint8_t>((packet_data_size + sizeof(vrcp::VRCPFrameCaptureFragment)) / 4);

                // Set last packet
                if (offset + data_size >= buf.size)
                {
                    packet->last = 1;
                }

                // Send packet
                vrcp_socket.reliable_send(reinterpret_cast<vrcp::VRCPBaseHeader *>(packet),
                                          sizeof(vrcp::VRCPFrameCaptureFragment) + packet_data_size,
                                          0);
            }

            measurement_bucket->add_saved_frame();
        }
    }

    void Client::Data::render_thread_main()
    {
        // Set thread name
        JNIEnv *env = nullptr;
        android_app->activity->vm->AttachCurrentThread(&env, nullptr);
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>("wvb_render"), 0, 0, 0);

        ApplicationInfo app_info {
            .android_app = android_app,
        };
        vr_system.init(app_info);

        LOG("-- WVB client initialized --");
        state = ClientState::CONNECTING; // The client can now try to connect

        const uint8_t                        *previous_data           = nullptr;
        size_t                                previous_size           = 0;
        uint32_t                              previous_frame_index    = 0;
        uint32_t                              previous_timestamp      = 0;
        uint32_t                              previous_pose_timestamp = 0;
        std::chrono::steady_clock::time_point previous_last_packet_received_time;
        bool                                  previous_save_frame = false;
        bool                                  end_of_stream       = false;
        uint32_t                              push_cooldown       = 0;

        ClientFrameTimeMeasurements frame_time {};

        while (!should_exit())
        {
            previous_data           = nullptr;
            previous_size           = 0;
            previous_frame_index    = 0;
            previous_timestamp      = 0;
            previous_pose_timestamp = 0;
            previous_save_frame     = false;
            end_of_stream           = false;
            push_cooldown           = 0;

            // Wait for setup phases to finish
            while (!is_running() && !should_exit())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

            LOG("Starting render loop\n");

            while (!should_soft_exit())
            {
                vr_system.handle_events();

                const bool has_decoder = vr_system.init_decoder();

                const bool running = vr_system.new_frame(frame_time);
                if (!running)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }

                // Poll for new packets
                bool should_try_again = true;
                while (has_decoder && push_cooldown == 0 && should_try_again)
                {
                    should_try_again = false;

                    const uint8_t                        *data           = nullptr;
                    size_t                                size           = 0;
                    uint32_t                              frame_index    = 0;
                    uint32_t                              timestamp      = 0;
                    uint32_t                              pose_timestamp = 0;
                    std::chrono::steady_clock::time_point last_packet_received_time;
                    bool                                  save_frame = false;
                    bool                                  eos        = false;

                    if (end_of_stream)
                    {
                        // After the end of stream, repeat the last packet
                        // Otherwise, some frames get stuck in the buffer
                        bool pushed = vr_system.push_frame_data(
                            previous_data,
                            previous_size,
                            previous_frame_index,
                            false,
                            previous_timestamp,
                            previous_pose_timestamp,
                            rtp_clock->to_rtp_timestamp(rtp_clock->from_steady_timepoint(previous_last_packet_received_time)),
                            previous_save_frame);
                        if (!pushed)
                        {
                            push_cooldown = 10;
                        }
                    }
                    else if (video_socket.receive_packet(&data,
                                                         &size,
                                                         &frame_index,
                                                         &eos,
                                                         &timestamp,
                                                         &pose_timestamp,
                                                         &last_packet_received_time,
                                                         &save_frame))
                    {
                        // Compute number of dropped frames since last one
                        // It could have been dropped by the driver, server, or depacketizer
                        if (frame_index > previous_frame_index)
                        {
                            auto nb_dropped = frame_index - previous_frame_index - 1;
                            measurement_bucket->add_dropped_frames(nb_dropped);
                        }

                        previous_data                      = data;
                        previous_size                      = size;
                        previous_frame_index               = frame_index;
                        previous_timestamp                 = timestamp;
                        previous_pose_timestamp            = pose_timestamp;
                        previous_last_packet_received_time = last_packet_received_time;
                        previous_save_frame                = save_frame;
                        end_of_stream                      = eos;

                        // Convert to RTP
                        const auto last_packet_received_timestamp =
                            rtp_clock->to_rtp_timestamp(rtp_clock->from_steady_timepoint(previous_last_packet_received_time));

                        // Decode packet
                        vr_system.push_frame_data(previous_data,
                                                  previous_size,
                                                  previous_frame_index,
                                                  false,
                                                  previous_timestamp,
                                                  previous_pose_timestamp,
                                                  last_packet_received_timestamp,
                                                  previous_save_frame);

                        if (!end_of_stream)
                        {
                            // We will reuse the last packet so don't release it yet
                            video_socket.release_frame_data();

                            // Try again to see if more frames can be sent to decoder
                            // It's better if these frames wait in the decoder (where they can be processed)
                            // than in the depacketizer where they just idle
                            should_try_again = EMPTY_DEPACKETIZER_EACH_FRAME;
                        }
                    }
                }

                if (push_cooldown > 0)
                {
                    push_cooldown--;
                }

                // Render frame
                vr_system.render(frame_time);

                // Save frame if needed
                save_and_send_frame_if_needed();

                // Send tracking update
                send_tracking_update();

                if (measurement_bucket)
                {
                    frame_time.frame_delay = vr_system.get_decoder_frame_delay();
                    measurement_bucket->add_frame_time_measurement(frame_time);
                }
            }

            if (end_of_stream)
            {
                video_socket.release_frame_data();
            }

            if (state == ClientState::SOFT_SHUTDOWN)
            {
                vr_system.soft_shutdown();
                sent_eos = false;
            }
        }

        android_app->activity->vm->DetachCurrentThread();
    }

    void Client::Data::syncing_thread_main()
    {
        // Set thread name
        JNIEnv *env = nullptr;
        android_app->activity->vm->AttachCurrentThread(&env, nullptr);
        prctl(PR_SET_NAME, reinterpret_cast<unsigned long>("wvb_sync"), 0, 0, 0);

        uint16_t                  ping_id             = 0;
        uint16_t                  nb_received_replies = 0;
        std::chrono::microseconds round_trip_time {0};

        const vrcp::VRCPBaseHeader *packet = nullptr;
        size_t                      size   = 0;

#if PING_COUNT > 0
        while (!should_exit())
        {
            // Send ping
            vrcp::VRCPPing ping {
                .ping_id = htons(++ping_id),
            };
            const auto send_time = std::chrono::high_resolution_clock::now();
            vrcp_socket.unreliable_send(reinterpret_cast<vrcp::VRCPBaseHeader *>(&ping), sizeof(ping));

            // Wait for reply
            bool reply_received = false;
            while (!should_exit() && !reply_received)
            {
                // Process received packets
                while (!should_exit() && vrcp_socket.unreliable_receive(&packet, &size))
                {
                    if (packet->ftype == vrcp::VRCPFieldType::PING_REPLY)
                    {
                        const auto  reply_time     = std::chrono::high_resolution_clock::now();
                        const auto  reply_time_rtp = rtp_clock->now();
                        const auto *reply          = reinterpret_cast<const vrcp::VRCPPingReply *>(packet);
                        if (size == sizeof(vrcp::VRCPPingReply))
                        {
                            const auto packet_ping_id = ntohs(reply->ping_id);
                            if (ping_id == packet_ping_id)
                            {
                                // Valid reply
                                reply_received = true;
                                nb_received_replies++;
                                round_trip_time   = std::chrono::duration_cast<std::chrono::microseconds>(reply_time - send_time);
                                const auto offset = round_trip_time / 2;
                                const auto expected_timestamp = ntohl(reply->reply_timestamp)
                                                                + std::chrono::duration_cast<rtp::RTPClock::duration>(offset).count();
                                // Compute error, the amount that has to be added to the actual timestamp to reach the expected
                                // timestamp
                                const auto error = rtp::rtp_timestamps_distance_us(reply_time_rtp.time_since_epoch().count(),
                                                                                   expected_timestamp,
                                                                                   *rtp_clock);

                                // Update clock. we need to use the opposite of the error, since moving the epoch back in time
                                // increases the value of the timestamps. So, for example, to remove 5 seconds from the timestamp, we
                                // need to advance the epoch by 5 seconds.
                                rtp_clock->move_epoch(-error);

                                if (measurement_bucket)
                                {
                                    measurement_bucket->add_network_measurement({
                                        .rtt_us         = static_cast<uint32_t>(round_trip_time.count()),
                                        .clock_error_us = static_cast<int32_t>(error.count()),
                                    });
                                }
                            }
                        }
                    }
                }

                if (!reply_received)
                {
                    const auto now = std::chrono::high_resolution_clock::now();
                    if (now - send_time > PING_TIMEOUT_MS)
                    {
                        // Timeout. Move on to the next ping
                        break;
                    }

                    // Don't sleep, as we want an accurate RTT measurement
                }
            }

            if (nb_received_replies >= PING_COUNT || ping_id >= PING_COUNT * 2)
            {
                break;
            }

            // Wait before sending the next ping
            std::this_thread::sleep_for(PING_INTERVAL_MS);
        }
#endif

        vrcp::VRCPSyncFinished sync_finished {};
        vrcp_socket.reliable_send(reinterpret_cast<vrcp::VRCPBaseHeader *>(&sync_finished), sizeof(sync_finished));

        uint32_t min_rtt = 0;
        uint32_t max_rtt = 0;
        uint32_t avg_rtt = 0;
        uint32_t med_rtt = 0;
        measurement_bucket->get_rtt_stats(min_rtt, max_rtt, avg_rtt, med_rtt);

        uint32_t min_clock_error = 0;
        uint32_t max_clock_error = 0;
        uint32_t med_clock_error = 0;
        measurement_bucket->get_clock_error_stats(min_clock_error, max_clock_error, med_clock_error);

        LOG("Synced with server (min RTT: %u us, max RTT: %u us, avg RTT: %u us, med RTT: %u us | min err: %u us, max err: %u us, med "
            "err: %u us)\n",
            min_rtt,
            max_rtt,
            avg_rtt,
            med_rtt,
            min_clock_error,
            max_clock_error,
            med_clock_error);
        LOG("Ready to start the app...\n");

        state = ClientState::RUNNING;
        android_app->activity->vm->DetachCurrentThread();
    }

    void Client::Data::soft_shutdown()
    {
        if (state == ClientState::RUNNING)
        {
            state = ClientState::SOFT_SHUTDOWN;
        }
    }

    void Client::Data::send_measurements()
    {
        if (!measurement_bucket || !measurement_bucket->measurements_complete())
        {
            return;
        }

        // Start resetting the system
        soft_shutdown();

        std::vector<vrcp::VRCPSocketMeasurement> vrcp_socket_measurements;
        const auto                              &socket_measurements = measurement_bucket->get_socket_measurements();
        for (const auto &socket_measurement : socket_measurements)
        {
            vrcp_socket_measurements.emplace_back(socket_measurement);
        }
        vrcp_socket.reliable_send(reinterpret_cast<vrcp::VRCPBaseHeader *>(vrcp_socket_measurements.data()),
                                  vrcp_socket_measurements.size() * sizeof(vrcp::VRCPSocketMeasurement));

        std::vector<vrcp::VRCPNetworkMeasurement> vrcp_network_measurements;
        const auto                               &network_measurements = measurement_bucket->get_network_measurements();
        for (const auto &network_measurement : network_measurements)
        {
            vrcp_network_measurements.emplace_back(network_measurement);
        }
        vrcp_socket.reliable_send(reinterpret_cast<vrcp::VRCPBaseHeader *>(vrcp_network_measurements.data()),
                                  vrcp_network_measurements.size() * sizeof(vrcp::VRCPNetworkMeasurement));

        std::vector<vrcp::VRCPFrameTimeMeasurement> vrcp_frame_time_measurements;
        const auto                                 &frame_time_measurements = measurement_bucket->get_frame_time_measurements();
        for (const auto &frame_time_measurement : frame_time_measurements)
        {
            vrcp_frame_time_measurements.emplace_back(frame_time_measurement);
        }
        vrcp_socket.reliable_send(reinterpret_cast<vrcp::VRCPBaseHeader *>(vrcp_frame_time_measurements.data()),
                                  vrcp_frame_time_measurements.size() * sizeof(vrcp::VRCPFrameTimeMeasurement));

        std::vector<vrcp::VRCPImageQualityMeasurement> vrcp_image_quality_measurements;
        const auto &image_quality_measurements = measurement_bucket->get_image_quality_measurements();
        for (const auto &image_quality_measurement : image_quality_measurements)
        {
            vrcp_image_quality_measurements.emplace_back(image_quality_measurement);
        }
        vrcp_socket.reliable_send(reinterpret_cast<vrcp::VRCPBaseHeader *>(vrcp_image_quality_measurements.data()),
                                  vrcp_image_quality_measurements.size() * sizeof(vrcp::VRCPImageQualityMeasurement));

        std::vector<vrcp::VRCPTrackingTimeMeasurement> vrcp_tracking_time_measurements;
        const auto                                    &tracking_time_measurements = measurement_bucket->get_tracking_measurements();
        for (const auto &tracking_time_measurement : tracking_time_measurements)
        {
            vrcp_tracking_time_measurements.emplace_back(tracking_time_measurement);
        }
        vrcp_socket.reliable_send(reinterpret_cast<vrcp::VRCPBaseHeader *>(vrcp_tracking_time_measurements.data()),
                                  vrcp_tracking_time_measurements.size() * sizeof(vrcp::VRCPTrackingTimeMeasurement));

        // We sent everything
        vrcp::VRCPMeasurementTransferFinished packet {
            .decoder_frame_delay  = static_cast<uint8_t>(measurement_bucket->get_decoder_frame_delay()),
            .nb_dropped_frames    = htonl(measurement_bucket->get_nb_dropped_frames()),
            .nb_catched_up_frames = htonl(measurement_bucket->get_nb_catched_up_frames()),
        };
        vrcp_socket.reliable_send(reinterpret_cast<vrcp::VRCPBaseHeader *>(&packet), sizeof(packet));

        LOG("Delay: %u vs %u\n", measurement_bucket->get_decoder_frame_delay(), vr_system.get_decoder_frame_delay());

        // TODO reset and get ready for the next pass
        measurement_bucket->reset();
    }

    // =======================================================================================
    // =                                         API                                         =
    // =======================================================================================

    Client::Client()
    {
        auto rtp_clock          = std::make_shared<rtp::RTPClock>();
        auto measurement_bucket = std::make_shared<ClientMeasurementBucket>();
        measurement_bucket->set_clock(rtp_clock);

        m_data = new Data {
            .measurement_bucket = measurement_bucket,
            .rtp_clock          = rtp_clock,
            .vr_system          = VRSystem(rtp_clock, std::move(measurement_bucket)),
        };
    }
    DEFAULT_PIMPL_DESTRUCTOR(Client);

    bool Client::init(const ApplicationInfo &app_info)
    {
        if (m_data->state != ClientState::UNINITIALIZED)
        {
            return true;
        }

        // Load modules
        m_data->android_app = app_info.android_app;
        m_data->modules     = load_modules();

        // Start render thread
        m_data->render_thread = std::thread(&Client::Data::render_thread_main, m_data);

        return true;
    }

    bool Client::update()
    {
        if (m_data->state == ClientState::UNINITIALIZED)
        {
            // Ignore this call
            return true;
        }

        bool connected = is_connected();

        if (!connected && m_data->is_connecting())
        {
            m_data->select_server();

            if (!m_data->is_running())
            {
                // Never connected
                // Try to connect to server
                if (m_data->connect())
                {
                    // Connected
                    connected = true;
                }
            }
            else
            {
                // Lost connection
                m_data->state = ClientState::SHUTDOWN;
                return false;
            }
        }

        if (connected)
        {
            m_data->poll_vrcp_socket();

            if (m_data->is_running())
            {
                // Poll for new packets
                m_data->video_socket.update();

                if (m_data->measurement_bucket->measurements_complete())
                {
                    if (m_data->measurement_bucket->has_saved_frames())
                    {
                        m_data->send_measurements();
                    }
                }
            }
        }

        return !m_data->should_exit();
    }

    const std::vector<VRCPServerCandidate> &Client::available_servers() const
    {
        return m_data->vrcp_socket.available_servers();
    }

    void Client::connect(const SocketAddr &addr)
    {
        m_data->server_addr = addr;
    }

    bool Client::is_connected() const
    {
        return m_data && (m_data->is_running() || m_data->is_syncing() || m_data->is_soft_shutdown())
               && m_data->vrcp_socket.is_connected_refresh() && m_data->video_socket.is_connected();
    }

    void Client::shutdown()
    {
        if (m_data->state == ClientState::UNINITIALIZED)
        {
            return;
        }

        m_data->state = ClientState::SHUTDOWN;

        if (m_data->render_thread.joinable())
        {
            m_data->render_thread.join();
        }

        m_data->vr_system.shutdown();

        if (m_data->syncing_thread.joinable())
        {
            m_data->syncing_thread.join();
        }

        // Reset state
        m_data->state = ClientState::UNINITIALIZED;
    }

} // namespace wvb::client