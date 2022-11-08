// Implementation is platform-dependant since we need OS-specific functions to create shared memory.
#ifdef _WIN32

#include "wvb_common/ipc.h"

#include <windows.h>

namespace wvb
{
    // =======================================================================================
    // =                                 Structs and classes                                 =
    // =======================================================================================

    struct _impl::SharedMemoryImpl::Data
    {
        HANDLE mutex        = nullptr;
        HANDLE file_mapping = nullptr;
        size_t size         = 0;
        void  *data         = nullptr;
    };

    struct InterProcessEvent::Data
    {
        HANDLE event     = nullptr;
        bool   is_sender = false;
    };

    // =======================================================================================
    // =                                   Implementation                                    =
    // =======================================================================================

    // Shared Memory

    _impl::SharedMemoryImpl::SharedMemoryImpl(size_t size, const char *mutex_name, const char *memory_name) : m_data(new Data)
    {
        // Create mutex
        m_data->mutex = CreateMutexA(nullptr, FALSE, mutex_name);
        // Create the file mapping using the system page file to avoid physical memory usage.
        m_data->file_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, size, memory_name);
        bool created         = GetLastError() != ERROR_ALREADY_EXISTS;

        // If it failed, clean up and return
        if (m_data->file_mapping == nullptr)
        {
            this->~SharedMemoryImpl();
            return;
        }

        // Create view of the whole file mapping
        m_data->size = size;
        m_data->data = MapViewOfFile(m_data->file_mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);

        // If it was just created, zero the memory
        if (created)
        {
            memset(m_data->data, 0, size);
        }
    }

    _impl::SharedMemoryImpl::~SharedMemoryImpl()
    {
        if (m_data != nullptr)
        {
            if (m_data->file_mapping != nullptr)
            {
                CloseHandle(m_data->file_mapping);
                m_data->file_mapping = nullptr;
            }

            if (m_data->mutex != nullptr)
            {
                CloseHandle(m_data->mutex);
                m_data->mutex = nullptr;
            }

            delete m_data;
            m_data = nullptr;
        }
    }

    void *_impl::SharedMemoryImpl::unsafe_lock(uint32_t timeout_ms) const
    {
        if (!is_valid())
        {
            return nullptr;
        }

        // Wait for the mutex
        DWORD result = WaitForSingleObject(m_data->mutex, timeout_ms);

        if (result == WAIT_OBJECT_0)
        {
            // Return the data
            return m_data->data;
        }
        else
        {
            return nullptr;
        }
    }

    void _impl::SharedMemoryImpl::unsafe_release() const
    {
        // Release the mutex
        ReleaseMutex(m_data->mutex);
    }

    // Inter Process Event

    InterProcessEvent::InterProcessEvent(const char *event_name, bool is_sender) : m_data(new Data)
    {
        m_data->is_sender = is_sender;

        // Create the event
        m_data->event = CreateEventA(nullptr, TRUE, FALSE, event_name);

        // If it failed, clean up and return
        if (m_data->event == nullptr)
        {
            this->~InterProcessEvent();
            return;
        }
    }

    InterProcessEvent::~InterProcessEvent()
    {
        if (m_data != nullptr)
        {
            if (m_data->event != nullptr)
            {
                CloseHandle(m_data->event);
                m_data->event = nullptr;
            }

            delete m_data;
            m_data = nullptr;
        }
    }

    void InterProcessEvent::signal() const
    {
        SetEvent(m_data->event);
    }

    bool InterProcessEvent::wait(uint32_t timeout_ms) const
    {
        auto res = WaitForSingleObject(m_data->event, timeout_ms);

        // Return true if the event was triggered
        auto triggered = res == WAIT_OBJECT_0;

        // If we are the sender, reset the event
        if (triggered)
        {
            ResetEvent(m_data->event);
        }

        return triggered;
    }

    bool InterProcessEvent::is_signaled() const
    {
        // Check if the event is triggered, without waiting and without resetting it
        auto res = WaitForSingleObject(m_data->event, 0);

        // Return true if the event was triggered
        return res == WAIT_OBJECT_0;
    }

    void InterProcessEvent::reset() const
    {
        ResetEvent(m_data->event);
    }



} // namespace wvb

#endif