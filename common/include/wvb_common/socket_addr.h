#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace wvb
{
    /** Represents an IPv4 address. */
    typedef uint32_t InetAddr;

/** Macro to create an IPv4 address from its components. */
#define INET_ADDR(a, b, c, d) ((a << 24) | (b << 16) | (c << 8) | d)
/** Any address (0.0.0.0) */
#define INET_ADDR_ANY 0x00000000
/** Loopback address (127.0.0.1) */
#define INET_ADDR_LOOPBACK 0x7F000001

#define INET_ADDR_B1(addr) (((addr) >> 24) & 0xFF)
#define INET_ADDR_B2(addr) (((addr) >> 16) & 0xFF)
#define INET_ADDR_B3(addr) (((addr) >> 8) & 0xFF)
#define INET_ADDR_B4(addr) ((addr) &0xFF)

/**
 * When this port is used, the program will try to find an available port automatically.
 */
#define PORT_AUTO 0

    struct SocketAddr
    {
        InetAddr addr = INET_ADDR_ANY;
        uint16_t port = PORT_AUTO;

        [[nodiscard]] inline bool is_any() const noexcept { return addr == INET_ADDR_ANY; }
        [[nodiscard]] inline bool is_port_auto() const noexcept { return port == PORT_AUTO; }

        [[nodiscard]] std::string to_string() const
        {
            return std::to_string((addr >> 24) & 0xFF) + "." + std::to_string((addr >> 16) & 0xFF) + "."
                   + std::to_string((addr >> 8) & 0xFF) + "." + std::to_string(addr & 0xFF) + ":" + std::to_string(port);
        }

        [[nodiscard]] inline bool operator==(const SocketAddr &other) const noexcept
        {
            return addr == other.addr && port == other.port;
        }
    };

    // String operators

    std::string to_string(InetAddr addr);
    std::string to_string(const SocketAddr &addr);

    struct VRCPServerCandidate
    {
        SocketAddr addr;
        uint32_t   timestamp = 0; // unix timestamp in seconds
        uint8_t    interval  = 0;
    };


} // namespace wvb