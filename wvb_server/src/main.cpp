#include <wvb_server/server.h>

using namespace wvb::server;

// Entry point of the server app
int main()
{
    Server server;

    server.run();

    return 0;
}