#include <wvb_common/vrcp_socket.h>

#include <iostream>
#include <sstream>
#include <test_framework.hpp>
#ifdef __linux__
#include <cstring>
#endif

#define MAX_REPEAT  30000
#define INTERVAL_MS 1

bool repeat(const std::function<bool()> &task)
{
    for (uint32_t i = 0; i < MAX_REPEAT; i++)
    {
        bool success = task();
        if (success)
        {
            return true;
        }

        // Wait a bit before trying again
        std::this_thread::sleep_for(std::chrono::milliseconds(INTERVAL_MS));
    }
    return false;
}

TEST
{
    // Address to use for advertisement broadcast
    wvb::InetAddr         bcast_addr = INET_ADDR_LOOPBACK;
    wvb::VRCPClientParams client_params {
        .video_port = 8931,
        .specs =
            {
                .system_name          = "Quest 2",
                .manufacturer_name    = "Oculus",
                .eye_resolution       = {1832, 1920},
                .refresh_rate         = {90, 1},
                .ipd                  = 0.064f,
                .eye_to_head_distance = 0.0f,
            },
        .supported_video_codecs = {"h264", "h265"},
        .ntp_timestamp         = 22123456789,
    };
    wvb::VRCPServerParams server_params {
        .video_port     = 8722,
        .supported_video_codecs = {"h264"},
    };

    START_THREAD(server,
                 [&]()
                 {
                     auto socket = wvb::VRCPSocket::create_server();
                     ASSERT_TRUE(socket.is_valid());

                     // Listen for connection and send advertisements
                     wvb::VRCPClientParams received_params {0};
                     wvb::VRCPConnectResp  resp {0};

                     std::vector<wvb::InetAddr> bcast_addrs;
                     bcast_addrs.push_back(bcast_addr);

                     bool connected = repeat([&socket, &bcast_addrs, &server_params, &received_params, &resp]()
                                             { return socket.listen(bcast_addrs, server_params, &received_params, &resp); });

                     ASSERT_TRUE(connected);

                     std::cout << "Client connected\n";

                     // Received params should be the ones of the client
                     EXPECT_EQ(received_params.video_port, client_params.video_port);
                     EXPECT_EQ(received_params.specs.system_name, client_params.specs.system_name);
                     EXPECT_EQ(received_params.specs.manufacturer_name, client_params.specs.manufacturer_name);
                     EXPECT_EQ(received_params.specs.eye_resolution.width, client_params.specs.eye_resolution.width);
                     EXPECT_EQ(received_params.specs.eye_resolution.height, client_params.specs.eye_resolution.height);
                     EXPECT_EQ(received_params.specs.refresh_rate.numerator, client_params.specs.refresh_rate.numerator);
                     EXPECT_EQ(received_params.specs.refresh_rate.denominator, client_params.specs.refresh_rate.denominator);
                     EXPECT_EQ(received_params.specs.ipd, client_params.specs.ipd);
                     EXPECT_EQ(received_params.specs.eye_to_head_distance, client_params.specs.eye_to_head_distance);
                     EXPECT_EQ(received_params.supported_video_codecs.size(), client_params.supported_video_codecs.size());
                     EXPECT_EQ(received_params.supported_video_codecs[0], std::string("h264"));
                     EXPECT_EQ(received_params.supported_video_codecs[1], std::string("h265"));

                     EXPECT_EQ(resp.peer_video_port, client_params.video_port);
                     EXPECT_EQ(resp.chosen_video_codec, std::string("h264"));

                     // It should be possible to send a message
                     char    msg[]                                                       = "Hello world";
                     uint8_t buffer[sizeof(msg) + sizeof(wvb::vrcp::VRCPUserDataHeader)] = {0};
                     {
                         auto *header   = reinterpret_cast<wvb::vrcp::VRCPUserDataHeader *>(buffer);
                         header->ftype  = wvb::vrcp::VRCPFieldType::USER_DATA;
                         header->n_rows = sizeof(buffer) / VRCP_ROW_SIZE;
                         header->size   = sizeof(msg);
                         memcpy(buffer + sizeof(wvb::vrcp::VRCPUserDataHeader), msg, sizeof(msg));
                     }
                     socket.reliable_send((wvb::vrcp::VRCPBaseHeader *) buffer, sizeof(buffer));

                     // Send another
                     char    msg2[]                                                        = "Another message";
                     uint8_t buffer2[sizeof(msg2) + sizeof(wvb::vrcp::VRCPUserDataHeader)] = {0};
                     {
                         auto *header2   = reinterpret_cast<wvb::vrcp::VRCPUserDataHeader *>(buffer2);
                         header2->ftype  = wvb::vrcp::VRCPFieldType::USER_DATA;
                         header2->n_rows = sizeof(buffer2) / VRCP_ROW_SIZE;
                         header2->size   = sizeof(msg2);
                         memcpy(buffer2 + sizeof(wvb::vrcp::VRCPUserDataHeader), msg2, sizeof(msg2));
                     }
                     socket.reliable_send((wvb::vrcp::VRCPBaseHeader *) buffer2, sizeof(buffer2));

                     // Receive
                     const wvb::vrcp::VRCPBaseHeader *packet      = nullptr;
                     size_t                           packet_size = 0;
                     bool                             received =
                         repeat([&socket, &packet, &packet_size]() { return socket.unreliable_receive(&packet, &packet_size); });
                     EXPECT_TRUE(received);
                     EXPECT_EQ(packet_size, sizeof(wvb::vrcp::VRCPUserDataHeader) + sizeof("Hello back") + 1);
                     EXPECT_EQ((int) packet->ftype, (int) wvb::vrcp::VRCPFieldType::USER_DATA);
                     EXPECT_EQ((int) ((wvb::vrcp::VRCPUserDataHeader *) packet)->size, (int) sizeof("Hello back"));
                     EXPECT_EQ(memcmp((char *) packet + sizeof(wvb::vrcp::VRCPUserDataHeader), "Hello back", sizeof("Hello back")), 0);
                 });

    START_THREAD(client,
                 [&]()
                 {
                     auto socket = wvb::VRCPSocket::create_client();
                     ASSERT_TRUE(socket.is_valid());

                     // Get list of servers
                     bool has_list = repeat(
                         [&socket]()
                         {
                             const auto &list = socket.available_servers();
                             return !list.empty();
                         });
                     const auto &server_list = socket.available_servers();
                     ASSERT_EQ((int) server_list.size(), 1);

                     std::stringstream ss;
                     ss << "Server list: \n";
                     for (const auto &server : server_list)
                     {
                         ss << "  " << wvb::to_string(server.addr) << "\n";
                     }
                     ss << "\n";
                     std::cout << ss.str();

                     // Connect to found server
                     wvb::VRCPConnectResp resp      = {0};
                     auto                 connected = repeat([&socket, &server_list, &client_params, &resp]()
                                             { return socket.connect(server_list[0].addr, client_params, &resp); });
                     ASSERT_TRUE(connected);

                     EXPECT_EQ(resp.peer_video_port, server_params.video_port);
                     EXPECT_EQ(resp.chosen_video_codec, std::string("h264"));

                     // Wait for a (small) while
                     std::this_thread::sleep_for(std::chrono::milliseconds(1));

                     // Wait for a message
                     const wvb::vrcp::VRCPBaseHeader *packet      = nullptr;
                     size_t                           packet_size = 0;
                     auto                             msg         = socket.reliable_receive(&packet,
                                                        &packet_size); // Because we waited, we should have 2 messages in the buffer
                     ASSERT_TRUE(msg);
                     EXPECT_EQ(packet_size, sizeof(wvb::vrcp::VRCPUserDataHeader) + sizeof("Hello world"));
                     EXPECT_EQ((uint8_t) packet->ftype, (uint8_t) wvb::vrcp::VRCPFieldType::USER_DATA);
                     const auto *header = reinterpret_cast<const wvb::vrcp::VRCPUserDataHeader *>(packet);
                     EXPECT_EQ(header->size, (uint16_t) sizeof("Hello world"));
                     EXPECT_EQ(memcmp(packet + 1, "Hello world", sizeof("Hello world")), 0);

                     auto msg2 = socket.reliable_receive(&packet, &packet_size); // Look at the second message
                     ASSERT_TRUE(msg2);
                     EXPECT_EQ(packet_size, sizeof(wvb::vrcp::VRCPUserDataHeader) + sizeof("Another message"));
                     EXPECT_EQ((uint8_t) packet->ftype, (uint8_t) wvb::vrcp::VRCPFieldType::USER_DATA);
                     const auto *header2 = reinterpret_cast<const wvb::vrcp::VRCPUserDataHeader *>(packet);
                     EXPECT_EQ(header2->size, (uint16_t) sizeof("Another message"));
                     EXPECT_EQ(memcmp(packet + 1, "Another message", sizeof("Another message")), 0);

                     // But now, it is empty
                     auto msg3 = socket.reliable_receive(&packet, &packet_size);
                     EXPECT_FALSE(msg3);

                     // Send a response
                     char    msg4[]                                                            = "Hello back";
                     uint8_t buffer4[sizeof(msg4) + sizeof(wvb::vrcp::VRCPUserDataHeader) + 1] = {0}; // Add 1 byte of padding
                     {
                         auto *header4   = reinterpret_cast<wvb::vrcp::VRCPUserDataHeader *>(buffer4);
                         header4->ftype  = wvb::vrcp::VRCPFieldType::USER_DATA;
                         header4->n_rows = sizeof(buffer4) / VRCP_ROW_SIZE;
                         header4->size   = sizeof(msg4);
                         memcpy(buffer4 + sizeof(wvb::vrcp::VRCPUserDataHeader), msg4, sizeof(msg4));
                     }
                     socket.unreliable_send((wvb::vrcp::VRCPBaseHeader *) buffer4, sizeof(buffer4));
                 });

    server.join();
    client.join();
}