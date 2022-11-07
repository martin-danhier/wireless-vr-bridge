// Implementation is platform-dependant since we need OS-specific functions to create shared memory.
#ifdef _WIN32

#include "wvb_common/ipc.h"

#include <windows.h>

namespace wvb::_impl
{
    // =======================================================================================
    // =                                 Structs and classes                                 =
    // =======================================================================================

    struct SharedMemoryImpl::Data
    {
        HANDLE mutex        = nullptr;
        HANDLE file_mapping = nullptr;
        size_t size         = 0;
        void  *data         = nullptr;
    };

    // =======================================================================================
    // =                                   Implementation                                    =
    // =======================================================================================

    SharedMemoryImpl::SharedMemoryImpl(size_t size, const char *mutex_name, const char *memory_name) : m_data(new Data)
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

    SharedMemoryImpl::~SharedMemoryImpl()
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

    void *SharedMemoryImpl::unsafe_lock(uint32_t timeout_ms) const
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

    void SharedMemoryImpl::unsafe_release() const
    {
        // Release the mutex
        ReleaseMutex(m_data->mutex);
    }

} // namespace wvb::_impl

#endif