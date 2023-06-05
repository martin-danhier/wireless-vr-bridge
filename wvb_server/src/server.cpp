#include "wvb_server/server.h"

#include <wvb_common/benchmark.h>
#include <wvb_common/module.h>
#include <wvb_common/network_utils.h>
#include <wvb_common/rtp.h>
#include <wvb_common/server_shared_state.h>
#include <wvb_common/socket.h>
#include <wvb_common/subprocess.h>
#include <wvb_common/video_socket.h>
#include <wvb_common/vrcp_socket.h>
#include <wvb_server/video_pipeline.h>

#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <thread>

#define VIDEO_PORT                PORT_AUTO
#define EXPORT_FILE_TABLE_DIVIDER "---"
#define EXPORT_FILE_SERVER_ID     "server"
#define EXPORT_FILE_DRIVER_ID     "driver"
#define EXPORT_FILE_CLIENT_ID     "client"

namespace wvb::server
{
    // =======================================================================================
    // =                                       Structs                                       =
    // =======================================================================================

    struct Server::Data
    {
        std::vector<Module> modules;
        Module              chosen_module;

        AppSettings settings;
        uint32_t    current_pass = 0;
        uint32_t    current_run  = 0;

        uint32_t image_offset         = 0;
        uint32_t total_received_bytes = 0;

        // Client communication
        std::vector<InetAddr>              bcast_addrs;
        VRCPSocket                         client_vrcp_socket;
        std::shared_ptr<ServerVideoSocket> video_socket = nullptr;
        VRCPClientParams                   client_params {0};
        uint16_t                           video_ssrc = 0;
        uint64_t                           ntp_epoch  = 0;
        rtp::RTPClock                      rtp_clock;

        // Driver communication
        Subprocess                                driver_process;
        std::shared_ptr<ServerDriverSharedMemory> shared_memory = nullptr;
        std::shared_ptr<DriverEvents>             driver_events = nullptr;
        std::shared_ptr<ServerEvents>             server_events = nullptr;

        // Processing
        const char                    *shader_dir_path = nullptr;
        VideoPipeline                  video_pipeline;
        std::shared_ptr<IVideoEncoder> video_encoder = nullptr;

        // States
        DriverConnectionState driver_connection_state = DriverConnectionState::AWAITING_DRIVER;
        ClientConnectionState client_connection_state = ClientConnectionState::AWAITING_CLIENT;
        AppState              app_state               = AppState::NOT_READY;
        bool                  should_stop             = false;

        // Tracking
        std::optional<uint32_t> latest_tracking_timestamp = std::nullopt;

        // Benchmark
        std::shared_ptr<ServerMeasurementBucket> measurement_bucket        = std::make_shared<ServerMeasurementBucket>();
        std::unique_ptr<DriverMeasurementBucket> driver_measurement_bucket = nullptr;
        std::unique_ptr<ClientMeasurementBucket> client_measurement_bucket = nullptr;

        // Capture
        IOBuffer capture_buffer;

        // ---------------------------------------------------------------------------------------

        void handle_driver_state_changed();
        void handle_new_driver_measurements();
        void setup_codec(std::string codec_id);
        void handle_vrcp_packet(const vrcp::VRCPBaseHeader *header, size_t size);
        void poll_vrcp();
        bool connect_to_client();
        void setup_benchmark_window();
        void launch_driver();
        void kill_driver();
        void ensure_client_bucket_exists();
        /** If all measurements were received, save them and move on to the next pass. */
        void handle_measurements_received();
    };

    // =======================================================================================
    // =                                   Implementation                                    =
    // =======================================================================================

    void Server::Data::handle_driver_state_changed()
    {
        // Get new value of driver state
        DriverState driver_state;
        {
            auto lock    = shared_memory->lock();
            driver_state = lock->driver_state;
        }

        // Handle the value
        if (driver_state == DriverState::NOT_RUNNING)
        {
            if (driver_connection_state != DriverConnectionState::AWAITING_DRIVER)
            {
                // Lost connection to driver
                LOG("Lost connection to driver.\n");
                FLUSH_LOG();
                app_state               = AppState::STANDBY;
                driver_connection_state = DriverConnectionState::AWAITING_DRIVER;
            }
        }
        else
        {
            if (driver_connection_state != DriverConnectionState::DRIVER_CONNECTED)
            {
                // Connected to driver
                LOG("Connected to driver.\n");
                FLUSH_LOG();
                driver_connection_state = DriverConnectionState::DRIVER_CONNECTED;

                if (client_connection_state == ClientConnectionState::AWAITING_CLIENT)
                {
                    // Connected to driver, but not client.
                    // We need the client to connect first to get the specs
                    // So close the driver connection
                    LOG("Driver connected, but not client. Killing driver.\n");
                    FLUSH_LOG();
                    app_state = AppState::NOT_READY;
                    kill_driver();
                }
            }
        }

        // Process more advanced states
        if (driver_state == DriverState::AWAITING_CLIENT_SPEC)
        {
            if (client_connection_state == ClientConnectionState::CLIENT_CONNECTED)
            {
                // Driver asks for client specs
                LOG("Sending client info\n");
                {
                    auto lock             = shared_memory->lock();
                    lock->vr_system_specs = client_params.specs;
                    lock->ntp_epoch       = ntp_epoch;
                }
                server_events->new_system_specs.signal();
            }
        }
        else if (driver_state == DriverState::READY)
        {
            if (app_state == AppState::RUNNING)
            {
                // Driver is not running anymore
                app_state = AppState::STANDBY;
            }
            else
            {
                LOG("Driver is ready\n");
                FLUSH_LOG();
            }
        }
        else if (driver_state == DriverState::RUNNING)
        {
            LOG("Driver is running\n");
            if (client_connection_state == ClientConnectionState::CLIENT_CONNECTED)
            {
                {
                    auto lock          = shared_memory->lock();
                    lock->server_state = ServerState::RUNNING;
                }
                server_events->server_state_changed.signal();
                video_pipeline.start_worker_thread();
                app_state = AppState::RUNNING;

                if (settings.app_mode == AppMode::BENCHMARK)
                {
                    setup_benchmark_window();
                }
            }
            FLUSH_LOG();
        }
    }

    void Server::Data::handle_new_driver_measurements()
    {
        {
            auto lock = shared_memory->lock();

            LOG("Received driver measurements\n");
            FLUSH_LOG();

            driver_measurement_bucket = std::make_unique<DriverMeasurementBucket>();
            driver_measurement_bucket->set_clock(std::make_shared<rtp::RTPClock>(ntp_epoch));
            driver_measurement_bucket->set_as_accept_all();

            for (uint32_t i = 0; i < lock->frame_time_measurements_count; i++)
            {
                driver_measurement_bucket->add_frame_time_measurement(lock->frame_time_measurements[i]);
            }

            for (uint32_t i = 0; i < lock->pose_access_time_measurements_count; i++)
            {
                driver_measurement_bucket->add_pose_access_measurement(lock->pose_access_time_measurements[i]);
            }

            for (uint32_t i = 0; i < lock->tracking_time_measurements_count; i++)
            {
                driver_measurement_bucket->add_tracking_time_measurement(lock->tracking_time_measurements[i]);
            }

            driver_measurement_bucket->set_as_finished();

            // Now that we have all the data locally, we can tell the driver that it is no longer needed
            lock->server_state = ServerState::PROCESSING_MEASUREMENTS;
        }
        server_events->server_state_changed.signal();

        handle_measurements_received();
    }

    void Server::Data::handle_vrcp_packet(const vrcp::VRCPBaseHeader *header, size_t size)
    {
        const auto now = rtp_clock.now_rtp_timestamp();

        // Ping
        if (header->ftype == vrcp::VRCPFieldType::PING)
        {
            const auto *ping = reinterpret_cast<const vrcp::VRCPPing *>(header);
            if (size == sizeof(vrcp::VRCPPing))
            {
                vrcp::VRCPPingReply reply {
                    .ping_id         = ping->ping_id,
                    .reply_timestamp = htonl(now),
                };
                client_vrcp_socket.unreliable_send(reinterpret_cast<vrcp::VRCPBaseHeader *>(&reply), sizeof(reply));
            }
        }
        // Tracking
        else if (header->ftype == vrcp::VRCPFieldType::TRACKING_DATA)
        {
            const auto *tracking_data = reinterpret_cast<const vrcp::VRCPTrackingData *>(header);
            if (size == sizeof(vrcp::VRCPTrackingData))
            {
                const auto timestamp = ntohl(tracking_data->sample_timestamp);
                if (!latest_tracking_timestamp.has_value()
                    || rtp::compare_rtp_timestamps(latest_tracking_timestamp.value(), timestamp)) // true if a < b
                {
                    latest_tracking_timestamp = timestamp;
                }
                else
                {
                    return;
                }

                {
                    auto lock = shared_memory->lock();
                    if (lock.is_valid())
                    {
                        tracking_data->to_tracking_state(lock->tracking_state);
                    }
                }
                server_events->new_tracking_data.signal();

                measurement_bucket->add_tracking_time_measurement({
                    .pose_timestamp               = ntohl(tracking_data->pose_timestamp),
                    .tracking_received_timestamp  = now,
                    .tracking_processed_timestamp = rtp_clock.now_rtp_timestamp(),
                });
            }
        }
        else if (header->ftype == vrcp::VRCPFieldType::SYNC_FINISHED)
        {
            if (size == sizeof(vrcp::VRCPSyncFinished) && client_connection_state == ClientConnectionState::SYNCING_CLOCKS)
            {
                LOG("Client synced.\n");
                if (driver_connection_state == DriverConnectionState::AWAITING_DRIVER)
                {
                    LOG("Awaiting driver...\n");
                }

                // We have a session, tell the driver that it can start at any time
                {
                    auto lock          = shared_memory->lock();
                    lock->server_state = ServerState::READY;
                }
                server_events->server_state_changed.signal();
                client_connection_state = ClientConnectionState::CLIENT_CONNECTED;

                launch_driver();

                // If the driver is waiting for client infos, send them
                handle_driver_state_changed();
                FLUSH_LOG();
            }
        }
        // Measurement reception
        else if (header->ftype == vrcp::VRCPFieldType::FRAME_TIME_MEASUREMENT)
        {
            if (size == sizeof(vrcp::VRCPFrameTimeMeasurement))
            {
                const auto                 *frame_time_vrcp = reinterpret_cast<const vrcp::VRCPFrameTimeMeasurement *>(header);
                ClientFrameTimeMeasurements frame_time;
                frame_time_vrcp->to_frame_time_measurements(frame_time);
                // Save measurement
                ensure_client_bucket_exists(); // Lazily create bucket when it is needed, typically after the measurement period
                client_measurement_bucket->add_frame_time_measurement(frame_time);
            }
        }
        else if (header->ftype == vrcp::VRCPFieldType::IMAGE_QUALITY_MEASUREMENT)
        {
            if (size == sizeof(vrcp::VRCPImageQualityMeasurement))
            {
                const auto              *image_quality_vrcp = reinterpret_cast<const vrcp::VRCPImageQualityMeasurement *>(header);
                ImageQualityMeasurements image_quality;
                image_quality_vrcp->to_image_quality_measurements(image_quality);
                // Save measurement
                ensure_client_bucket_exists(); // Lazily create bucket when it is needed, typically after the measurement period
                client_measurement_bucket->add_image_quality_measurement(std::move(image_quality));
            }
        }
        else if (header->ftype == vrcp::VRCPFieldType::TRACKING_TIME_MEASUREMENT)
        {
            if (size == sizeof(vrcp::VRCPFieldType::TRACKING_TIME_MEASUREMENT))
            {
                const auto              *tracking_time_vrcp = reinterpret_cast<const vrcp::VRCPTrackingTimeMeasurement *>(header);
                TrackingTimeMeasurements tracking_time;
                tracking_time_vrcp->to_tracking_time_measurements(tracking_time);
                // Save measurement
                ensure_client_bucket_exists(); // Lazily create bucket when it is needed, typically after the measurement period
                client_measurement_bucket->add_tracking_time_measurement(std::move(tracking_time));
            }
        }
        else if (header->ftype == vrcp::VRCPFieldType::NETWORK_MEASUREMENT)
        {
            if (size == sizeof(vrcp::VRCPNetworkMeasurement))
            {
                const auto         *network_measurement_vrcp = reinterpret_cast<const vrcp::VRCPNetworkMeasurement *>(header);
                NetworkMeasurements network_measurement;
                network_measurement_vrcp->to_network_measurements(network_measurement);
                // Save measurement
                ensure_client_bucket_exists(); // Lazily create bucket when it is needed, typically after the measurement period
                client_measurement_bucket->add_network_measurement(std::move(network_measurement));
            }
        }
        else if (header->ftype == vrcp::VRCPFieldType::SOCKET_MEASUREMENT)
        {
            if (size == sizeof(vrcp::VRCPSocketMeasurement))
            {
                const auto        *socket_measurement_vrcp = reinterpret_cast<const vrcp::VRCPSocketMeasurement *>(header);
                SocketMeasurements socket_measurement;
                socket_measurement_vrcp->to_socket_measurements(socket_measurement);
                // Save measurement
                ensure_client_bucket_exists(); // Lazily create bucket when it is needed, typically after the measurement period
                client_measurement_bucket->add_socket_measurements(std::move(socket_measurement));
            }
        }
        else if (header->ftype == vrcp::VRCPFieldType::MEASUREMENT_TRANSFER_FINISHED)
        {
            if (size == sizeof(vrcp::VRCPMeasurementTransferFinished))
            {
                const auto *measurement_transfer_finished_vrcp =
                    reinterpret_cast<const vrcp::VRCPMeasurementTransferFinished *>(header);

                // We got all client measurements, we can proceed
                LOG("Received all client measurements\n");
                FLUSH_LOG();
                ensure_client_bucket_exists(); // Lazily create bucket when it is needed, typically after the measurement period
                client_measurement_bucket->set_decoder_frame_delay(measurement_transfer_finished_vrcp->decoder_frame_delay);
                client_measurement_bucket->set_nb_dropped_frames(ntohl(measurement_transfer_finished_vrcp->nb_dropped_frames));
                client_measurement_bucket->set_nb_catched_up_frames(ntohl(measurement_transfer_finished_vrcp->nb_catched_up_frames));

                client_measurement_bucket->set_as_finished();
                handle_measurements_received();
            }
        }
        else if (header->ftype == vrcp::VRCPFieldType::FRAME_CAPTURE_FRAGMENT)
        {
            if (size >= sizeof(vrcp::VRCPFrameCaptureFragment))
            {
                const auto *frame_capture_fragment_vrcp = reinterpret_cast<const vrcp::VRCPFrameCaptureFragment *>(header);
                if (ntohl(frame_capture_fragment_vrcp->size) <= size - sizeof(vrcp::VRCPFrameCaptureFragment)
                    && ntohl(frame_capture_fragment_vrcp->full_size) > 0)
                {
                    if (capture_buffer.data != nullptr && capture_buffer.size != ntohl(frame_capture_fragment_vrcp->full_size))
                    {
                        LOG("Capture buffer size mismatch\n");
                        capture_buffer = {};
                    }

                    // Allocate capture buffer
                    if (capture_buffer.data == nullptr && ntohl(frame_capture_fragment_vrcp->offset) == 0)
                    {
                        ensure_client_bucket_exists();
                        LOG("Allocated capture buffer for saved frame %d\n", client_measurement_bucket->get_nb_saved_frames());
                        capture_buffer.data = new uint8_t[ntohl(frame_capture_fragment_vrcp->full_size)];
                        capture_buffer.size = ntohl(frame_capture_fragment_vrcp->full_size);
                        image_offset        = 0;
                    }

                    // Copy data
                    if (capture_buffer.data != nullptr)
                    {
                        if (ntohl(frame_capture_fragment_vrcp->offset) != image_offset)
                        {
                            LOG("Capture buffer offset mismatch\n");
                            // image_offset = ntohl(frame_capture_fragment_vrcp->offset);
                            // Skip this one.
                            ensure_client_bucket_exists();
                            client_measurement_bucket->add_saved_frame();
                            capture_buffer = {};
                            image_offset   = 0;
                            return;
                        }

                        // Prevent overflow by taking the minimum size
                        auto copied_size = std::min((int) ntohl(frame_capture_fragment_vrcp->size),
                                                    (int) (capture_buffer.size - ntohl(frame_capture_fragment_vrcp->offset)));

                        memcpy(capture_buffer.data + ntohl(frame_capture_fragment_vrcp->offset),
                               frame_capture_fragment_vrcp + 1,
                               copied_size);
                        total_received_bytes += copied_size;
                        image_offset += copied_size;

                        if (frame_capture_fragment_vrcp->last != 0)
                        {
                            // Got the last fragment, save the capture
                            ensure_client_bucket_exists();
                            client_measurement_bucket->add_saved_frame();
                            std::string filename = "wvb_capture_pass_" + std::to_string(current_pass) + "_run_"
                                                   + std::to_string(current_run) + "_client_"
                                                   + std::to_string(client_measurement_bucket->get_nb_saved_frames() - 1) + ".rgba";

                            std::ofstream file(filename, std::ios::binary);
                            if (file.is_open())
                            {
                                file.write((const char *) capture_buffer.data, capture_buffer.size);
                                file.close();
                            }

                            capture_buffer = {};
                        }
                    }
                }
            }
            else
            {
                LOG("Invalid frame capture fragment size\n");
            }
        }
        else if (header->ftype == vrcp::VRCPFieldType::INVALID)
        {
            LOG("Invalid packet received\n");
        }
        else
        {
            LOG("Unknown packet type: %d\n", header->ftype);
        }
    }

    void Server::Data::poll_vrcp()
    {
        const vrcp::VRCPBaseHeader *header = nullptr;
        size_t                      size   = 0;
        if (!client_vrcp_socket.is_connected())
        {
            return;
        }

        while (client_vrcp_socket.reliable_receive(&header, &size))
        {
            handle_vrcp_packet(header, size);
        }

        while (client_vrcp_socket.unreliable_receive(&header, &size))
        {
            handle_vrcp_packet(header, size);
        }
    }

    void Server::Data::setup_codec(std::string codec_id)
    {
        // Codec changed
        if (chosen_module.codec_id != codec_id)
        {
            // Find module with new codec
            bool found = false;
            for (auto &module : modules)
            {
                if (module.codec_id == codec_id)
                {
                    chosen_module = module;
                    found         = true;
                    break;
                }
            }

            // No module found. Shouldn't happen if VRCP is working correctly
            if (!found)
            {
                throw std::runtime_error("No module found for codec \"" + codec_id + "\"");
            }

            LOG("Video codec: \"%s\"\n", chosen_module.name.c_str());

            // Free other modules in normal mode
            // In benchmark mode, we may need them for the next pass
            if (settings.app_mode == AppMode::NORMAL)
            {
                for (auto &module : modules)
                {
                    if (module.handle != nullptr && module.handle != chosen_module.handle)
                    {
                        module.close();
                    }
                }
                modules.clear();
            }
        }

        // Setup packetizer. Existing ones will be destroyed and recreated
#ifdef WVB_VIDEO_SOCKET_USE_RTP
        if (chosen_module.create_packetizer != nullptr)
        {
            // Choose random ssrc
            std::random_device              rd;
            std::mt19937                    gen(rd());
            std::uniform_int_distribution<> dis(0, 0xFFFF);
            video_ssrc = dis(gen);

            video_socket->set_packetizer(chosen_module.create_packetizer(video_ssrc));
        }
        else
        {
            throw std::runtime_error("No packetizer found for codec \"" + resp.chosen_video_codec + "\"");
        }
#else
        // Use simple stream-based packetizer
        video_socket->set_packetizer(nullptr);
#endif

        if (chosen_module.create_video_encoder == nullptr)
        {
            // TODO maybe skip pass instead
            throw std::runtime_error("No video encoder found for codec \"" + chosen_module.codec_id + "\"");
        }

        IVideoEncoder::EncoderCreateInfo create_info {
            .src_size        = {client_params.specs.eye_resolution.width * 2, client_params.specs.eye_resolution.height},
            .refresh_rate    = client_params.specs.refresh_rate,
            .shader_dir_path = shader_dir_path,
        };
        if (settings.app_mode == AppMode::BENCHMARK)
        {
            const auto &pass    = settings.benchmark_settings.passes[current_pass];
            create_info.bpp     = pass.codec_settings.bpp;
            create_info.delay   = pass.codec_settings.delay;
            create_info.bitrate = pass.codec_settings.bitrate;
        }
        video_encoder = chosen_module.create_video_encoder(create_info);

        // Create pipeline
        video_pipeline = VideoPipeline(shared_memory,
                                       driver_events,
                                       server_events,
                                       video_encoder,
                                       video_socket,
                                       client_params.specs,
                                       ntp_epoch,
                                       measurement_bucket,
                                       [this]()
                                       {
                                           // On worker thread stop
                                           if (settings.app_mode == AppMode::BENCHMARK && measurement_bucket->measurements_complete())
                                           {
                                               app_state = AppState::GATHERING_RESULTS;

                                               // Signal the driver that it can send the results
                                               {
                                                   auto lock          = shared_memory->lock();
                                                   lock->server_state = ServerState::AWAITING_DRIVER_MEASUREMENTS;
                                               }
                                               server_events->server_state_changed.signal();
                                           }
                                       });
    }

    bool Server::Data::connect_to_client()
    {
        VRCPServerParams params {
            .video_port = video_socket->local_addr().port,
        };

        // Check if preference is supported
        do
        {
            bool preferred_codec_found = false;
            for (auto &module : modules)
            {
                if (module.codec_id == settings.preferred_codec)
                {
                    preferred_codec_found = true;
                    break;
                }
            }

            // App mode
            if (settings.app_mode == AppMode::NORMAL)
            {
                if (preferred_codec_found)
                {
                    // Place preferred codec first
                    params.supported_video_codecs.push_back(settings.preferred_codec);
                }
                else
                {
                    LOGE("Preferred codec is not supported. Falling back to default.\n");
                }

                // Add the other codecs
                for (auto &module : modules)
                {
                    if (module.codec_id != settings.preferred_codec)
                    {
                        params.supported_video_codecs.push_back(module.codec_id);
                    }
                }

                if (params.supported_video_codecs.empty())
                {
                    LOGE("No codecs supported. Exiting.\n");
                    return false;
                }
            }
            else if (settings.app_mode == AppMode::BENCHMARK)
            {
                if (preferred_codec_found)
                {
                    // Only support the codec specified in the current pass
                    // At this point, we are still at the first one.
                    params.supported_video_codecs.push_back(modules[0].codec_id);
                }
                else
                {
                    LOGE("Pass #%u codec \"%s\" is not supported. Skipping pass.\n", current_pass, settings.preferred_codec.c_str());

                    // Skip pass
                    if (current_pass == settings.benchmark_settings.passes.size() - 1)
                    {
                        // No more passes
                        return false;
                    }
                    current_pass++;
                    current_run = 0;
                }
            }
            else
            {
                throw std::runtime_error("Unknown app mode");
            }
        } while (params.supported_video_codecs.empty());

        VRCPConnectResp resp {0};

        // Await valid client connection
        while (!should_stop)
        {
            if (client_vrcp_socket.listen(bcast_addrs, params, &client_params, &resp))
            {
                // Client connected
                ntp_epoch = resp.ntp_timestamp;
                rtp_clock.set_epoch(ntp_epoch);
                measurement_bucket->set_clock(std::make_shared<rtp::RTPClock>(ntp_epoch));

                // Setup codec
                setup_codec(resp.chosen_video_codec);

                // Now, wait for the client to connect to the video socket
                SocketAddr client_video_addr {
                    .addr = client_vrcp_socket.peer_inet_addr(),
                    .port = resp.peer_video_port,
                };
                LOG("Awaiting %s to connect to %s\n",
                    wvb::to_string(client_video_addr).c_str(),
                    wvb::to_string(video_socket->local_addr()).c_str());

                while (!video_socket->listen(client_video_addr) && !should_stop)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                if (should_stop || !client_vrcp_socket.is_connected_refresh())
                {
                    return false;
                }

                LOG("Client connected. Syncing clocks...\n");
                FLUSH_LOG();
                client_connection_state = ClientConnectionState::SYNCING_CLOCKS;

                return true;
            }
            else
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        return false;
    }

    void Server::Data::launch_driver()
    {
        // Launch SteamVR
        LOG("Launching SteamVR...\n");
        FLUSH_LOG();

        // Start process
        driver_process.start();
    }

    void Server::Data::kill_driver() {}

    void Server::Data::ensure_client_bucket_exists()
    {
        if (client_measurement_bucket == nullptr)
        {
            client_measurement_bucket = std::make_unique<ClientMeasurementBucket>();
            client_measurement_bucket->set_clock(std::make_shared<rtp::RTPClock>(ntp_epoch));
            client_measurement_bucket->set_as_accept_all();
            LOG("Received first client measurement\n");
            FLUSH_LOG();
        }
    }

    void Server::Data::handle_measurements_received()
    {
        // Don't do anything if we didn't receive all measurements
        if (driver_measurement_bucket == nullptr || client_measurement_bucket == nullptr)
        {
            return;
        }
        // Driver is received in one go, so it is always complete if not null
        if (!client_measurement_bucket->measurements_complete())
        {
            return;
        }

        LOG("All measurements received. Exporting...\n");

        // Export measurement
        // const auto offset =
        //     rtp_clock.from_rtp_timestamp(0).time_since_epoch().count(); // should be added to timestamp before converting to time
        // rtp_clock.offset = -offset; // rtp clock's offset is added during time->timestamp conversion and subtracted during
        //                             // timestamp->time conversion, so we need to negate it here

        std::string filename =
            "wvb_measurements_pass_" + std::to_string(current_pass) + "_run_" + std::to_string(current_run) + ".csv";
        std::ofstream file(filename, std::ios::out | std::ios::trunc);
        if (!file.is_open())
        {
            LOGE("Failed to open output file %s\n", filename.c_str());
            return;
        }

        // Sockets
        file << "socket_measurements\n";
        SocketMeasurements::export_csv_header(file);
        SocketMeasurements::export_csv_body(file, measurement_bucket->get_socket_measurements(), EXPORT_FILE_SERVER_ID);
        SocketMeasurements::export_csv_body(file, client_measurement_bucket->get_socket_measurements(), EXPORT_FILE_CLIENT_ID);
        file << EXPORT_FILE_TABLE_DIVIDER << std::endl;

        // Driver
        file << "driver_frame_time_measurements\n";
        DriverFrameTimeMeasurements::export_csv(file, rtp_clock, driver_measurement_bucket->get_frame_time_measurements());
        file << EXPORT_FILE_TABLE_DIVIDER << std::endl;

        file << "driver_tracking_measurements\n";
        TrackingTimeMeasurements::export_csv(file, rtp_clock, driver_measurement_bucket->get_tracking_measurements());
        file << EXPORT_FILE_TABLE_DIVIDER << std::endl;

        file << "driver_pose_access_measurements\n";
        PoseAccessTimeMeasurements::export_csv(file, rtp_clock, driver_measurement_bucket->get_pose_access_measurements());
        file << EXPORT_FILE_TABLE_DIVIDER << std::endl;

        // Server
        file << "server_frame_time_measurements\n";
        ServerFrameTimeMeasurements::export_csv(file, rtp_clock, measurement_bucket->get_frame_time_measurements());
        file << EXPORT_FILE_TABLE_DIVIDER << std::endl;

        file << "server_tracking_measurements\n";
        TrackingTimeMeasurements::export_csv(file, rtp_clock, measurement_bucket->get_tracking_time_measurements());
        file << EXPORT_FILE_TABLE_DIVIDER << std::endl;

        file << "server_image_quality_measurements\n";
        ImageQualityMeasurements::export_csv(file, measurement_bucket->get_image_quality_measurements());
        file << EXPORT_FILE_TABLE_DIVIDER << std::endl;

        // Client
        file << "client_frame_time_measurements\n";
        ClientFrameTimeMeasurements::export_csv(file, rtp_clock, client_measurement_bucket->get_frame_time_measurements());
        file << EXPORT_FILE_TABLE_DIVIDER << std::endl;

        file << "client_tracking_measurements\n";
        TrackingTimeMeasurements::export_csv(file, rtp_clock, client_measurement_bucket->get_tracking_measurements());
        file << EXPORT_FILE_TABLE_DIVIDER << std::endl;

        file << "client_image_quality_measurements\n";
        ImageQualityMeasurements::export_csv(file, client_measurement_bucket->get_image_quality_measurements());
        file << EXPORT_FILE_TABLE_DIVIDER << std::endl;

        file << "network_measurements\n";
        NetworkMeasurements::export_csv(file, client_measurement_bucket->get_network_measurements());
        file << EXPORT_FILE_TABLE_DIVIDER << std::endl;

        // Misc
        file << "misc_measurements\n";
        export_misc_measurements_csv(file,
                                     measurement_bucket->get_dropped_frames(),
                                     client_measurement_bucket->get_nb_dropped_frames(),
                                     client_measurement_bucket->get_nb_catched_up_frames(),
                                     video_encoder->get_frame_delay(),
                                     client_measurement_bucket->get_decoder_frame_delay());
        file << EXPORT_FILE_TABLE_DIVIDER << std::endl;

        file.close();

        LOG("Measurements exported to %s\n", filename.c_str());
        FLUSH_LOG();

        // Move on to next run
        current_run++;
        if (current_run == settings.benchmark_settings.passes[current_pass].num_repetitions)
        {
            // Move on to next pass
            current_pass++;
            current_run = 0;
        }
        if (current_pass == settings.benchmark_settings.passes.size())
        {
            // All passes done
            LOG("All passes finished.\n");
            FLUSH_LOG();
            should_stop = true;
            return;
        }

        const auto &pass = settings.benchmark_settings.passes[current_pass];

        // Check if codec is supported
        bool codec_supported = false;
        for (const auto &module : modules)
        {
            if (module.codec_id == pass.codec_id)
            {
                // Codec is supported
                codec_supported = true;
                break;
            }
        }

        if (!codec_supported)
        {
            LOGE("Codec %s is not supported by the server.\n", pass.codec_id.c_str());
            // TODO skip pass
            should_stop = true;
            return;
        }

        // Share new parameters to client
        {
            const size_t video_codec_size   = std::min(pass.codec_id.length(), (size_t) 32) + 2;
            const size_t packet_size        = sizeof(vrcp::VRCPNextPass) + video_codec_size;
            const size_t padded_packet_size = (packet_size + 3) & ~3;
            auto        *packet             = new uint8_t[padded_packet_size];
            auto        *next_pass          = (vrcp::VRCPNextPass *) packet;

            *next_pass = {
                .n_rows = static_cast<uint8_t>(padded_packet_size / 4),
                .pass   = static_cast<uint8_t>(current_pass),
                .run    = static_cast<uint8_t>(current_run),
            };
            // Add TLV field
            auto *field   = (vrcp::VRCPAdditionalField *) (packet + sizeof(vrcp::VRCPNextPass));
            field->type   = vrcp::VRCPFieldType::CHOSEN_VIDEO_CODEC_TLV;
            field->length = video_codec_size - 2;
            memcpy(field->value, pass.codec_id.c_str(), field->length);
            // Pad with zeros
            memset(packet + packet_size, 0, padded_packet_size - packet_size);

            client_vrcp_socket.reliable_send(reinterpret_cast<vrcp::VRCPBaseHeader *>(packet), padded_packet_size);
            delete[] packet;
        }

        // Reset
        measurement_bucket->reset();
        client_measurement_bucket = nullptr;
        driver_measurement_bucket = nullptr;
        latest_tracking_timestamp = std::nullopt;

        // Wait for a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(settings.benchmark_settings.duration_inter_run_interval_ms));

        LOG("Restarting server for pass %d, run %d\n", current_pass, current_run);
        FLUSH_LOG();

        setup_codec(pass.codec_id);

        // Tell driver to restart
        {
            auto lock = shared_memory->lock();
            if (lock.is_valid())
            {
                lock->server_state       = ServerState::READY;
                lock->tracking_state     = {};
                lock->measurement_window = {};
            }
        }
        server_events->server_state_changed.signal();
        launch_driver();
    }

    void Server::Data::setup_benchmark_window()
    {
        const auto &pass = settings.benchmark_settings.passes[current_pass];

        measurement_bucket->set_pass_id(current_pass);
        measurement_bucket->set_run_id(current_run);

        // The window will start after an initial delay.
        // This delay allows the benchmark info to be shared with the client and driver, and allows the app
        // to start.
        auto start_time = rtp_clock.now();
        start_time += std::chrono::milliseconds(pass.duration_startup_phase_ms);

        // Once the first measurement phase is over, switch to the next phase
        auto start_frame_quality_phase = start_time + std::chrono::milliseconds(pass.duration_timing_phase_ms);

        // End measurements after the last phase
        auto end_measurements_time = start_frame_quality_phase + std::chrono::milliseconds(pass.duration_frame_quality_phase_ms);

        auto end_time = end_measurements_time + std::chrono::milliseconds(pass.duration_end_margin_ms);

        // Create window
        MeasurementWindow window {
            .start_timing_phase        = start_time,
            .start_image_quality_phase = start_frame_quality_phase,
            .end_measurements          = end_measurements_time,
            .end                       = end_time,
        };

        // Communicate to other components
        {
            auto lock                = shared_memory->lock();
            lock->measurement_window = window;
        }
        server_events->new_benchmark_data.signal();

        measurement_bucket->set_window(window);

        vrcp::VRCPBenchmarkInfo info(window, rtp_clock);
        client_vrcp_socket.reliable_send(reinterpret_cast<vrcp::VRCPBaseHeader *>(&info), sizeof(info));
    }

    // =======================================================================================
    // =                                         API                                         =
    // =======================================================================================

    Server::Server(AppSettings &&settings, const char *shader_dir_path)
        : m_data(new Data {
            .settings        = std::move(settings),
            .shader_dir_path = shader_dir_path,
        })
    {
        if (shader_dir_path == nullptr)
        {
            throw std::runtime_error("Shader dir path cannot be null");
        }

        // Validate settings
        if (m_data->settings.app_mode == AppMode::NORMAL && m_data->settings.preferred_codec.empty())
        {
            throw std::runtime_error("Preferred codec cannot be empty");
        }
        else if (m_data->settings.app_mode == AppMode::BENCHMARK)
        {
            if (m_data->settings.benchmark_settings.passes.empty())
            {
                throw std::runtime_error("At least one benchmark pass is required");
            }
            for (const auto &pass : m_data->settings.benchmark_settings.passes)
            {
                if (pass.codec_id.empty())
                {
                    throw std::runtime_error("Benchmark pass codec cannot be empty");
                }
                if (pass.num_repetitions == 0)
                {
                    throw std::runtime_error("Benchmark pass repetitions cannot be 0");
                }
            }
        }

        // Log settings
        LOG("Mode: %s\n", wvb::to_string(m_data->settings.app_mode).c_str());

        // Init sockets
        m_data->video_socket = std::make_shared<ServerVideoSocket>(VIDEO_PORT, m_data->measurement_bucket);
        m_data->client_vrcp_socket =
            VRCPSocket::create_server(3, PORT_AUTO, PORT_AUTO, PORT_AUTO, VRCP_DEFAULT_ADVERTISEMENT_PORT, m_data->measurement_bucket);

        // Find broadcast addresses
        m_data->bcast_addrs = wvb::get_broadcast_addresses();
        // Print them
        LOG("Broadcast addresses:\n");
        for (const auto &addr : m_data->bcast_addrs)
        {
            LOG(" - %s\n", wvb::to_string(addr).c_str());
        }
        // Init shared memory and events
        m_data->shared_memory =
            std::make_shared<ServerDriverSharedMemory>(WVB_SERVER_DRIVER_MUTEX_NAME, WVB_SERVER_DRIVER_MEMORY_NAME);
        m_data->server_events = std::make_shared<ServerEvents>(true);
        m_data->driver_events = std::make_shared<DriverEvents>(false);

        // Set shared state accordingly, but driver should be started after the client is connected as there is no way to defer device
        // creation
        {
            auto lock          = m_data->shared_memory->lock();
            lock->server_state = ServerState::AWAITING_CONNECTION;
        }
        m_data->server_events->server_state_changed.signal();

        // Check if driver is connected
        m_data->handle_driver_state_changed();

        m_data->modules = load_modules();

        // Prepare driver subprocess
        std::string steamvr_path = settings.steamvr_path;
        if (steamvr_path.empty())
        {
            steamvr_path = WVB_DEFAULT_STEAMVR_PATH;
        }
        if (steamvr_path[steamvr_path.size() - 1] != '/' && steamvr_path[steamvr_path.size() - 1] != '\\')
        {
            steamvr_path += '\\';
        }
        steamvr_path += WVB_STEAMVR_EXE_PATH;

        m_data->driver_process = Subprocess(steamvr_path, std::string(WVB_DEFAULT_STEAMVR_PATH));

        // Log
        LOG("Server started.\nAwaiting connection from client...\n");
        FLUSH_LOG();
    }

    Server::~Server()
    {
        FLUSH_LOG();
        FLUSH_LOGE();
        if (m_data != nullptr)
        {
            m_data->video_pipeline.send_kill_signal();

            {
                auto lock          = m_data->shared_memory->lock();
                lock->server_state = ServerState::NOT_RUNNING;
            }
            m_data->server_events->server_state_changed.signal();

            // Stop pipeline
            m_data->video_pipeline.join();

            delete m_data;
            m_data = nullptr;
        }
    }

    void Server::run()
    {
        // Block until client is connected
        if (!m_data->connect_to_client())
        {
            LOGE("Failed to connect to client\n");
            return;
        }

        // Main event loop
        while (!m_data->should_stop)
        {
            // Poll driver
            DriverEvent driver_event = DriverEvent::NO_EVENT;
            while (m_data->driver_events->poll(driver_event))
            {
                switch (driver_event)
                {
                    case DriverEvent::DRIVER_STATE_CHANGED: m_data->handle_driver_state_changed(); break;
                    case DriverEvent::NEW_MEASUREMENTS: m_data->handle_new_driver_measurements(); break;
                    default: break;
                }
            }

            if (!m_data->client_vrcp_socket.is_connected_refresh())
            {
                m_data->should_stop = true;
            }

            m_data->poll_vrcp();

            // If no app is running, sleep longer to avoid wasting CPU
            if (m_data->app_state == AppState::NOT_READY && m_data->client_connection_state != ClientConnectionState::SYNCING_CLOCKS)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            else
            {
                //                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }

            if (m_data->settings.app_mode == AppMode::BENCHMARK && m_data->app_state == AppState::RUNNING
                && m_data->measurement_bucket->measurements_complete())
            {
                // Check if the measurement window is over and the last frame has been saved
                if (m_data->measurement_bucket->has_saved_frames())
                {
                    // Measurements have stop, so now we will gather the results, then proceed to the next pass
                    // We can stop the pipeline now
                    // It will do a graceful stop, it will try to send one last frame
                    m_data->video_pipeline.send_stop_signal();
                }
            }
        }

        m_data->video_pipeline.send_kill_signal();
        m_data->client_vrcp_socket.close();
    }

} // namespace wvb::server