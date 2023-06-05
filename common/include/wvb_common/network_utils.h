#pragma once

#include <cstdint>

namespace wvb
{
    constexpr uint32_t htonl(uint32_t host_long)
    {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return ((host_long & 0xFF000000) >> 24) | ((host_long & 0x00FF0000) >> 8) | ((host_long & 0x0000FF00) << 8)
               | ((host_long & 0x000000FF) << 24);
#else
        return host_long;
#endif
    }

    constexpr uint16_t htons(uint16_t host_short)
    {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return ((host_short & 0xFF00) >> 8) | ((host_short & 0x00FF) << 8);
#else
        return host_short;
#endif
    }

    constexpr uint64_t htonll(uint64_t host_longlong)
    {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return ((host_longlong & 0xFF00000000000000) >> 56) | ((host_longlong & 0x00FF000000000000) >> 40)
               | ((host_longlong & 0x0000FF0000000000) >> 24) | ((host_longlong & 0x000000FF00000000) >> 8)
               | ((host_longlong & 0x00000000FF000000) << 8) | ((host_longlong & 0x0000000000FF0000) << 24)
               | ((host_longlong & 0x000000000000FF00) << 40) | ((host_longlong & 0x00000000000000FF) << 56);
#else
        return host_longlong;
#endif
    }
    constexpr uint32_t ntohl(uint32_t net_long)
    {
        return htonl(net_long);
    }

    constexpr uint16_t ntohs(uint16_t net_short)
    {
        return htons(net_short);
    }

    constexpr uint64_t ntohll(uint64_t net_longlong)
    {
        return htonll(net_longlong);
    }

    constexpr uint32_t htonf(float host_float)
    {
        union
        {
            float f;
            uint32_t i;
        } u;
        u.f = host_float;
        return htonl(u.i);
    }

    constexpr float ntohf(uint32_t net_float)
    {
        union
        {
            float f;
            uint32_t i;
        } u;
        u.i = ntohl(net_float);
        return u.f;
    }
} // namespace wvb