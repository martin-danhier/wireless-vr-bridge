
#include <test_framework.hpp>
#if defined(ANDROID) && defined(WIN32) // No support currently for windows client

#include <wvb_client/client.h>
#include <wvb_server/server.h>

#include <iostream>
#include <thread>

enum class State
{
    SERVER_SELECTION,
    LOADING,
    STREAM,
};

TEST
{
    START_THREAD(client_thread,
                 [&]
                 {
                     try
                     {
                         wvb::client::Client client;
                         State               state = State::SERVER_SELECTION;
                         client.init();

                         auto &specs                    = client.system_specs();
                         specs.system_name              = "Mirror";
                         specs.manufacturer_name        = "WVB";
                         specs.eye_resolution.width     = 1024;
                         specs.eye_resolution.height    = 1024;
                         specs.refresh_rate.numerator   = 72;
                         specs.refresh_rate.denominator = 1;
                         specs.ipd                      = 0.064f;

                         bool is_running = true;
                         while (is_running)
                         {
                             if (state == State::SERVER_SELECTION)
                             {
                                 // Get the list of servers
                                 const auto &servers = client.available_servers();
                                 if (!servers.empty())
                                 {
                                     // Select the first server
                                     client.connect(servers[0].addr);
                                     state = State::LOADING;
                                 }
                             }
                             is_running = client.update();
                             std::this_thread::sleep_for(std::chrono::milliseconds(10));

                         }
                     }
                     catch (const std::exception &e)
                     {
                         std::cerr << "Client thread exception: " << e.what() << std::endl;
                         ERROR_AND_QUIT("Client thread exception");
                     }
                 });

    START_THREAD(server_thread,
                 [&]
                 {
                     try
                     {
                         wvb::server::Server server("../wvb_server/shaders/");
                         server.run();
                     }
                     catch (const std::exception &e)
                     {
                         std::cerr << "Server thread exception: " << e.what() << std::endl;
                         ERROR_AND_QUIT("Server thread exception");
                     }
                 });

    client_thread.join();
    server_thread.join();
}

#else
TEST {};
#endif
