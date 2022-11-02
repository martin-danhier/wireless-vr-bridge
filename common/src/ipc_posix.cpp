// Implementation is platform-dependant since we need OS-specific functions to create shared memory.

#ifdef __linux__

#include "wvb_common/ipc.h"

#include <sys/sem.h>
#include <sys/shm.h>

namespace wvb::_impl
{
    // =======================================================================================
    // =                                      Constants                                      =
    // =======================================================================================

#define INVALID_HANDLE_VALUE (-1)

    // =======================================================================================
    // =                                 Structs and classes                                 =
    // =======================================================================================

    struct SharedDataImpl::Data
    {
        int32_t shared_memory_id = INVALID_HANDLE_VALUE;
        int32_t semaphore_id     = INVALID_HANDLE_VALUE;
        size_t  size             = 0;
        void   *data             = nullptr;
    };

    // =======================================================================================
    // =                                   Implementation                                    =
    // =======================================================================================

    SharedDataImpl::SharedDataImpl(size_t size, const char *mutex_name, const char *memory_name) : m_data(new Data)
    {
        // Convert names to key
        key_t semaphore_key     = ftok(mutex_name, 1);
        key_t shared_memory_key = ftok(memory_name, 1);

        // Create semaphore
        m_data->semaphore_id = semget(semaphore_key, 1, IPC_CREAT | 0666);
        if (m_data->semaphore_id == -1)
        {
            this->~SharedDataImpl();
            return;
        }

        // Create shared memory
        m_data->shared_memory_id = shmget(shared_memory_key, size, IPC_CREAT | 0666);
        if (m_data->shared_memory_id == INVALID_HANDLE_VALUE)
        {
            this->~SharedDataImpl();
            return;
        }

        // Attach to shared memory
        m_data->size = size;
        m_data->data = shmat(m_data->shared_memory_id, nullptr, 0);
    }

    SharedDataImpl::~SharedDataImpl()
    {
        if (m_data != nullptr)
        {
            if (m_data->shared_memory_id != -1)
            {
                // Detach from shared memory
                shmdt(m_data->data);
                m_data->data = nullptr;

                shmctl(m_data->shared_memory_id, IPC_RMID, nullptr);
                m_data->shared_memory_id = -1;
            }

            if (m_data->semaphore_id != -1)
            {
                semctl(m_data->semaphore_id, 0, IPC_RMID);
                m_data->semaphore_id = -1;
            }

            delete m_data;
            m_data = nullptr;
        }
    }

    void *SharedDataImpl::unsafe_lock(uint32_t timeout_ms) const
    {
        if (m_data == nullptr)
        {
            return nullptr;
        }

        // Timespec is used to specify timeout
        struct sembuf semaphore_operation
        {
            0, -1, 0
        };
        const struct timespec timeout
        {
            .tv_sec = 0, .tv_nsec = timeout_ms * 1000000
        };
        auto result = semtimedop(m_data->semaphore_id, &semaphore_operation, 1, &timeout);

        // Return the data when the semaphore is acquired
        if (result == 0)
        {
            return m_data->data;
        }
        else {
            return nullptr;
        }
    }

    void SharedDataImpl::unsafe_release() const {
        if (m_data == nullptr)
        {
            return;
        }

        // Release the semaphore
        struct sembuf semaphore_operation
        {
            0, 1, 0
        };
        semop(m_data->semaphore_id, &semaphore_operation, 1);
    }

} // namespace wvb::_impl

#endif