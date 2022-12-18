#include <wvb_common/socket.h>

#include <iostream>
#include <test_framework.hpp>

TEST
{
    wvb::SocketAddr server_addr {INET_ADDR_LOOPBACK, 12342};
    wvb::SocketAddr client_addr {INET_ADDR_LOOPBACK, 12343};

    START_THREAD(server,
        [&]
        {
            // Create UDP socket
            wvb::UDPSocket socket;

            ASSERT_NO_THROWS(socket = wvb::UDPSocket(server_addr.port));

            // Receive data
            uint8_t buffer[256];
            auto    res = socket.receive(buffer, sizeof(buffer));

            ASSERT_TRUE(res.is_ok());
            EXPECT_EQ(res.size, static_cast<size_t>(5));

            EXPECT_EQ(buffer[0], static_cast<uint8_t>('t'));
            EXPECT_EQ(buffer[1], static_cast<uint8_t>('e'));
            EXPECT_EQ(buffer[2], static_cast<uint8_t>('s'));
            EXPECT_EQ(buffer[3], static_cast<uint8_t>('t'));

            std::string msg = "Received " + std::to_string(res.size) + " bytes\n";
            std::cout << msg;

            // Send data
            uint8_t data[] = "response";
            res           = socket.send(data, sizeof(data), client_addr);
            EXPECT_TRUE(res.is_ok());
            EXPECT_EQ(res.size, sizeof(data));

            msg = "Sent " + std::to_string(res.size) + " bytes\n";
            std::cout << msg;
        });
    START_THREAD(client,
        [&]
        {
            // Create UDP socket
            wvb::UDPSocket socket;

            ASSERT_NO_THROWS(socket = wvb::UDPSocket(client_addr.port));

            // Send data
            uint8_t buffer[] = "test";
            auto    res     = socket.send(buffer, sizeof(buffer), server_addr);

            EXPECT_TRUE(res.is_ok());
            EXPECT_EQ(res.size, static_cast<size_t>(5));

            std::string msg = "Sent " + std::to_string(res.size) + " bytes\n";
            std::cout << msg;

            // Receive data
            uint8_t data[256];
            res = socket.receive(data, sizeof(data));
            EXPECT_TRUE(res.is_ok());
            EXPECT_EQ(res.size, static_cast<size_t>(9));

            msg = "Received " + std::to_string(res.size) + " bytes\n";
            std::cout << msg;

            EXPECT_EQ(data[0], static_cast<uint8_t>('r'));
            EXPECT_EQ(data[1], static_cast<uint8_t>('e'));
            EXPECT_EQ(data[2], static_cast<uint8_t>('s'));
            EXPECT_EQ(data[3], static_cast<uint8_t>('p'));
            EXPECT_EQ(data[4], static_cast<uint8_t>('o'));
            EXPECT_EQ(data[5], static_cast<uint8_t>('n'));
            EXPECT_EQ(data[6], static_cast<uint8_t>('s'));
            EXPECT_EQ(data[7], static_cast<uint8_t>('e'));
        });

    server.join();
    client.join();
}