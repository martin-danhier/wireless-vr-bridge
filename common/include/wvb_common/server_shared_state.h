/**
 * Shared state between the server and the driver.
 */

#pragma once

#include <wvb_common/ipc.h>

namespace wvb
{

#define WVB_SERVER_DRIVER_MEMORY_NAME "WVB_SERVER_DRIVER_MEMORY"
#define WVB_SERVER_DRIVER_MUTEX_NAME  "WVB_SERVER_DRIVER_MUTEX"

    enum ServerState : uint32_t
    {
        /** The server app is entirely stopped. */
        NOT_RUNNING = 0,
        /** The server app is started and listening for a client. */
        AWAITING_CONNECTION = 1,
        /** The server app is connected to a client and has set the VR system specs in the shared memory. It is ready for the driver to
           start. */
        READY = 2,
        /** The server is running */
        RUNNING = 3,
    };

    enum class DriverState : uint32_t
    {
        /** The driver is entirely stopped. */
        NOT_RUNNING = 0,
        /** The driver is started and waiting for the server to set the VR system specs in the shared memory. */
        AWAITING_SERVER = 2,
        /** The driver is set up, but no VR content is currently running. */
        IDLING = 3,
        /** The driver is running, actively receiving frames from SteamVR */
        RUNNING = 4,
    };

    struct ServerDriverSharedData
    {
        ServerState server_state = ServerState::NOT_RUNNING;
        DriverState driver_state = DriverState::NOT_RUNNING;
    };

    typedef SharedMemory<ServerDriverSharedData> ServerDriverSharedMemory;
} // namespace wvb