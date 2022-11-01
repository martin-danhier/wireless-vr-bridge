#pragma once

#include <cstdint>

namespace wvb::server
{
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
        explicit Server(uint16_t port = 5591);
        Server(const Server &other) = delete;
        ~Server();

        /** Start listening for incoming connections.
         *
         * When a connection is established, the server will try to start the two-way data streaming.
         * */
        void run();

        /** Stop listening for incoming connections. */
        // void stop();
    };
} // namespace wvb::server