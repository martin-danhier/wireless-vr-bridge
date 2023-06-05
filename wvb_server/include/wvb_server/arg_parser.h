#pragma once

#include <wvb_common/settings.h>
#include <optional>

namespace wvb::server
{
    /**
     * Processes command line arguments into a settings object.
     * Throws an exception if the arguments are invalid.
    */
    std::optional<AppSettings> parse_arguments(int argc, char **argv);

    /**
     * Prints the usage of the server application.
    */
    void print_usage();

} // namespace wvb::server
