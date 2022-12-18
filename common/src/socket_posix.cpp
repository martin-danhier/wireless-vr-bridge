// Implementation is platform-dependant since we need OS-specific functions to create UDP sockets.

#ifdef __linux__
#include "wvb_common/socket.h"

#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace wvb
{

    // region UDPSocket

    // =======================================================================================
    // =                                 Structs and classes                                 =
    // =======================================================================================

    typedef int32_t SOCKET;
#define INVALID_HANDLE_VALUE (-1)

    struct UDPSocket::Data
    {
        SOCKET     socket     = INVALID_HANDLE_VALUE;
        SocketAddr local_addr = {};
    };

    // =======================================================================================
    // =                                   Implementation                                    =
    // =======================================================================================

    UDPSocket::UDPSocket(uint16_t port) : m_data(new Data)
    {
        // Create socket
        m_data->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_data->socket == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error("Failed to create socket");
        }

        // Bind socket
        sockaddr_in addr {};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(port);
        addr.sin_addr.s_addr = htonl(INET_ADDR_LOOPBACK);
        auto res             = bind(m_data->socket, (sockaddr *) &addr, sizeof(addr));
        if (res != 0)
        {
            // Check if the port is already in use
            if (errno == EADDRINUSE)
            {
                throw std::runtime_error("Port is already in use");
            }
            throw std::runtime_error("Failed to bind socket");
        }

        // Set timeout
        timeval timeout {};
        timeout.tv_sec  = DEFAULT_TIMEOUT / 1000;
        timeout.tv_usec = (DEFAULT_TIMEOUT % 1000) * 1000;
        res             = setsockopt(m_data->socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (res != 0)
        {
            throw std::runtime_error("Failed to set socket timeout");
        }

        // Get the actual port
        socklen_t addr_len = sizeof(addr);
        res                = getsockname(m_data->socket, (sockaddr *) &addr, &addr_len);
        if (res != 0)
        {
            throw std::runtime_error("Failed to get socket address");
        }

        m_data->local_addr.addr = ntohl(addr.sin_addr.s_addr);
        m_data->local_addr.port = ntohs(addr.sin_port);
    }

    UDPSocket::~UDPSocket()
    {
        if (m_data != nullptr)
        {
            if (m_data->socket != INVALID_HANDLE_VALUE)
            {
                close(m_data->socket);
            }

            delete m_data;
            m_data = nullptr;
        }
    }

    SocketResult UDPSocket::send(const void *data, size_t size, const SocketAddr &dest) const
    {
        sockaddr_in dest_addr {};
        dest_addr.sin_family      = AF_INET;
        dest_addr.sin_port        = htons(dest.port);
        dest_addr.sin_addr.s_addr = htonl(dest.addr);

        auto res = sendto(m_data->socket, data, size, 0, (sockaddr *) &dest_addr, sizeof(dest_addr));

        if (res < 0)
        {
            // Check if timeout
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return SocketResult::timeout();
            }
            else
            {
                return SocketResult::error();
            }
        }

        return SocketResult::ok(static_cast<size_t>(res));
    }

    SocketResult UDPSocket::receive(void *data, size_t size, const SocketAddr &src) const
    {
        ssize_t result = 0;

        if (src.is_any())
        {
            result = recvfrom(m_data->socket, data, size, 0, nullptr, nullptr);
        }
        else
        {
            sockaddr_in sender {};
            sender.sin_family      = AF_INET;
            sender.sin_port        = htons(src.port);
            sender.sin_addr.s_addr = htonl(src.addr);

            socklen_t len = sizeof(sender);
            result        = recvfrom(m_data->socket, data, size, 0, (sockaddr *) &sender, &len);
        }

        if (result < 0)
        {
            // Check if timeout
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return SocketResult::timeout();
            }
            else
            {
                return SocketResult::error();
            }
        }
        return SocketResult::ok(static_cast<size_t>(result));
    }

    uint16_t UDPSocket::port() const
    {
        return m_data->local_addr.port;
    }

    InetAddr UDPSocket::inet_addr() const
    {
        return m_data->local_addr.addr;
    }

    SocketAddr UDPSocket::socket_addr() const
    {
        return m_data->local_addr;
    }

    // endregion

    // region TCPSocket

    // =======================================================================================
    // =                                 Structs and classes                                 =
    // =======================================================================================

    enum class TCPSocketState
    {
        CLOSED,
        LISTENING,
        CONNECTED
    };

    struct TCPSocket::Data
    {
        SOCKET         socket     = INVALID_HANDLE_VALUE;
        TCPSocketState state      = TCPSocketState::CLOSED;
        SocketAddr     local_addr = {};
        SocketAddr     peer_addr  = {};
    };

    // =======================================================================================
    // =                                   Implementation                                    =
    // =======================================================================================

    TCPSocket::TCPSocket(uint16_t port, bool force_port) : m_data(new Data)
    {
        // Create socket
        m_data->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_data->socket == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error("Failed to create socket");
        }

        // Force reuse of the port
        if (force_port)
        {
            int  optval = 1;
            auto res    = setsockopt(m_data->socket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
            if (res != 0)
            {
                throw std::runtime_error("Failed to set socket options");
            }
        }

        // Bind
        sockaddr_in addr {};
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(port);
        addr.sin_addr.s_addr = htonl(INET_ADDR_LOOPBACK);
        socklen_t addr_len   = sizeof(addr);
        auto      res        = bind(m_data->socket, (sockaddr *) &addr, addr_len);
        if (res != 0)
        {
            // Check if the port is already in use
            if (errno == EADDRINUSE)
            {
                throw std::runtime_error("Port is already in use");
            }
            throw std::runtime_error("Failed to bind socket");
        }

        // Set timeout
        timeval timeout {};
        timeout.tv_sec  = DEFAULT_TIMEOUT / 1000;
        timeout.tv_usec = (DEFAULT_TIMEOUT % 1000) * 1000;
        res             = setsockopt(m_data->socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (res != 0)
        {
            throw std::runtime_error("Failed to set socket timeout");
        }

        // Get the local address
        res = getsockname(m_data->socket, (sockaddr *) &addr, &addr_len);
        if (res != 0)
        {
            throw std::runtime_error("Failed to get socket address");
        }

        m_data->local_addr.addr = ntohl(addr.sin_addr.s_addr);
        m_data->local_addr.port = ntohs(addr.sin_port);
    }

    TCPSocket::~TCPSocket()
    {
        if (m_data != nullptr)
        {
            if (m_data->socket != INVALID_HANDLE_VALUE)
            {
                close(m_data->socket);
            }

            delete m_data;
            m_data = nullptr;
        }
    }

    SocketResultType TCPSocket::accept() const
    {
        // Can't accept if connected
        if (m_data->state == TCPSocketState::CONNECTED)
        {
            throw std::runtime_error("Can't accept connection if already connected");
        }

        // Listen for a connection
        if (m_data->state == TCPSocketState::CLOSED)
        {
            auto res = listen(m_data->socket, 1);
            if (res != 0)
            {
                throw std::runtime_error("Failed to listen for connection");
            }

            m_data->state = TCPSocketState::LISTENING;
        }

        // Wait for a connection
        sockaddr_in addr {};
        addr.sin_family         = AF_INET;
        addr.sin_port           = htons(port());
        addr.sin_addr.s_addr    = htonl(inet_addr());
        socklen_t addr_len      = sizeof(addr);
        auto      client_socket = ::accept(m_data->socket, (sockaddr *) &addr, &addr_len);
        if (client_socket == INVALID_HANDLE_VALUE)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return SocketResultType::TIMEOUT;
            }

            throw std::runtime_error("Failed to accept connection");
        }

        // Set peer address
        m_data->peer_addr.addr = ntohl(addr.sin_addr.s_addr);
        m_data->peer_addr.port = ntohs(addr.sin_port);

        // Close listening socket
        close(m_data->socket);
        m_data->socket = client_socket;

        // Get the local address
        auto res = getsockname(m_data->socket, (sockaddr *) &addr, &addr_len);
        if (res != 0)
        {
            throw std::runtime_error("Failed to get socket address");
        }

        m_data->local_addr.addr = ntohl(addr.sin_addr.s_addr);
        m_data->local_addr.port = ntohs(addr.sin_port);

        // Set timeout on new socket
        timeval timeout {};
        timeout.tv_sec  = DEFAULT_TIMEOUT / 1000;
        timeout.tv_usec = (DEFAULT_TIMEOUT % 1000) * 1000;
        res             = setsockopt(m_data->socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (res != 0)
        {
            throw std::runtime_error("Failed to set socket timeout");
        }

        // Update state
        m_data->state = TCPSocketState::CONNECTED;

        return SocketResultType::OK;
    }

    SocketResultType TCPSocket::connect(const SocketAddr &peer) const
    {
        // Must be in CLOSED state
        if (m_data->state != TCPSocketState::CLOSED)
        {
            throw std::runtime_error("Socket is not in CLOSED state");
        }

        // Connect
        sockaddr_in peer_addr {};
        peer_addr.sin_family      = AF_INET;
        peer_addr.sin_port        = htons(peer.port);
        peer_addr.sin_addr.s_addr = htonl(peer.addr);
        auto res                  = ::connect(m_data->socket, (sockaddr *) &peer_addr, sizeof(peer_addr));
        if (res != 0)
        {
            if (errno == EINPROGRESS)
            {
                return SocketResultType::TIMEOUT;
            }
            return SocketResultType::ERROR;
        }

        // Set peer address
        m_data->peer_addr = peer;

        // Update state
        m_data->state = TCPSocketState::CONNECTED;

        return SocketResultType::OK;
    }

    SocketResult TCPSocket::send(const void *data, size_t size) const
    {
        auto result = ::send(m_data->socket, data, size, 0);

        if (result < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return SocketResult::timeout();
            }
            else
            {
                return SocketResult::error();
            }
        }
        return SocketResult::ok(static_cast<size_t>(result));
    }

    SocketResult TCPSocket::receive(void *data, size_t size) const
    {
        auto result = ::recv(m_data->socket, data, size, 0);

        if (result < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return SocketResult::timeout();
            }
            else
            {
                return SocketResult::error();
            }
        }
        return SocketResult::ok(static_cast<size_t>(result));
    }

    InetAddr TCPSocket::inet_addr() const
    {
        return m_data->local_addr.addr;
    }

    uint16_t TCPSocket::port() const
    {
        return m_data->local_addr.port;
    }

    SocketAddr TCPSocket::socket_addr() const
    {
        return m_data->local_addr;
    }

    InetAddr TCPSocket::peer_inet_addr() const
    {
        return m_data->peer_addr.addr;
    }

    uint16_t TCPSocket::peer_port() const
    {
        return m_data->peer_addr.port;
    }

    SocketAddr TCPSocket::peer_socket_addr() const
    {
        return m_data->peer_addr;
    }

    bool TCPSocket::is_connected() const
    {
        return m_data->state == TCPSocketState::CONNECTED;
    }

    // endregion

} // namespace wvb

#endif