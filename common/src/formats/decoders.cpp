#ifdef __ANDROID__

#include <wvb_common/formats/media_codec.h>

namespace wvb
{

    std::shared_ptr<IVideoDecoder> create_h264_decoder(const IVideoDecoder::DecoderCreateInfo &create_info)
    {
        return create_media_codec_video_decoder({
            .codec_name       = "video/avc",
            .base_create_info = create_info,
        });
    }

    std::shared_ptr<IVideoDecoder> create_hevc_decoder(const IVideoDecoder::DecoderCreateInfo &create_info)
    {
        return create_media_codec_video_decoder({
            .codec_name       = "video/hevc",
            .base_create_info = create_info,
        });
    }

    std::shared_ptr<IVideoDecoder> create_av1_decoder(const IVideoDecoder::DecoderCreateInfo &create_info)
    {
        return create_media_codec_video_decoder({
            .codec_name       = "video/AV1",
            .base_create_info = create_info,
        });
    }

    std::shared_ptr<IVideoDecoder> create_vp9_decoder(const IVideoDecoder::DecoderCreateInfo &create_info)
    {
        return create_media_codec_video_decoder({
            .codec_name       = "video/x-vnd.on2.vp9",
            .base_create_info = create_info,
        });
    }

} // namespace wvb

#endif