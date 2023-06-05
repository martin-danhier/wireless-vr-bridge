#ifdef _WIN32
#include "wvb_common/dx11_utils/rgba_to_nv12.h"

#include <wvb_common/macros.h>
#include <wvb_common/vr_structs.h>

#include <d3d11.h>
#include <fstream>
#include <stdexcept>


#define SHADER_BINARY_FILE_NAME "rgba_to_nv12.comp.cso"

namespace wvb
{
    void RgbaToNv12Converter::init(Extent2D         src_max_size,
                                   ID3D11Device    *device,
                                   const char      *shader_dir_path,
                                   ID3D11Texture2D *dst_texture)
    {
        // Width and height must be a multiple of 2
        if (src_max_size.width % 2 != 0 || src_max_size.height % 2 != 0)
        {
            throw std::runtime_error("Width and height must be a multiple of 2");
        }

        // Create UAVs for output NV12
        D3D11_UNORDERED_ACCESS_VIEW_DESC nv12_uav_desc = {
            .Format        = DXGI_FORMAT_R8_UNORM, // Y plane
            .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
            .Texture2D =
                {
                    .MipSlice = 0,
                },
        };
        HRESULT res = device->CreateUnorderedAccessView(dst_texture, &nv12_uav_desc, &m_nv12_gpu_uavs[0]);
        if (FAILED(res))
        {
            release();
            throw std::runtime_error("Failed to create NV12 GPU UAV for Y plane");
        }

        nv12_uav_desc.Format = DXGI_FORMAT_R8G8_UNORM; // UV plane
        res                  = device->CreateUnorderedAccessView(dst_texture, &nv12_uav_desc, &m_nv12_gpu_uavs[1]);
        if (FAILED(res))
        {
            release();
            throw std::runtime_error("Failed to create NV12 GPU UAV for UV plane");
        }

        // Create cbuffer
        D3D11_BUFFER_DESC cbuffer_desc = {
            .ByteWidth           = sizeof(CBufferExtent2D),
            .Usage               = D3D11_USAGE_DYNAMIC,
            .BindFlags           = D3D11_BIND_CONSTANT_BUFFER,
            .CPUAccessFlags      = D3D11_CPU_ACCESS_WRITE,
            .MiscFlags           = 0,
            .StructureByteStride = 0,
        };
        res = device->CreateBuffer(&cbuffer_desc, nullptr, &m_cbuffer);
        if (FAILED(res))
        {
            release();
            throw std::runtime_error("Failed to create cbuffer");
        }

        // Load shader binary
        std::string shader_binary_path = shader_dir_path;
        shader_binary_path += SHADER_BINARY_FILE_NAME;
        std::ifstream shader_file(shader_binary_path, std::ios::binary);
        if (!shader_file.is_open())
        {
            release();

            // Try to move to the parent directory
            shader_binary_path = "../";
            shader_binary_path += shader_dir_path;
            shader_binary_path += SHADER_BINARY_FILE_NAME;

            shader_file.open(shader_binary_path, std::ios::binary);

            if (!shader_file.is_open())
            {
                LOGE("Failed to open shader binary: %s", shader_binary_path.c_str());
                throw std::runtime_error("Failed to open shader binary");
            }
        }

        shader_file.seekg(0, std::ios::end);
        size_t shader_size = shader_file.tellg();
        shader_file.seekg(0, std::ios::beg);
        auto *shader_binary = new uint8_t[shader_size];
        shader_file.read((char *) shader_binary, static_cast<std::streamsize>(shader_size));
        shader_file.close();

        // Create shader
        res = device->CreateComputeShader(shader_binary, shader_size, nullptr, &m_compute_shader);
        delete[] shader_binary;
        if (FAILED(res))
        {
            release();
            throw std::runtime_error("Failed to create compute shader");
        }
    }

    RgbaToNv12Converter::RgbaToNv12Converter(Extent2D         src_max_size,
                                             ID3D11Device    *device,
                                             const char      *shader_dir_path,
                                             ID3D11Texture2D *dst_texture)
    {
        init(src_max_size, device, shader_dir_path, dst_texture);
    }

    bool RgbaToNv12Converter::update_size(uint32_t width, uint32_t height, ID3D11DeviceContext *device_context)
    {
        if (m_cbuffer == nullptr)
        {
            return false;
        }

        // Map
        D3D11_MAPPED_SUBRESOURCE mapped_resource;
        HRESULT                  res = device_context->Map(m_cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_resource);
        if (FAILED(res))
        {
            return false;
        }

        // Update
        auto *cbuffer_data   = (CBufferExtent2D *) mapped_resource.pData;
        cbuffer_data->width  = width;
        cbuffer_data->height = height;

        // Unmap
        device_context->Unmap(m_cbuffer, 0);

        return true;
    }

    void RgbaToNv12Converter::convert(ID3D11ShaderResourceView *m_rgba_gpu_srv,
                                      ID3D11DeviceContext      *device_context,
                                      Extent2D                  src_size,
                                      ID3D11Texture2D          *dst_texture)
    {
        device_context->CSSetShader(m_compute_shader, nullptr, 0);
        device_context->CSSetConstantBuffers(0, 1, &m_cbuffer);
        device_context->CSSetShaderResources(0, 1, &m_rgba_gpu_srv);
        device_context->CSSetUnorderedAccessViews(0, 2, m_nv12_gpu_uavs, nullptr);

        // Run shader
        device_context->Dispatch(NB_THREADS(src_size.width), NB_THREADS(src_size.height), 1);

        // Unbind
        ID3D11ShaderResourceView *null_srv = nullptr;
        device_context->CSSetShaderResources(0, 1, &null_srv);
        ID3D11UnorderedAccessView *null_uavs[2] = {nullptr, nullptr};
        device_context->CSSetUnorderedAccessViews(0, 2, null_uavs, nullptr);
        device_context->CSSetShader(nullptr, nullptr, 0);
    }

    void RgbaToNv12Converter::release()
    {
        // Free used resources
        if (m_compute_shader)
        {
            m_compute_shader->Release();
            m_compute_shader = nullptr;
        }
        if (m_cbuffer)
        {
            m_cbuffer->Release();
            m_cbuffer = nullptr;
        }
        if (m_nv12_gpu_uavs[0])
        {
            m_nv12_gpu_uavs[0]->Release();
            m_nv12_gpu_uavs[0] = nullptr;
        }
        if (m_nv12_gpu_uavs[1])
        {
            m_nv12_gpu_uavs[1]->Release();
            m_nv12_gpu_uavs[1] = nullptr;
        }
    }

    RgbaToNv12Converter::~RgbaToNv12Converter()
    {
        release();
    }

} // namespace wvb
#endif