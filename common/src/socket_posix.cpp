// Implementation is platform-dependant since we need OS-specific functions to create UDP sockets.

#ifdef __linux__
#include "wvb_common/socket.h"

#include <chrono>
#include <deque>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>


namespace wvb
{

    // =======================================================================================
    // =                                 Structs and classes                                 =
    // =======================================================================================

    typedef int32_t SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)

    // Non-blocking TCP socket
    struct TCPSocket::Data
    {
        SOCKET socket = INVALID_SOCKET;

        TCPSocketState state = TCPSocketState::NOT_STARTED;
        SocketAddr     local_addr;
        SocketAddr     peer_addr;

        std::shared_ptr<SocketMeasurementBucket> measurements_bucket;
        int32_t                                  measurement_storage_id = -1;
    };

    // Non-blocking UDP socket
    struct UDPSocket::Data
    {
        SOCKET socket = INVALID_SOCKET;

        SocketAddr local_addr;

        std::shared_ptr<SocketMeasurementBucket> measurements_bucket;
        int32_t                                  measurement_storage_id = -1;
    };

    // ========================================================================================
    // =                               TCP Socket implementation                              =
    // ========================================================================================

    TCPSocket::TCPSocket(uint16_t local_port, bool force_port, std::shared_ptr<SocketMeasurementBucket> bucket, SocketId socket_id)
        : m_data(new Data {.measurements_bucket = std::move(bucket)})
    {
        // Create the socket
        m_data->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_data->socket == INVALID_SOCKET)
        {
            throw std::runtime_error("Failed to create socket");
        }

        // Force the port to be used
        if (force_port)
        {
            int optval = 1;
            if (setsockopt(m_data->socket, SOL_SOCKET, SO_REUSEADDR, (const char *) &optval, sizeof(optval)) == SOCKET_ERROR)
            {
                throw std::runtime_error("Failed to set socket option");
            }
        }

        // Set the socket to non-blocking mode
        // (Get current flags, add non-blocking flag, set flags again)
        int flags = fcntl(m_data->socket, F_GETFL, 0);
        if (flags == -1)
        {
            throw std::runtime_error("Failed to get socket flags");
        }
        flags |= O_NONBLOCK;
        if (fcntl(m_data->socket, F_SETFL, flags) == -1)
        {
            throw std::runtime_error("Failed to set socket flags");
        }

        // Bind socket
        sockaddr_in addr     = {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INET_ADDR_ANY);
        addr.sin_port        = htons(local_port);
        auto res             = bind(m_data->socket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        if (res != 0)
        {
            // Check if port is already in use
            if (errno == EADDRINUSE)
            {
                throw std::runtime_error("Port already in use");
            }

            throw std::runtime_error("Failed to bind socket");
        }

        // Get local address
        socklen_t addr_len = sizeof(addr);
        res                = getsockname(m_data->socket, reinterpret_cast<sockaddr *>(&addr), &addr_len);
        if (res != 0)
        {
            throw std::runtime_error("Failed to get local address");
        }
        m_data->local_addr.addr = ntohl(addr.sin_addr.s_addr);
        m_data->local_addr.port = ntohs(addr.sin_port);

        if (m_data->measurements_bucket != nullptr)
        {
            m_data->measurement_storage_id = m_data->measurements_bucket->register_socket(socket_id, SocketType::SOCKET_TYPE_TCP);
        }
    }

    TCPSocket::~TCPSocket()
    {
        if (m_data != nullptr)
        {
            close(); // Close if needed

            delete m_data;
            m_data = nullptr;
        }
    }

    // Connection management

    void TCPSocket::enable_server() const
    {
        // The first time, enable listening
        if (m_data->state == TCPSocketState::NOT_STARTED)
        {
            // Start listening
            if (::listen(m_data->socket, 1) != 0)
            {
                throw std::runtime_error("Failed to start listening");
            }
            m_data->state = TCPSocketState::LISTENING;
        }
    }

    bool TCPSocket::listen() const
    {
        // The first time, enable listening
        if (m_data->state == TCPSocketState::NOT_STARTED)
        {
            // Start listening
            if (::listen(m_data->socket, 1) != 0)
            {
                throw std::runtime_error("Failed to start listening");
            }
            m_data->state = TCPSocketState::LISTENING;
        }

        // Socket must be listening
        if (m_data->state != TCPSocketState::LISTENING)
        {
            throw std::runtime_error("Socket is not listening");
        }

        // Then try to accept a connection
        sockaddr_in addr     = {};
        socklen_t   addr_len = sizeof(addr);
        auto        client   = accept(m_data->socket, reinterpret_cast<sockaddr *>(&addr), &addr_len);

        // Check if a connection was accepted
        if (client == INVALID_SOCKET)
        {
            // Check if there was an error
            auto err = errno;
            // Return false if it is a timeout or a would block error. Otherwise, it is a real error
            if (err != EAGAIN && err != EWOULDBLOCK && err != ETIMEDOUT && err != EINPROGRESS)
            {
                throw std::runtime_error("Failed to accept connection");
            }
            return false;
        }

        // The connection worked, remove old one
        ::close(m_data->socket);
        m_data->socket = client;

        // Set the socket to non-blocking mode
        // (Get current flags, add non-blocking flag, set flags again)
        int flags = fcntl(m_data->socket, F_GETFL, 0);
        if (flags == -1)
        {
            throw std::runtime_error("Failed to get socket flags");
        }
        flags |= O_NONBLOCK;
        if (fcntl(m_data->socket, F_SETFL, flags) == -1)
        {
            throw std::runtime_error("Failed to set socket flags");
        }

        // Get peer address
        m_data->peer_addr.addr = ntohl(addr.sin_addr.s_addr);
        m_data->peer_addr.port = ntohs(addr.sin_port);

        // Update local address
        addr_len = sizeof(addr);
        auto res = getsockname(m_data->socket, reinterpret_cast<sockaddr *>(&addr), &addr_len);
        if (res != 0)
        {
            throw std::runtime_error("Failed to get local address");
        }
        m_data->local_addr.addr = ntohl(addr.sin_addr.s_addr);
        m_data->local_addr.port = ntohs(addr.sin_port);

        // Connection completed
        m_data->state = TCPSocketState::CONNECTED;

        return true;
    }

    bool TCPSocket::connect(const SocketAddr &addr) const
    {
        if (m_data->state == TCPSocketState::NOT_STARTED)
        {
            m_data->state = TCPSocketState::CONNECTING;
        }

        // Socket must be connecting
        if (m_data->state != TCPSocketState::CONNECTING)
        {
            throw std::runtime_error("Socket is not connecting");
        }

        // Try to connect
        sockaddr_in sock_addr     = {};
        sock_addr.sin_family      = AF_INET;
        sock_addr.sin_addr.s_addr = htonl(addr.addr);
        sock_addr.sin_port        = htons(addr.port);
        auto res                  = ::connect(m_data->socket, reinterpret_cast<sockaddr *>(&sock_addr), sizeof(sock_addr));

        // Check if the connection was successful
        if (res != 0)
        {
            // Check if there was an error
            auto err = errno;

            // EISCONN means the connection is already established, probably that it finished since the last call. Consider it as a
            // success
            if (err != EISCONN)
            {
                // Return false if it is a timeout or a would block error. Otherwise, it is a real error
                if (err != EWOULDBLOCK && err != EALREADY && err != ETIMEDOUT && err != EINPROGRESS)
                {
                    throw std::runtime_error("Failed to connect (" + std::to_string(err) + ")");
                }
                return false;
            }
        }

        // Connection completed
        m_data->state = TCPSocketState::CONNECTED;

        // Get peer address
        m_data->peer_addr = addr;

        return true;
    }

    void TCPSocket::close() const
    {
        if (m_data->socket != INVALID_SOCKET)
        {
            // Close socket
            ::close(m_data->socket);
            m_data->socket = INVALID_SOCKET;
            m_data->state  = TCPSocketState::CLOSED;
        }
    }

    TCPSocketState TCPSocket::refresh_state() const
    {
        if (m_data->state == TCPSocketState::CONNECTED)
        {
            // See if still connected
            char buf;
            auto res = ::recv(m_data->socket, &buf, 1, MSG_PEEK);

            // Check if the connection was successful
            if (res == 0)
            {
                // Connection closed
                close();
            }
            else if (res == SOCKET_ERROR)
            {
                // Check if there was an error
                auto err = errno;

                // Return false if it is a timeout or a would block error. Otherwise, it is a real error
                if (err != EWOULDBLOCK && err != EALREADY && err != ETIMEDOUT)
                {
                    // Connection closed
                    close();
                }
            }
        }
        return m_data->state;
    }

    // Transmission

    void TCPSocket::send(const uint8_t *data, size_t size, uint32_t timeout_us) const
    {
        // Socket must be connected
        if (m_data->state != TCPSocketState::CONNECTED)
        {
            throw std::runtime_error("Socket is not connected");
        }

        // TCP can send partial messages, so we need to loop until everything is sent
        size_t sent = 0;

        // Send message
        auto time0 = std::chrono::high_resolution_clock::now();
        while (timeout_us == 0 || std::chrono::high_resolution_clock::now() - time0 < std::chrono::microseconds(timeout_us))
        {
            auto res = ::send(m_data->socket, reinterpret_cast<const char *>(data + sent), static_cast<int>(size - sent), 0);
            if (res == SOCKET_ERROR)
            {
                const auto err = errno;
                if (err != EWOULDBLOCK && err != EAGAIN)
                {
                    throw std::runtime_error("Failed to send message");
                }
            }
            else
            {
                sent += res;
                if (sent == size)
                {
                    // No error
                    if (m_data->measurements_bucket)
                    {
                        m_data->measurements_bucket->add_bytes_sent(m_data->measurement_storage_id, sent);
                        m_data->measurements_bucket->add_packets_sent(m_data->measurement_storage_id, 1);
                        // Packets are not very useful for TCP as they are not sent one by one
                        // We will just count the number of calls to send
                    }
                    return;
                }
            }

            // Wait a bit
            if (timeout_us == 0
                || std::chrono::high_resolution_clock::now() - time0 + std::chrono::microseconds(10)
                       < std::chrono::microseconds(timeout_us))
            {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
        }
        std::cerr << "Failed to send message: timeout\n";
    }

    bool TCPSocket::receive(uint8_t *data, size_t size, size_t *actual_size) const
    {
        // Socket must be connected
        if (m_data->state != TCPSocketState::CONNECTED)
        {
            throw std::runtime_error("Socket is not connected");
        }

        // Receive message
        auto res = ::recv(m_data->socket, reinterpret_cast<char *>(data), static_cast<int>(size), 0);
        if (res == SOCKET_ERROR)
        {
            // Check if there was an error
            auto err = errno;
            if (err == ECONNRESET)
            {
                // Connection closed
                close();
                return false;
            }
            // Return false if it is a timeout or a would block error. Otherwise, it is a real error
            if (err != EWOULDBLOCK && err != EALREADY && err != ETIMEDOUT && err != EINPROGRESS)
            {
                throw std::runtime_error("Failed to receive_from message");
            }
            return false; // No new message
        }

        // Update actual size
        *actual_size = res;

        if (m_data->measurements_bucket)
        {
            m_data->measurements_bucket->add_bytes_received(m_data->measurement_storage_id, res);
            m_data->measurements_bucket->add_packets_received(m_data->measurement_storage_id, 1);
        }

        return true; // New message
    }

    // Getters
    TCPSocketState TCPSocket::state() const
    {
        return m_data->state;
    }

    const SocketAddr &TCPSocket::local_addr() const
    {
        return m_data->local_addr;
    }

    const SocketAddr &TCPSocket::peer_addr() const
    {
        return m_data->peer_addr;
    }

    // ========================================================================================
    // =                               UDP Socket implementation                              =
    // ========================================================================================

    // Constructors

    UDPSocket::UDPSocket(uint16_t                                 local_port,
                         bool                                     force_port,
                         bool                                     allow_broadcast,
                         std::shared_ptr<SocketMeasurementBucket> bucket,
                         SocketId                                 socket_id)
        : m_data(new Data {.measurements_bucket = std::move(bucket)})
    {
        // Create the socket
        m_data->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_data->socket == INVALID_SOCKET)
        {
            throw std::runtime_error("Failed to create socket");
        }

        // Force the port to be used
        if (force_port)
        {
            int optval = 1;
            if (setsockopt(m_data->socket, SOL_SOCKET, SO_REUSEADDR, (const char *) &optval, sizeof(optval)) == SOCKET_ERROR)
            {
                throw std::runtime_error("Failed to set socket option");
            }
        }

        // Allow broadcast
        if (allow_broadcast)
        {
            int optval = 1;
            if (setsockopt(m_data->socket, SOL_SOCKET, SO_BROADCAST, (const char *) &optval, sizeof(optval)) == SOCKET_ERROR)
            {
                throw std::runtime_error("Failed to set socket option");
            }
        }

        // Set the socket to non-blocking mode
        int flags = fcntl(m_data->socket, F_GETFL, 0);
        if (flags == -1)
        {
            throw std::runtime_error("Failed to get socket flags");
        }
        flags |= O_NONBLOCK;
        if (fcntl(m_data->socket, F_SETFL, flags) == -1)
        {
            throw std::runtime_error("Failed to set socket flags");
        }

        sockaddr_in addr     = {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INET_ADDR_ANY);
        addr.sin_port        = htons(local_port);
        auto res             = bind(m_data->socket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        if (res != 0)
        {
            // Check if port is already in use
            if (errno == EADDRINUSE)
            {
                throw std::runtime_error("Port already in use");
            }

            throw std::runtime_error("Failed to bind socket");
        }

        // Get local address
        socklen_t addr_len = sizeof(addr);
        res                = getsockname(m_data->socket, reinterpret_cast<sockaddr *>(&addr), &addr_len);
        if (res != 0)
        {
            throw std::runtime_error("Failed to get local address");
        }
        m_data->local_addr.addr = ntohl(addr.sin_addr.s_addr);
        m_data->local_addr.port = ntohs(addr.sin_port);

        if (m_data->measurements_bucket)
        {
            m_data->measurement_storage_id = m_data->measurements_bucket->register_socket(socket_id, SocketType::SOCKET_TYPE_UDP);
        }
    }

    UDPSocket::~UDPSocket()
    {
        if (m_data != nullptr)
        {
            close(); // Close if needed

            delete m_data;
            m_data = nullptr;
        }
    }

    // Connection management

    void UDPSocket::close() const
    {
        if (m_data->socket != INVALID_SOCKET)
        {
            // Close socket
            ::close(m_data->socket);
            m_data->socket = INVALID_SOCKET;
        }
    }

    // Transmission

    bool UDPSocket::send_to(const SocketAddr &addr, const uint8_t *data, size_t size) const
    {
        if (m_data->socket == INVALID_SOCKET)
        {
            return false;
        }

        // Send message
        sockaddr_in sock_addr     = {};
        sock_addr.sin_family      = AF_INET;
        sock_addr.sin_addr.s_addr = htonl(addr.addr);
        sock_addr.sin_port        = htons(addr.port);

        auto res = ::sendto(m_data->socket,
                            reinterpret_cast<const char *>(data),
                            static_cast<int>(size),
                            0,
                            reinterpret_cast<sockaddr *>(&sock_addr),
                            sizeof(sock_addr));

        if (m_data->measurements_bucket && res > 0)
        {
            m_data->measurements_bucket->add_bytes_sent(m_data->measurement_storage_id, res);
            m_data->measurements_bucket->add_packets_sent(m_data->measurement_storage_id, 1);
        }

        return res != SOCKET_ERROR;
    }

    bool UDPSocket::receive_from(uint8_t *data, size_t size, size_t *actual_size, SocketAddr *addr) const
    {
        if (m_data->socket == INVALID_SOCKET)
        {
            return false;
        }

        // Receive message
        sockaddr_in sock_addr = {};
        socklen_t   addr_len  = sizeof(sock_addr);
        auto        res       = ::recvfrom(m_data->socket,
                              reinterpret_cast<char *>(data),
                              static_cast<int>(size),
                              0,
                              reinterpret_cast<sockaddr *>(&sock_addr),
                              &addr_len);
        if (res == SOCKET_ERROR)
        {
            // Check if there was an error
            auto err = errno;
            // Return false if it is a "timeout" or a "would block" error. Otherwise, it is a real error
            if (err != EWOULDBLOCK && err != EALREADY && err != ETIMEDOUT)
            {
                throw std::runtime_error("Failed to receive_from message");
            }
            return false; // No new message
        }

        // Update actual size
        *actual_size = res;

        if (m_data->measurements_bucket)
        {
            m_data->measurements_bucket->add_bytes_received(m_data->measurement_storage_id, res);
            m_data->measurements_bucket->add_packets_received(m_data->measurement_storage_id, 1);
        }

        // Update address
        if (addr != nullptr)
        {
            addr->addr = ntohl(sock_addr.sin_addr.s_addr);
            addr->port = ntohs(sock_addr.sin_port);
        }

        return true; // New message
    }

    // Getters

    bool UDPSocket::is_open() const
    {
        return m_data->socket != INVALID_SOCKET;
    }

    const SocketAddr &UDPSocket::local_addr() const
    {
        return m_data->local_addr;
    }

} // namespace wvb

#endif