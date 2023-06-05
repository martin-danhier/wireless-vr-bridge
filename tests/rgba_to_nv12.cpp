#include <test_framework.hpp>

// Since we use DirectX, sadly only support Windows
#ifdef WIN32
#include <wvb_common/dx11_utils/rgba_to_nv12.h>

#undef ERROR

#include <wvb_common/vr_structs.h>

#include <algorithm>
#include <d3d11.h>
#include <stb_image.h>
#include <stb_image_write.h>

#define SHADER_DIR_PATH "../wvb_server/shaders/"

TEST
{
    // Load test image (RGBA)
    int      width, height, n_channels;
    uint8_t *src_img = stbi_load("resources/frame_1000_rgba.png", &width, &height, &n_channels, 4);
    ASSERT_NOT_NULL(src_img);
    const wvb::Extent2D extent {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

    // region Setup D3D11 to simulate received frame

    // Create D3D11 context
    ID3D11Device        *device;
    ID3D11DeviceContext *device_context;
    D3D_FEATURE_LEVEL    feature_level;
    auto                 hr = D3D11CreateDevice(nullptr,
                                D3D_DRIVER_TYPE_HARDWARE,
                                nullptr,
                                0,
                                nullptr,
                                0,
                                D3D11_SDK_VERSION,
                                &device,
                                &feature_level,
                                &device_context);
    ASSERT_TRUE(SUCCEEDED(hr));
    ASSERT_EQ(feature_level, D3D_FEATURE_LEVEL_11_0);

    // Create staging texture for input (CPU->GPU)
    D3D11_TEXTURE2D_DESC staging_desc;
    staging_desc.Width              = width;
    staging_desc.Height             = height;
    staging_desc.MipLevels          = 1;
    staging_desc.ArraySize          = 1;
    staging_desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
    staging_desc.SampleDesc.Count   = 1;
    staging_desc.SampleDesc.Quality = 0;
    staging_desc.Usage              = D3D11_USAGE_STAGING;
    staging_desc.BindFlags          = 0;
    staging_desc.CPUAccessFlags     = D3D11_CPU_ACCESS_WRITE;
    staging_desc.MiscFlags          = 0;

    ID3D11Texture2D *staging_texture_in;
    hr = device->CreateTexture2D(&staging_desc, nullptr, &staging_texture_in);
    ASSERT_TRUE(SUCCEEDED(hr));

    // Create GPU only texture for RGBA
    D3D11_TEXTURE2D_DESC gpu_texture_desc;
    gpu_texture_desc.Width              = width;
    gpu_texture_desc.Height             = height;
    gpu_texture_desc.MipLevels          = 1;
    gpu_texture_desc.ArraySize          = 1;
    gpu_texture_desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
    gpu_texture_desc.SampleDesc.Count   = 1;
    gpu_texture_desc.SampleDesc.Quality = 0;
    gpu_texture_desc.Usage              = D3D11_USAGE_DEFAULT;
    gpu_texture_desc.BindFlags          = D3D11_BIND_SHADER_RESOURCE;
    gpu_texture_desc.CPUAccessFlags     = 0;
    gpu_texture_desc.MiscFlags          = 0;

    ID3D11Texture2D *gpu_texture_rgba;
    hr = device->CreateTexture2D(&gpu_texture_desc, nullptr, &gpu_texture_rgba);
    ASSERT_TRUE(SUCCEEDED(hr));

    // Create GPU only texture for NV12
    gpu_texture_desc.Format = DXGI_FORMAT_NV12;
    gpu_texture_desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    ID3D11Texture2D *gpu_texture_nv12 = nullptr;
    hr                                = device->CreateTexture2D(&gpu_texture_desc, nullptr, &gpu_texture_nv12);

    // Create staging texture for output (GPU->CPU)
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.Format         = DXGI_FORMAT_NV12;
    ID3D11Texture2D *staging_texture_out;
    hr = device->CreateTexture2D(&staging_desc, nullptr, &staging_texture_out);
    ASSERT_TRUE(SUCCEEDED(hr));

    // Create resource view for input
    ID3D11ShaderResourceView *gpu_rgba_srv;
    hr = device->CreateShaderResourceView(gpu_texture_rgba, nullptr, &gpu_rgba_srv);
    ASSERT_TRUE(SUCCEEDED(hr));

    // Copy data from CPU to GPU
    D3D11_MAPPED_SUBRESOURCE mapped_resource;
    hr = device_context->Map(staging_texture_in, 0, D3D11_MAP_WRITE, 0, &mapped_resource);
    ASSERT_TRUE(SUCCEEDED(hr));

    auto src_pitch = width * 4;
    if (src_pitch == mapped_resource.RowPitch)
    {
        // No padding, we can copy the whole image at once
        memcpy(mapped_resource.pData, src_img, width * height * 4);
    }
    else
    {
        // There is a padding, we need to copy line by line
        for (int y = 0; y < height; ++y)
        {
            memcpy((uint8_t *) mapped_resource.pData + y * mapped_resource.RowPitch, src_img + y * src_pitch, src_pitch);
        }
    }

    device_context->Unmap(staging_texture_in, 0);

    // Copy data from GPU to GPU
    device_context->CopyResource(gpu_texture_rgba, staging_texture_in);

    // endregion

    // gpu_texture_rgba now contains the RGBA frame in GPU memory, as if it was received from SteamVR.

    // Create converter
    wvb::RgbaToNv12Converter converter(extent, device, SHADER_DIR_PATH, gpu_texture_nv12);
    converter.update_size(width, height, device_context);

    // Run shader
    converter.convert(gpu_rgba_srv, device_context, extent, gpu_texture_nv12);

    // region Download texture

    // Copy GPU texture to staging texture
    device_context->CopyResource(staging_texture_out, gpu_texture_nv12);
    // Map staging texture
    D3D11_MAPPED_SUBRESOURCE mapped_staging_texture;
    hr = device_context->Map(staging_texture_out, 0, D3D11_MAP_READ, 0, &mapped_staging_texture);
    ASSERT_TRUE(SUCCEEDED(hr));

    // Copy data to output buffer
    auto *staging_texture_data = (uint8_t *) mapped_staging_texture.pData;

    // Save Y plane as grayscale image with stbi

    stbi_write_png("out_y.png", width, height, 1, staging_texture_data, static_cast<int>(mapped_staging_texture.RowPitch));

    // Save UV plane as R8G8 image with stbi
    stbi_write_png("out_uv.png",
                   width / 2,
                   height / 2,
                   2,
                   staging_texture_data + height * mapped_staging_texture.RowPitch,
                   static_cast<int>(mapped_staging_texture.RowPitch));

    // Unmap staging texture
    device_context->Unmap(staging_texture_out, 0);

    // endregion

    // Cleanup
    converter.release();
    gpu_rgba_srv->Release();
    gpu_texture_rgba->Release();
    staging_texture_in->Release();
    staging_texture_out->Release();
    device_context->Release();
    device->Release();
}

#else
TEST {}
#endif