#include "wvb_common/video_socket.h"

#include <wvb_common/formats/simple_packetizer.h>
#include <wvb_common/socket_addr.h>

#include <iostream>
#include <utility>

namespace wvb
{
    ClientVideoSocket::ClientVideoSocket(uint16_t local_port, std::shared_ptr<SocketMeasurementBucket> measurements_bucket)
        :
#ifdef WVB_VIDEO_SOCKET_USE_UDP
          m_socket(local_port, true, false, std::move(measurements_bucket), SocketId::VIDEO_SOCKET)
#else
          m_socket(local_port, true, std::move(measurements_bucket), SocketId::VIDEO_SOCKET)
#endif
    {
    }

    ServerVideoSocket::ServerVideoSocket(uint16_t local_port, std::shared_ptr<SocketMeasurementBucket> measurements_bucket)
        :
#ifdef WVB_VIDEO_SOCKET_USE_UDP
          m_socket(local_port, true, false, std::move(measurements_bucket), SocketId::VIDEO_SOCKET)
#else
          m_socket(local_port, true, std::move(measurements_bucket), SocketId::VIDEO_SOCKET)
#endif
    {
#ifndef WVB_VIDEO_SOCKET_USE_UDP
        m_socket.enable_server();
#endif
    }

    void ServerVideoSocket::send_all_generated_packets(uint32_t timeout_us)
    {
        // Send all generated packets
        const uint8_t *packet      = nullptr;
        size_t         packet_size = 0;
        bool           has_next    = true;
        while (has_next &&
#ifdef WVB_VIDEO_SOCKET_USE_UDP
               m_socket.is_open()
#else
               m_socket.is_connected()
#endif
        )
        {
            has_next = m_packetizer->create_next_packet(&packet, &packet_size);
            if (packet_size == 0)
            {
                continue;
            }

#ifdef WVB_VIDEO_SOCKET_USE_UDP
            m_socket.send_to(m_peer_addr, packet, packet_size);
#else
            m_socket.send(packet, packet_size, timeout_us);
#endif
        }
    }

    void ServerVideoSocket::set_packetizer(std::shared_ptr<IPacketizer> packetizer)
    {
        if (packetizer == nullptr)
        {
#ifdef WVB_VIDEO_SOCKET_USE_UDP
            throw std::runtime_error("Packetizer cannot be null when using UDP");
#else
            // Simple packetizer that simply prepends the size and timestamp to the data
            m_packetizer = wvb::create_simple_packetizer();
#endif
        }
        else
        {
            m_packetizer = std::move(packetizer);
        }

        std::cout << "Server using packetizer: " << m_packetizer->name() << "\n";
    }

    bool ClientVideoSocket::connect(const SocketAddr &peer_addr)
    {
        m_peer_addr = peer_addr;
#ifdef WVB_VIDEO_SOCKET_USE_UDP
        return true;
#else
        return m_socket.connect(peer_addr);
#endif
    }

    void ClientVideoSocket::set_depacketizer(std::shared_ptr<IDepacketizer> depacketizer)
    {
        if (depacketizer == nullptr)
        {
#ifdef WVB_VIDEO_SOCKET_USE_UDP
            throw std::runtime_error("Depacketizer cannot be null when using UDP");
#else
            // Simple packetizer that simply prepends the size and timestamp to the data
            m_depacketizer = wvb::create_simple_depacketizer();
#endif
        }
        else
        {
            m_depacketizer = std::move(depacketizer);
        }

        std::cout << "Client using depacketizer: " << m_depacketizer->name() << "\n";
    }

    bool ServerVideoSocket::listen(const SocketAddr &peer_addr)
    {
#ifdef WVB_VIDEO_SOCKET_USE_UDP
        m_peer_addr = peer_addr;
        return true;
#else
        if (m_socket.listen())
        {
            // Check if the peer is the one we desire
            const auto connected_peer = m_socket.peer_addr();
            if (peer_addr != connected_peer)
            {
                // Reset socket
                m_socket = Socket(m_socket.local_addr().port);
                m_socket.enable_server();
                return false;
            }
            m_peer_addr = peer_addr;
            return true;
        }
        return false;
#endif
    }

    void ServerVideoSocket::send_packet(const uint8_t *data,
                                        size_t         size,
                                        uint32_t       frame_index,
                                        bool           end_of_stream,
                                        uint32_t       rtp_timestamp,
                                        uint32_t       rtp_pose_timestamp,
                                        bool           save_frame,
                                        bool           last,
                                        uint32_t       timeout_us)
    {
        m_packetizer->add_frame_data(data, size, frame_index, end_of_stream, rtp_timestamp, rtp_pose_timestamp, save_frame, last);

        send_all_generated_packets(timeout_us);
    }

    void ClientVideoSocket::update()
    {
        uint8_t buffer[1500];
        size_t  size = 0;
#ifdef WVB_VIDEO_SOCKET_USE_UDP
        SocketAddr sender_addr;
#endif

        while (
#ifdef WVB_VIDEO_SOCKET_USE_UDP
            m_socket.is_open() && m_socket.receive_from(buffer, sizeof(buffer), &size, &sender_addr)
#else
            m_socket.is_connected() && m_socket.receive(buffer, sizeof(buffer), &size)
#endif
        )
        {
#ifdef WVB_VIDEO_SOCKET_USE_UDP
            if (sender_addr != m_peer_addr)
            {
                continue;
            }
#endif

            m_depacketizer->add_packet(buffer, size);
        }
    }

    bool ClientVideoSocket::receive_packet(const uint8_t **__restrict out_data,
                                           size_t *__restrict out_size,
                                           uint32_t *__restrict out_frame_id,
                                           bool *__restrict out_end_of_stream,
                                           uint32_t *__restrict out_rtp_timestamp,
                                           uint32_t *__restrict out_rtp_pose_timestamp,
                                           std::chrono::steady_clock::time_point *out_last_packet_received_timestamp,
                                           bool *__restrict out_save_frame)
    {
        if (m_depacketizer == nullptr)
        {
            return false;
        }

        // Do we have a frame ?
        return m_depacketizer->receive_frame_data(out_data,
                                                  out_size,
                                                  out_frame_id,
                                                  out_end_of_stream,
                                                  out_rtp_timestamp,
                                                  out_rtp_pose_timestamp,
                                                  out_last_packet_received_timestamp,
                                                  out_save_frame);
    }

    void ClientVideoSocket::release_frame_data()
    {
        m_depacketizer->release_frame_data();
    }

    void ClientVideoSocket::flush()
    {
        uint8_t buffer[1500];
        size_t  size = 0;
#ifdef WVB_VIDEO_SOCKET_USE_UDP
        SocketAddr sender_addr;
#endif

        while (
#ifdef WVB_VIDEO_SOCKET_USE_UDP
            m_socket.is_open() && m_socket.receive_from(buffer, sizeof(buffer), &size, &sender_addr)
#else
            m_socket.is_connected() && m_socket.receive(buffer, sizeof(buffer), &size)
#endif
        )
        {
        }
    }

} // namespace wvb
