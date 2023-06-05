#pragma once
#ifdef _WIN32

#include <cstdint>

// Forward declarations of DirectX classes
typedef struct ID3D11Texture2D           ID3D11Texture2D;
typedef struct ID3D11Device              ID3D11Device;
typedef struct ID3D11DeviceContext       ID3D11DeviceContext;
typedef struct ID3D11ShaderResourceView  ID3D11ShaderResourceView;
typedef struct ID3D11UnorderedAccessView ID3D11UnorderedAccessView;
typedef struct ID3D11Buffer              ID3D11Buffer;
typedef struct ID3D11ComputeShader       ID3D11ComputeShader;

namespace wvb
{

#define DEFAULT_THREAD_GROUP_SIZE 16
/**
 * Compute the number of threads needed to process a dimension. If the dimension is not a multiple of the group size, an extra thread
 * group is added. The shader should check if a pixel is outside of the image bounds.
 */
#define NB_THREADS_2(dimension, group_size) (((dimension) + (group_size) - 1) / (group_size))
#define NB_THREADS(dimension) NB_THREADS_2((dimension), DEFAULT_THREAD_GROUP_SIZE)
#define ALIGNED               __declspec(align(16))

    /** Represents a 2D size as a shader-friendly structure. */
    ALIGNED struct CBufferExtent2D
    {
        uint32_t width;
        uint32_t height;
    };
} // namespace wvb::server
#endif