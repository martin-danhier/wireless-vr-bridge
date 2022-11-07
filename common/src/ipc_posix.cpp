// Implementation is platform-dependant since we need OS-specific functions to create shared memory.

#ifdef __linux__

#include "wvb_common/ipc.h"

#include <fcntl.h>
#include <iostream>
#include <semaphore.h>
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

    struct SharedMemoryImpl::Data
    {
        int32_t shared_memory_id = INVALID_HANDLE_VALUE;
        sem_t  *semaphore        = SEM_FAILED;
        const char *semaphore_name   = nullptr;
        size_t  size             = 0;
        void   *data             = nullptr;
    };

    // =======================================================================================
    // =                                   Implementation                                    =
    // =======================================================================================

    bool sem_timed_wait(sem_t *sem, uint32_t timeout_ms)
    {
        timespec timeout {};
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += timeout_ms / 1000;
        timeout.tv_nsec += (timeout_ms % 1000) * 1000000;

        // Wait
        return sem_timedwait(sem, &timeout) == 0;
    }

    SharedMemoryImpl::SharedMemoryImpl(size_t size, const char *mutex_name, const char *memory_name) : m_data(new Data)
    {
        // Create semaphore
        m_data->semaphore = sem_open(mutex_name, O_CREAT, 0644, 1);
        m_data->semaphore_name = mutex_name;
        if (m_data->semaphore == SEM_FAILED)
        {
            this->~SharedMemoryImpl();
            return;
        }

        // Ensure that the semaphore is valid
        // It can be invalid in two cases:
        // - it exists and has a non-binary value (e.g 2). Trivial to detect.
        // - it exists and is stucked to 0 (for example if the previous process crashed).
        //      Harder to detect: it could be another valid process that is using it.

        // Stuck detection
        // The idea is to try to lock the semaphore. Since we are in a real time program, well-behaved processes will not lock it for
        // more than a few milliseconds. If we can't lock it in a reasonable amount of time, we assume that the semaphore is stucked,
        // and we can unlock it.

        int32_t value;
        auto    res = sem_getvalue(m_data->semaphore, &value);

        if (res == 0 && value > 1)
        {
            // Lock it until it reaches 0
            while (value > 1)
            {
                sem_wait(m_data->semaphore);
                sem_getvalue(m_data->semaphore, &value);
            }
        }
        else if (value == 0)
        {
            // If the value is 0, either another process is using the lock and will release it soon, or it is stucked.
            // Try to lock it with a timeout
            // If the active process releases it, it will lock successfully. We can then unlock it. (1 -> 0 -> 1)
            // If the semaphore is stucked, it will time out. We thus need to unlock it.            (0 -> 1)
            sem_timed_wait(m_data->semaphore, 100);
            sem_post(m_data->semaphore);
        }

        sem_getvalue(m_data->semaphore, &value);

        // Convert name to key
        key_t shared_memory_key = ftok(memory_name, 1);

        // Create shared memory
        m_data->shared_memory_id = shmget(shared_memory_key, size, IPC_CREAT | 0666);
        if (m_data->shared_memory_id == INVALID_HANDLE_VALUE)
        {
            this->~SharedMemoryImpl();
            return;
        }

        // Attach to shared memory
        m_data->size = size;
        m_data->data = shmat(m_data->shared_memory_id, nullptr, 0);
    }

    SharedMemoryImpl::~SharedMemoryImpl()
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

            if (m_data->semaphore != SEM_FAILED)
            {
                sem_close(m_data->semaphore);
                sem_unlink(m_data->semaphore_name);
                m_data->semaphore = SEM_FAILED;
            }

            delete m_data;
            m_data = nullptr;
        }
    }

    void *SharedMemoryImpl::unsafe_lock(uint32_t timeout_ms) const
    {
        if (m_data != nullptr)
        {
            // Unlimited wait
            if (timeout_ms == UINT32_MAX)
            {
                bool wait = true;

                int32_t result = 0;
                while (wait)
                {
                    result = sem_wait(m_data->semaphore);

                    // Try again if it is interrupted
                    if (result != EINTR)
                    {
                        wait = false;
                    }
                }

                if (result == 0)
                {
                    return m_data->data;
                }
            }
            // Timed wait
            else
            {
                if (sem_timed_wait(m_data->semaphore, timeout_ms))
                {
                    return m_data->data;
                }
            }
        }
        return nullptr;
    }

    void SharedMemoryImpl::unsafe_release() const
    {
        if (m_data == nullptr)
        {
            return;
        }

        sem_post(m_data->semaphore);
    }

} // namespace wvb::_impl

#endif