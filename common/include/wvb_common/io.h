#pragma once

#include <wvb_common/macros.h>

#include <cstdint>

namespace wvb
{
    /** Simple wrapper around dynamically allocated buffers, with a destructor automatically handling
     * deallocation.
     */
    struct IOBuffer
    {
        uint8_t *data = nullptr;
        size_t   size = 0;

        IOBuffer() = default;

        IOBuffer(IOBuffer &&other) noexcept : data(other.data), size(other.size)
        {
            other.data = nullptr;
            other.size = 0;
        }
        IOBuffer &operator=(IOBuffer &&other) noexcept
        {
            this->~IOBuffer();

            if (this != &other)
            {
                data       = other.data;
                size       = other.size;
                other.data = nullptr;
                other.size = 0;
            }
            return *this;
        }
        IOBuffer(const IOBuffer &other)            = delete;
        IOBuffer &operator=(const IOBuffer &other) = delete;
        ~IOBuffer();
    };

    /** Wrapper around OS-specific file IO functions. */
    class IO
    {
        PIMPL_CLASS_COPIABLE(IO);

      public:
        /** The asset manager is only used on Android. */
        explicit IO(void *asset_manager = nullptr);

        IO(const IO &other);
        IO &operator=(const IO &other);

        void read_file(const char *path, IOBuffer &out_buffer) const;
    };

} // namespace wvb