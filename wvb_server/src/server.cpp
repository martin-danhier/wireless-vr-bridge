#include "wvb_server/server.h"

#include <wvb_common/server_shared_state.h>
#include <iostream>

namespace wvb::server
{
    // =======================================================================================
    // =                                       Structs                                       =
    // =======================================================================================

    struct Server::Data
    {
        uint16_t port;
        ServerDriverSharedMemory shared_memory;
    };

    // =======================================================================================
    // =                                   Implementation                                    =
    // =======================================================================================

    Server::Server(uint16_t port) : m_data(new Data {port}) {
        m_data->shared_memory = ServerDriverSharedMemory(WVB_SERVER_DRIVER_MUTEX_NAME, WVB_SERVER_DRIVER_MEMORY_NAME);
        {
            auto lock = m_data->shared_memory.lock();
            lock->server_state = ServerState::AWAITING_CONNECTION;
        }
    }

    Server::~Server()
    {
        if (m_data != nullptr)
        {
            {
                auto lock = m_data->shared_memory.lock();
                lock->server_state = ServerState::NOT_RUNNING;
            }

            delete m_data;
            m_data = nullptr;
        }
    }

    void Server::run() {
        bool driver_ready = false;
        while (!driver_ready) {
            // Check if the driver is ready
            {
                auto lock = m_data->shared_memory.lock();
                if (lock->driver_state == DriverState::AWAITING_SERVER) {
                    driver_ready = true;
                }
            }

            if (driver_ready) {
                std::cout << "The driver is ready!" << std::endl;
            }
        }
    }
} // namespace wvb::server