#pragma once

#include "macros.h"
#include "socket_addr.h"
#include "vrcp.h"
#include <wvb_common/vr_structs.h>

#include <cstdint>
#include <vector>

namespace wvb
{

    enum class VRCPSocketState : int8_t
    {
        AWAITING_CONNECTION = 0,
        NEGOTIATING         = 1,
        CONNECTED           = 2,
        CLOSED              = 3,
    };

    std::string to_string(const vrcp::VRCPRejectReason &reason, uint8_t err_data);
    std::string to_string(const vrcp::VRCPFieldType &ftype);
    std::string to_string(const vrcp::VRCPVideoMode &video_mode);

    struct VRCPClientParams
    {
        uint16_t                 video_port;
        VRSystemSpecs            specs;
        std::vector<std::string> supported_video_codecs;
        uint64_t                 ntp_timestamp;
    };

    struct VRCPServerParams
    {
        uint16_t                 video_port;
        std::vector<std::string> supported_video_codecs;
    };

    struct VRCPConnectResp
    {
        uint16_t    peer_video_port;
        std::string chosen_video_codec;
        uint64_t    ntp_timestamp;
    };

    /**
     * Wrapper around TCP and UDP sockets for the custom Virtual Reality Control Protocol.
     *
     * Similarly to the underlying sockets, it is non-blocking.
     *
     * Lifetime:
     * 1. A server socket will repeatedly advertise itself over a well-known UDP port, 7672 by default.
     *    This will allow clients to discover the server.
     * 2. To start a session, a client will establish a TCP session to the server using the port specified in the advertisement.
     *    It also sends its device specification to the server.
     * 3. The server can then either accept or reject the connection. If it accepts, it also sends additional parameters for the
     * session.
     * 4. The client and server can then communicate either via UDP or TCP.
     * 5. The session ends when the TCP socket is closed.
     *
     * The socket also includes a buffer for TCP messages. This way, the "receive" function returns
     * one full message at a time, similarly to what we would expect from a UDP socket.
     */
    class VRCPSocket
    {
        PIMPL_CLASS(VRCPSocket);

      private:
        explicit VRCPSocket(Data *data) : m_data(data) {};
        [[nodiscard]] bool next_tcp_packet(const vrcp::VRCPBaseHeader **dest_packet, size_t *dest_size) const;
        [[nodiscard]] bool next_udp_packet(const vrcp::VRCPBaseHeader **dest_packet, size_t *dest_size) const;

        [[nodiscard]] bool listen_for_tcp_connection(const std::vector<InetAddr> &bcast_addrs) const;
        /** Next step: listen for VRCP session. Returns true when the socket is connected. */
        [[nodiscard]] bool
            listen_for_conn_req(const VRCPServerParams &server_params, VRCPClientParams *client_params, VRCPConnectResp *resp) const;
        /** Used by the client to wait for a CONN_RESP.
         * Returns true when the socket is connected. */
        [[nodiscard]] bool listen_for_conn_resp(const VRCPClientParams &params, VRCPConnectResp *resp) const;

      public:
        // Constructor
        VRCPSocket() = default;

        /**
         * Creates a basic socket as a server. It doesn't do anything yet.
         * @param tcp_port TCP Port that will be used for session establishment and reliable transfer
         * @param advert_udp_port UDP port used for server advertisements. By default, the 7672 port is used.
         */
        static VRCPSocket create_server(uint8_t                                  advertisement_interval_sec = 3,
                                        uint16_t                                 tcp_port                   = PORT_AUTO,
                                        uint16_t                                 udp_vrcp_port              = PORT_AUTO,
                                        uint16_t                                 local_advert_port          = PORT_AUTO,
                                        uint16_t                                 udp_advert_port     = VRCP_DEFAULT_ADVERTISEMENT_PORT,
                                        std::shared_ptr<SocketMeasurementBucket> measurements_bucket = nullptr);
        static VRCPSocket create_client(uint16_t tcp_port        = PORT_AUTO,
                                        uint16_t udp_vrcp_port   = PORT_AUTO,
                                        uint16_t advert_udp_port = VRCP_DEFAULT_ADVERTISEMENT_PORT,
                                        std::shared_ptr<SocketMeasurementBucket> measurements_bucket = nullptr);

        // Connection management - server

        /** Listen for TCP connection. Returns true when the socket is connected. */
        [[nodiscard]] bool listen(const std::vector<InetAddr> &bcast_addrs,
                                  const VRCPServerParams      &server_params,
                                  VRCPClientParams            *client_params,
                                  VRCPConnectResp             *resp) const;

        // Connection management - client

        /** Returns the list of servers that sent valid advertisements. */
        [[nodiscard]] const std::vector<VRCPServerCandidate> &available_servers() const;

        /** Connect to TCP socket and send CONN_REQ */
        [[nodiscard]] bool connect(const SocketAddr &addr, const VRCPClientParams &params, VRCPConnectResp *resp) const;

        // Connection management - both
        void close() const;

        /**
         * Close session, reset TCP socket and listen again for advertisements.
         */
        void reset_client() const;

        /**
         * Close session, reset TCP socket and start sending advertisements and listening for connections.
         */
        void reset_server() const;

        // Transmission

        /** Receive a VRCP message from TCP socket. Receive a pointer into the inner buffer of the socket.
         *
         * Similarly to a UDP socket, this function splits all the messages into individual packets.
         * It should thus be called repeatedly until it returns false.
         *
         * Calling this function again will invalidate the previous pointer and assumes
         * that the processing of the previous m_packet is complete, or that it was copied.
         * */
        [[nodiscard]] bool reliable_receive(const vrcp::VRCPBaseHeader **dest_packet, size_t *dest_size) const;
        /** Receive VRCP message from UDP socket */
        [[nodiscard]] bool unreliable_receive(const vrcp::VRCPBaseHeader **dest_packet, size_t *dest_size) const;
        /** Send VRCP message via TCP socket */
        void reliable_send(vrcp::VRCPBaseHeader *packet, size_t size, uint32_t timeout_us = 100000) const;
        /** Send VRCP message via UDP socket */
        bool unreliable_send(vrcp::VRCPBaseHeader *packet, size_t size) const;

        // Getters

        /** Returns true if the socket is connected and a VRCP session is established. */
        [[nodiscard]] bool is_connected_refresh() const;
        [[nodiscard]] bool is_connected() const;

        [[nodiscard]] InetAddr peer_inet_addr() const;
    };
} // namespace wvb