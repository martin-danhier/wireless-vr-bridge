#include <wvb_common/socket_addr.h>
#include <iostream>

namespace wvb
{
    std::string to_string(InetAddr addr)
    {
        return std::to_string(INET_ADDR_B1(addr)) + "." + std::to_string(INET_ADDR_B2(addr)) + "." + std::to_string(INET_ADDR_B3(addr))
               + "." + std::to_string(INET_ADDR_B4(addr));
    }

    std::string to_string(const SocketAddr &addr)
    {
        return to_string(addr.addr) + ":" + std::to_string(addr.port);
    }
} // namespace wvb


