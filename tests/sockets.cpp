#include <test_framework.hpp>
#include <thread>
#include <iostream>
#include <wvb_common/socket.h>

TEST
{
    std::thread server(
        [&]
        {
            // Create UDP socket
            wvb::UDPSocket socket;

            ASSERT_NO_THROWS(socket = wvb::UDPSocket(12345));

            // Receive data
            uint8_t buffer[256];
            auto    size = socket.receive_from(buffer, sizeof(buffer), 12345);

            EXPECT_EQ(size, static_cast<size_t>(4));

            EXPECT_EQ(buffer[0], static_cast<uint8_t>('t'));
            EXPECT_EQ(buffer[1], static_cast<uint8_t>('e'));
            EXPECT_EQ(buffer[2], static_cast<uint8_t>('s'));
            EXPECT_EQ(buffer[3], static_cast<uint8_t>('t'));

            std::string msg = "Received " + std::to_string(size) + " bytes\n";
            std::cout << msg;


        });

    std::thread client(
        [&]
        {
            // Create UDP socket
            wvb::UDPSocket socket;

            ASSERT_NO_THROWS(socket = wvb::UDPSocket(12346));

            // Send data
            uint8_t buffer[4] = { 't', 'e', 's', 't' };
            auto    size      = socket.send_to(buffer, sizeof(buffer), 12345, INET_ADDR_LOOPBACK);

            EXPECT_EQ(size, static_cast<size_t>(4));

            std::string msg = "Sent " + std::to_string(size) + " bytes\n";
            std::cout << msg;


        });

    server.join();
    client.join();
}