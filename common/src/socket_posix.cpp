// Implementation is platform-dependant since we need OS-specific functions to create UDP sockets.

#ifdef __linux__
#include "wvb_common/socket.h"

#include <netinet/in.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace wvb
{

    // =======================================================================================
    // =                                 Structs and classes                                 =
    // =======================================================================================

    typedef int32_t SOCKET;
#define INVALID_HANDLE_VALUE (-1)

    struct UDPSocket::Data
    {
        SOCKET socket = INVALID_HANDLE_VALUE;
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
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        auto res         = bind(m_data->socket, (sockaddr *) &addr, sizeof(addr));
        if (res != 0)
        {
            throw std::runtime_error("Failed to bind socket");
        }
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

    size_t UDPSocket::send(const void *data, size_t size, const SocketAddr &dest) const
    {
        sockaddr_in dest_addr {};
        dest_addr.sin_family      = AF_INET;
        dest_addr.sin_port        = htons(dest.port);
        dest_addr.sin_addr.s_addr = htonl(dest.addr);

        auto res = sendto(m_data->socket, data, size, 0, (sockaddr *) &dest_addr, sizeof(dest_addr));

        if (res < 0)
        {
            throw std::runtime_error("Failed to send data (" + std::to_string(errno) + ")");
        }

        return static_cast<size_t>(res);
    }

    size_t UDPSocket::receive(void *data, size_t size, const SocketAddr &src) const
    {
        ssize_t result = 0;

        if (src.is_any())
        {
            result = recvfrom(m_data->socket, data, size, 0, nullptr, nullptr);
        }
        else {
            sockaddr_in sender {};
            sender.sin_family      = AF_INET;
            sender.sin_port        = htons(src.port);
            sender.sin_addr.s_addr = htonl(src.addr);

            socklen_t len = sizeof(sender);
            result = recvfrom(m_data->socket, data, size, 0, (sockaddr *) &sender, &len);
        }

        if (result < 0)
        {
            throw std::runtime_error("Failed to receive data");
        }

        return static_cast<size_t>(result);
    }

} // namespace wvb

#endif