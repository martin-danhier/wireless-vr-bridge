#include "wvb_server/server.h"

#include <wvb_common/rtc.h>
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
        // Client communication
        VRStream stream;
        // Driver communication
        ServerDriverSharedMemory shared_memory;
        DriverEvents             driver_events {false};
        ServerEvents             server_events {true};
        // States
        DriverConnectionState driver_connection_state = DriverConnectionState::AWAITING_DRIVER;
        ClientConnectionState client_connection_state = ClientConnectionState::AWAITING_CLIENT;
        AppState              app_state               = AppState::NOT_READY;

        // ---------------------------------------------------------------------------------------

        void handle_driver_state_changed();
    };

    // =======================================================================================
    // =                                   Implementation                                    =
    // =======================================================================================

    void Server::Data::handle_driver_state_changed()
    {
        // Get new value of driver state
        DriverState driver_state = DriverState::NOT_RUNNING;
        {
            auto lock    = shared_memory.lock();
            driver_state = lock->driver_state;
        }

        // Handle the value
        if (driver_state == DriverState::NOT_RUNNING)
        {
            if (driver_connection_state != DriverConnectionState::AWAITING_DRIVER)
            {
                // Lost connection to driver
                std::cout << "Lost connection to driver.\n";
                // TODO handle this (e.g put app in standby...)
            }
            driver_connection_state = DriverConnectionState::AWAITING_DRIVER;
        }
        else
        {
            if (driver_connection_state != DriverConnectionState::DRIVER_CONNECTED)
            {
                // Connected to driver
                std::cout << "Connected to driver.\n";
            }
            driver_connection_state = DriverConnectionState::DRIVER_CONNECTED;
        }
    }

    // =======================================================================================
    // =                                         API                                         =
    // =======================================================================================

    Server::Server() : m_data(new Data {})
    {
        // Init shared memory and tell the driver that the server is alive, but waiting for a client to connect
        m_data->shared_memory = ServerDriverSharedMemory(WVB_SERVER_DRIVER_MUTEX_NAME, WVB_SERVER_DRIVER_MEMORY_NAME);
        {
            auto lock          = m_data->shared_memory.lock();
            lock->server_state = ServerState::AWAITING_CONNECTION;
        }
        m_data->server_events.server_state_changed.signal();

        // Check if driver is connected
        m_data->handle_driver_state_changed();

        // Log
        std::cout << "Server started.\nAwaiting connection from client..." << std::endl;
    }

    Server::~Server()
    {
        if (m_data != nullptr)
        {
            {
                auto lock          = m_data->shared_memory.lock();
                lock->server_state = ServerState::NOT_RUNNING;
            }
            m_data->server_events.server_state_changed.signal();

            delete m_data;
            m_data = nullptr;
        }
    }

    void Server::run()
    {
        // Main event
        bool running = true;
        while (running)
        {
            // Poll driver
            DriverEvent driver_event = DriverEvent::NO_EVENT;
            while (m_data->driver_events.poll(driver_event))
            {
                switch (driver_event)
                {
                    case DriverEvent::DRIVER_STATE_CHANGED: m_data->handle_driver_state_changed(); break;
                    default: break;
                }
            }

            // Poll client events

            // If no app is running, sleep to avoid wasting CPU
            if (m_data->app_state == AppState::NOT_READY)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

} // namespace wvb::server