#include "wvb_server/server.h"

#include <wvb_common/server_shared_state.h>

#include <iostream>
#include <thread>

namespace wvb::server
{
    // =======================================================================================
    // =                                       Structs                                       =
    // =======================================================================================

    struct Server::Data
    {
        uint16_t                 port;
        ServerDriverSharedMemory shared_memory;
        // States
        DriverConnectionState driver_connection_state = DriverConnectionState::AWAITING_DRIVER;
        ClientConnectionState client_connection_state = ClientConnectionState::AWAITING_CLIENT;
        AppState              app_state               = AppState::NOT_READY;
    };

    // =======================================================================================
    // =                                         API                                         =
    // =======================================================================================

    Server::Server(uint16_t port) : m_data(new Data {port})
    {
        // Init shared memory and tell the driver that the server is alive, but waiting for a client to connect
        m_data->shared_memory = ServerDriverSharedMemory(WVB_SERVER_DRIVER_MUTEX_NAME, WVB_SERVER_DRIVER_MEMORY_NAME);
        {
            auto lock          = m_data->shared_memory.lock();
            lock->server_state = ServerState::AWAITING_CONNECTION;
        }

        // Log
        std::cout << "Server started on port " << port << "\nAwaiting connection from client..." << std::endl;
    }

    Server::~Server()
    {
        if (m_data != nullptr)
        {
            {
                auto lock          = m_data->shared_memory.lock();
                lock->server_state = ServerState::NOT_RUNNING;
            }

            delete m_data;
            m_data = nullptr;
        }
    }

    void Server::run()
    {
        // Main event
        bool running = true;
        while(running) {
            // Poll driver events

            // Poll client events

            // If no app is running, sleep to avoid wasting CPU
            if (m_data->app_state == AppState::NOT_READY) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }



} // namespace wvb::server