#ifndef ANDROID

#include "wvb_common/io.h"

#include <fstream>

namespace wvb
{
    struct IO::Data
    {
    };

    IO::IO(void *asset_manager) : m_data(new Data) {}

    DEFAULT_PIMPL_DESTRUCTOR(IO)

    void IO::read_file(const char *path, IOBuffer &out_buffer) const
    {
        std::string actual_path = "";
        // If the path is relative, look in the assets directory
        if (path[0] == '/' || path[1] == ':')
        {
            actual_path = path;
        }
        else
        {
            actual_path = "../../assets/" + std::string(path);
        }

        // Get error
        std::ifstream file;
        auto          exceptions = file.exceptions() | std::ios::failbit;
        file.exceptions(exceptions);

        try
        {
            file.open(actual_path, std::ios::binary);
            if (!file.is_open())
            {
                LOGE("Failed to open file %s\n", actual_path.c_str());
                return;
            }

            file.seekg(0, std::ios::end);
            out_buffer.size = file.tellg();
            file.seekg(0, std::ios::beg);

            if (out_buffer.size == 0)
            {
                LOGE("File %s is empty\n", path);
                return;
            }

            out_buffer.data = reinterpret_cast<uint8_t *>(malloc(out_buffer.size + 1));
            file.read((char *) out_buffer.data, out_buffer.size);

            file.close();

            out_buffer.data[out_buffer.size] = '\0';
        }
        catch (std::ifstream::failure &e)
        {
            LOGE("Failed to read file %s with error %d\n", actual_path.c_str(), e.code().value());
        }
    }

    IOBuffer::~IOBuffer()
    {
        if (data != nullptr)
        {
            free(data);
            // data = nullptr;
        }
        size = 0;
    }
} // namespace wvb
#endif