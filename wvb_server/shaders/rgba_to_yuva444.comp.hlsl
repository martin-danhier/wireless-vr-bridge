
#define GROUP_SIZE 8

// Output texture for AYUV
RWTexture2D<float4> tex : register(u0);

// Get the texture dimensions with cbuffers
cbuffer cb0 : register(b0)
{
    uint src_width;
    uint src_height;
};

// Compute shader entry point
[numthreads(GROUP_SIZE, GROUP_SIZE, 1)] void main(uint3 thread_id
                                                  : SV_DispatchThreadID)
{
    uint2 coords = uint2(thread_id.xy);

    if (coords.x >= src_width || coords.y >= src_height)
        return;

    // Read RGBA from input texture with a sampler
    float4 rgba = tex[coords];

    float y  =  0.2126f  * rgba.r + 0.7152f  * rgba.g + 0.0722f  * rgba.b;
    float Cb = -0.09991f * rgba.r - 0.33609f * rgba.g + 0.436f   * rgba.b + 0.5f;
    float Cr =  0.615f   * rgba.r - 0.55861f * rgba.g - 0.05639f * rgba.b + 0.5f;

    // Write to output texture with red
    tex[coords] = float4(y, Cb, Cr, rgba.a);
}