#ifdef __ANDROID__
#pragma once

#include <wvb_common/video_encoder.h>

#include <memory>

namespace wvb
{
    struct MediaCodecDecoderCreateInfo
    {
        const char                             *codec_name = "";
        const IVideoDecoder::DecoderCreateInfo &base_create_info;
    };

    std::shared_ptr<IVideoDecoder> create_media_codec_video_decoder(const MediaCodecDecoderCreateInfo &create_info);
} // namespace wvb
#endif
