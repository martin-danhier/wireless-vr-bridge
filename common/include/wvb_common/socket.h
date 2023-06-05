#pragma once

#include "macros.h"
#include "socket_addr.h"

#include <cstddef>
#include <wvb_common/benchmark.h>
#include <functional>
#include <optional>

namespace wvb
{
    enum class TCPSocketState : int8_t
    {
        NOT_STARTED = 0,
        LISTENING   = 1,
        CONNECTING  = 2,
        CONNECTED   = 3,
        CLOSED      = 4,
    };

    /** Non-blocking TCP socket. */
    class TCPSocket
    {
        PIMPL_CLASS(TCPSocket);

      public:
        /** Create a new socket bound to the given port. By default, it doesn't do anything. */
        TCPSocket() = default;
        explicit TCPSocket(uint16_t                                 local_port,
                           bool                                     force_port = true,
                           std::shared_ptr<SocketMeasurementBucket> bucket     = nullptr,
                           SocketId                                 socket_id  = SocketId::UNKNOWN_SOCKET);

        // Connection management

        /** Start listening as a server without accepting connections yet.
         * Optional, listen() will enable server if not done already.
         */
        void               enable_server() const;
        [[nodiscard]] bool listen() const;
        [[nodiscard]] bool connect(const SocketAddr &addr) const;
        void               close() const;

        // Transmission
        void               send(const uint8_t *data, size_t size, uint32_t timeout_us = 100000) const;
        [[nodiscard]] bool receive(uint8_t *data, size_t size, size_t *actual_size) const;

        // Getters
        [[nodiscard]] TCPSocketState    refresh_state() const;
        [[nodiscard]] TCPSocketState    state() const;
        [[nodiscard]] inline bool       is_connected() const { return state() == TCPSocketState::CONNECTED; };
        [[nodiscard]] const SocketAddr &local_addr() const;
        [[nodiscard]] const SocketAddr &peer_addr() const;
    };

    /** Non-blocking UDP socket. */
    class UDPSocket
    {
        PIMPL_CLASS(UDPSocket);

      public:
        /** Create a new socket bound to the given port. */
        UDPSocket() = default;
        explicit UDPSocket(uint16_t                                 local_port,
                           bool                                     force_port      = true,
                           bool                                     allow_broadcast = false,
                           std::shared_ptr<SocketMeasurementBucket> bucket          = nullptr,
                           SocketId                                 socket_id       = SocketId::UNKNOWN_SOCKET);

        // Connection
        void close() const;

        // Transmission
        [[nodiscard]] bool send_to(const SocketAddr &addr, const uint8_t *data, size_t size) const;
        [[nodiscard]] bool receive_from(uint8_t *data, size_t size, size_t *actual_size, SocketAddr *addr) const;

        // Getters
        [[nodiscard]] bool              is_open() const;
        [[nodiscard]] const SocketAddr &local_addr() const;
    };

    std::vector<InetAddr> get_broadcast_addresses();

} // namespace wvb