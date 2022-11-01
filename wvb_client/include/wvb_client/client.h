#pragma once

#include <cstdint>

namespace wvb::client
{
    /**
     * The client is located on the wireless headset. It connects to the server, receives output data (image, audio, etc.) and sends
     * input data (controller, etc.).
     */
    class Client
    {
      private:
        struct Data;
        Data *m_data = nullptr;

      public:
        explicit Client(uint16_t port = 5591);
        Client(const Client &other) = delete;
        ~Client();
    };
} // namespace wvb::client