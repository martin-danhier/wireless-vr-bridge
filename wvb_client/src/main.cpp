#include <wvb_client/client.h>

using namespace wvb::client;

enum class State {
    SERVER_SELECTION,
    LOADING,
    STREAM,
};

// Entry point of the client app
int main()
{
    Client client;
    State state = State::SERVER_SELECTION;
    client.init();

    client.system_specs() = {
            .system_name = "Mirror",
            .manufacturer_name = "WVB",
            .eye_resolution = { 1024, 1024 },
            .refresh_rate = {72, 0},
            .ipd = 0.064f,
    };

    bool is_running = true;
    while (is_running) {

        if (state == State::SERVER_SELECTION) {
            // Get the list of servers
            const auto &servers = client.available_servers();
            if (!servers.empty()) {
                // Select the first server
                client.connect(servers[0].addr);
                state = State::LOADING;
            }
        }

        is_running = client.update();
    }

    return 0;
}