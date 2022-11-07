#include <wvb_server/server.h>

#include <iostream>

using namespace wvb::server;

// Entry point of the server app
int main()
{
    Server server;

    // Run the server
    server.run();



    return 0;
}