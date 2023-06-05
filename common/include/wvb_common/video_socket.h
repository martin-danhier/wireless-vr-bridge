#pragma once

#include <wvb_common/packetizer.h>
#include <wvb_common/socket.h>

#include <memory>

namespace wvb
{

    /** Simple abstraction that allows to quickly switch between video over TCP vs UDP to compare their performance. */
    class ClientVideoSocket
    {
      private:
#ifdef WVB_VIDEO_SOCKET_USE_UDP
        using Socket = UDPSocket;
#else
        using Socket = TCPSocket;
#endif

        Socket                         m_socket;
        SocketAddr                     m_peer_addr;
        std::shared_ptr<IDepacketizer> m_depacketizer = nullptr;

      public:
        ClientVideoSocket() = default;
        explicit ClientVideoSocket(uint16_t local_port, std::shared_ptr<SocketMeasurementBucket> measurements_bucket = nullptr);

        // Connection setup

        /**
         * Setup the socket as a client and connects to the peer.
         * If the socket is in UDP mode, this function will simply save the peer address.
         */
        bool connect(const SocketAddr &peer_addr);

        void set_depacketizer(std::shared_ptr<IDepacketizer> depacketizer);

        /** Empty the socket's buffer. */
        void flush();

        // Data transfer

        /**
         * Checks if a "packet" of data was received. It will be depacketized if RTP is enabled.
         * Returns false if no packet was received.
         */
        bool receive_packet(const uint8_t **__restrict out_data,
                            size_t *__restrict out_size,
                            uint32_t *__restrict out_frame_id,
                            bool *__restrict out_end_of_stream,
                            uint32_t *__restrict out_rtp_timestamp,
                            uint32_t *__restrict out_rtp_pose_timestamp,
                            std::chrono::steady_clock::time_point *out_last_packet_received_timestamp,
                            bool *__restrict out_save_frame);

        void release_frame_data();

        /**
         * Process packets in the socket and process depacketization, but do not consume the data.
         */
        void update();

        [[nodiscard]] inline const SocketAddr &local_addr() const { return m_socket.local_addr(); }
        [[nodiscard]] inline const SocketAddr &peer_addr() const { return m_peer_addr; }
        [[nodiscard]] inline bool              is_connected() const
        {
#ifdef WVB_VIDEO_SOCKET_USE_UDP
            return m_socket.is_valid();
#else
            return m_socket.is_connected();
#endif
        }
    };

    class ServerVideoSocket
    {
      private:
#ifdef WVB_VIDEO_SOCKET_USE_UDP
        using Socket = UDPSocket;
#else
        using Socket = TCPSocket;
#endif

        Socket                       m_socket;
        SocketAddr                   m_peer_addr;
        std::shared_ptr<IPacketizer> m_packetizer = nullptr;

        void send_all_generated_packets(uint32_t timeout_us);

      public:
        ServerVideoSocket() = default;
        explicit ServerVideoSocket(uint16_t local_port, std::shared_ptr<SocketMeasurementBucket> measurements_bucket = nullptr);

        void set_packetizer(std::shared_ptr<IPacketizer> packetizer);

        // Connection setup

        /**
         * Checks if the client sent a SYN message, and if so, connects to it. Only accepts the given client for TCP.
         * With UDP, only save the peer address.
         */
        bool listen(const SocketAddr &peer_addr);

        // Data transfer

        /**
         * Send a "packet" of data. The data is raw encoded frame data.
         * It will be packetized in RTP or not depending on the settings.
         */
        void send_packet(const uint8_t *data,
                         size_t         size,
                         uint32_t       frame_id,
                         bool           end_of_stream,
                         uint32_t       rtp_timestamp,
                         uint32_t       rtp_pose_timestamp,
                         bool           save_frame = false,
                         bool           last       = true,
                         uint32_t       timeout_us = 100000);

        [[nodiscard]] inline const SocketAddr &local_addr() const { return m_socket.local_addr(); }
        [[nodiscard]] inline const SocketAddr &peer_addr() const { return m_peer_addr; }
        [[nodiscard]] inline bool              is_connected() const
        {
#ifdef WVB_VIDEO_SOCKET_USE_UDP
            return m_socket.is_valid();
#else
            return m_socket.is_connected();
#endif
        }
    };

} // namespace wvb