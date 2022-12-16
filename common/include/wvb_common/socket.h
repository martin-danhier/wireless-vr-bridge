#pragma once

#include <cstddef>
#include <cstdint>

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

#define NO_TIMEOUT UINT32_MAX

    struct SocketAddr
    {
        InetAddr addr = INET_ADDR_ANY;
        uint16_t port = 0;

        [[nodiscard]] inline bool is_any() const noexcept { return addr == INET_ADDR_ANY; }
    };

    /**
     * Cross-platform wrapper around UDP sockets.
     * Since UDP sockets are not connection-oriented, the destination address must be specified for each send() call.
     * */
    class UDPSocket
    {
      private:
        struct Data;
        Data *m_data = nullptr;

      public:
        UDPSocket() = default;

        /**
         * Create a new datagram socket.
         * @param port The port to listen on.
         * */
        explicit UDPSocket(uint16_t port);
        ~UDPSocket();
        UDPSocket(const UDPSocket &other) = delete;
        inline UDPSocket(UDPSocket &&other) noexcept : m_data(other.m_data) { other.m_data = nullptr; }
        inline UDPSocket &operator=(UDPSocket &&other) noexcept
        {
            this->~UDPSocket();

            m_data       = other.m_data;
            other.m_data = nullptr;
            return *this;
        }

        [[nodiscard]] inline bool is_valid() const noexcept { return m_data != nullptr; };

        /** Send a datagram to a specific address.
         * @param data The data to send.
         * @param size The size of the data to send.
         * @param dest The socket address to send the data to.
         * @return The number of bytes sent, or -1 if an error occurred.
         * */
        size_t send(const void *data, size_t size, const SocketAddr &dest) const;

        /** Receive a datagram from a specific address.
         * @param data The buffer to store the received data in.
         * @param size The size of the buffer.
         * @param src  The socket address of the sender.
         * @return The number of bytes received, or -1 if an error occurred.
         *
         * If the buffer is too small, the datagram will be truncated.
         * */
        size_t receive(void *data, size_t size, const SocketAddr &src = SocketAddr()) const;
    };

} // namespace wvb