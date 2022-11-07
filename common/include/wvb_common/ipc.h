/**
 * Set of tools to manage cross-platform Inter Process Communication.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace wvb
{
    // Inspired by https://github.com/ValveSoftware/virtual_display/blob/master/shared/sharedstate.h, but adapted to be
    // cross-platform.

    namespace _impl
    {
        /** Inner class for SharedData. Implementation is platform-dependant since we need OS-specific functions to create
         * shared memory. */
        class SharedMemoryImpl
        {
          private:
            // Define platform-dependant data in source file
            struct Data;
            Data *m_data = nullptr;

          public:
            SharedMemoryImpl() = default;
            explicit SharedMemoryImpl(size_t size, const char *mutex_name, const char *memory_name);
            SharedMemoryImpl(const SharedMemoryImpl &other) = delete;
            SharedMemoryImpl(SharedMemoryImpl &&other) noexcept: m_data(other.m_data) { other.m_data = nullptr; }
            ~SharedMemoryImpl();

            SharedMemoryImpl &operator=(const SharedMemoryImpl &other) = delete;
            SharedMemoryImpl &operator=(SharedMemoryImpl &&other) noexcept
            {
                if (this != &other)
                {
                    this->~SharedMemoryImpl();
                    m_data = other.m_data;
                    other.m_data = nullptr;
                }
                return *this;
            }

            [[nodiscard]] inline bool is_valid() const { return m_data != nullptr; }

            /** Waits for the mutex, locks it and returns a pointer to the data */
            [[nodiscard]] void *unsafe_lock(uint32_t timeout_ms) const;

            /** Unlocks the mutex. Unsafe if the pointer is used afterwards. */
            void unsafe_release() const;

        };
    } // namespace _impl

    template<typename T>
    class SharedMemory;

    /** Smart pointer to locked data. Frees the lock when destroyed. */
    template<typename T>
    class LockedDataPtr
    {
      private:
        T                             *m_data        = nullptr;
        const _impl::SharedMemoryImpl *m_shared_data = nullptr;

        // Only the SharedMemory class can create this object
        friend class SharedMemory<T>;

        explicit LockedDataPtr(_impl::SharedMemoryImpl *shared_data, uint32_t timeout_ms) : m_shared_data(shared_data)
        {
            m_data = static_cast<T *>(shared_data->unsafe_lock(timeout_ms));
        }

      public:
        LockedDataPtr(const LockedDataPtr &other) = delete;
        LockedDataPtr(LockedDataPtr &&other) noexcept : m_data(other.m_data), m_shared_data(other.m_shared_data)
        {
            other.m_data        = nullptr;
            other.m_shared_data = nullptr;
        }
        ~LockedDataPtr()
        {
            if (m_data != nullptr)
            {
                m_shared_data->unsafe_release();
            }
            m_data        = nullptr;
            m_shared_data = nullptr;
        }

        [[nodiscard]] inline bool is_valid() const { return m_data != nullptr; }

        [[nodiscard]] inline T *operator->() const { return m_data; }
        [[nodiscard]] inline T &operator*() const { return *m_data; }
    };

    /** Create platform-dependant shared memory to safely share data between processes. */
    template<typename T>
    class SharedMemory
    {
      private:
        _impl::SharedMemoryImpl m_impl;

      public:
        SharedMemory() = default;
        explicit SharedMemory(const char *mutex_name, const char *memory_name) : m_impl(sizeof(T), mutex_name, memory_name) {}
        SharedMemory(const SharedMemory &other) = delete;
        SharedMemory(SharedMemory &&other) noexcept: m_impl(std::move(other.m_impl)) {
            other.m_impl = _impl::SharedMemoryImpl();
        }
        ~SharedMemory()                         = default;

        SharedMemory &operator=(const SharedMemory &other) = delete;
        SharedMemory &operator=(SharedMemory &&other) noexcept
        {
            if (this != &other)
            {
                // Destroy the current data
                this->~SharedMemory();

                // Move the data
                m_impl = std::move(other.m_impl);
                other.m_impl = _impl::SharedMemoryImpl();
            }

            return *this;
        }

        [[nodiscard]] inline bool is_valid() const { return m_impl.is_valid(); }

        // Smart pointer to lock the mutex
        [[nodiscard]] inline LockedDataPtr<T> lock(uint32_t timeout_ms = UINT32_MAX) { return LockedDataPtr<T>(&m_impl, timeout_ms); }
    };
} // namespace wvb