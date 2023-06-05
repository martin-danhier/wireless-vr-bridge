#pragma once
#ifdef _WIN32
#include <wvb_common/dx11_utils/dx11_common_utils.h>

#include <cstdint>

namespace wvb
{
    struct Extent2D;

    /**
     * Handler class for the RGBA to NV12 conversion shader.
     */
    class RgbaToNv12Converter
    {
      private:
        // Input
        ID3D11Buffer *m_cbuffer = nullptr;
        // Output
        ID3D11UnorderedAccessView *m_nv12_gpu_uavs[2] = {nullptr, nullptr};
        // Shader
        ID3D11ComputeShader *m_compute_shader = nullptr;

      public:
        RgbaToNv12Converter(Extent2D src_max_size, ID3D11Device *device, const char *shader_dir_path, ID3D11Texture2D *dst_texture);
        RgbaToNv12Converter() = default;
        RgbaToNv12Converter(const RgbaToNv12Converter &) = delete;
        ~RgbaToNv12Converter();

        void init (Extent2D src_max_size, ID3D11Device *device, const char *shader_dir_path, ID3D11Texture2D *dst_texture);

        bool                           update_size(uint32_t width, uint32_t height, ID3D11DeviceContext *device_context);
        void convert(ID3D11ShaderResourceView *m_rgba_gpu_srv,
                                               ID3D11DeviceContext      *device_context,
                                               Extent2D                  src_size,
                                               ID3D11Texture2D *dst_texture);
        void                           release();
    };
} // namespace wvb::server

#endif