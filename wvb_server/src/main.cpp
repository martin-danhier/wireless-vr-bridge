#include <wvb_server/arg_parser.h>
#include <wvb_server/server.h>
#include <wvb_common/macros.h>

#include <iostream>

using namespace wvb::server;

// Entry point of the server app
int main(int argc, char **argv)
{
    LOG("+---------------------------+\n");
    LOG("| Wireless VR Bridge Server |\n");
    LOG("+---------------------------+\n\n");

    auto args = parse_arguments(argc, argv);
    if (!args.has_value())
    {
        print_usage();
        return EXIT_FAILURE;
    }

    Server server(std::move(args.value()));

    // Run the server
    server.run();
    return EXIT_SUCCESS;
}