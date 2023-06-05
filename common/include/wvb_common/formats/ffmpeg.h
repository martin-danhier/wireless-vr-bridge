#pragma once

#include <wvb_common/video_encoder.h>

#include <memory>
#include <vector>

namespace wvb
{
#ifdef _WIN32
    struct FFMpegOption
    {
        const char *key   = "";
        std::string value = "";
    };

    /** As the FFMpeg encoder is general and can support multiple codecs, additional settings
     * can be defined.
    */
    struct FFMpegSpecificEncoderOptions
    {
        const char               *codec_name   = "";
        int                       profile      = 0;
        int                       gop_size     = 0;
        int                       max_b_frames = 0;
        int                       delay        = 0; // -1 = auto
        int                       thread_count = 1;
        bool                      supports_precise_format = true; // true: use NV12. false: use YUV420P
        std::vector<FFMpegOption> options      = {};
    };

    /** Additionally to regular encoder create info, take ffmpeg-specific parameters, that can be defined
     * for each codec.
    */
    struct FFMpegEncoderCreateInfo
    {
        IVideoEncoder::EncoderCreateInfo base_create_info {};
        FFMpegSpecificEncoderOptions     specific_options {};
    };


    // Factory functions

    std::shared_ptr<IVideoEncoder> create_ffmpeg_gpu_video_encoder(const FFMpegEncoderCreateInfo &create_info);
    std::shared_ptr<IVideoEncoder> create_ffmpeg_cpu_video_encoder(const FFMpegEncoderCreateInfo &create_info);
#endif
} // namespace wvb
