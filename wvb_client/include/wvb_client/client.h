#pragma once

#include <wvb_common/macros.h>
#include <wvb_common/socket_addr.h>
#include <wvb_common/vr_structs.h>
#include <wvb_client/structs.h>
#include <cstdint>
#include <vector>


namespace wvb::client {

    /**
     * The client is located on the wireless headset. It connects to the server, receives output data (image, audio, etc.) and sends
     * input data (controller, etc.).
     */
    class Client {
        PIMPL_CLASS(Client);

    public:
        explicit Client();

        bool init(const ApplicationInfo &app_info);
        void shutdown();
        // Returns false if the app should exit
        bool update();

        /** Returns the list of servers that sent valid advertisements. */
        [[nodiscard]] const std::vector<VRCPServerCandidate> &available_servers() const;
        void connect(const SocketAddr &addr);

        [[nodiscard]] bool is_connected() const;
    };
} // namespace wvb::client