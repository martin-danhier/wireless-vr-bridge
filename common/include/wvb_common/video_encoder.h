#pragma once

#include <wvb_common/io.h>
#include <wvb_common/vr_structs.h>

#include <optional>
#include <string>

namespace wvb
{
    enum class ImageFormat : uint8_t
    {
        ANY = 0,
        /** packed 32-bit RGBA */
        R8G8B8A8_UNORM = 1,
        /** packed 32-bit BGRA */
        B8G8R8A8_UNORM = 2,
        /** planar 12-bit YUV 4:2:0 : 1 plane for Y, 1 plane for interleaved UV */
        NV12 = 3,
        /** packed 16-bit YUV 4:2:2 */
        U8Y8V8Y8_UNORM = 4,
    };

    /** CPU image descriptor. The contents of data and pitch depends on the used format. */
    struct RawFrame
    {
        ImageFormat format   = ImageFormat::R8G8B8A8_UNORM;
        uint8_t    *data[4]  = {nullptr}; // Pointer to the planes
        uint32_t    pitch[4] = {0};       // Pitch of each respective plane
        uint32_t    width    = 0;
        uint32_t    height   = 0;
    };

    /** OpenGL GPU image descriptor. */
    struct GLFrameTexture
    {
        ImageFormat format         = ImageFormat::R8G8B8A8_UNORM;
        uint32_t    textures[3]    = {0};
        size_t      size           = 0;
    };

    /** A video encoder handles the video compression of frames.
     *
     * Encoders can be defined either as hardware or software. Based on the selected type, the pipeline will call the appropriate
     * functions.
     *
     * Optionally, for CPU encoder, pre-processing can be performed on the GPU before the frame is downloaded on the CPU. This is
     * particularly useful for format conversion, as most encoders don't support the RGBA format used by SteamVR.
     *
     * Video encoders are only used on Windows by the server process.
    */
    class IVideoEncoder
    {
      public:
        struct EncoderCreateInfo
        {
            Extent2D    src_size;                  // Full size (both eyes)
            RefreshRate refresh_rate;
            const char *shader_dir_path = nullptr; // Ends with a slash
            uint32_t    bpp             = 3;       // When applicable
            uint32_t    bitrate         = 0;       // When applicable
            int32_t     delay            = 0;       // When applicable
        };

        enum class EncoderType : uint8_t
        {
            /** CPU encoder. Only new_frame_cpu is called. */
            SOFTWARE = 0b0001,
            /** GPU encoder. Frame is not downloaded on the CPU by the pipeline, but is still opened. */
            HARDWARE_D3D11TEXTURE2D = 0b0010,
            /** GPU encoder. Directly uses the shared handle. */
            HARDWARE_SHARED_HANDLE = 0b0100,
            /** Hybrid encoder: the preprocess function is called first with the opened GPU texture, and the returned texture is the
               one used after it. */
            HARDWARE_PREPROCESS_D3D11TEXTURE2D = 0b1000,
        };

      protected:
        EncoderCreateInfo m_create_info;

      public:
        explicit IVideoEncoder(const EncoderCreateInfo &create_info) : m_create_info(create_info) {}

        virtual ~IVideoEncoder() = default;

        [[nodiscard]] virtual std::string name() const = 0;

        [[nodiscard]] virtual EncoderType type() const = 0;

        /** Performs initial configuration if needed. */
        virtual bool init(void *__restrict d3d11_device, void *__restrict d3d11_device_context) { return true; };

        virtual void new_frame_gpu_with_shared_handle(uint64_t                 frame_id,
                                                      uint32_t                 rtp_timestamp,
                                                      bool end_of_stream,
                                                      wvb::SharedTextureHandle d3d11_shared_handle,
                                                      void *__restrict d3d11_device,
                                                      void *__restrict d3d11_device_context) {};

        /** Called using the GPU-only backbuffer texture.
         * */
        virtual void new_frame_gpu_with_texture(uint64_t frame_id,
                                                uint32_t rtp_timestamp,
                                                bool end_of_stream,
                                                void *__restrict d3d11_src_texture,
                                                void *__restrict d3d11_device,
                                                void *__restrict d3d11_device_context) {};

        /** Called using the CPU-accessible backbuffer texture. */
        virtual void new_frame_cpu(uint64_t frame_id, uint32_t rtp_timestamp, bool end_of_stream, const RawFrame &frame) {};

        /** Called using the GPU-only backbuffer texture. Expected to return a GPU-only texture that can then be used in place of the
         * backbuffer one. Useful for format conversion*/
        virtual void *preprocess_frame_gpu_with_texture(uint64_t frame_id,
                                                        uint32_t rtp_timestamp,
                                                        void *__restrict d3d11_src_texture,
                                                        void *__restrict d3d11_device,
                                                        void *__restrict d3d11_device_context)
        {
            return d3d11_src_texture;
        }

        virtual ImageFormat staging_texture_format() const { return ImageFormat::R8G8B8A8_UNORM; }

        /** Returns the next frame "m_packet" (e.g AVPacket). Not the same as an RTP m_packet.
         * There is typically one m_packet per frame, but there can be more.
         * Returns true if there are more packets after this one.
         * */
        virtual bool get_next_packet(const uint8_t **__restrict out_packet, size_t *__restrict out_packet_size) = 0;

        /**
         * Returns the number of frames before a pushed frame can be popped.
         * Typically called at the end of execution, so the measure can be computed over time.
         */
        virtual uint32_t get_frame_delay() const = 0;
    };

    /**
     * A video decoder handles the video decompression of frames.
     *
     * Decoders can be defined either as hardware or software.
     * Based on the selected type, the pipeline will retrieve the frame in the CPU or GPU.
     * Note: CPU mode is not implemented yet.
    */
    class IVideoDecoder
    {
      public:
        enum class DecoderType
        {
            /** CPU decoder. The pipeline will handle the upload. */
            SOFTWARE,
            /** GPU decoder. Frame is not uploaded on the CPU by the pipeline. */
            HARDWARE,
        };

        struct DecoderCreateInfo
        {
            Extent2D    src_size; // Full size (both eyes)
            RefreshRate refresh_rate;
            IO          io;
        };

      protected:
        DecoderCreateInfo m_create_info;

      public:
        explicit IVideoDecoder(const DecoderCreateInfo &create_info) : m_create_info(create_info) {}

        virtual ~IVideoDecoder() = default;

        [[nodiscard]] virtual std::string name() const = 0;

        [[nodiscard]] virtual DecoderType type() const = 0;

        /** Performs initial configuration if needed. */
        virtual void init() {};

        /** Called when a full frame m_packet is received. Thus only called once per frame. */
        virtual bool push_packet(const uint8_t *packet, size_t packet_size, bool end_of_stream) = 0;

        /** Returns true if there is a frame. */
        virtual bool get_frame_cpu(RawFrame &frame) { return false; };

        /** Returns true if there is a frame. Updates the given OpenGL texture with the latest frame */
        virtual std::optional<GLFrameTexture> get_frame_gpu() { return std::nullopt; };

        virtual uint32_t get_frame_delay() const = 0;
    };

    constexpr IVideoEncoder::EncoderType operator|(IVideoEncoder::EncoderType a, IVideoEncoder::EncoderType b)
    {
        return static_cast<IVideoEncoder::EncoderType>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    constexpr uint8_t operator&(IVideoEncoder::EncoderType a, IVideoEncoder::EncoderType b)
    {
        return static_cast<uint8_t>(a) & static_cast<uint8_t>(b);
    }

} // namespace wvb
