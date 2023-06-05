#pragma once

#include "macros.h"

#include <string>

#define WVB_SUBPROCESS_TIMEOUT_MS 1000

namespace wvb
{

    /** Wrapper around OS-specific subprocess management. */
    class Subprocess
    {
        PIMPL_CLASS(Subprocess);

      public:
        Subprocess() = default;
        /** Creates the context for managing this subprocess. Doesn't start it yet. */
        Subprocess(const std::string &executable_path, const std::string &working_directory);

        /** Starts the subprocess. */
        bool start();

        /** Gracefully stops the subprocess. */
        void stop(uint32_t timeout_ms = WVB_SUBPROCESS_TIMEOUT_MS, bool send_signal = false);

        void kill();

        [[nodiscard]] bool is_running() const;
    };
} // namespace wvb