
#define GROUP_SIZE 16

// Input texture, RGBA
Texture2D<float4> inputTexture : register(t0);

// Output texture, BGRA
RWTexture2D<float4>  outputTexture : register(u0);

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

    // Convert to BGRA
    outputTexture[thread_id.xy] = float4(rgba.z, rgba.y, rgba.x, rgba.w);
}