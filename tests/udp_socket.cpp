#include <wvb_common/socket.h>

#include <iostream>
#include <test_framework.hpp>

#define MAX_REPEAT   1000
#define INTERVAL    std::chrono::milliseconds(1)

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
        std::this_thread::sleep_for(INTERVAL);
    }
    return false;
}

TEST
{
    wvb::SocketAddr server_addr {INET_ADDR_LOOPBACK, 12420};
    wvb::SocketAddr client_addr {INET_ADDR_LOOPBACK, 12421};

    START_THREAD(server,
                 [&]
                 {
                     // Accept connections
                     wvb::UDPSocket server_socket(server_addr.port);
                     EXPECT_EQ((int) server_socket.local_addr().addr, INET_ADDR_ANY);
                     EXPECT_EQ(server_socket.local_addr().port, server_addr.port);

                     // Wait for a while
                     std::this_thread::sleep_for(std::chrono::seconds(1));

                     // Receive some data.
                     uint8_t         buffer[1024];
                     size_t          actual_size = 0;
                     wvb::SocketAddr sender_addr;
                     bool            success = repeat([&server_socket, &buffer, &actual_size, &sender_addr]
                                           { return server_socket.receive_from(buffer, sizeof(buffer), &actual_size, &sender_addr); });
                     EXPECT_TRUE(success);
                     EXPECT_EQ((int) actual_size, 12);
                     EXPECT_EQ(sender_addr.addr, client_addr.addr);
                     EXPECT_EQ(sender_addr.port, client_addr.port);
                     std::string received_data(reinterpret_cast<char *>(buffer), actual_size);
                     EXPECT_EQ(received_data, std::string("Hello world!")); // UDP splits each message because they are standalone

                     // Receive again. Should work without waiting since we had 2 packets in the queue.
                     success = server_socket.receive_from(buffer, sizeof(buffer), &actual_size, &sender_addr);
                     EXPECT_TRUE(success);
                     EXPECT_EQ((int) actual_size, 15);
                     EXPECT_EQ(sender_addr.addr, client_addr.addr);
                     EXPECT_EQ(sender_addr.port, client_addr.port);
                     received_data = std::string(reinterpret_cast<char *>(buffer), actual_size);
                     EXPECT_EQ(received_data, std::string("Another message"));
                     std::cout << "Received msg\n";

                     // Send some data back
                     std::string data_to_send = "Hello back!";
                     success =
                         server_socket.send_to(client_addr, reinterpret_cast<uint8_t *>(data_to_send.data()), data_to_send.size());
                     EXPECT_TRUE(success);
                 });

    START_THREAD(client,
                 [&]
                 {
                     // Connect to server
                     wvb::UDPSocket client_socket(client_addr.port);
                     EXPECT_EQ((int) client_socket.local_addr().addr, INET_ADDR_ANY);
                     EXPECT_EQ(client_socket.local_addr().port, client_addr.port);

                     // Send some data.
                     std::string data_to_send = "Hello world!";
                     bool        success =
                         client_socket.send_to(server_addr, reinterpret_cast<uint8_t *>(data_to_send.data()), data_to_send.size());
                     EXPECT_TRUE(success);

                     data_to_send = "Another message";
                     success =
                         client_socket.send_to(server_addr, reinterpret_cast<uint8_t *>(data_to_send.data()), data_to_send.size());
                     EXPECT_TRUE(success);

                     // Receive some data.
                     uint8_t         buffer[1024];
                     size_t          actual_size = 0;
                     wvb::SocketAddr sender_addr;
                     success = repeat([&client_socket, &buffer, &actual_size, &sender_addr]
                                      { return client_socket.receive_from(buffer, sizeof(buffer), &actual_size, &sender_addr); });
                     EXPECT_TRUE(success);
                     EXPECT_EQ((int) actual_size, 11);
                     EXPECT_EQ(sender_addr.addr, server_addr.addr);
                     EXPECT_EQ(sender_addr.port, server_addr.port);
                     std::string received_data(reinterpret_cast<char *>(buffer), actual_size);
                     EXPECT_EQ(received_data, std::string("Hello back!"));
                 });

    server.join();
    client.join();
}