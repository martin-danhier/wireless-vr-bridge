#pragma once

#include <wvb_common/packetizer.h>
#include <wvb_common/video_encoder.h>

#include <memory>

#define WVB_EARLY_FRAME_TOLERANCE 128
#define WVB_RTP_MTU               1500

namespace wvb
{
    std::shared_ptr<IPacketizer>   create_h264_rtp_packetizer(uint32_t ssrc);
    std::shared_ptr<IDepacketizer> create_h264_rtp_depacketizer();

#ifdef _WIN32
    std::shared_ptr<IVideoEncoder> create_h264_encoder(const IVideoEncoder::EncoderCreateInfo &create_info);
#endif
#ifdef __ANDROID__
    std::shared_ptr<IVideoDecoder> create_h264_decoder(const IVideoDecoder::DecoderCreateInfo &create_info);
#endif
} // namespace wvb