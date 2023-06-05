#pragma once

#include <wvb_common/video_encoder.h>

#include <memory>


namespace wvb
{
#ifdef _WIN32
    std::shared_ptr<IVideoEncoder> create_vp9_encoder(const IVideoEncoder::EncoderCreateInfo &create_info);
#endif
 #ifdef __ANDROID__
     std::shared_ptr<IVideoDecoder> create_vp9_decoder(const IVideoDecoder::DecoderCreateInfo &create_info);
 #endif
} // namespace wvb