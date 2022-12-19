#ifdef _WIN32
#include "wvb_common/socket.h"

#include <stdexcept>
#include <winsock2.h>
#undef ERROR

namespace wvb
{
    // region UDPSocket

    // =======================================================================================
    // =                                 Structs and classes                                 =
    // =======================================================================================

    struct UDPSocket::Data
    {
        SOCKET     socket     = INVALID_SOCKET;
        SocketAddr local_addr = {};
    };

    // This class handles the initialization and cleanup of the Winsock library
    class SocketInitializer
    {
        bool m_initialized = false;

      public:
        void init()
        {
            if (m_initialized)
                return;

            WSADATA wsa_data;
            if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
            {
                throw std::runtime_error("Failed to initialize WinSock");
            }

            m_initialized = true;
        }

        ~SocketInitializer()
        {
            if (m_initialized)
            {
                WSACleanup();
                m_initialized = false;
            }
        }
    };

    static SocketInitializer g_socket_initializer;

    // =======================================================================================
    // =                                   Implementation                                    =
    // =======================================================================================

    UDPSocket::UDPSocket(uint16_t port) : m_data(new Data)
    {
        g_socket_initializer.init();

        // Create socket
        m_data->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_data->socket == INVALID_SOCKET)
        {
            throw std::runtime_error("Failed to create socket (" + std::to_string(WSAGetLastError()) + ")");
        }

        // Bind socket
        sockaddr_in addr     = {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INET_ADDR_LOOPBACK);
        addr.sin_port        = htons(port);
        auto res             = bind(m_data->socket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        if (res != 0)
        {
            // Check if port is already in use
            if (WSAGetLastError() == WSAEADDRINUSE)
            {
                throw std::runtime_error("Port is already in use");
            }
            throw std::runtime_error("Failed to bind socket");
        }

        // Set timeout
        timeval timeout = {};
        timeout.tv_sec  = DEFAULT_TIMEOUT / 1000;
        timeout.tv_usec = (DEFAULT_TIMEOUT % 1000) * 1000;
        res = setsockopt(m_data->socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
        if (res != 0)
        {
            throw std::runtime_error("Failed to set socket timeout");
        }

        // Get the actual port
        int addr_len = sizeof(addr);
        res          = getsockname(m_data->socket, reinterpret_cast<sockaddr *>(&addr), &addr_len);
        if (res != 0)
        {
            throw std::runtime_error("Failed to get socket address");
        }

        // Save the address
        m_data->local_addr.addr = ntohl(addr.sin_addr.s_addr);
        m_data->local_addr.port = ntohs(addr.sin_port);
    }

    UDPSocket::~UDPSocket()
    {
        if (m_data != nullptr)
        {
            if (m_data->socket != INVALID_SOCKET)
            {
                closesocket(m_data->socket);
            }

            delete m_data;
            m_data = nullptr;
        }
    }

    SocketResult UDPSocket::send(const void *data, size_t size, const SocketAddr &dest) const
    {
        // Create destination address
        sockaddr_in addr     = {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(dest.addr);
        addr.sin_port        = htons(dest.port);

        // Send data
        auto res = sendto(m_data->socket,
                          reinterpret_cast<const char *>(data),
                          static_cast<int>(size),
                          0,
                          reinterpret_cast<sockaddr *>(&addr),
                          sizeof(addr));
        if (res < 0)
        {
            // Check if timeout
            if (WSAGetLastError() == WSAETIMEDOUT)
            {
                return SocketResult::timeout();
            }
            return SocketResult::error();
        }

        return SocketResult::ok(static_cast<size_t>(res));
    }

    SocketResult UDPSocket::receive(void *data, size_t size, const SocketAddr &src) const
    {
        int result = 0;

        if (src.is_any())
        {
            // Receive data
            result = recvfrom(m_data->socket, reinterpret_cast<char *>(data), static_cast<int>(size), 0, nullptr, nullptr);
        }
        else
        {
            // Create source address
            sockaddr_in addr     = {};
            addr.sin_family      = AF_INET;
            addr.sin_addr.s_addr = htonl(src.addr);
            addr.sin_port        = htons(src.port);

            // Receive data
            int addr_len = sizeof(addr);
            result       = recvfrom(m_data->socket,
                              reinterpret_cast<char *>(data),
                              static_cast<int>(size),
                              0,
                              reinterpret_cast<sockaddr *>(&addr),
                              &addr_len);
        }

        if (result < 0)
        {
            // Check if timeout
            if (WSAGetLastError() == WSAETIMEDOUT)
            {
                return SocketResult::timeout();
            }
            return SocketResult::error();
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
        SOCKET         socket     = INVALID_SOCKET;
        TCPSocketState state      = TCPSocketState::CLOSED;
        SocketAddr     local_addr = {};
        SocketAddr     peer_addr  = {};
    };

    // =======================================================================================
    // =                                   Implementation                                    =
    // =======================================================================================

    TCPSocket::TCPSocket(uint16_t port, bool force_port) : m_data(new Data)
    {
        g_socket_initializer.init();

        // Create socket
        m_data->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (m_data->socket == INVALID_SOCKET)
        {
            throw std::runtime_error("Failed to create socket (" + std::to_string(WSAGetLastError()) + ")");
        }

        // Force reuse of the port
        if (force_port)
        {
            int optval = 1;
            auto res = setsockopt(m_data->socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&optval), sizeof(optval));
            if (res != 0)
            {
                throw std::runtime_error("Failed to set socket option");
            }
        }

        // Bind socket
        sockaddr_in addr     = {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INET_ADDR_LOOPBACK);
        addr.sin_port        = htons(port);
        auto res = bind(m_data->socket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        if (res != 0)
        {
            // Check if port is already in use
            if (WSAGetLastError() == WSAEADDRINUSE)
            {
                throw std::runtime_error("Port already in use");
            }

            throw std::runtime_error("Failed to bind socket");
        }

        // Set timeout
        timeval timeout = {};
        timeout.tv_sec  = DEFAULT_TIMEOUT / 1000;
        timeout.tv_usec = (DEFAULT_TIMEOUT % 1000) * 1000;
        res = setsockopt(m_data->socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
        if (res != 0)
        {
            throw std::runtime_error("Failed to set socket timeout");
        }

        // Get local address
        int addr_len = sizeof(addr);
        res = getsockname(m_data->socket, reinterpret_cast<sockaddr *>(&addr), &addr_len);
        if (res != 0)
        {
            throw std::runtime_error("Failed to get local address");
        }
        m_data->local_addr.addr = ntohl(addr.sin_addr.s_addr);
        m_data->local_addr.port = ntohs(addr.sin_port);
    }

    TCPSocket::~TCPSocket()
    {
        if (m_data != nullptr)
        {
            if (m_data->socket != INVALID_SOCKET)
            {
                closesocket(m_data->socket);
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
        sockaddr_in addr     = {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(inet_addr());
        addr.sin_port        = htons(port());
        int addr_len = sizeof(addr);
        auto client_socket = ::accept(m_data->socket, reinterpret_cast<sockaddr *>(&addr), &addr_len);
        if (client_socket == INVALID_SOCKET)
        {
            // Check if timeout
            if (WSAGetLastError() == WSAETIMEDOUT)
            {
                return SocketResultType::TIMEOUT;
            }
            throw std::runtime_error("Failed to accept connection");
        }

        // Set peer address
        m_data->peer_addr.addr = ntohl(addr.sin_addr.s_addr);
        m_data->peer_addr.port = ntohs(addr.sin_port);

        // Close listening socket
        closesocket(m_data->socket);
        m_data->socket = client_socket;

        // Get local address
        addr_len = sizeof(addr);
        auto res = getsockname(m_data->socket, reinterpret_cast<sockaddr *>(&addr), &addr_len);
        if (res != 0)
        {
            throw std::runtime_error("Failed to get local address");
        }
        m_data->local_addr.addr = ntohl(addr.sin_addr.s_addr);
        m_data->local_addr.port = ntohs(addr.sin_port);

        // Set timeout
        timeval timeout = {};
        timeout.tv_sec  = DEFAULT_TIMEOUT / 1000;
        timeout.tv_usec = (DEFAULT_TIMEOUT % 1000) * 1000;
        res = setsockopt(m_data->socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout), sizeof(timeout));
        if (res != 0)
        {
            throw std::runtime_error("Failed to set socket timeout");
        }

        // Set connected state
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

        // Connect to peer
        sockaddr_in addr     = {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(peer.addr);
        addr.sin_port        = htons(peer.port);
        auto res = ::connect(m_data->socket, reinterpret_cast<sockaddr *>(&addr), sizeof(addr));
        if (res != 0)
        {
            // Check if timeout
            if (WSAGetLastError() == WSAETIMEDOUT)
            {
                return SocketResultType::TIMEOUT;
            }
            throw std::runtime_error("Failed to connect to peer");
        }

        // Set peer address
        m_data->peer_addr = peer;

        // Update state
        m_data->state = TCPSocketState::CONNECTED;

        return SocketResultType::OK;
    }

    SocketResult TCPSocket::send(const void *data, size_t size) const
    {
        auto result = ::send(m_data->socket, static_cast<const char *>(data), size, 0);

        if (result < 0)
        {
            if (WSAGetLastError() == WSAETIMEDOUT)
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
        auto result = ::recv(m_data->socket, static_cast<char *>(data), size, 0);

        if (result < 0)
        {
            if (WSAGetLastError() == WSAETIMEDOUT)
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