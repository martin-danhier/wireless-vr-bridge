#include <wvb_common/socket.h>

#include <test_framework.hpp>

#define MAX_REPEAT  10000
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
    wvb::SocketAddr server_addr {INET_ADDR_LOOPBACK, 22345};
    wvb::SocketAddr client_addr {INET_ADDR_LOOPBACK, 22346};

    START_THREAD(
        server,
        [&]
        {
            // Accept connections
            wvb::TCPSocket server_socket(server_addr.port);
            bool           success = repeat([&server_socket] { return server_socket.listen(); });
            ASSERT_TRUE(success);

            // Wait for a while
            std::this_thread::sleep_for(std::chrono::seconds(1));

            // The socket is now connected. Receive some data.
            uint8_t buffer[1024];
            size_t  actual_size = 0;
            success             = repeat([&server_socket, &buffer, &actual_size]
                             { return server_socket.receive(buffer, sizeof(buffer), &actual_size); });
            EXPECT_TRUE(success);
            EXPECT_EQ((int) actual_size, 27);
            std::string received_data(reinterpret_cast<char *>(buffer), actual_size);
            EXPECT_EQ(received_data,
                      std::string("Hello world!Another message")); // TCP doesn't distinguish messages in the stream

            // Send some data back
            std::string data_to_send = "Hello back!";
            server_socket.send(reinterpret_cast<uint8_t *>(data_to_send.data()), data_to_send.size());

            // Receive a very large message that would block the socket
            auto                                  nb_a_received = 0;
            std::chrono::steady_clock::time_point start         = std::chrono::steady_clock::now();
            while (nb_a_received < 1024 * 1024)
            {
                success = repeat([&server_socket, &buffer, &actual_size]
                                 { return server_socket.receive(buffer, sizeof(buffer), &actual_size); });
                EXPECT_TRUE(success);
                EXPECT_NEQ((int) actual_size, 0);
                received_data = std::string(reinterpret_cast<char *>(buffer), actual_size);
                // Should be all a's
                EXPECT_EQ(received_data, std::string(actual_size, 'a'));

                nb_a_received += actual_size;

                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
                if (elapsed > 1000)
                {
                    break;
                }
            }
            EXPECT_EQ(nb_a_received, 1024 * 1024);

            // Should not receive anything else
            success =  server_socket.receive(buffer, sizeof(buffer), &actual_size);
            EXPECT_FALSE(success);
            EXPECT_EQ((int) actual_size, 0);
        });

    START_THREAD(client,
                 [&]
                 {
                     // Connect to server
                     wvb::TCPSocket client_socket(client_addr.port);
                     bool           success = repeat([&client_socket, &server_addr] { return client_socket.connect(server_addr); });
                     ASSERT_TRUE(success);

                     // The socket is now connected. Send some data.
                     std::string message = "Hello world!";
                     client_socket.send(reinterpret_cast<uint8_t *>(message.data()), message.size());

                     // Send again
                     message = "Another message";
                     client_socket.send(reinterpret_cast<uint8_t *>(message.data()), message.size());

                     // Receive some data back
                     uint8_t buffer[1024];
                     size_t  actual_size = 0;
                     success             = repeat([&client_socket, &buffer, &actual_size]
                                      { return client_socket.receive(buffer, sizeof(buffer), &actual_size); });
                     EXPECT_TRUE(success);
                     EXPECT_EQ((int) actual_size, 11);
                     std::string received_data(reinterpret_cast<char *>(buffer), actual_size);
                     EXPECT_EQ(received_data, std::string("Hello back!"));

                     // Send a very large message that would block the socket
                     std::string large_message(1024 * 1024, 'a');
                     client_socket.send(reinterpret_cast<uint8_t *>(large_message.data()), large_message.size());
                 });

    client.join();
    server.join();
}