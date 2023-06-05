#include "wvb_driver/driver_logger.h"

#include <cstdarg>
#include <cstring>
#include <openvr_driver.h>

namespace wvb::driver
{
    void log_inner(vr::IVRDriverLog *log_file, const char *prefix, const char *format, va_list args)
    {
        char buffer[1024];

        // Add prefix at the beginning of the message
        sprintf(buffer, "%s", prefix);

        // Add the actual message
        vsprintf(buffer + strlen(prefix), format, args);

        // Log
        if (log_file != nullptr)
        {
            log_file->Log(buffer);
        }
    }

    void DriverLogger::log(const char *format, ...)
    {
        va_list args;
        va_start(args, format);

        log_inner(m_log_file, "", format, args);

        va_end(args);
    }

    void DriverLogger::debug_log(const char *format, ...)
    {
#ifndef NDEBUG
        va_list args;
        va_start(args, format);

        log_inner(m_log_file, "[DEBUG] ", format, args);

        va_end(args);
#endif
    }

} // namespace wvb::server
