#pragma once

#include <wvb_common/packetizer.h>
#include <wvb_common/video_encoder.h>

#include <memory>
#include <string>
#include <vector>
#include <wvb_common/io.h>

#ifdef _WIN32
#define WVB_EXPORT __declspec(dllexport)
#else
#define WVB_EXPORT
#endif

namespace wvb
{
    // Create functions
    typedef std::shared_ptr<IPacketizer> (*CreatePacketizerFunction)(uint32_t ssrc);
    typedef std::shared_ptr<IDepacketizer> (*CreateDepacketizerFunction)();
    typedef std::shared_ptr<IVideoEncoder> (*CreateVideoEncoderFunction)(const IVideoEncoder::EncoderCreateInfo &create_info);
    typedef std::shared_ptr<IVideoDecoder> (*CreateVideoDecoderFunction)(const IVideoDecoder::DecoderCreateInfo &create_info);
    typedef void (*TestFunction)(const IO &io);

    /**
     * To allow testing of closed-source libraries, WVB has a plug-in system.
     * Third-party encoders/decoders and (de)packetizers can be defined in an external shared library.
     * Such a library must be named "wvb_module_<id>.so" and be located next to the executable.
     */
    struct Module
    {
        std::string                codec_id;
        std::string                name;
        CreatePacketizerFunction   create_packetizer    = nullptr;
        CreateDepacketizerFunction create_depacketizer  = nullptr;
        CreateVideoEncoderFunction create_video_encoder = nullptr;
        CreateVideoDecoderFunction create_video_decoder = nullptr;
        TestFunction               test_function        = nullptr;
        void                      *handle               = nullptr;

        void close() const;
    };

    typedef wvb::Module (*GetModuleInfoFunction)();

    std::vector<Module> load_modules();
} // namespace wvb
