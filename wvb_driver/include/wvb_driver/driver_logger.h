#pragma once

// Forward declarations
namespace vr
{
    class IVRDriverLog;
}

namespace wvb::driver
{
    class DriverLogger
    {
      private:
        vr::IVRDriverLog *m_log_file = nullptr;

      public:
        explicit DriverLogger(vr::IVRDriverLog *log_file) : m_log_file(log_file) {}

        void log(const char *format, ...);
        void debug_log(const char *format, ...);
        [[nodiscard]] inline bool is_valid() const { return m_log_file != nullptr; }
    };

} // namespace wvb::server