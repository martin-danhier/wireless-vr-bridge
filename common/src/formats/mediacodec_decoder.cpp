#ifdef __ANDROID__

#include "wvb_common/formats/media_codec.h"
#include <wvb_common/macros.h>

#include <android/surface_texture.h>
#include <GLES3/gl3.h>
#include <iostream>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <stb_image_write.h>
#include <vector>

#define COLOR_FormatYUV420Flexible 0x7F420888
#define COLOR_FormatSurface        0x7f000789
// Output format of the decoder
#define COLOR_QCOM_FormatYUV420SemiPlanar32m 0x7fa30c04

namespace wvb
{
    // ---- H264VideoDecoder ----

    class MediaCodecVideoDecoder : public IVideoDecoder
    {
      private:
        using super = IVideoDecoder;

        const char   *m_codec_name   = "";
        AMediaCodec  *m_codec        = nullptr;
        AMediaFormat *m_input_format = nullptr;

        GLuint m_texture_y  = 0;
        GLuint m_texture_uv = 0;

        int32_t m_stride       = 0;
        int32_t m_slice_height = 0;

        uint32_t                m_nb_pushed   = 0;
        uint32_t                m_nb_popped   = 0;
        std::optional<uint32_t> m_frame_delay = std::nullopt;

      public:
        ~MediaCodecVideoDecoder() override;

        explicit MediaCodecVideoDecoder(const MediaCodecDecoderCreateInfo &create_info)
            : super(create_info.base_create_info),
              m_codec_name(create_info.codec_name) {};

        [[nodiscard]] std::string name() const override { return "MediaCodecVideoDecoder[" + std::string(m_codec_name) + "]"; }

        [[nodiscard]] DecoderType type() const override { return DecoderType::SOFTWARE; }

        void init() override;

        bool push_packet(const uint8_t *packet, size_t packet_size, bool end_of_stream) override;

        std::optional<GLFrameTexture> get_frame_gpu() override;

        [[nodiscard]] uint32_t get_frame_delay() const override
        {
            if (m_frame_delay.has_value())
            {
                return m_frame_delay.value();
            }
            return 0;
        }
    };

    // ---- Implementation ----

    std::shared_ptr<IVideoDecoder> create_media_codec_video_decoder(const MediaCodecDecoderCreateInfo &create_info)
    {
        return std::make_shared<MediaCodecVideoDecoder>(create_info);
    }

    void MediaCodecVideoDecoder::init()
    {
        m_codec = AMediaCodec_createDecoderByType(m_codec_name);
        if (m_codec == nullptr)
        {
            LOGE("AMediaCodec_createDecoderByType failed");
            return;
        }

        // Configure input
        m_input_format = AMediaFormat_new();
        AMediaFormat_setString(m_input_format, AMEDIAFORMAT_KEY_MIME, m_codec_name);
        AMediaFormat_setInt32(m_input_format, AMEDIAFORMAT_KEY_WIDTH, static_cast<int32_t>(m_create_info.src_size.width));
        AMediaFormat_setInt32(m_input_format, AMEDIAFORMAT_KEY_HEIGHT, static_cast<int32_t>(m_create_info.src_size.height));

        AMediaFormat_setInt32(m_input_format, AMEDIAFORMAT_KEY_COLOR_FORMAT, COLOR_FormatYUV420Flexible);

        // Init output textures
        GLuint textures[2];
        glGenTextures(2, textures);
        m_texture_y  = textures[0];
        m_texture_uv = textures[1];

        // R texture for Y plane
        glBindTexture(GL_TEXTURE_2D, m_texture_y);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_R8,
                     static_cast<int32_t>(m_create_info.src_size.width),
                     static_cast<int32_t>(m_create_info.src_size.height),
                     0,
                     GL_RED,
                     GL_UNSIGNED_BYTE,
                     nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glBindTexture(GL_TEXTURE_2D, 0);

        // RG down sampled interleaved texture for UV plane
        glBindTexture(GL_TEXTURE_2D, m_texture_uv);
        glTexImage2D(GL_TEXTURE_2D,
                     0,
                     GL_RG8,
                     static_cast<int32_t>(m_create_info.src_size.width / 2),
                     static_cast<int32_t>(m_create_info.src_size.height / 4),
                     0,
                     GL_RG,
                     GL_UNSIGNED_BYTE,
                     nullptr);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glBindTexture(GL_TEXTURE_2D, 0);

        AMediaFormat_setInt32(m_input_format, AMEDIACODEC_KEY_LOW_LATENCY, 1);

        auto status = AMediaCodec_configure(m_codec, m_input_format, nullptr, nullptr, 0);
        if (status != AMEDIA_OK)
        {
            LOGE("AMediaCodec_configure failed with error %d", status);
            AMediaFormat_delete(m_input_format);
            return;
        }

        AMediaCodec_start(m_codec);

        LOG("Media codec started !");
    }

    bool MediaCodecVideoDecoder::push_packet(const uint8_t *packet, size_t packet_size, bool end_of_stream)
    {
        if (packet_size == 0)
        {
            return false;
        }

        // Get input buffer
        AMediaCodecBufferInfo info;
        auto                  status = AMediaCodec_dequeueInputBuffer(m_codec, 0);
        if (status >= 0)
        {
            size_t   input_buffer_size = 0;
            uint8_t *input_buffer      = AMediaCodec_getInputBuffer(m_codec, status, &input_buffer_size);
            if (input_buffer == nullptr)
            {
                LOGE("AMediaCodec_getInputBuffer failed");
                return false;
            }

            // Copy data to input buffer
            memcpy(input_buffer, packet, packet_size);

            uint32_t flags = end_of_stream ? AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM : 0;
            if (end_of_stream)
            {
                LOG("Submitted end of stream");
            }

            // Submit input buffer
            AMediaCodec_queueInputBuffer(m_codec, status, 0, packet_size, 0, flags);
            m_nb_pushed++;

            return true;
        }
        else if (status == AMEDIACODEC_INFO_TRY_AGAIN_LATER)
        {
//            LOG("input AMEDIACODEC_INFO_TRY_AGAIN_LATER");
        }
        else if (status == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED)
        {
            LOG("input AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED");
        }
        else if (status == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED)
        {
            LOG("input AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED");
        }
        else
        {
            LOGE("AMediaCodec_dequeueInputBuffer failed with error %ld", status);
        }
        return false;
    }

    std::optional<GLFrameTexture> MediaCodecVideoDecoder::get_frame_gpu()
    {
        // Decode to texture
        AMediaCodecBufferInfo info;

        bool    should_retry = false;
        uint8_t n_iter       = 0;

        do
        {
            should_retry = false;
            n_iter++;

            auto status = AMediaCodec_dequeueOutputBuffer(m_codec, &info, 0);
            if (status >= 0)
            {
                // Get output buffer
                size_t   output_buffer_size = 0;
                uint8_t *output_buffer      = AMediaCodec_getOutputBuffer(m_codec, status, &output_buffer_size);
                if (output_buffer == nullptr)
                {
                    LOGE("AMediaCodec_getOutputBuffer failed");
                    return std::nullopt;
                }

                if (m_stride == 0)
                {
                    LOGE("No stride");
                    return std::nullopt;
                }

                if (!m_frame_delay.has_value())
                {
                    m_frame_delay = m_nb_pushed - m_nb_popped;
                }

                // Copy Y plane
                glBindTexture(GL_TEXTURE_2D, m_texture_y);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, m_stride);
                glTexSubImage2D(GL_TEXTURE_2D,
                                0,
                                0,
                                0,
                                static_cast<int32_t>(m_create_info.src_size.width),
                                static_cast<int32_t>(m_create_info.src_size.height),
                                GL_RED,
                                GL_UNSIGNED_BYTE,
                                output_buffer);

                auto err = glGetError();
                if (err != GL_NO_ERROR)
                {
                    LOGE("glTexSubImage2D failed: %d", err);
                }

                // TODO: use PBO for better performance

                // Get second plane
                glBindTexture(GL_TEXTURE_2D, m_texture_uv);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, m_stride);
                glTexSubImage2D(GL_TEXTURE_2D,
                                0,
                                0,
                                0,
                                static_cast<int32_t>(m_create_info.src_size.width / 2),
                                static_cast<int32_t>(m_create_info.src_size.height / 4),
                                GL_RG,
                                GL_UNSIGNED_BYTE,
                                output_buffer + m_stride * m_slice_height);

                err = glGetError();
                if (err != GL_NO_ERROR)
                {
                    LOGE("glTexSubImage2D failed: %d", err);
                }

                glBindTexture(GL_TEXTURE_2D, 0);

                // Release output buffer
                AMediaCodec_releaseOutputBuffer(m_codec, status, false);

                m_nb_popped++;
                return GLFrameTexture {
                    .format   = ImageFormat::NV12,
                    .textures = {m_texture_y, m_texture_uv},
                    .size = static_cast<uint32_t>(m_slice_height) * m_stride + static_cast<uint32_t>(m_stride) * m_slice_height / 2,
                };
            }
            else if (status == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED)
            {
                LOG("output AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED");

                // Retry
                should_retry = true;
            }
            else if (status == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED)
            {
                LOG("output AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED");

                auto format = AMediaCodec_getOutputFormat(m_codec);
                // Get stride
                AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_STRIDE, &m_stride);

                LOG("Width: %d, Stride: %d (%d padding)",
                    m_create_info.src_size.width,
                    m_stride,
                    m_stride - m_create_info.src_size.width);

                // Get inter-plane stride
                AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SLICE_HEIGHT, &m_slice_height);
                LOG("Height: %d, Slice height: %d (%d padding)",
                    m_create_info.src_size.height,
                    m_slice_height,
                    m_slice_height - m_create_info.src_size.height);

                // Get color format
                int32_t color_format = 0;
                AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, &color_format);

                if (color_format == COLOR_QCOM_FormatYUV420SemiPlanar32m)
                {
                    LOG("Color format: COLOR_QCOM_FormatYUV420SemiPlanar32m");
                }
                else
                {
                    LOGE("Unsupported color format: %x", color_format);
                    throw std::runtime_error("Unsupported color format");
                }

                AMediaFormat_delete(format);

                should_retry = true;
            }
            else if (status != AMEDIACODEC_INFO_TRY_AGAIN_LATER)
            {
                LOGE("output AMediaCodec_dequeueOutputBuffer failed with error %ld", status);
            }

            if (n_iter > 10)
            {
                LOGE("Too many iterations");
                return std::nullopt;
            }
        } while (should_retry);

        return std::nullopt;
    }

    MediaCodecVideoDecoder::~MediaCodecVideoDecoder()
    {
        if (m_codec != nullptr)
        {
            AMediaCodec_stop(m_codec);
            AMediaCodec_delete(m_codec);
        }
        if (m_input_format != nullptr)
        {
            AMediaFormat_delete(m_input_format);
        }
    }

} // namespace wvb
#endif