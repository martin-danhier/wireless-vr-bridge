#include <wvb_common/formats/h264.h>
#include <wvb_common/video_socket.h>

#include <iostream>
#include <test_framework.hpp>

#define MAX_REPEAT  250
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
    wvb::SocketAddr server_addr {INET_ADDR_LOOPBACK, 22340};
    wvb::SocketAddr client_addr {INET_ADDR_LOOPBACK, 22341};

    START_THREAD(client,
                 [&]
                 {
                     wvb::ClientVideoSocket client_socket {client_addr.port};
                     bool success = repeat([&client_socket, &server_addr] { return client_socket.connect(server_addr); });
                     ASSERT_TRUE(success);

                     client_socket.set_depacketizer(nullptr);

                     const uint8_t *data      = nullptr;
                     size_t         size      = 0;
                     uint32_t       timestamp = 0;

                     bool                                  end_of_stream             = false;
                     bool                                  save_frame                = false;
                     uint32_t                              frame_index               = 0;
                     uint32_t                              pose_timestamp            = 0;
                     std::chrono::steady_clock::time_point last_packet_received_time = std::chrono::steady_clock::now();
                     success                                                         = repeat(
                         [&client_socket,
                          &data,
                          &size,
                          &timestamp,
                          &frame_index,
                          &end_of_stream,
                          &pose_timestamp,
                          &last_packet_received_time,
                          &save_frame]
                         {
                             client_socket.update();
                             return client_socket.receive_packet(&data,
                                                                 &size,
                                                                 &frame_index,
                                                                 &end_of_stream,
                                                                 &timestamp,
                                                                 &pose_timestamp,
                                                                 &last_packet_received_time,
                                                                 &save_frame);
                         });

                     ASSERT_TRUE(success);
                     ASSERT_NOT_NULL(data);
                     EXPECT_EQ(timestamp, 124578u);
                     ASSERT_EQ(size, (size_t) 1024 * 1024);
                     EXPECT_EQ(frame_index, 1u);
                     for (auto i = 0; i < 1024 * 1024; i++)
                     {
                         EXPECT_EQ(data[i], static_cast<uint8_t>(i % 256));
                     }

                     client_socket.release_frame_data();

                     data = nullptr;

                     std::cout << "Client: Waiting for packet 2\n";

                 });

    START_THREAD(server,
                 [&]
                 {
                     wvb::ServerVideoSocket server_socket {server_addr.port};
                     bool success = repeat([&server_socket, &client_addr] { return server_socket.listen(client_addr); });
                     ASSERT_TRUE(success);

                     server_socket.set_packetizer(nullptr);

                     std::vector<uint8_t> data_to_send;
                     data_to_send.reserve(1024 * 1024);
                     for (auto i = 0; i < 1024 * 1024; i++)
                     {
                         data_to_send.push_back(static_cast<uint8_t>(i % 256));
                     }

                     server_socket.send_packet(data_to_send.data(), data_to_send.size(), 1, false, 124578u, 456789, true, 0);

                     std::this_thread::sleep_for(std::chrono::milliseconds(100));

                 });

    server.join();
    client.join();
}