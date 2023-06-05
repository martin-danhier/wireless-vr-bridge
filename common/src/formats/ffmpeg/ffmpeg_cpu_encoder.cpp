#ifdef _WIN32

#include "wvb_common/formats/ffmpeg.h"
#include <wvb_common/dx11_utils/rgba_to_nv12.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <vector>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

namespace wvb
{
    // ---- H264VideoEncoder ----

    class FFMpegCPUVideoEncoder : public IVideoEncoder
    {
      private:
        using super = IVideoEncoder;

        struct SrcFrameSRVCacheEntry
        {
            ID3D11Texture2D          *texture;
            ID3D11ShaderResourceView *srv;
        };

        FFMpegSpecificEncoderOptions m_ffmpeg_options;

        uint64_t                           m_current_frame_id       = 0;
        uint64_t                           m_last_finished_frame_id = 0;
        RgbaToNv12Converter                m_rgba_to_nv12_converter;
        AVCodecContext                    *m_codec_ctx            = nullptr;
        AVFrame                           *m_frame                = nullptr;
        AVPacket                          *m_packet               = nullptr;
        ID3D11Texture2D                   *m_intermediate_texture = nullptr;
        std::vector<SrcFrameSRVCacheEntry> m_src_frame_srv_cache;
        uint8_t                           *m_extra_buffer      = nullptr;
        size_t                             m_extra_buffer_size = 0;
        uint32_t                           m_frame_delay       = 0;

        [[nodiscard]] ID3D11ShaderResourceView *get_src_frame_srv(ID3D11Texture2D *src_texture, ID3D11Device *device);

      public:
        explicit FFMpegCPUVideoEncoder(const FFMpegEncoderCreateInfo &create_info)
            : super(create_info.base_create_info),
              m_ffmpeg_options(std::move(create_info.specific_options))
        {
            m_src_frame_srv_cache.reserve(3);
        }
        ~FFMpegCPUVideoEncoder() override;

        [[nodiscard]] std::string name() const override
        {
            return "FFMpegCPUVideoEncoder[" + std::string(m_ffmpeg_options.codec_name) + "]";
        }
        [[nodiscard]] EncoderType type() const override { return EncoderType::HARDWARE_PREPROCESS_D3D11TEXTURE2D | EncoderType::SOFTWARE; }

        [[nodiscard]] ImageFormat staging_texture_format() const override { return ImageFormat::NV12; }

        bool  init(void *__restrict d3d11_device, void *__restrict d3d11_device_context) override;
        void *preprocess_frame_gpu_with_texture(uint64_t frame_id,
                                                uint32_t rtp_sample_timestamp,
                                                void *__restrict src_texture,
                                                void *__restrict d3d11_device,
                                                void *__restrict d3d11_device_context) override;
        void  new_frame_cpu(uint64_t frame_id, uint32_t rtp_timestamp, bool last_of_stream, const RawFrame &frame) override;
        bool  get_next_packet(const uint8_t **__restrict out_packet, size_t *__restrict out_packet_size) override;

        uint32_t get_frame_delay() const override { return m_frame_delay; }
    };

    std::shared_ptr<IVideoEncoder> create_ffmpeg_cpu_video_encoder(const FFMpegEncoderCreateInfo &create_info)
    {
        return std::make_shared<FFMpegCPUVideoEncoder>(create_info);
    }

    // ---- Implementation ----

    FFMpegCPUVideoEncoder::~FFMpegCPUVideoEncoder()
    {
        // Free extra buffer
        if (m_intermediate_texture)
        {
            m_intermediate_texture->Release();
        }

        // Clear SRVs
        for (auto &entry : m_src_frame_srv_cache)
        {
            if (entry.srv)
            {
                entry.srv->Release();
            }
        }

        // Cleanup FFMPEG
        if (m_codec_ctx)
        {
            avcodec_free_context(&m_codec_ctx);
        }
    }

    bool FFMpegCPUVideoEncoder::init(void *__restrict d3d11_device, void *__restrict d3d11_device_context)
    {
        auto *device         = (ID3D11Device *) d3d11_device;
        auto *device_context = (ID3D11DeviceContext *) d3d11_device_context;

        // Set ffmpeg log level
        av_log_set_level(AV_LOG_INFO);
        
        // Create encoder
        const AVCodec *codec = avcodec_find_encoder_by_name(m_ffmpeg_options.codec_name);
        if (!codec)
        {
            LOGE("Failed to find \"%s\" CPU encoder\n", m_ffmpeg_options.codec_name);
            return false;
        }
        m_codec_ctx = avcodec_alloc_context3(codec);
        if (!m_codec_ctx)
        {
            LOGE("Failed to allocate codec context\n");
            return false;
        }
        m_codec_ctx->profile = m_ffmpeg_options.profile;

        // Set options
        int res = 0;
        for (const auto &option : m_ffmpeg_options.options)
        {
            res = av_opt_set(m_codec_ctx->priv_data, option.key, option.value.c_str(), 0);
            if (res < 0)
            {
                LOGE("Failed to set option \"%s\" to value \"%s\" with error code %d\n", option.key, option.value.c_str(), res);
                return false;
            }
        }

        m_codec_ctx->width        = static_cast<int>(m_create_info.src_size.width);
        m_codec_ctx->height       = static_cast<int>(m_create_info.src_size.height);
        m_codec_ctx->pix_fmt      = m_ffmpeg_options.supports_precise_format ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
        m_codec_ctx->time_base    = {static_cast<int>(m_create_info.refresh_rate.denominator),
                                     static_cast<int>(m_create_info.refresh_rate.numerator)};
        m_codec_ctx->framerate    = {static_cast<int>(m_create_info.refresh_rate.numerator),
                                     static_cast<int>(m_create_info.refresh_rate.denominator)};
        m_codec_ctx->gop_size     = m_ffmpeg_options.gop_size;
        m_codec_ctx->max_b_frames = m_ffmpeg_options.max_b_frames;
        m_codec_ctx->thread_count = m_ffmpeg_options.thread_count; // Use 1 thread to have no delay
        //        m_codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        m_codec_ctx->delay    = m_ffmpeg_options.delay;
        m_codec_ctx->bit_rate = m_create_info.bitrate;
        // We use BT.709 color space
        m_codec_ctx->color_primaries = AVCOL_PRI_BT709;
        m_codec_ctx->color_trc       = AVCOL_TRC_BT709;
        m_codec_ctx->colorspace      = AVCOL_SPC_BT709;
        m_codec_ctx->color_range     = AVCOL_RANGE_MPEG;
        // m_codec_ctx->slices          = 1;


        res = avcodec_open2(m_codec_ctx, codec, nullptr);
        if (res < 0)
        {
            LOGE("Failed to open codec with error code %d\n", res);

            char error[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(error, AV_ERROR_MAX_STRING_SIZE, res);
            LOGE("Error: %s\n", error);

            FLUSH_LOG();
            FLUSH_LOGE();
            return false;
        }

        // Create m_packet
        m_packet = av_packet_alloc();
        if (!m_packet)
        {
            LOGE("Failed to allocate packet\n");
            return false;
        }
        m_packet->stream_index = 0;

        // Get frame
        m_frame = av_frame_alloc();
        if (!m_frame)
        {
            LOGE("Failed to allocate frame\n");
            return false;
        }
        m_frame->format = AV_PIX_FMT_NV12;
        m_frame->width  = static_cast<int>(m_create_info.src_size.width);
        m_frame->height = static_cast<int>(m_create_info.src_size.height);
        m_frame->pts    = 0;     


        // Create GPU only NV12 texture for format conversion
        D3D11_TEXTURE2D_DESC texture_desc = {};
        texture_desc.Width                = static_cast<UINT>(m_create_info.src_size.width);
        texture_desc.Height               = static_cast<UINT>(m_create_info.src_size.height);
        texture_desc.MipLevels            = 1;
        texture_desc.ArraySize            = 1;
        texture_desc.Format               = DXGI_FORMAT_NV12;
        texture_desc.SampleDesc.Count     = 1;
        texture_desc.SampleDesc.Quality   = 0;
        texture_desc.Usage                = D3D11_USAGE_DEFAULT;
        texture_desc.BindFlags            = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        texture_desc.CPUAccessFlags       = 0;
        texture_desc.MiscFlags            = 0;
        res                               = device->CreateTexture2D(&texture_desc, nullptr, &m_intermediate_texture);
        if (FAILED(res))
        {
            LOGE("Failed to create intermediate texture with error code %d\n", res);
            return false;
        }

        // Init RGBA to NV12 converter
        m_rgba_to_nv12_converter.init(m_create_info.src_size, device, m_create_info.shader_dir_path, m_intermediate_texture);
        m_rgba_to_nv12_converter.update_size(m_create_info.src_size.width, m_create_info.src_size.height, device_context);

        return true;
    }

    ID3D11ShaderResourceView *FFMpegCPUVideoEncoder::get_src_frame_srv(ID3D11Texture2D *src_texture, ID3D11Device *device)
    {
        // Try to find it in cache
        for (auto &entry : m_src_frame_srv_cache)
        {
            if (entry.texture == src_texture)
            {
                return entry.srv;
            }
        }

        // Not found, create a new one
        ID3D11ShaderResourceView *srv = nullptr;
        D3D11_TEXTURE2D_DESC      src_texture_desc;
        src_texture->GetDesc(&src_texture_desc);
        HRESULT res = device->CreateShaderResourceView(src_texture, nullptr, &srv);
        if (FAILED(res))
        {
            LOGE("Failed to create shader resource view with error code %d\n", res);
            return nullptr;
        }

        // Add to cache
        m_src_frame_srv_cache.push_back({src_texture, srv});

        return srv;
    }

    void *FFMpegCPUVideoEncoder::preprocess_frame_gpu_with_texture(uint64_t frame_id,
                                                                   uint32_t rtp_sample_timestamp,
                                                                   void *__restrict d3d11_src_texture,
                                                                   void *__restrict d3d11_device,
                                                                   void *__restrict d3d11_device_context)
    {
        auto *device         = static_cast<ID3D11Device *>(d3d11_device);
        auto *device_context = static_cast<ID3D11DeviceContext *>(d3d11_device_context);
        auto *src_texture    = static_cast<ID3D11Texture2D *>(d3d11_src_texture);

        // Get SRV. We will receive different textures from SteamVR, so we cannot reuse the same SRV
        auto *src_texture_srv = get_src_frame_srv(src_texture, device);
        if (src_texture_srv == nullptr)
        {
            return nullptr;
        }

        // Run conversion
        m_rgba_to_nv12_converter.convert(src_texture_srv, device_context, m_create_info.src_size, m_intermediate_texture);

        return m_intermediate_texture;
    }

    void FFMpegCPUVideoEncoder::new_frame_cpu(uint64_t frame_id, uint32_t rtp_timestamp, bool last_of_stream, const RawFrame &frame)
    {
        // Send to FFMPEG
        m_current_frame_id = frame_id;

        if (m_current_frame_id <= m_last_finished_frame_id)
        {
            // We already processed this frame
            return;
        }

        // Copy data to AVFrame
        m_frame->data[0]     = frame.data[0];
        m_frame->data[1]     = frame.data[1];
        m_frame->linesize[0] = static_cast<int>(frame.pitch[0]);
        m_frame->linesize[1] = static_cast<int>(frame.pitch[1]);
        m_frame->pts         = static_cast<int64_t>(frame_id);
        
        // If this is the last frame, set the flag
        if (last_of_stream)
        {
            m_frame->key_frame = 1;
            m_frame->pict_type = AV_PICTURE_TYPE_I;
            m_frame->flags    |= AV_PKT_FLAG_KEY;
        }

        // Send to encoder
        int res = avcodec_send_frame(m_codec_ctx, m_frame);
        if (res < 0)
        {
            LOGE("Failed to send frame to encoder with error code %d\n", res);

            char error[AV_ERROR_MAX_STRING_SIZE];
            av_make_error_string(error, AV_ERROR_MAX_STRING_SIZE, res);
            LOGE("Error: %s\n", error);
        }

        
    }

    bool FFMpegCPUVideoEncoder::get_next_packet(const uint8_t **__restrict out_packet, size_t *__restrict out_packet_size)
    {
        if (m_current_frame_id <= m_last_finished_frame_id)
        {
            // We already processed this frame
            return false;
        }

        // Get AVPacket
        int res = avcodec_receive_packet(m_codec_ctx, m_packet);
        if (res == AVERROR(EAGAIN) || res == AVERROR_EOF)
        {
            // No packet available
            LOG("Encoder: no packet available\n");
            *out_packet      = nullptr;
            *out_packet_size = 0;

            // Measure delay
            if (m_last_finished_frame_id == 0)
            {
                m_frame_delay++;
            }

            return false;
        }
        else if (res < 0)
        {
            LOGE("Failed to receive packet from encoder with error code %d\n", res);
            *out_packet      = nullptr;
            *out_packet_size = 0;
            return false;
        }

        // For first frames, add extra data
        if (m_current_frame_id < 5)
        {
            // Create buffer with space for extra data
            const size_t new_size = m_packet->size + m_codec_ctx->extradata_size;
            if (!m_extra_buffer)
            {
                m_extra_buffer      = new uint8_t[new_size];
                m_extra_buffer_size = new_size;
            }
            else if (m_extra_buffer_size < new_size)
            {
                delete[] m_extra_buffer;
                m_extra_buffer      = new uint8_t[new_size];
                m_extra_buffer_size = new_size;
            }
            memcpy(m_extra_buffer, m_codec_ctx->extradata, m_codec_ctx->extradata_size);
            memcpy(m_extra_buffer + m_codec_ctx->extradata_size, m_packet->data, m_packet->size);
            *out_packet      = m_extra_buffer;
            *out_packet_size = new_size;

            m_last_finished_frame_id = m_current_frame_id;
            return false;
        }
        else if (m_extra_buffer)
        {
            delete[] m_extra_buffer;
            m_extra_buffer      = nullptr;
            m_extra_buffer_size = 0;
        }

        *out_packet      = m_packet->data;
        *out_packet_size = m_packet->size;

        m_last_finished_frame_id = m_current_frame_id;
        return false;
    }

} // namespace wvb

#endif