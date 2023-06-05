#ifdef _WIN32

#include <wvb_common/subprocess.h>

#include <windows.h>

namespace wvb
{
    struct Subprocess::Data
    {
        std::string path              = "";
        std::string working_directory = "";

        HANDLE process_handle = nullptr;
        HANDLE thread_handle  = nullptr;
        DWORD  process_id     = 0;
        DWORD  thread_id      = 0;
    };

    Subprocess::Subprocess(const std::string &path, const std::string &working_directory)
        : m_data(new Data {
            .path              = path,
            .working_directory = working_directory,
        })
    {
    }

    Subprocess::~Subprocess()
    {
        if (m_data)
        {
            stop();

            delete m_data;
            m_data = nullptr;
        }
    }

    bool Subprocess::start()
    {
        if (m_data->process_handle)
        {
            // Already started: exit if not done and cleanup
            stop();
        }

        STARTUPINFOA startup_info = {0};
        startup_info.cb           = sizeof(startup_info);

        PROCESS_INFORMATION process_info = {0};

        auto ret = CreateProcessA(m_data->path.c_str(),
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  false,
                                  0,
                                  nullptr,
                                  m_data->working_directory.c_str(),
                                  &startup_info,
                                  &process_info);
        if (!ret)
        {
            auto error = GetLastError();
            // Print error
            LOGE("Failed to create process: \"%s\" with error code: %d\n", m_data->path.c_str(), error);
            return false;
        }

        m_data->process_handle = process_info.hProcess;
        m_data->thread_handle  = process_info.hThread;
        m_data->process_id     = process_info.dwProcessId;
        m_data->thread_id      = process_info.dwThreadId;

        return true;
    }

    void Subprocess::kill()
    {
        if (!m_data->process_handle)
        {
            return;
        }

        if (!TerminateProcess(m_data->process_handle, 0))
        {
            return;
        }

        CloseHandle(m_data->process_handle);
        CloseHandle(m_data->thread_handle);

        m_data->process_handle = nullptr;
        m_data->thread_handle  = nullptr;
        m_data->process_id     = 0;
        m_data->thread_id      = 0;
    }

    void Subprocess::stop(uint32_t timeout_ms, bool send_signal)
    {
        if (!m_data->process_handle)
        {
            return;
        }

        // Send WM_CLOSE to the main window of the process
        if (send_signal)
        {
            HWND hwnd = GetTopWindow(nullptr);
            while (hwnd)
            {
                DWORD process_id = 0;
                GetWindowThreadProcessId(hwnd, &process_id);

                if (process_id == m_data->process_id)
                {
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                    break;
                }

                hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);
            }
        }

        if (WaitForSingleObject(m_data->process_handle, timeout_ms) == WAIT_TIMEOUT)
        {
            kill();
        }

        CloseHandle(m_data->process_handle);
        CloseHandle(m_data->thread_handle);

        m_data->process_handle = nullptr;
        m_data->thread_handle  = nullptr;
        m_data->process_id     = 0;
        m_data->thread_id      = 0;
    }

    bool Subprocess::is_running() const
    {
        if (!m_data || !m_data->process_handle)
        {
            return false;
        }

        DWORD exit_code = 0;
        if (!GetExitCodeProcess(m_data->process_handle, &exit_code))
        {
            return false;
        }

        return exit_code == STILL_ACTIVE;
    }

} // namespace wvb

#endif