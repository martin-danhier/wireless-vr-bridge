#include "wvb_common/vrcp_socket.h"

#include <wvb_common/network_utils.h>
#include <wvb_common/rtp_clock.h>
#include <wvb_common/socket.h>

#ifdef __linux__
#include <cstring>
#endif

namespace wvb
{

    // =============================================================
    // =                          Structs                          =
    // =============================================================

    // Margin added to advertisement intervals to check if they expire
    // FIXME: the headset can greatly drift in time, and sometimes its system clock can be 10 minutes
    // in the future.
#define ADVERTISEMENT_TIMEOUT_MARGIN_SEC 10000
    // The size of the reception buffer must be large enough to accommodate the largest allowed packet
#define DEFAULT_RECEPTION_BUFFER_SIZE (UINT8_MAX * VRCP_ROW_SIZE * 4)

    struct VRCPSocket::Data
    {
        VRCPSocketState state = VRCPSocketState::AWAITING_CONNECTION;

        // Sockets
        TCPSocket tcp_socket;
        UDPSocket udp_socket {};
        UDPSocket udp_broadcast_socket;

        // Config
        bool     is_server                  = false;
        uint8_t  advertisement_interval_sec = 2;
        uint16_t local_advert_port          = PORT_AUTO;
        uint16_t udp_advert_port            = VRCP_DEFAULT_ADVERTISEMENT_PORT;
        uint16_t udp_vrcp_port              = PORT_AUTO;

        // Peer info
        SocketAddr peer_udp_addr {};

        // Other state keeping
        uint32_t                         last_advertisement_time = 0;
        std::vector<VRCPServerCandidate> server_candidates;

        // Benchmarking
        std::shared_ptr<SocketMeasurementBucket> measurements_bucket = nullptr;

        // TCP reception buffer to reconstruct and split packets
        // Idea:
        // - Only call "receive" when there is only an incomplete packet or no m_packet in the buffer. If there is a full m_packet,
        // simply
        //   consume and return it.
        // - Call the "receive" using the tail index as the offset. This will write at the end of the buffer.
        // - Read from the head.
        // - If we find an incomplete packet, move everything back to the beginning of the buffer to avoid fragmentation.

        uint8_t  tcp_reception_buffer[DEFAULT_RECEPTION_BUFFER_SIZE] = {0};
        uint16_t tcp_head                                            = 0;
        uint16_t tcp_tail                                            = 0;
        uint8_t  udp_reception_buffer[DEFAULT_RECEPTION_BUFFER_SIZE] = {0};
        uint16_t udp_head                                            = 0;
        uint16_t udp_tail                                            = 0;
    };

    // =============================================================
    // =                   Helper implementation                   =
    // =============================================================

    std::string to_string(const vrcp::VRCPRejectReason &reason, uint8_t err_data)
    {
        if (reason == vrcp::VRCPRejectReason::GENERIC_ERROR)
        {
            return "Generic error. Error data: " + std::to_string(err_data);
        }
        else if (reason == vrcp::VRCPRejectReason::VERSION_MISMATCH)
        {
            return "Version mismatch. Expected version: " + std::to_string(err_data);
        }
        else if (reason == vrcp::VRCPRejectReason::INVALID_VRCP_PORT)
        {
            return "Invalid VRCP port";
        }
        else if (reason == vrcp::VRCPRejectReason::INVALID_VIDEO_PORT)
        {
            return "Invalid video port";
        }
        else if (reason == vrcp::VRCPRejectReason::INVALID_EYE_SIZE)
        {
            return "Invalid eye size";
        }
        else if (reason == vrcp::VRCPRejectReason::INVALID_REFRESH_RATE)
        {
            return "Invalid refresh rate";
        }
        else if (reason == vrcp::VRCPRejectReason::INVALID_MANUFACTURER_NAME)
        {
            return "Invalid manufacturer name";
        }
        else if (reason == vrcp::VRCPRejectReason::INVALID_SYSTEM_NAME)
        {
            return "Invalid system name";
        }
        else if (reason == vrcp::VRCPRejectReason::INVALID_VIDEO_CODECS)
        {
            return "Invalid video codecs";
        }
        else if (reason == vrcp::VRCPRejectReason::NO_SUPPORTED_VIDEO_CODEC)
        {
            return "No supported video codec";
        }
        else if (reason == vrcp::VRCPRejectReason::VIDEO_MODE_MISMATCH)
        {
            return "Video mode mismatch. Expected mode: " + to_string(err_data);
        }
        else
        {
            return "Unknown reason. Reason code: " + std::to_string(static_cast<uint8_t>(reason))
                   + ". Error data: " + std::to_string(err_data);
        }
    }

    std::string to_string(const vrcp::VRCPFieldType &ftype)
    {
        if (ftype == vrcp::VRCPFieldType::CONN_REQ)
        {
            return "CONN_REQ";
        }
        else if (ftype == vrcp::VRCPFieldType::CONN_ACCEPT)
        {
            return "CONN_ACCEPT";
        }
        else if (ftype == vrcp::VRCPFieldType::CONN_REJECT)
        {
            return "CONN_REJECT";
        }
        else if (ftype == vrcp::VRCPFieldType::INPUT_DATA)
        {
            return "INPUT_DATA";
        }
        else if (ftype == vrcp::VRCPFieldType::MANUFACTURER_NAME_TLV)
        {
            return "MANUFACTURER_NAME_TLV";
        }
        else if (ftype == vrcp::VRCPFieldType::SYSTEM_NAME_TLV)
        {
            return "SYSTEM_NAME_TLV";
        }
        else if (ftype == vrcp::VRCPFieldType::SUPPORTED_VIDEO_CODECS_TLV)
        {
            return "SUPPORTED_VIDEO_CODECS_TLV";
        }
        else if (ftype == vrcp::VRCPFieldType::CHOSEN_VIDEO_CODEC_TLV)
        {
            return "CHOSEN_VIDEO_CODEC_TLV";
        }
        else if (ftype == vrcp::VRCPFieldType::SERVER_ADVERTISEMENT)
        {
            return "SERVER_ADVERTISEMENT";
        }
        else if (ftype == vrcp::VRCPFieldType::USER_DATA)
        {
            return "USER_DATA";
        }
        else if (ftype == vrcp::VRCPFieldType::PING)
        {
            return "PING";
        }
        else if (ftype == vrcp::VRCPFieldType::PING_REPLY)
        {
            return "PING_REPLY";
        }
        else if (ftype == vrcp::VRCPFieldType::SYNC_FINISHED)
        {
            return "SYNC_FINISHED";
        }
        else
        {
            return "INVALID";
        }
    }

    std::string to_string(const vrcp::VRCPVideoMode &video_mode)
    {
        if (video_mode == vrcp::VRCPVideoMode::UDP)
        {
            return "UDP";
        }
        else if (video_mode == vrcp::VRCPVideoMode::TCP)
        {
            return "TCP";
        }
        else
        {
            return "INVALID";
        }
    }
    // =============================================================
    // =                VRCP Socket implementation                 =
    // =============================================================

    // Constructor

    VRCPSocket VRCPSocket::create_server(uint8_t                                  advertisement_interval_sec,
                                         uint16_t                                 tcp_port,
                                         uint16_t                                 udp_vrcp_port,
                                         uint16_t                                 local_advert_port,
                                         uint16_t                                 udp_advert_port,
                                         std::shared_ptr<SocketMeasurementBucket> measurements_bucket)
    {
        return VRCPSocket {new Data {
            .tcp_socket           = TCPSocket {tcp_port, true, measurements_bucket, SocketId::VRCP_TCP_SOCKET},
            .udp_broadcast_socket = UDPSocket(local_advert_port,
                                              true,
                                              true,
                                              measurements_bucket,
                                              SocketId::VRCP_BCAST_SOCKET), // Servers don't need to listen on the advert port; so
                                                                            // choose another one so that we can run local tests
            .is_server                  = true,
            .advertisement_interval_sec = advertisement_interval_sec,
            .local_advert_port          = local_advert_port,
            .udp_advert_port            = udp_advert_port,
            .udp_vrcp_port              = udp_vrcp_port,
            .measurements_bucket        = std::move(measurements_bucket),
        }};
    }

    VRCPSocket VRCPSocket::create_client(uint16_t                                 tcp_port,
                                         uint16_t                                 udp_vrcp_port,
                                         uint16_t                                 udp_advert_port,
                                         std::shared_ptr<SocketMeasurementBucket> measurements_bucket)
    {
        return VRCPSocket {new Data {
            .tcp_socket                 = TCPSocket {tcp_port, true, measurements_bucket, SocketId::VRCP_TCP_SOCKET},
            .udp_broadcast_socket       = UDPSocket(udp_advert_port,
                                              true,
                                              true,
                                              measurements_bucket,
                                              SocketId::VRCP_BCAST_SOCKET), // Clients listen on the UDP port for advertisements
            .is_server                  = false,
            .advertisement_interval_sec = 0,                                      // No advertisement interval for clients
            .udp_advert_port            = udp_advert_port,
            .udp_vrcp_port              = udp_vrcp_port,
            .measurements_bucket        = std::move(measurements_bucket),
        }};
    }

    VRCPSocket::~VRCPSocket()
    {
        if (m_data != nullptr)
        {
            delete m_data;
            m_data = nullptr;
        }
    }

    // Connection management

    bool VRCPSocket::listen_for_tcp_connection(const std::vector<InetAddr> &bcast_addrs) const
    {
        // Listen for TCP connection.
        const bool connected = m_data->tcp_socket.listen();
        if (connected)
        {
            // We have a connection, but we need to wait the client CONN_REQ.
            m_data->state = VRCPSocketState::NEGOTIATING;

            return true;
        }

        // Not connected: maybe we should send an advertisement

        // Get unix timestamp in seconds
        uint32_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        if (now - m_data->last_advertisement_time >= m_data->advertisement_interval_sec)
        {
            m_data->last_advertisement_time = now;

            // Send advertisement
            const vrcp::VRCPServerAdvertisement packet {
                .tcp_port  = htons(m_data->tcp_socket.local_addr().port),
                .interval  = m_data->advertisement_interval_sec,
                .timestamp = htonl(now),
            };

            for (const InetAddr &bcast_addr : bcast_addrs)
            {
                const SocketAddr addr {bcast_addr, m_data->udp_advert_port};

                m_data->udp_broadcast_socket.send_to(addr, (uint8_t *) &packet, sizeof(packet));
            }
        }
        return false;
    }

    bool VRCPSocket::listen_for_conn_req(const VRCPServerParams &server_params,
                                         VRCPClientParams       *client_params,
                                         VRCPConnectResp        *resp) const
    {
        const vrcp::VRCPBaseHeader *header = nullptr;
        size_t                      size   = 0;
        // While there are packets to read in the buffer
        while (reliable_receive(&header, &size))
        {
            // Check if it is a VRCP connection request.
            // For simplicity, all VRCP packets are discarded until a valid connection is set up.
            if (header->ftype != vrcp::VRCPFieldType::CONN_REQ)
            {
                continue;
            }

            // We have a connection request
            const auto *conn_req = (const vrcp::VRCPConnectionRequest *) header;

            // Check if the client is compatible with our server
            vrcp::VRCPRejectReason reason   = vrcp::VRCPRejectReason::NONE;
            uint8_t                err_data = 0;

#ifdef WVB_VIDEO_SOCKET_USE_UDP
            constexpr vrcp::VRCPVideoMode video_mode = vrcp::VRCPVideoMode::UDP;
#else
            constexpr vrcp::VRCPVideoMode video_mode = vrcp::VRCPVideoMode::TCP;
#endif

            if (conn_req->version != VRCP_VERSION)
            {
                // Only accept the exact same version
                reason   = vrcp::VRCPRejectReason::VERSION_MISMATCH;
                err_data = VRCP_VERSION; // Expected version
            }
            else if (conn_req->video_mode != vrcp::to_u8(video_mode))
            {
                // Only accept the exact same video mode
                reason   = vrcp::VRCPRejectReason::VIDEO_MODE_MISMATCH;
                err_data = vrcp::to_u8(video_mode); // Expected video mode
            }
            // Ports must be real, no AUTO port since they are supposed to be running sockets
            else if (ntohs(conn_req->udp_vrcp_port) == 0)
            {
                reason = vrcp::VRCPRejectReason::INVALID_VRCP_PORT;
            }
            else if (ntohs(conn_req->video_port) == 0)
            {
                reason = vrcp::VRCPRejectReason::INVALID_VIDEO_PORT;
            }
            // Device specs must be reasonable
            else if (ntohs(conn_req->eye_width) > 4000 || ntohs(conn_req->eye_height) > 4000)
            {
                reason = vrcp::VRCPRejectReason::INVALID_EYE_SIZE;
            }
            else if (ntohs(conn_req->refresh_rate_denominator) == 0 || ntohs(conn_req->refresh_rate_numerator) == 0)
            {
                reason = vrcp::VRCPRejectReason::INVALID_REFRESH_RATE;
            }
            // NTP timestamp must be recent enough
            else if (ntohll(conn_req->ntp_timestamp) < UNIX_EPOCH_NTP)
            {
                reason = vrcp::VRCPRejectReason::INVALID_NTP_TIMESTAMP;
            }
            else
            {
                std::string              manufacturer_name;
                std::string              system_name;
                std::vector<std::string> supported_video_codecs;

                // Check TLV fields
                const auto *data           = (const uint8_t *) (conn_req + 1);
                size_t      remaining_size = size - sizeof(vrcp::VRCPConnectionRequest);

                while (remaining_size >= 2)
                {
                    const auto *field = (const vrcp::VRCPAdditionalField *) data;
                    if (field->type == vrcp::VRCPFieldType::MANUFACTURER_NAME_TLV)
                    {
                        if (field->length == 0 || field->length + 2 > remaining_size)
                        {
                            reason = vrcp::VRCPRejectReason::INVALID_MANUFACTURER_NAME;
                            break;
                        }

                        // Copy string
                        manufacturer_name = std::string((const char *) (field->value), field->length);
                    }
                    else if (field->type == vrcp::VRCPFieldType::SYSTEM_NAME_TLV)
                    {
                        if (field->length == 0 || field->length + 2 > remaining_size)
                        {
                            reason = vrcp::VRCPRejectReason::INVALID_SYSTEM_NAME;
                            break;
                        }

                        system_name = std::string((const char *) (field->value), field->length);
                    }
                    else if (field->type == vrcp::VRCPFieldType::SUPPORTED_VIDEO_CODECS_TLV)
                    {
                        if (field->length == 0 || field->length + 2 > remaining_size)
                        {
                            reason = vrcp::VRCPRejectReason::INVALID_VIDEO_CODECS;
                            break;
                        }

                        const auto *codec_data = field->value;
                        // Split by comma
                        while (codec_data < field->value + field->length)
                        {
                            const char *end = (const char *) std::find(codec_data, field->value + field->length, ',');

                            // Copy string to new string
                            supported_video_codecs.emplace_back((const char *) codec_data, end - (const char *) codec_data);
                            codec_data = (const uint8_t *) end + 1;
                        }
                    }
                    else
                    {
                        // Unknown field, ignore
                    }

                    // Move to next field
                    data += field->length + 2;
                    remaining_size -= field->length + 2;
                }

                if (reason == vrcp::VRCPRejectReason::NONE)
                {
                    // Choose codec based on server preferences
                    std::string video_codec;
                    for (const auto &server_codec : server_params.supported_video_codecs)
                    {
                        // If the client supports it as well
                        if (std::find(supported_video_codecs.begin(), supported_video_codecs.end(), server_codec)
                            != supported_video_codecs.end())
                        {
                            video_codec = server_codec;
                            break;
                        }
                    }
                    if (video_codec.empty())
                    {
                        reason = vrcp::VRCPRejectReason::NO_SUPPORTED_VIDEO_CODEC;
                    }
                    else
                    {
                        // All checks passed, so it is compatible

                        // Save client
                        m_data->peer_udp_addr.addr = m_data->tcp_socket.peer_addr().addr;
                        m_data->peer_udp_addr.port = ntohs(conn_req->udp_vrcp_port);

                        // Video port will not be used by the socket, the caller will use it in the video socket
                        resp->peer_video_port    = ntohs(conn_req->video_port);
                        resp->chosen_video_codec = video_codec;
                        resp->ntp_timestamp      = ntohll(conn_req->ntp_timestamp);

                        // Save client params
                        client_params->specs.eye_resolution       = {ntohs(conn_req->eye_width), ntohs(conn_req->eye_height)};
                        client_params->specs.refresh_rate         = {ntohs(conn_req->refresh_rate_numerator),
                                                                     ntohs(conn_req->refresh_rate_denominator)};
                        client_params->specs.manufacturer_name    = manufacturer_name;
                        client_params->specs.system_name          = system_name;
                        client_params->specs.ipd                  = ntohf(conn_req->ipd);
                        client_params->specs.eye_to_head_distance = ntohf(conn_req->eye_to_head_distance);
                        client_params->specs.world_bounds.width   = ntohf(conn_req->world_bounds_width);
                        client_params->specs.world_bounds.height  = ntohf(conn_req->world_bounds_height);
                        client_params->supported_video_codecs     = supported_video_codecs;
                        client_params->video_port                 = ntohs(conn_req->video_port);

                        // We don't need the advertisement socket anymore
                        m_data->udp_broadcast_socket = UDPSocket {};

                        // But we need the VRCP one
                        if (!m_data->udp_socket.is_valid())
                        {
                            m_data->udp_socket = UDPSocket {
                                m_data->udp_vrcp_port,
                                true,
                                false,
                                m_data->measurements_bucket,
                                SocketId::VRCP_UDP_SOCKET,
                            };
                        }

                        // Create a CONN_ACCEPT with a TLV field containing the chosen codec
                        const size_t video_codec_size   = std::min(video_codec.length(), (size_t) 32) + 2;
                        const size_t packet_size        = sizeof(vrcp::VRCPConnectionAccept) + video_codec_size;
                        const size_t padded_packet_size = (packet_size + 3) & ~3;

                        auto *packet      = new uint8_t[padded_packet_size];
                        auto *conn_accept = (vrcp::VRCPConnectionAccept *) packet;
                        *conn_accept      = vrcp::VRCPConnectionAccept {
                                 .n_rows        = static_cast<uint8_t>(padded_packet_size / 4),
                                 .udp_vrcp_port = htons(m_data->udp_socket.local_addr().port),
                                 .video_port    = htons(server_params.video_port),
                        };
                        // Add TLV field
                        auto *field   = (vrcp::VRCPAdditionalField *) (packet + sizeof(vrcp::VRCPConnectionAccept));
                        field->type   = vrcp::VRCPFieldType::CHOSEN_VIDEO_CODEC_TLV;
                        field->length = video_codec_size - 2;
                        memcpy(field->value, video_codec.c_str(), field->length);
                        // Pad with zeros
                        memset(packet + packet_size, 0, padded_packet_size - packet_size);

                        m_data->tcp_socket.send((uint8_t *) conn_accept, padded_packet_size);

                        // We are connected ! The server considers itself as connected to the client.
                        m_data->state = VRCPSocketState::CONNECTED;

                        // We can now start sending actual packets. Because TCP is ordered, the client will receive them after the
                        // CONN_ACCEPT. However, the first UDP messages may experience some delay, as the receiver will not check the
                        // received packets until it is connected.

                        return true;
                    }
                }
            }

            // Didn't return, so it is not compatible.
            // We don't want this client. Send a rejection.
            vrcp::VRCPConnectionReject conn_reject {
                .reason = reason,
                .data   = err_data,
            };
            m_data->tcp_socket.send((uint8_t *) &conn_reject, sizeof(conn_reject));

            // Reset the socket to AWAITING_CONNECTION
            reset_server();
        }

        return false;
    }

    bool VRCPSocket::listen(const std::vector<InetAddr> &bcast_addrs,
                            const VRCPServerParams      &server_params,
                            VRCPClientParams            *client_params,
                            VRCPConnectResp             *resp) const
    {
        if (m_data->state != VRCPSocketState::AWAITING_CONNECTION && m_data->state != VRCPSocketState::NEGOTIATING)
        {
            // Invalid state
            throw std::runtime_error("Socket is not in AWAITING_CONNECTION or NEGOTIATING state");
        }

        if (server_params.supported_video_codecs.empty() || client_params == nullptr || resp == nullptr)
        {
            throw std::invalid_argument("Invalid parameters");
        }

        if (m_data->state == VRCPSocketState::AWAITING_CONNECTION)
        {
            // No TCP connection: send advertisements until we have one
            if (!listen_for_tcp_connection(bcast_addrs))
            {
                return false;
            }
        }

        if (m_data->state == VRCPSocketState::NEGOTIATING)
        {
            // We have a TCP connection and we sent a CONN_REQ. Wait for response
            return listen_for_conn_req(server_params, client_params, resp);
        }

        // Should be unreachable
        throw std::runtime_error("Invalid state");
    }

    const std::vector<VRCPServerCandidate> &VRCPSocket::available_servers() const
    {
        // Only works if we are not connected or a connecting client
        if (m_data->tcp_socket.state() != TCPSocketState::NOT_STARTED && m_data->tcp_socket.state() != TCPSocketState::CONNECTING)
        {
            throw std::runtime_error("The socket is not NOT_STARTED or CONNECTING");
        }

        // Get unix timestamp in seconds
        const uint32_t now =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

        // Remove invalid advertisements from the list
        m_data->server_candidates.erase(
            std::remove_if(m_data->server_candidates.begin(),
                           m_data->server_candidates.end(),
                           [now](const VRCPServerCandidate &candidate)
                           { return now - candidate.timestamp > candidate.interval + ADVERTISEMENT_TIMEOUT_MARGIN_SEC; }),
            m_data->server_candidates.end());

        // Receive UDP messages to update the list
        uint8_t    buffer[1024];
        size_t     actual_size = 0;
        SocketAddr addr;
        // While we have new packets (it is non-blocking, so we won't wait for new ones)
        while (m_data->udp_broadcast_socket.receive_from(buffer, sizeof(buffer), &actual_size, &addr))
        {
            // Check if the m_packet is a VRCPServerAdvertisement
            if (actual_size == sizeof(vrcp::VRCPServerAdvertisement))
            {
                const vrcp::VRCPServerAdvertisement *packet = (vrcp::VRCPServerAdvertisement *) buffer;
                // Packet is valid if
                // - the type is a server advertisement
                // - the magic number is correct
                // - the version is the same as ours
                // - it is not outdated
                // - the tcp port is not 0
                // Filter out invalid senders/wrong protocols
                if (packet->ftype == vrcp::VRCPFieldType::SERVER_ADVERTISEMENT && packet->magic == VRCP_MAGIC
                    && packet->version == VRCP_VERSION
                    && now - ntohl(packet->timestamp) < packet->interval + ADVERTISEMENT_TIMEOUT_MARGIN_SEC
                    && ntohs(packet->tcp_port) != 0)
                {
                    // This is a valid advertisement
                    SocketAddr server_addr {addr.addr, ntohs(packet->tcp_port)};

                    // Is it already in the list?
                    bool found = false;
                    for (VRCPServerCandidate &candidate : m_data->server_candidates)
                    {
                        if (candidate.addr.addr == server_addr.addr)
                        {
                            found = true;

                            // Update it if new timestamp is newer
                            if (candidate.timestamp < ntohl(packet->timestamp))
                            {
                                candidate.timestamp = ntohl(packet->timestamp);
                                candidate.interval  = packet->interval;
                                candidate.addr      = server_addr;
                            }

                            break;
                        }
                    }

                    // Not found
                    if (!found)
                    {
                        // Add it
                        m_data->server_candidates.push_back({
                            .addr      = server_addr,
                            .timestamp = ntohl(packet->timestamp),
                            .interval  = packet->interval,
                        });
                    }
                }
            }
        }

        return m_data->server_candidates;
    }

    bool VRCPSocket::connect(const SocketAddr &addr, const VRCPClientParams &params, VRCPConnectResp *resp) const
    {
        if (params.specs.manufacturer_name.empty() || params.specs.system_name.empty() || params.supported_video_codecs.empty())
        {
            throw std::runtime_error("Invalid specs");
        }
        if (m_data->state != VRCPSocketState::AWAITING_CONNECTION && m_data->state != VRCPSocketState::NEGOTIATING)
        {
            throw std::runtime_error("Socket is not in AWAITING_CONNECTION or NEGOTIATING state");
        }

        if (m_data->state == VRCPSocketState::AWAITING_CONNECTION)
        {
            if (!m_data->tcp_socket.connect(addr))
            {
                return false;
            }

            // We have a connection, but we need to send a CONN_REQ and wait for the response
            m_data->state = VRCPSocketState::NEGOTIATING;

            if (!m_data->udp_socket.is_valid())
            {
                // Create socket when we need it
                m_data->udp_socket = UDPSocket {
                    m_data->udp_vrcp_port,
                    true,
                    false,
                    m_data->measurements_bucket,
                    SocketId::VRCP_UDP_SOCKET,
                };
            }

            // Send a connection request

            const size_t manufacturer_size = std::min(params.specs.manufacturer_name.length(), (size_t) 32) + 2;
            const size_t system_size       = std::min(params.specs.system_name.length(), (size_t) 32) + 2;
            size_t       video_codecs_size = 2; // 2 bytes for the TLV header
            for (const auto &codec : params.supported_video_codecs)
            {
                if (video_codecs_size > 2)
                {
                    video_codecs_size += 1; // 1 byte for the separator
                }
                video_codecs_size += std::min(codec.length(), (size_t) 32);
            }

            // Compute full m_packet size: header + all TLV values with 2 bytes of header each, padded to 4 bytes alignment
            const size_t packet_size = sizeof(vrcp::VRCPConnectionRequest) + manufacturer_size + system_size + video_codecs_size;
            const size_t padded_packet_size = (packet_size + 3) & ~3;

            // Allocate memory for the m_packet
            auto *packet   = new uint8_t[padded_packet_size];
            auto *conn_req = (vrcp::VRCPConnectionRequest *) packet;
            *conn_req      = vrcp::VRCPConnectionRequest {
                     .n_rows = static_cast<uint8_t>(padded_packet_size / 4),
#ifdef WVB_VIDEO_SOCKET_USE_UDP
                .video_mode = vrcp::to_u8(vrcp::VRCPVideoMode::UDP),
#else
                .video_mode = vrcp::to_u8(vrcp::VRCPVideoMode::TCP),
#endif
                .udp_vrcp_port            = htons(m_data->udp_socket.local_addr().port),
                .video_port               = htons(params.video_port),
                .eye_width                = htons(params.specs.eye_resolution.width),
                .eye_height               = htons(params.specs.eye_resolution.height),
                .refresh_rate_numerator   = htons(static_cast<uint16_t>(params.specs.refresh_rate.numerator)),
                .refresh_rate_denominator = htons(static_cast<uint16_t>(params.specs.refresh_rate.denominator)),
                .ipd                      = htonf(params.specs.ipd),
                .eye_to_head_distance     = htonf(params.specs.eye_to_head_distance),
                .world_bounds_width       = htonf(params.specs.world_bounds.width),
                .world_bounds_height      = htonf(params.specs.world_bounds.height),
                .ntp_timestamp            = htonll(params.ntp_timestamp),
            };
            // Manufacturer name
            auto *field   = (vrcp::VRCPAdditionalField *) (packet + sizeof(vrcp::VRCPConnectionRequest));
            field->type   = vrcp::VRCPFieldType::MANUFACTURER_NAME_TLV;
            field->length = static_cast<uint8_t>(manufacturer_size - 2);
            memcpy(field->value, params.specs.manufacturer_name.c_str(), field->length);
            // System name
            field         = (vrcp::VRCPAdditionalField *) (packet + sizeof(vrcp::VRCPConnectionRequest) + manufacturer_size);
            field->type   = vrcp::VRCPFieldType::SYSTEM_NAME_TLV;
            field->length = static_cast<uint8_t>(system_size - 2);
            memcpy(field->value, params.specs.system_name.c_str(), field->length);
            // Video codecs
            field = (vrcp::VRCPAdditionalField *) (packet + sizeof(vrcp::VRCPConnectionRequest) + manufacturer_size + system_size);
            field->type   = vrcp::VRCPFieldType::SUPPORTED_VIDEO_CODECS_TLV;
            field->length = static_cast<uint8_t>(video_codecs_size - 2);
            size_t offset = 0;
            for (const auto &codec : params.supported_video_codecs)
            {
                const size_t codec_size = std::min(codec.length(), (size_t) 32);
                memcpy(field->value + offset, codec.c_str(), codec_size);
                offset += codec_size;
                if (offset < field->length)
                {
                    field->value[offset] = ',';
                    offset += 1;
                }
            }

            // Padding
            memset(packet + packet_size, 0, padded_packet_size - packet_size);

            // Send the m_packet
            m_data->tcp_socket.send(packet, padded_packet_size);

            delete[] packet;
        }

        if (m_data->state == VRCPSocketState::NEGOTIATING)
        {
            return listen_for_conn_resp(params, resp);
        }

        return false;
    }

    bool VRCPSocket::listen_for_conn_resp(const VRCPClientParams &params, VRCPConnectResp *resp) const
    {
        if (m_data->state != VRCPSocketState::NEGOTIATING)
        {
            throw std::runtime_error("Socket is not in NEGOTIATING state");
        }

        const vrcp::VRCPBaseHeader *header = nullptr;
        size_t                      size   = 0;
        // While there are packets to read in the buffer
        while (reliable_receive(&header, &size))
        {
            // Check if it is a VRCP connection accept.
            // For simplicity, all VRCP packets are discarded until a valid connection is set up.
            // TCP ordering will guarantee that packets sent after the CONN_ACCEPT will be received after the CONN_ACCEPT.
            if (header->ftype == vrcp::VRCPFieldType::CONN_ACCEPT && size >= sizeof(vrcp::VRCPConnectionAccept) + 2)
            {
                // We have a CONN_ACCEPT
                const vrcp::VRCPConnectionAccept *conn_accept = (vrcp::VRCPConnectionAccept *) header;

                // Save the peer data
                m_data->peer_udp_addr.addr = m_data->tcp_socket.peer_addr().addr;
                m_data->peer_udp_addr.port = ntohs(conn_accept->udp_vrcp_port);
                resp->peer_video_port      = ntohs(conn_accept->video_port);
                resp->ntp_timestamp        = params.ntp_timestamp;

                // Load TLV name
                std::string chosen_codec;
                size_t      remaining_size = size - sizeof(vrcp::VRCPConnectionAccept);
                auto       *field          = (vrcp::VRCPAdditionalField *) (conn_accept + 1);
                while (remaining_size >= 2)
                {
                    if (field->type == vrcp::VRCPFieldType::CHOSEN_VIDEO_CODEC_TLV && remaining_size >= field->length + 2)
                    {
                        chosen_codec = std::string((const char *) field->value, field->length);
                        break;
                    }
                    if (remaining_size < field->length + 2)
                    {
                        break;
                    }

                    remaining_size -= field->length + 2;
                    field = (vrcp::VRCPAdditionalField *) (field->value + field->length);
                }
                if (chosen_codec.empty())
                {
                    throw std::runtime_error("No chosen video codec in CONN_ACCEPT");
                }
                resp->chosen_video_codec = chosen_codec;

                // We are connected
                m_data->state = VRCPSocketState::CONNECTED;

                // We don't need the broadcast socket anymore
                m_data->udp_broadcast_socket = UDPSocket {};
                // Also deallocate the candidate list
                m_data->server_candidates.clear();
                m_data->server_candidates.shrink_to_fit();

                return true;
            }
            else if (header->ftype == vrcp::VRCPFieldType::CONN_REJECT)
            {
                // We have a CONN_REJECT
                const vrcp::VRCPConnectionReject *conn_reject = (vrcp::VRCPConnectionReject *) header;

                // Reset and start listening again for advertisements
                reset_client();

                // For now throw an exception with the reason
                throw std::runtime_error(std::string("Connection rejected: ")
                                         + wvb::to_string(conn_reject->reason, conn_reject->data));
            }
            // None of them: skip m_packet
        }

        return false;
    }

    void VRCPSocket::close() const
    {
        // Close sockets
        if (m_data->tcp_socket.is_valid())
        {
            m_data->tcp_socket.close();
        }
        if (m_data->udp_socket.is_valid())
        {
            m_data->udp_socket.close();
        }
        if (m_data->udp_broadcast_socket.is_valid())
        {
            m_data->udp_broadcast_socket.close();
        }
        // Change state
        m_data->state = VRCPSocketState::CLOSED;
    }

    void VRCPSocket::reset_client() const
    {
        if (m_data->state == VRCPSocketState::AWAITING_CONNECTION)
        {
            // We are already in the initial state
            return;
        }

        if (m_data->tcp_socket.is_valid())
        {
            // Recreate TCP socket
            auto tcp_port = m_data->tcp_socket.local_addr().port;
            m_data->tcp_socket.close();
            m_data->tcp_socket = TCPSocket(tcp_port, true, m_data->measurements_bucket, SocketId::VRCP_TCP_SOCKET);
            // Empty buffers
            m_data->tcp_head = 0;
            m_data->tcp_tail = 0;
        }

        if (!(m_data->udp_broadcast_socket.is_valid() && m_data->udp_broadcast_socket.local_addr().port == m_data->udp_advert_port))
        {
            // If the socket is not a valid UDP advert port, recreate it
            m_data->udp_broadcast_socket.close();
            m_data->udp_broadcast_socket =
                UDPSocket(m_data->udp_advert_port, true, true, m_data->measurements_bucket, SocketId::VRCP_BCAST_SOCKET);
        }

        // Reset reception socket
        m_data->udp_head = 0;
        m_data->udp_tail = 0;

        // Reset state
        m_data->state = VRCPSocketState::AWAITING_CONNECTION;
    }

    void VRCPSocket::reset_server() const
    {
        if (m_data->state == VRCPSocketState::AWAITING_CONNECTION)
        {
            return;
        }

        // Reset TCP socket
        if (m_data->tcp_socket.is_valid())
        {
            // Recreate TCP socket
            auto tcp_port = m_data->tcp_socket.local_addr().port;
            m_data->tcp_socket.close();
            m_data->tcp_socket = TCPSocket(tcp_port, true, m_data->measurements_bucket, SocketId::VRCP_TCP_SOCKET);
            // Empty buffers
            m_data->tcp_head = 0;
            m_data->tcp_tail = 0;
        }

        if (!(m_data->udp_broadcast_socket.is_valid() && m_data->udp_broadcast_socket.local_addr().port == m_data->local_advert_port))
        {
            // If the socket is not a valid UDP advert port, recreate it
            m_data->udp_broadcast_socket.close();
            m_data->udp_broadcast_socket =
                UDPSocket(m_data->local_advert_port, true, true, m_data->measurements_bucket, SocketId::VRCP_BCAST_SOCKET);
        }
    }

    // Transmission

    bool VRCPSocket::next_tcp_packet(const vrcp::VRCPBaseHeader **dest_packet, size_t *dest_size) const
    {
        // If we got enough data to read a packet
        if (m_data->tcp_tail - m_data->tcp_head >= sizeof(vrcp::VRCPBaseHeader))
        {
            const vrcp::VRCPBaseHeader *header = (vrcp::VRCPBaseHeader *) &m_data->tcp_reception_buffer[m_data->tcp_head];

            // Read n_rows field to get m_packet length
            size_t packet_length = header->n_rows * VRCP_ROW_SIZE;

            if (packet_length == 0)
            {
                // Invalid packet specified 0 rows
                // Consider it as 1 malformed row and skip it
                packet_length = VRCP_ROW_SIZE;
            }
            if (m_data->tcp_tail - m_data->tcp_head >= packet_length)
            {
                // Let user read the packet
                *dest_packet = header;
                *dest_size   = packet_length;

                m_data->tcp_head += packet_length;
                if (m_data->tcp_head == m_data->tcp_tail)
                {
                    // We read all the data, reset the buffer
                    m_data->tcp_head = 0;
                    m_data->tcp_tail = 0;
                }

                return true;
            }
        }
        return false;
    }

    bool VRCPSocket::next_udp_packet(const vrcp::VRCPBaseHeader **dest_packet, size_t *dest_size) const
    {
        // If we got enough data to read a m_packet
        if (m_data->udp_tail - m_data->udp_head >= sizeof(vrcp::VRCPBaseHeader))
        {
            const vrcp::VRCPBaseHeader *header = (vrcp::VRCPBaseHeader *) &m_data->udp_reception_buffer[m_data->udp_head];
            // Read n_rows field to get m_packet length
            size_t packet_length = header->n_rows * VRCP_ROW_SIZE;

            if (packet_length == 0)
            {
                // Invalid packet specified 0 rows
                // Consider it as 1 malformed row and skip it
                packet_length = VRCP_ROW_SIZE;
            }

            if (m_data->udp_tail >= packet_length)
            {
                // Let user read the m_packet
                *dest_packet = header;
                *dest_size   = packet_length;

                m_data->udp_head += packet_length;
                if (m_data->udp_head == m_data->udp_tail)
                {
                    // We read all the data, reset the buffer
                    m_data->udp_head = 0;
                    m_data->udp_tail = 0;
                }

                return true;
            }
        }
        // Else the rest of the m_packet is invalid (no partial packets in UDP), drop it
        m_data->udp_head = 0;
        m_data->udp_tail = 0;
        return false;
    }

    bool VRCPSocket::reliable_receive(const vrcp::VRCPBaseHeader **dest_packet, size_t *dest_size) const
    {
        bool full_packet_in_buffer = next_tcp_packet(dest_packet, dest_size);
        if (full_packet_in_buffer)
        {
            // We have a full packet in the buffer, return it
            return true;
        }

        // We arrive at this point: we have either an incomplete packet or an empty buffer
        if (m_data->tcp_head > 0)
        {
            // We have an incomplete packet, move it to the beginning of the buffer
            memmove(m_data->tcp_reception_buffer,
                    &m_data->tcp_reception_buffer[m_data->tcp_head],
                    m_data->tcp_tail - m_data->tcp_head);
            m_data->tcp_tail -= m_data->tcp_head;
            m_data->tcp_head = 0;
        }

        // Receive at the tail (0 if buffer is empty)
        size_t     received_size = 0;
        const bool received      = m_data->tcp_socket.receive(&m_data->tcp_reception_buffer[m_data->tcp_tail],
                                                         sizeof(m_data->tcp_reception_buffer) - m_data->tcp_tail,
                                                         &received_size);
        if (received)
        {
            m_data->tcp_tail += received_size;
            return next_tcp_packet(dest_packet, dest_size);
        }

        return false;
    }

    bool VRCPSocket::unreliable_receive(const vrcp::VRCPBaseHeader **dest_packet, size_t *dest_size) const
    {
        bool packet_in_buffer = next_udp_packet(dest_packet, dest_size);
        if (packet_in_buffer)
        {
            // We have a m_packet in the buffer, return it
            return true;
        }

        // We arrive at this point: we have an empty buffer (no partial packets in UDP)
        // Receive at the beginning, since the tail should be 0. Tail is only use to split compound packets
        size_t     received_size = 0;
        bool       should_retry  = true;
        SocketAddr sender_addr {};

        while (should_retry)
        {
            const bool received = m_data->udp_socket.receive_from(&m_data->udp_reception_buffer[0],
                                                                  sizeof(m_data->udp_reception_buffer),
                                                                  &received_size,
                                                                  &sender_addr);

            // Filter out requests from other clients
            if (received && sender_addr == m_data->peer_udp_addr)
            {
                m_data->udp_tail += received_size;
                return next_udp_packet(dest_packet, dest_size);
            }
            // Else retry, except if no m_packet was received
            should_retry = received;
        }

        return false;
    }

    void VRCPSocket::reliable_send(vrcp::VRCPBaseHeader *packet, size_t size, uint32_t timeout_us) const
    {
        m_data->tcp_socket.send((uint8_t *) packet, size, timeout_us);
    }

    bool VRCPSocket::unreliable_send(vrcp::VRCPBaseHeader *packet, size_t size) const
    {
        return m_data->udp_socket.send_to(m_data->peer_udp_addr, (uint8_t *) packet, size);
    }

    // Getters

    bool VRCPSocket::is_connected_refresh() const
    {
        if (m_data && m_data->state == VRCPSocketState::CONNECTED)
        {
            const bool tcp_connected = m_data->tcp_socket.refresh_state() == TCPSocketState::CONNECTED;
            if (!tcp_connected)
            {
                close();
            }
            return tcp_connected;
        }
        return false;
    }
    bool VRCPSocket::is_connected() const
    {
        return m_data && m_data->state == VRCPSocketState::CONNECTED && m_data->tcp_socket.is_connected();
    }
    InetAddr VRCPSocket::peer_inet_addr() const
    {
        return m_data->tcp_socket.peer_addr().addr;
    }

} // namespace wvb