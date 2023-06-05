#ifdef _WIN32

#include <wvb_common/formats/ffmpeg.h>
#include <wvb_common/formats/h264.h>

extern "C"
{
#include <libavcodec/avcodec.h>
}

namespace wvb
{

    std::shared_ptr<IVideoEncoder> create_h264_encoder(const IVideoEncoder::EncoderCreateInfo &create_info)
    {
        LOG("Using delay %d\n", create_info.delay);
        return create_ffmpeg_gpu_video_encoder({
            .base_create_info = create_info,
            .specific_options =
                FFMpegSpecificEncoderOptions {
                    .codec_name   = "h264_nvenc",
                    .profile      = FF_PROFILE_H264_HIGH,
                    .gop_size     = 0,
                    .max_b_frames = 0,
                    .thread_count = 1,
                    .options =
                        {
                            {"preset", "p1"},
                            {"rc-lookahead", "0"},
                            {"tune", "ull"},
                            {"zerolatency", "1"},
                            {"delay", std::to_string(create_info.delay)},
                        },
                },
        });
    }

    std::shared_ptr<IVideoEncoder> create_hevc_encoder(const IVideoEncoder::EncoderCreateInfo &create_info)
    {
        LOG("Using delay %d\n", create_info.delay);
        LOG("Using bitrate %d\n", create_info.bitrate);

        std::vector<FFMpegOption> options = {
            {"preset", "p4"},
            {"rc-lookahead", "0"},
            // {"tune", "ull"},
            {"zerolatency", "1"},
            // {"intra-refresh", "1"},
            // {"rc", "vbr"},
        };
        if (create_info.delay >= 0) {
            options.push_back({"delay", std::to_string(create_info.delay)});
        }

        return create_ffmpeg_gpu_video_encoder(FFMpegEncoderCreateInfo {
            .base_create_info = create_info,
            .specific_options =
                {
                    .codec_name   = "hevc_nvenc",
                    .profile      = FF_PROFILE_HEVC_MAIN,
                    .gop_size     = 0,
                    .max_b_frames = 0,
                    .options = options,
                },
        });
    }

    std::shared_ptr<IVideoEncoder> create_av1_encoder(const IVideoEncoder::EncoderCreateInfo &create_info)
    {
        return create_ffmpeg_cpu_video_encoder(FFMpegEncoderCreateInfo {
            .base_create_info = create_info,
            .specific_options =
                {
                    .codec_name              = "libaom-av1",
                    .profile                 = FF_PROFILE_AV1_MAIN,
                    .supports_precise_format = false,
                    .options =
                        {
                            // {"preset", "p1"},
                            // {"rc-lookahead", "0"},
                            // {"tune", "ull"},
                            // {"zerolatency", "1"},
                            // {"delay", "0"},
                        },
                },
        });
    }

    std::shared_ptr<IVideoEncoder> create_vp9_encoder(const IVideoEncoder::EncoderCreateInfo &create_info)
    {
        return create_ffmpeg_cpu_video_encoder(FFMpegEncoderCreateInfo {
            .base_create_info = create_info,
            .specific_options =
                {
                    .codec_name              = "libvpx-vp9",
                    .profile                 = FF_PROFILE_VP9_0,
                    .gop_size                = 30,
                    .max_b_frames            = 0,
                    .supports_precise_format = false,
                    .options =
                        {
                            {"cpu-used", "8"},
                            {"deadline", "realtime"},
                            // {"preset", "veryfast"}
                        },
                },
        });
    }

} // namespace wvb

#endif