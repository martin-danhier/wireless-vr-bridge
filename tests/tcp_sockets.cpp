#include <wvb_common/socket.h>

#include <iostream>
#include <stdexcept>
#include <test_framework.hpp>
#include <thread>

TEST
{
    wvb::SocketAddr server_addr {INET_ADDR_LOOPBACK, 12340};
    wvb::SocketAddr client_addr {INET_ADDR_LOOPBACK, 12341};

    START_THREAD(server,
        [&]
        {
            // Create TCP socket in listening mode
            wvb::TCPSocket socket(server_addr.port, true);
            ASSERT_TRUE(socket.is_valid());

            // Update server address
            server_addr = socket.socket_addr();

            // Print server address
            std::string msg = "Server address: " + server_addr.to_string() + "\n";
            std::cout << msg;

            // Accept incoming connection
            auto i = 4;
            while (i > 0) {
                auto result = socket.accept();
                if (result == wvb::SocketResultType::OK) {
                    break;
                }
                i--;
            }
            ASSERT_TRUE(i > 0);
            ASSERT_TRUE(socket.is_connected());

            // Update server address
            server_addr = socket.socket_addr();

            // Send data
            std::string data = "Hello world!";
            auto        result = socket.send(data.data(), data.size());
            EXPECT_TRUE(result.type == wvb::SocketResultType::OK);
            EXPECT_TRUE(result.size == data.size());

            // Receive data
            data.clear();
            data.resize(9);
            result = socket.receive(data.data(), data.size());
            EXPECT_TRUE(result.type == wvb::SocketResultType::OK);
            EXPECT_EQ(result.size, data.size());
            EXPECT_EQ(data, std::string("Received!"));

        });

    START_THREAD(client,
        [&]
        {
            auto i = 4;
            // Wait until server has a valid address
            while (i > 0) {
                if (server_addr.port != PORT_AUTO) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                i--;
            }
            ASSERT_TRUE(i > 0);

            // Create TCP socket in connection mode
            wvb::TCPSocket socket(client_addr.port, true);
            ASSERT_TRUE(socket.is_valid());

            // Update client address
            client_addr = socket.socket_addr();

            // Print client address
            std::string msg = "Client address: " + client_addr.to_string() + "\n";
            std::cout << msg;

            // Connect to server
            i = 10;
            while (i > 0) {
                auto result = socket.connect(server_addr);
                if (result == wvb::SocketResultType::OK) {
                    break;
                }
                i--;
            }
            ASSERT_TRUE(i > 0);
            ASSERT_TRUE(socket.is_connected());

            // Receive data
            std::string data;
            data.resize(12);
            auto result = socket.receive(data.data(), data.size());
            EXPECT_TRUE(result.type == wvb::SocketResultType::OK);
            EXPECT_EQ(result.size, data.size());
            EXPECT_EQ(data, std::string("Hello world!"));

            // Send data
            data = "Received!";
            result = socket.send(data.data(), data.size());
            EXPECT_TRUE(result.type == wvb::SocketResultType::OK);
            EXPECT_EQ(result.size, data.size());
        });

    server.join();
    client.join();
}