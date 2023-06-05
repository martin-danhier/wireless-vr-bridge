#include <wvb_common/formats/simple_packetizer.h>
#include <wvb_common/module.h>

#include <iostream>

// Export dll

std::shared_ptr<wvb::IPacketizer> create_test_module_packetizer(uint32_t ssrc)
{
    return wvb::create_simple_packetizer();
}

std::shared_ptr<wvb::IDepacketizer> create_test_module_depacketizer()
{
    return wvb::create_simple_depacketizer();
}

extern "C"
{
    WVB_EXPORT wvb::Module get_module_info()
    {
        std::cout << "Hello from module !\n";

        return {
            "test_module",
            "Test module",
            create_test_module_packetizer,
            create_test_module_depacketizer,
        };
    }
}