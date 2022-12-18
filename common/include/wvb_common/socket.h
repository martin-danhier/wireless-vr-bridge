#pragma once

#include "socket_addr.h"

#include <cstddef>
#include <optional>

#define DEFAULT_TIMEOUT 1000
#define NO_TIMEOUT      UINT32_MAX

namespace wvb
{
    enum class SocketResultType
    {
        OK,
        ERROR,
        TIMEOUT
    };

    /**
     *  Wrapper around the result of a send or receive call to a UDP or TCP socket.
     * The result can either be OK, ERROR or TIMEOUT.
     * In case it is OK, the "size" field contains the number of bytes sent or received.
     */
    struct SocketResult
    {
        SocketResultType type;
        size_t           size;

        [[nodiscard]] inline bool is_ok() const noexcept { return type == SocketResultType::OK; }
        [[nodiscard]] inline bool is_error() const noexcept { return type == SocketResultType::ERROR; }
        [[nodiscard]] inline bool is_timeout() const noexcept { return type == SocketResultType::TIMEOUT; }

        // Helper functions to create SocketResult objects
        static constexpr SocketResult timeout() noexcept { return {SocketResultType::TIMEOUT, 0}; }
        static constexpr SocketResult error() noexcept { return {SocketResultType::ERROR, 0}; }
        static constexpr SocketResult ok(size_t size) noexcept { return {SocketResultType::OK, size}; }
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
        SocketResult send(const void *data, size_t size, const SocketAddr &dest) const;

        /** Receive a datagram from a specific address.
         * @param data The buffer to store the received data in.
         * @param size The size of the buffer.
         * @param src  The socket address of the sender.
         * @return The number of bytes received, or -1 if an error occurred.
         *
         * If the buffer is too small, the datagram will be truncated.
         * */
        SocketResult receive(void *data, size_t size, const SocketAddr &src = SocketAddr()) const;

        /** Returns the port that is actually in use by the socket. Useful when the port is chosen automatically. */
        [[nodiscard]] uint16_t port() const;
        /** Returns the address that is actually in use by the socket. Useful when the address is chosen automatically. */
        [[nodiscard]] InetAddr inet_addr() const;
        /** Returns the address that is actually in use by the socket. Useful when the address is chosen automatically. */
        [[nodiscard]] SocketAddr socket_addr() const;
    };

    /**
     * Cross-platform wrapper around TCP sockets.
     * */
    class TCPSocket
    {
      private:
        struct Data;
        Data *m_data = nullptr;

      public:
        TCPSocket() = default;

        /**
         * Create a new TCP socket. By default, it is in CLOSED state. Either call accept() to start listening for new connections, or
         * call connect() to try to connect to a listening socket.
         * @param port The port to listen on.
         * @param force_port If true, the socket will be created even if the port is already in use.
         * */
        explicit TCPSocket(uint16_t port, bool force_port = false);

        ~TCPSocket();
        TCPSocket(const TCPSocket &other) = delete;
        inline TCPSocket(TCPSocket &&other) noexcept : m_data(other.m_data) { other.m_data = nullptr; }
        inline TCPSocket &operator=(TCPSocket &&other) noexcept
        {
            this->~TCPSocket();

            m_data       = other.m_data;
            other.m_data = nullptr;
            return *this;
        }

        [[nodiscard]] inline bool is_valid() const noexcept { return m_data != nullptr; };

        /**
         * Accept an incoming connection. If it returns OK, the socket is now connected and cannot be used to listen for new
         * connections anymore. If it returns TIMEOUT, you should call this function again.
         */
        SocketResultType accept() const;

        /** Tries to connect to a peer. If it returns OK, the socket is now connected and cannot be used to connect anymore.
         * If it returns TIMEOUT, you should call this function again.
         * */
        SocketResultType connect(const SocketAddr &peer) const;

        /** Send data to the connected peer.
         * @param data The data to send.
         * @param size The size of the data to send.
         * @return The number of bytes sent
         * */
        SocketResult send(const void *data, size_t size) const;

        /** Receive data from the connected peer.
         * @param data The buffer to store the received data in.
         * @param size The size of the buffer.
         * @return The number of bytes received
         *
         * If the buffer is too small, the datagram will be truncated.
         * */
        SocketResult receive(void *data, size_t size) const;

        /** Returns the port that is actually in use by the socket. Useful when the port is chosen automatically. */
        [[nodiscard]] uint16_t port() const;
        /** Returns the address that is actually in use by the socket. Useful when the address is chosen automatically. */
        [[nodiscard]] InetAddr inet_addr() const;
        /** Returns the address that is actually in use by the socket. Useful when the address is chosen automatically. */
        [[nodiscard]] SocketAddr socket_addr() const;

        /** Returns the port of the connected peer. */
        [[nodiscard]] uint16_t peer_port() const;
        /** Returns the address of the connected peer. */
        [[nodiscard]] InetAddr peer_inet_addr() const;
        /** Returns the address of the connected peer. */
        [[nodiscard]] SocketAddr peer_socket_addr() const;

        [[nodiscard]] bool is_connected() const;
    };

} // namespace wvb