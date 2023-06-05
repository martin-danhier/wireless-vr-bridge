#pragma once

#include <wvb_common/packetizer.h>
#include <memory>

namespace wvb
{

    // Factory functions for a simple packetization scheme designed for TCP

    std::shared_ptr<IPacketizer> create_simple_packetizer();
    std::shared_ptr<IDepacketizer> create_simple_depacketizer();
} // namespace wvb
