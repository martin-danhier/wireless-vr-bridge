#ifdef ANDROID
#include "wvb_common/io.h"
#include <android/asset_manager.h>

namespace wvb
{
    struct IO::Data {
        AAssetManager *asset_manager;
    };

    IO::IO(void *asset_manager) {
        if (asset_manager == nullptr) {
            LOGE("Asset manager is null\n");
            return;
        }

        m_data = new Data;
        m_data->asset_manager = (AAssetManager *) asset_manager;
    }

    DEFAULT_PIMPL_DESTRUCTOR(IO)

    void IO::read_file(const char *path, IOBuffer &out_buffer) const {

        AAsset *asset = AAssetManager_open(m_data->asset_manager, path, AASSET_MODE_BUFFER);
        if (asset == nullptr) {
            LOGE("Failed to open asset %s\n", path);
            return;
        }

        off_t size = AAsset_getLength(asset);
        if (size == 0) {
            LOGE("Asset %s is empty\n", path);
            return;
        }

        // read
        out_buffer.data = new uint8_t[size];
        if (AAsset_read(asset, out_buffer.data, size) != size) {
            LOGE("Failed to read asset %s\n", path);
            return;
        }

        AAsset_close(asset);

        out_buffer.size = size;
    }

    IO::IO(const IO &other): m_data(new Data) {
        m_data->asset_manager = other.m_data->asset_manager;
    }

    IO &IO::operator=(const IO &other) {
        this->~IO();

        m_data = new Data;
        m_data->asset_manager = other.m_data->asset_manager;
        return *this;
    }

    IOBuffer::~IOBuffer() {
        if (data != nullptr) {
            delete[] data;
            data = nullptr;
        }
        size = 0;
    }
}
#endif