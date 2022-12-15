#pragma once

#include <cstdint>

namespace wvb::server
{
    enum DriverConnectionState : uint32_t
    {
        /** The driver is not connected. The server is waiting for it to launch. */
        AWAITING_DRIVER = 0,
        /** The driver is connected. */
        DRIVER_CONNECTED = 1,
    };

    enum ClientConnectionState : uint32_t
    {
        /** The client is not connected. The server is waiting for it to connect. */
        AWAITING_CLIENT = 0,
        /** The client is connected. */
        CLIENT_CONNECTED = 1,
    };

    enum AppState : uint32_t
    {
        /** The server is not ready to run an app, typically because the client and driver are not both connected. */
        NOT_READY = 0,
        /** The server is ready to run an app, but is currently idling. */
        READY = 1,
        /** The server is currently running an app. */
        RUNNING = 1,
        /** Same as READY, except that a running app is paused. It can be resumed without restarting. */
        STANDBY = 2,
    };

    /**
     * The server is responsible for managing the connection to the client VR system.
     *
     * It is the central element of the host application.
     *
     * Its main responsibilities are:
     * - Connecting to the client VR system and retrieve device specifications / watch for device changes
     * - On connection, signal the driver to init a virtual device and provide the necessary device specifications
     * - During an application main loop, coordinate the two-way streaming of data between the client and the server
     */
    class Server
    {
      private:
        struct Data;
        Data *m_data = nullptr;

      public:
        explicit Server();
        Server(const Server &other) = delete;
        ~Server();

        /**
         * Start the server state machine.
         * */
        void run();

    };
} // namespace wvb::server