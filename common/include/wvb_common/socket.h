#pragma once

#include "socket_addr.h"

#include <cstddef>

namespace wvb
{
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

        /** Returns the port that is actually in use by the socket. Useful when the port is chosen automatically. */
        [[nodiscard]] uint16_t port() const;
        /** Returns the address that is actually in use by the socket. Useful when the address is chosen automatically. */
        [[nodiscard]] InetAddr inet_addr() const;
        /** Returns the address that is actually in use by the socket. Useful when the address is chosen automatically. */
        [[nodiscard]] SocketAddr socket_addr() const;
    };

} // namespace wvb