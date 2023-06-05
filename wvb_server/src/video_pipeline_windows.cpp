// Only support Windows: SteamVR uses D3D11 on Windows and Vulkan on Linux.
// Creating layers of abstractions to support both Vulkan and D3D would be complicated, so it is easier to
// separate implementations. Also, SteamVR for Linux isn't very stable at the moment.
#ifdef _WIN32

#include "wvb_common/image.h"
#include <wvb_common/benchmark.h>
#include <wvb_common/rtp.h>
#include <wvb_common/rtp_clock.h>
#include <wvb_common/server_shared_state.h>
#include <wvb_common/video_encoder.h>
#include <wvb_common/video_socket.h>
#include <wvb_server/video_pipeline.h>

#include <d3d11.h>
#include <deque>
#include <iostream>
#include <stb_image_write.h>
#include <stdexcept>
#include <thread>

// SteamVR will typically do triple buffering
#define DEFAULT_TEXTURE_CACHE_CAPACITY 3
#define BUFFER_CAPACITY                3
#define WAIT_TIMEOUT_MS                250

// Inspired by official example https://github.com/ValveSoftware/virtual_display

namespace wvb::server
{
    // =======================================================================================
    // =                                        Types                                        =
    // =======================================================================================

    struct CachedTexture
    {
        SharedTextureHandle handle  = NULL;
        ID3D11Texture2D    *texture = nullptr;
    };

    struct FrameInfo
    {
        // Info given by the driver
        uint32_t frame_id             = 0;
        uint32_t sample_rtp_timestamp = 0;
        uint32_t pose_rtp_timestamp   = 0;
        // Info of the push phase
        uint32_t frame_event_received_timestamp    = 0;
        uint32_t present_info_received_timestamp   = 0;
        uint32_t shared_texture_opened_timestamp   = 0; // optional
        uint32_t shared_texture_acquired_timestamp = 0; // optional
        uint32_t staging_texture_mapped_timestamp  = 0; // optional
        uint32_t frame_pushed_timestamp            = 0;
    };

    struct VideoPipeline::Data
    {
        // Measures
        std::chrono::high_resolution_clock::time_point start_time        = std::chrono::high_resolution_clock::now();
        int                                            i                 = 0;
        std::chrono::high_resolution_clock::time_point last_measure_time = std::chrono::high_resolution_clock::now();
        wvb::rtp::RTPClock                             rtp_clock;
        std::shared_ptr<ServerMeasurementBucket>       measurements;
        std::function<void()>                          on_worker_thread_stopped;

        // Driver communication
        std::shared_ptr<ServerDriverSharedMemory> shared_memory;
        std::shared_ptr<DriverEvents>             driver_events;
        std::shared_ptr<ServerEvents>             server_events;

        const VRSystemSpecs           &specs;
        std::shared_ptr<IVideoEncoder> video_encoder;

        // Worker thread
        std::thread           worker_thread;
        std::atomic<bool>     should_stop = false;
        std::atomic<bool>     should_kill = false;
        std::deque<FrameInfo> frame_info_queue;

        // DirectX 11 context
        ID3D11Device              *device         = nullptr;
        ID3D11DeviceContext       *device_context = nullptr;
        std::vector<CachedTexture> texture_cache {};
        ID3D11Texture2D           *staging_texture = nullptr;

        // Client communication
        std::shared_ptr<ServerVideoSocket> video_socket = nullptr;

        // Functions
        ~Data();
        void                                 init_d3d11();
        ID3D11Texture2D                     *open_shared_texture(SharedTextureHandle handle);
        void                                 start_worker_thread();
        void                                 worker_thread_main();
        [[nodiscard]] static inline uint64_t get_frame_data_index(uint64_t frame_id) { return frame_id % BUFFER_CAPACITY; }
        void                                 save_texture(ID3D11Texture2D *src_texture);
    };

    // =======================================================================================
    // =                                   Implementation                                    =
    // =======================================================================================

    DXGI_FORMAT to_dxgi_format(ImageFormat format)
    {
        switch (format)
        {
            case ImageFormat::R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
            case ImageFormat::NV12: return DXGI_FORMAT_NV12;
            case ImageFormat::B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
            case ImageFormat::U8Y8V8Y8_UNORM: return DXGI_FORMAT_R8G8_B8G8_UNORM;
            default: throw std::invalid_argument("Unsupported image format");
        }
    }

    // === Init ===

    VideoPipeline::VideoPipeline(std::shared_ptr<ServerDriverSharedMemory> shared_memory,
                                 std::shared_ptr<DriverEvents>             driver_events,
                                 std::shared_ptr<ServerEvents>             server_events,
                                 std::shared_ptr<IVideoEncoder>            video_encoder,
                                 std::shared_ptr<ServerVideoSocket>        video_socket,
                                 const VRSystemSpecs                      &specs,
                                 uint64_t                                  ntp_timestamp,
                                 std::shared_ptr<ServerMeasurementBucket>  measurements,
                                 std::function<void()>                     on_worker_thread_stopped)
        : m_data(new Data {
            .rtp_clock                = wvb::rtp::RTPClock {ntp_timestamp},
            .measurements             = std::move(measurements),
            .on_worker_thread_stopped = std::move(on_worker_thread_stopped),
            .shared_memory            = std::move(shared_memory),
            .driver_events            = std::move(driver_events),
            .server_events            = std::move(server_events),
            .specs                    = specs,
            .video_encoder            = std::move(video_encoder),
            .video_socket             = std::move(video_socket),
        })
    {
        if (m_data->shared_memory == nullptr || m_data->driver_events == nullptr || m_data->server_events == nullptr
            || m_data->video_encoder == nullptr)
        {
            throw std::invalid_argument("shared_memory, driver_events, server_events, and video_encoder must not be null");
        }

        m_data->texture_cache.reserve(DEFAULT_TEXTURE_CACHE_CAPACITY);
        m_data->init_d3d11();

        if (!m_data->video_encoder->init(m_data->device, m_data->device_context))
        {
            throw std::runtime_error("Failed to initialize video encoder");
        }
    }
    DEFAULT_PIMPL_DESTRUCTOR(VideoPipeline);

    void VideoPipeline::Data::init_d3d11()
    {
        UINT flags = 0;
#ifdef _DEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

        // Create a D3D11 device
        D3D_FEATURE_LEVEL feature_level;
        HRESULT           result = D3D11CreateDevice(nullptr,
                                           D3D_DRIVER_TYPE_HARDWARE,
                                           nullptr,
                                           flags,
                                           nullptr,
                                           0,
                                           D3D11_SDK_VERSION,
                                           &device,
                                           &feature_level,
                                           &device_context);

        if (FAILED(result))
        {
            throw std::runtime_error("Failed to create D3D11 device");
        }

        if (feature_level != D3D_FEATURE_LEVEL_11_0)
        {
            throw std::runtime_error("D3D11 device does not support feature level 11.0");
        }

        if (device == nullptr || device_context == nullptr)
        {
            throw std::runtime_error("D3D11 device or device context is null");
        }

        // Set debug name
        device->SetPrivateData(WKPDID_D3DDebugObjectName, 11, "WVBVideoPipeline");

        if (video_encoder->type() & IVideoEncoder::EncoderType::SOFTWARE)
        {
            // Create staging texture
            D3D11_TEXTURE2D_DESC staging_desc;
            staging_desc.Width              = specs.eye_resolution.width * 2;
            staging_desc.Height             = specs.eye_resolution.height;
            staging_desc.MipLevels          = 1;
            staging_desc.ArraySize          = 1;
            staging_desc.Format             = to_dxgi_format(video_encoder->staging_texture_format());
            staging_desc.SampleDesc.Count   = 1;
            staging_desc.SampleDesc.Quality = 0;
            staging_desc.Usage              = D3D11_USAGE_STAGING;
            staging_desc.BindFlags          = 0;
            staging_desc.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;
            staging_desc.MiscFlags          = 0;
            auto hr                         = device->CreateTexture2D(&staging_desc, nullptr, &staging_texture);
            if (FAILED(hr))
            {
                throw std::runtime_error("Failed to create staging texture.");
            }
        }
    }

    VideoPipeline::Data::~Data()
    {
        should_stop = true;
        if (worker_thread.joinable())
        {
            worker_thread.join();
        }

        // Wait for GPU to finish
        if (device_context != nullptr)
        {
            device_context->Flush();
        }

        if (staging_texture != nullptr)
        {
            staging_texture->Release();
        }

        if (device_context != nullptr)
        {
            device_context->Release();
        }

        if (device != nullptr)
        {
            device->Release();
        }

        for (auto &cached_texture : texture_cache)
        {
            if (cached_texture.texture != nullptr)
            {
                cached_texture.texture->Release();
            }
        }

        texture_cache.clear();
    }

    void VideoPipeline::Data::start_worker_thread()
    {
        should_stop   = false;
        worker_thread = std::thread(&VideoPipeline::Data::worker_thread_main, this);
    }

    void VideoPipeline::Data::save_texture(ID3D11Texture2D *src_texture)
    {
        const auto staging_texture_format = video_encoder->staging_texture_format();
        // If the encoder already uses an RGBA staging texture, we can use it
        const bool can_use_pipeline_texture =
            video_encoder->type() & IVideoEncoder::EncoderType::SOFTWARE && staging_texture_format == ImageFormat::R8G8B8A8_UNORM;

        ID3D11Texture2D *staging_tex = nullptr;
        if (can_use_pipeline_texture)
        {
            staging_tex = staging_texture;
        }
        else
        {
            // Otherwise we will have to create one. We do that here since save_texture is a one-time call
            D3D11_TEXTURE2D_DESC staging_desc;
            staging_desc.Width              = specs.eye_resolution.width * 2;
            staging_desc.Height             = specs.eye_resolution.height;
            staging_desc.MipLevels          = 1;
            staging_desc.ArraySize          = 1;
            staging_desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM; // Use RGBA because we want to save the original backbuffer
            staging_desc.SampleDesc.Count   = 1;
            staging_desc.SampleDesc.Quality = 0;
            staging_desc.Usage              = D3D11_USAGE_STAGING;
            staging_desc.BindFlags          = 0;
            staging_desc.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;
            staging_desc.MiscFlags          = 0;
            auto hr                         = device->CreateTexture2D(&staging_desc, nullptr, &staging_tex);
            if (FAILED(hr))
            {
                throw std::runtime_error("Failed to create staging texture.");
            }
        }

        // Copy texture to staging texture
        device_context->CopyResource(staging_tex, src_texture);

        // Map staging texture
        D3D11_MAPPED_SUBRESOURCE mapped_resource;
        HRESULT                  result = device_context->Map(staging_tex, 0, D3D11_MAP_READ, 0, &mapped_resource);
        if (FAILED(result))
        {
            throw std::runtime_error("Failed to map staging texture.");
        }

        // Save raw data
        const auto  height    = specs.eye_resolution.height;
        const auto  stride    = mapped_resource.RowPitch;
        const auto *data      = static_cast<const uint8_t *>(mapped_resource.pData);
        std::string file_name = "wvb_capture_pass_" + std::to_string(measurements->get_pass_id()) + "_run_"
                                + std::to_string(measurements->get_run_id()) + "_server_"
                                + std::to_string(measurements->get_nb_saved_frames()) + ".rgba";

        std::ofstream file(file_name, std::ios::out | std::ios::binary);
        file.write(reinterpret_cast<const char *>(data), stride * height);
        file.close();

        // Unmap staging texture
        device_context->Unmap(staging_tex, 0);

        if (!can_use_pipeline_texture)
        {
            staging_tex->Release();
        }
    }

    ID3D11Texture2D *VideoPipeline::Data::open_shared_texture(SharedTextureHandle handle)
    {
        if (handle == NULL)
        {
            return nullptr;
        }

        // If the texture is already in cache, return that
        for (const auto &cached_texture : texture_cache)
        {
            if (cached_texture.handle == handle)
            {
                return cached_texture.texture;
            }
        }

        // Else, open the shared resource
        ID3D11Texture2D *texture = nullptr;
        HRESULT result = device->OpenSharedResource(reinterpret_cast<HANDLE>(handle), __uuidof(ID3D11Texture2D), (void **) &texture);
        if (FAILED(result))
        {
            return nullptr;
        }

        // Add to cache
        texture_cache.emplace_back(CachedTexture {
            .handle  = handle,
            .texture = texture,
        });
        return texture;
    }

    void VideoPipeline::Data::worker_thread_main()
    {
        // Set thread to the highest priority
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#ifdef _DEBUG
        SetThreadDescription(GetCurrentThread(), L"VideoPipeline::worker_thread_main");
#endif

        ServerFrameTimeMeasurements frame_time {};
        size_t                      raw_size               = 0;
        size_t                      encoded_size           = 0;
        ID3D11Texture2D            *src_backbuffer_texture = nullptr;
        IDXGIKeyedMutex            *backbuffer_mutex       = nullptr;

        const ImageFormat staging_texture_format = video_encoder->staging_texture_format();

        try
        {
            LOG("Starting worker thread using encoder \"%s\"\n", video_encoder->name().c_str());

            const auto        encoder_type = video_encoder->type();
            OpenVRPresentInfo present_info;

            bool last_frame_sent = false;
            while (!should_kill && !(should_stop && last_frame_sent))
            {
                bool last_frame =
                    should_stop; // Read the value of atomic bool once, because otherwise it can change in the middle of the loop

                // Wait until a frame is received
                while (!driver_events->new_present_info.wait(WAIT_TIMEOUT_MS) && !should_kill)
                {
                }

                if (should_kill)
                {
                    break;
                }

                // Reset frame time measurements
                frame_time                   = {};
                const bool should_save_frame = measurements->measurements_complete() && !measurements->has_saved_frames();

                frame_time.frame_event_received_timestamp = rtp_clock.now_rtp_timestamp();

                // Load present info
                {
                    auto present_info_lock = shared_memory->lock(WAIT_TIMEOUT_MS);
                    if (!present_info_lock.is_valid())
                    {
                        server_events->frame_finished.signal();
                        frame_time.finished_signal_sent_timestamp = rtp_clock.now_rtp_timestamp();
                        measurements->add_dropped_frame();
                        frame_time.dropped = true;
                        measurements->add_frame_time_measurement(frame_time);
                        std::cerr << "Failed to lock shared memory\n";
                        continue;
                    }
                    present_info = present_info_lock->latest_present_info;
                }

                frame_time.frame_id                        = present_info.frame_id;
                frame_time.present_info_received_timestamp = rtp_clock.now_rtp_timestamp();

                // SteamVR uses triple buffering. So we can tell it that it can begin the next frame if it wants to.
                // If it is too fast (it finishes the next frame before the current one is sent), it will block after submitting it,
                // until the next iteration of this loop and the signal is sent.
                server_events->frame_finished.signal();
                frame_time.finished_signal_sent_timestamp = rtp_clock.now_rtp_timestamp();

                // The timestamp indicates when the frame was created.
                // If too big of a delay is created, drop frames early to catch up and to avoid congesting the network
                const auto sample_time = rtp_clock.from_rtp_timestamp(present_info.sample_rtp_timestamp);
                if (std::chrono::duration_cast<std::chrono::microseconds>(rtp_clock.now() - sample_time).count()
                    > specs.refresh_rate.inter_frame_delay_us() + 1000)
                {
                    frame_time.dropped = true;
                    measurements->add_dropped_frame();
                    measurements->add_frame_time_measurement(frame_time);
                    continue;
                }

                raw_size = 0;

                // Acquire the shared backbuffer texture
                if (encoder_type == IVideoEncoder::EncoderType::HARDWARE_SHARED_HANDLE)
                {
                    // No need to open the shared texture
                    video_encoder->new_frame_gpu_with_shared_handle(present_info.frame_id,
                                                                    present_info.sample_rtp_timestamp,
                                                                    last_frame,
                                                                    present_info.backbuffer_texture_handle,
                                                                    device,
                                                                    device_context);
                }
                else
                {
                    // Open shared texture
                    src_backbuffer_texture = open_shared_texture(present_info.backbuffer_texture_handle);
                    if (src_backbuffer_texture == nullptr)
                    {
                        measurements->add_dropped_frame();
                        frame_time.dropped = true;
                        measurements->add_frame_time_measurement(frame_time);
                        std::cerr << "Failed to open shared texture\n";
                        continue;
                    }

                    frame_time.shared_texture_opened_timestamp = rtp_clock.now_rtp_timestamp();

                    backbuffer_mutex = nullptr;
                    HRESULT result   = src_backbuffer_texture->QueryInterface(__uuidof(IDXGIKeyedMutex), (void **) &backbuffer_mutex);
                    if (FAILED(result))
                    {
                        measurements->add_dropped_frame();
                        frame_time.dropped = true;
                        measurements->add_frame_time_measurement(frame_time);
                        std::cerr << "Failed to query keyed mutex\n";
                        continue;
                    }
                    result = backbuffer_mutex->AcquireSync(0, 10);
                    if (FAILED(result) || result == WAIT_TIMEOUT || result == WAIT_ABANDONED)
                    {
                        backbuffer_mutex->Release();
                        measurements->add_dropped_frame();
                        frame_time.dropped = true;
                        measurements->add_frame_time_measurement(frame_time);
                        std::cerr << "Failed to acquire mutex " << result << "\n";
                        continue;
                    }

                    frame_time.shared_texture_acquired_timestamp = rtp_clock.now_rtp_timestamp();

                    ID3D11Texture2D *intermediate_texture = src_backbuffer_texture;

                    if (encoder_type & IVideoEncoder::EncoderType::HARDWARE_PREPROCESS_D3D11TEXTURE2D)
                    {
                        intermediate_texture = static_cast<ID3D11Texture2D *>(
                            video_encoder->preprocess_frame_gpu_with_texture(present_info.frame_id,
                                                                             present_info.sample_rtp_timestamp,
                                                                             src_backbuffer_texture,
                                                                             device,
                                                                             device_context));
                        if (intermediate_texture == nullptr)
                        {
                            backbuffer_mutex->ReleaseSync(0);
                            backbuffer_mutex->Release();

                            frame_time.dropped = true;

                            measurements->add_dropped_frame();
                            measurements->add_frame_time_measurement(frame_time);
                            std::cerr << "Failed to preprocess frame\n";
                            continue;
                        }
                    }
                    // Give frame to encoder
                    if (encoder_type & IVideoEncoder::EncoderType::HARDWARE_D3D11TEXTURE2D)
                    {
                        video_encoder->new_frame_gpu_with_texture(present_info.frame_id,
                                                                  present_info.sample_rtp_timestamp,
                                                                  last_frame,
                                                                  intermediate_texture,
                                                                  device,
                                                                  device_context);
                    }
                    if (encoder_type & IVideoEncoder::EncoderType::SOFTWARE)
                    {
                        // Copy texture to staging
                        device_context->CopyResource(staging_texture, intermediate_texture);

                        D3D11_MAPPED_SUBRESOURCE mapped_subresource {};
                        result = device_context->Map(staging_texture, 0, D3D11_MAP_READ, 0, &mapped_subresource);
                        if (FAILED(result))
                        {
                            backbuffer_mutex->ReleaseSync(0);
                            backbuffer_mutex->Release();

                            LOGE("Failed to map staging texture\n");
                            measurements->add_dropped_frame();
                            frame_time.dropped = true;
                            measurements->add_frame_time_measurement(frame_time);
                            continue;
                        }

                        raw_size                                    = mapped_subresource.RowPitch * specs.eye_resolution.height;
                        frame_time.staging_texture_mapped_timestamp = rtp_clock.now_rtp_timestamp();

                        wvb::RawFrame frame {
                            .format = staging_texture_format,
                            .width  = specs.eye_resolution.width * 2,
                            .height = specs.eye_resolution.height,
                        };

                        if (staging_texture_format == ImageFormat::NV12)
                        {
                            frame.data[0] = (uint8_t *) mapped_subresource.pData;                        // Y plane
                            frame.data[1] = (uint8_t *) mapped_subresource.pData
                                            + mapped_subresource.RowPitch * specs.eye_resolution.height; // UV plane
                            frame.pitch[0] = mapped_subresource.RowPitch;
                            frame.pitch[1] = mapped_subresource.RowPitch;
                        }
                        else
                        {
                            // Packed
                            frame.data[0]  = (uint8_t *) mapped_subresource.pData;
                            frame.pitch[0] = mapped_subresource.RowPitch;
                        }

                        video_encoder->new_frame_cpu(present_info.frame_id, present_info.sample_rtp_timestamp, last_frame, frame);
                        device_context->Unmap(staging_texture, 0);
                    }

                    if (!should_save_frame)
                    {
                        backbuffer_mutex->ReleaseSync(0);
                        backbuffer_mutex->Release();
                    }
                    // Don't release the frame yet if we want to save it
                    // To avoid disrupting the client, we will save it after it has been sent, so we need
                    // to keep access to the original texture
                }

                frame_info_queue.push_back({
                    .frame_id                          = static_cast<uint32_t>(present_info.frame_id),
                    .sample_rtp_timestamp              = present_info.sample_rtp_timestamp,
                    .pose_rtp_timestamp                = present_info.pose_rtp_timestamp,
                    .frame_event_received_timestamp    = frame_time.frame_event_received_timestamp,
                    .present_info_received_timestamp   = frame_time.present_info_received_timestamp,
                    .shared_texture_opened_timestamp   = frame_time.shared_texture_opened_timestamp,
                    .shared_texture_acquired_timestamp = frame_time.shared_texture_acquired_timestamp,
                    .staging_texture_mapped_timestamp  = frame_time.staging_texture_mapped_timestamp,
                    .frame_pushed_timestamp            = rtp_clock.now_rtp_timestamp(),
                });
                // --- End of push phase ---

                // --- Start of pull/send phase ---
                // Because of delay, the frame that the encoder will give us may not be the one we just pushed
                // This is why we needed to enqueue everything
                const auto frame_info                        = frame_info_queue.front();
                frame_time                                   = {};
                frame_time.frame_id                          = frame_info.frame_id;
                frame_time.frame_event_received_timestamp    = frame_info.frame_event_received_timestamp;
                frame_time.present_info_received_timestamp   = frame_info.present_info_received_timestamp;
                frame_time.shared_texture_opened_timestamp   = frame_info.shared_texture_opened_timestamp;
                frame_time.shared_texture_acquired_timestamp = frame_info.shared_texture_acquired_timestamp;
                frame_time.staging_texture_mapped_timestamp  = frame_info.staging_texture_mapped_timestamp;
                frame_time.frame_pushed_timestamp            = frame_info.frame_pushed_timestamp;
                frame_time.frame_pulled_timestamp            = rtp_clock.now_rtp_timestamp();

                encoded_size = 0;

                // Create the next packet

                const uint8_t *packet      = nullptr;
                size_t         packet_size = 0;

                // Pull frame from encoder
                frame_time.before_last_get_next_packet_timestamp = rtp_clock.now_rtp_timestamp();
                video_encoder->get_next_packet(&packet, &packet_size);
                frame_time.after_last_get_next_packet_timestamp = rtp_clock.now_rtp_timestamp();
                if (packet == nullptr)
                {
                    // No packet to send
                    measurements->add_dropped_frame(); // It counts as a dropped frame, since we didn't send anything
                    frame_time.dropped = true;
                    measurements->add_frame_time_measurement(frame_time);
                    continue;
                }

                // A frame was pulled, we can remove the frame info from the queue
                frame_info_queue.pop_front();

                encoded_size += packet_size;

                i++;
                if (i % 100 == 0)
                {
                    auto       now     = std::chrono::high_resolution_clock::now();
                    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - last_measure_time).count();
                    auto       avg     = (i * 1000000.0) / static_cast<double>(elapsed);
                    LOG("Sent %d frames in %.3fms (%.3f FPS)\n", i, elapsed / 1000.f, avg);
                    last_measure_time = now;
                    i                 = 0;
                }

                frame_time.before_last_send_packet_timestamp = rtp_clock.now_rtp_timestamp();
                video_socket->send_packet(packet,
                                          packet_size,
                                          static_cast<uint32_t>(frame_info.frame_id),
                                          last_frame,
                                          frame_info.sample_rtp_timestamp,
                                          frame_info.pose_rtp_timestamp,
                                          should_save_frame,
                                          true,
                                          0);
                frame_time.after_last_send_packet_timestamp = rtp_clock.now_rtp_timestamp();
                if (last_frame)
                {
                    last_frame_sent = true;
                }

                if (!video_socket->is_connected())
                {
                    should_stop     = true;
                    last_frame_sent = true; // can't send it anymore
                    should_kill     = true;
                }

                if (should_save_frame && backbuffer_mutex != nullptr)
                {
                    LOG("Saving frame %lld\n", frame_info.frame_id);
                    save_texture(src_backbuffer_texture);
                    backbuffer_mutex->ReleaseSync(0);
                    backbuffer_mutex->Release();
                    measurements->add_saved_frame();
                }

                // Save measurements
                measurements->add_image_quality_measurement({
                    .frame_id        = static_cast<uint32_t>(frame_info.frame_id),
                    .codestream_size = static_cast<uint32_t>(encoded_size),
                    .raw_size        = static_cast<uint32_t>(raw_size),
                    .psnr            = 0, // TODO
                });
                measurements->add_frame_time_measurement(frame_time);
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Exception in worker thread: " << e.what() << std::endl;
        }
        catch (...)
        {
            std::cerr << "Unknown exception in worker thread" << std::endl;
        }

        // Leaving worker thread
        if (on_worker_thread_stopped != nullptr)
        {
            on_worker_thread_stopped();
        }
    }

    void VideoPipeline::start_worker_thread()
    {
        if (m_data == nullptr)
        {
            return;
        }

        m_data->start_worker_thread();
    }

    void VideoPipeline::send_stop_signal()
    {
        if (m_data == nullptr)
        {
            return;
        }

        m_data->should_stop = true;
    }

    void VideoPipeline::send_kill_signal()
    {
        if (m_data == nullptr)
        {
            return;
        }

        m_data->should_kill = true;
    }

    void VideoPipeline::join()
    {
        if (m_data == nullptr)
        {
            return;
        }

        if (m_data->worker_thread.joinable())
        {
            m_data->worker_thread.join();
        }
    }
} // namespace wvb::server

#endif