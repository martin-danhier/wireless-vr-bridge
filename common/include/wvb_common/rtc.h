/**
 * Set of tools to manage real-time communication between the server and the client.
 */

#pragma once

#include "socket_addr.h"

/** When a port is defined as AUTO, a free port will be automatically selected. */
#define PORT_AUTO 0

namespace wvb
{
    /** Wrapper around the RTC library, design for virtual reality streaming. */
    class VRStream
    {
      private:
        struct Data;
        Data *m_data;

      public:

        /**
         * Create a new VRStream.
         * @param local_port The port to listen on.
         * @param remote_addr The address of the remote peer.
         * */
        VRStream(uint16_t local_port, SocketAddr remote_addr);
        ~VRStream();
        VRStream(const VRStream &) = delete;

        [[nodiscard]] inline bool is_valid() const noexcept { return m_data != nullptr; }
    };

} // namespace wvb