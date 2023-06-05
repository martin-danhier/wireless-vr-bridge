
#define GROUP_SIZE 16

// Input texture, RGBA
Texture2D<float4> inputTexture : register(t0);

// Output texture, NV12 (planar YUV 4:2:0, Y plane followed by interleaved UV plane)
//
// YYYYYYYYYYYY
// YYYYYYYYYYYY
// UVUVUVUVUVUV
RWTexture2D<float>  outputTextureY : register(u0);  // Y, 8-bit
RWTexture2D<float2> outputTextureUV : register(u1); // UV, 16-bit

// Resolution
cbuffer cb0 : register(b0)
{
    // Source resolution
    uint src_width;
    uint src_height;
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
    // Crop to source resolution
    if (thread_id.x >= src_width || thread_id.y >= src_height)
        return;

    float4 rgba = inputTexture[thread_id.xy];

    // Compute Y
    float y = 0.2126f * rgba.r + 0.7152f * rgba.g + 0.0722f * rgba.b;
    outputTextureY[thread_id.xy] = y;

    // Write UV for odd even and columns
    if ((thread_id.x & 1) == 0 && (thread_id.y & 1) == 0) {
        float u = -0.09991f * rgba.r - 0.33609f * rgba.g + 0.436f   * rgba.b + 0.5f;
        float v =  0.615f   * rgba.r - 0.55861f * rgba.g - 0.05639f * rgba.b + 0.5f;

        outputTextureUV[thread_id.xy / 2] = float2(u, v);
    }
}