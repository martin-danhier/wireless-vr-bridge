#include <test_framework.hpp>

// Since we use DirectX, sadly only support Windows
#ifdef WIN32

#undef ERROR
#include <wvb_common/vr_structs.h>
#include <wvb_common/dx11_utils/rgba_to_nv12.h>

#include <d3d11.h>
#include <fstream>
#include <iostream>
#include <stb_image.h>
#include <stb_image_write.h>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

const char *ffmpeg_result_to_string(int result)
{
    switch (result)
    {
        case AVERROR(EAGAIN): return "EAGAIN";
        case AVERROR(EINVAL): return "EINVAL";
        default: return "Unknown error";
    }
}

#define SHADER_DIR_PATH     "../wvb_server/shaders/"
#define H264_LINE_PER_SLICE 128
#define H264_MAX_SLICES     256

#define BITRATE 100000000

TEST
{
    // Load test image (RGBA)
    int      width, height, n_channels;
    uint8_t *src_img = stbi_load("resources/frame_1000_rgba.png", &width, &height, &n_channels, 4);
    ASSERT_NOT_NULL(src_img);

    // region Setup D3D11

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

    // Create staging texture
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
    ID3D11Texture2D *staging_texture;
    hr = device->CreateTexture2D(&staging_desc, nullptr, &staging_texture);
    ASSERT_TRUE(SUCCEEDED(hr));

    // Copy image data to staging texture
    D3D11_MAPPED_SUBRESOURCE mapped_subresource;
    hr = device_context->Map(staging_texture, 0, D3D11_MAP_WRITE, 0, &mapped_subresource);
    ASSERT_TRUE(SUCCEEDED(hr));

    auto img_pitch = width * 4;
    if (img_pitch == mapped_subresource.RowPitch)
    {
        memcpy(mapped_subresource.pData, src_img, img_pitch * height);
    }
    else
    {
        for (int y = 0; y < height; y++)
        {
            memcpy((uint8_t *) mapped_subresource.pData + y * mapped_subresource.RowPitch, src_img + y * img_pitch, img_pitch);
        }
    }

    device_context->Unmap(staging_texture, 0);

    // Create GPU only texture
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
    ID3D11Texture2D *gpu_texture;
    hr = device->CreateTexture2D(&gpu_texture_desc, nullptr, &gpu_texture);
    ASSERT_TRUE(SUCCEEDED(hr));

    // Create SRV
    ID3D11ShaderResourceView *gpu_texture_srv;
    hr = device->CreateShaderResourceView(gpu_texture, nullptr, &gpu_texture_srv);
    ASSERT_TRUE(SUCCEEDED(hr));

    // Copy staging texture to GPU texture
    device_context->CopyResource(gpu_texture, staging_texture);

    // endregion

    // We now have reproduced the setup from the encoder: we have an image in a GPU texture
    // We now want to convert it to H.264 RTP packets with FFmpeg

    // region Setup FFmpeg

    // Initialize FFmpeg

    // Create device context
    AVBufferRef *hw_device_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
    ASSERT_NOT_NULL(hw_device_ctx);
    auto *hw_device_ctx_data = (AVHWDeviceContext *) hw_device_ctx->data;
    ASSERT_NOT_NULL(hw_device_ctx_data);
    auto *hw_device_ctx_data_d3d11 = (AVD3D11VADeviceContext *) hw_device_ctx_data->hwctx;
    ASSERT_NOT_NULL(hw_device_ctx_data_d3d11);
    hw_device_ctx_data_d3d11->device         = device;
    hw_device_ctx_data_d3d11->device_context = device_context;
    int result                               = av_hwdevice_ctx_init(hw_device_ctx);
    ASSERT_TRUE(result == 0);

    // Create hardware frame
    AVBufferRef *hw_frames_ctx = av_hwframe_ctx_alloc(hw_device_ctx);
    ASSERT_NOT_NULL(hw_frames_ctx);
    auto *hw_frames_ctx_data = (AVHWFramesContext *) hw_frames_ctx->data;
    ASSERT_NOT_NULL(hw_frames_ctx_data);
    hw_frames_ctx_data->format            = AV_PIX_FMT_D3D11;
    hw_frames_ctx_data->sw_format         = AV_PIX_FMT_NV12;
    hw_frames_ctx_data->width             = width;
    hw_frames_ctx_data->height            = height;
    hw_frames_ctx_data->initial_pool_size = 1;
    auto *hw_frames_ctx_data_d3d11        = (AVD3D11VAFramesContext *) hw_frames_ctx_data->hwctx;
    ASSERT_NOT_NULL(hw_frames_ctx_data_d3d11);
    hw_frames_ctx_data_d3d11->BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    result                              = av_hwframe_ctx_init(hw_frames_ctx);
    ASSERT_TRUE(result == 0);

    // Create codec context
    const AVCodec *codec = avcodec_find_encoder_by_name("h264_nvenc");
    ASSERT_NOT_NULL(codec);
    AVCodecContext *codec_context = avcodec_alloc_context3(codec);
    ASSERT_NOT_NULL(codec_context);
    // Preset for low latency intra frame encoding
    codec_context->profile = FF_PROFILE_H264_HIGH_422_INTRA;
    result                 = av_opt_set(codec_context->priv_data, "preset", "p1", 0);
    //    result                 = av_opt_set(codec_context->priv_data, "tune", "fastdecode", 0);
    ASSERT_TRUE(result == 0);
    // Allow using previous frames, but no look ahead
    result = av_opt_set(codec_context->priv_data, "rc-lookahead", "0", 0);
    ASSERT_TRUE(result == 0);
    codec_context->width         = width;
    codec_context->height        = height;
    codec_context->pix_fmt       = AV_PIX_FMT_D3D11;
    codec_context->hw_device_ctx = hw_device_ctx;
    codec_context->hw_frames_ctx = hw_frames_ctx;
    codec_context->time_base     = {1, 72};
    codec_context->framerate     = {72, 1};
    codec_context->gop_size      = 20;
    codec_context->max_b_frames  = 0;
    codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    codec_context->delay    = 0;
    codec_context->bit_rate = BITRATE;
    // Optimize decoding
//    codec_context->bidir_refine = 1;
//    codec_context->refs         = 1;
//    codec_context->trellis      = 0;
//    codec_context->me_range     = 16;

    // We use BT.709 color space
    codec_context->color_primaries = AVCOL_PRI_BT709;
    codec_context->color_trc       = AVCOL_TRC_BT709;
    codec_context->colorspace      = AVCOL_SPC_BT709;
    codec_context->color_range     = AVCOL_RANGE_MPEG;
    // Divide in small slices to reduce NALU size
    codec_context->slices = min(static_cast<int>(height / H264_LINE_PER_SLICE), H264_MAX_SLICES);

    // Open codec
    result = avcodec_open2(codec_context, codec, nullptr);
    ASSERT_TRUE(result == 0);

    // endregion

    // region Encode

    // Create frame

    AVPacket *m_packet = av_packet_alloc();
    ASSERT_NOT_NULL(m_packet);

    AVFrame *frame = nullptr;
    frame          = av_frame_alloc();
    frame->format  = AV_PIX_FMT_D3D11;
    //    frame->chroma_location = AVCHROMA_LOC_LEFT;
    frame->width  = width;
    frame->height = height;
    result        = av_hwframe_get_buffer(codec_context->hw_frames_ctx, frame, 0);
    ASSERT_TRUE(result == 0);
    auto *d3d11_texture = (ID3D11Texture2D *) frame->data[0];

    // Convert it to NV12
    wvb::Extent2D                    size {(uint32_t) width, (uint32_t) height};
    wvb::RgbaToNv12Converter converter(size, device, SHADER_DIR_PATH, d3d11_texture);
    converter.update_size(size.width, size.height, device_context);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Measure conversion time
    auto start = std::chrono::high_resolution_clock::now();
    converter.convert(gpu_texture_srv, device_context, size, d3d11_texture);

    // Map texture
    D3D11_MAPPED_SUBRESOURCE mapped_subresource2;
    hr = device_context->Map(d3d11_texture, 0, D3D11_MAP_READ, 0, &mapped_subresource2);
    auto end = std::chrono::high_resolution_clock::now();
    device_context->Unmap(d3d11_texture, 0);

    std::cout << "Conversion time: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << " us"
              << std::endl;




    ASSERT_TRUE(result == 0);
    int  i     = 0;
    auto time1 = std::chrono::high_resolution_clock::now();
    auto time0 = std::chrono::high_resolution_clock::now();
    for (i = 0; i < 25; i++)
    {
        auto time = std::chrono::high_resolution_clock::now();
        converter.convert(gpu_texture_srv, device_context, size, d3d11_texture);
        frame->pts = i;
        // Encode frame
        result = avcodec_send_frame(codec_context, frame);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF)
            break;
        time1 = time;
        //        ASSERT_EQ(result, 0);

        //                std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    result     = avcodec_receive_packet(codec_context, m_packet);
    auto time2 = std::chrono::high_resolution_clock::now();
    ASSERT_EQ(result, 0);

    std::cout << "i: " << i << std::endl;
    std::cout << "time of last frame: " << std::chrono::duration_cast<std::chrono::microseconds>(time2 - time1).count() << " us"
              << std::endl;
    std::cout << "time of all frames: " << std::chrono::duration_cast<std::chrono::microseconds>(time2 - time0).count() << " us"
              << std::endl;

    // Encode again
    //    auto time2 = std::chrono::high_resolution_clock::now();
    //    result     = avcodec_send_frame(codec_context, nullptr);
    //    result     = avcodec_receive_packet(codec_context, m_packet);
    //    auto time3 = std::chrono::high_resolution_clock::now();
    //    ASSERT_EQ(result, 0);
    //
    //    std::cout << "time: " << std::chrono::duration_cast<std::chrono::microseconds>(time3 - time2).count() << " us" << std::endl;
    // endregion

    // region Test decoder

//     const AVCodec *decode_codec = avcodec_find_decoder_by_name("h264");
//     ASSERT_NOT_NULL(decode_codec);
//     AVCodecContext *decode_codec_context = avcodec_alloc_context3(decode_codec);
//     ASSERT_NOT_NULL(decode_codec_context);
//     //    decode_codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER | AV_CODEC_FLAG_LOW_DELAY;
//     // Lowest decoding time
//     //    auto ret = av_opt_set(decode_codec_context->priv_data, "preset", "superfast", 0);
//     //    EXPECT_TRUE(ret == 0);

//         decode_codec_context->profile = FF_PROFILE_H264_HIGH_422_INTRA;
//     //    decode_codec_context->flags2 |= AV_CODEC_FLAG2_FAST;
//     //    decode_codec_context->thread_count = 1;
//     //    decode_codec_context->thread_type  = FF_THREAD_SLICE;
//     //    decode_codec_context->pix_fmt      = AV_PIX_FMT_NV12;
// //        decode_codec_context->width        = width;
// //        decode_codec_context->height       = height;
//     //    decode_codec_context->time_base    = {1, 72};
//     //    decode_codec_context->pkt_timebase = {1, 72};
//     //    decode_codec_context->framerate    = {72, 1};
//     //    decode_codec_context->gop_size     = 3;
//     //    decode_codec_context->max_b_frames = 2;
//     //    decode_codec_context->delay        = 0;
//     //    decode_codec_context->bit_rate     = BITRATE;
//     // We use BT.709 color space
//     //    decode_codec_context->color_primaries = AVCOL_PRI_BT709;
//     //    decode_codec_context->color_trc       = AVCOL_TRC_BT709;
//     //    decode_codec_context->colorspace      = AVCOL_SPC_BT709;
//     //    decode_codec_context->color_range     = AVCOL_RANGE_MPEG;
//     // We want to lowest latency possible

//     // show decoder name
//     std::cout << "decoder name: " << decode_codec->name << std::endl;

//     auto res = avcodec_open2(decode_codec_context, decode_codec, nullptr);
//     ASSERT_TRUE(res == 0);

//     AVFrame *decode_frame = av_frame_alloc();
//     ASSERT_NOT_NULL(decode_frame);

//     // Send extra data
//     AVPacket *decode_packet = av_packet_alloc();
//     decode_packet->data     = new uint8_t[codec_context->extradata_size + m_packet->size];
//     memcpy(decode_packet->data, codec_context->extradata, codec_context->extradata_size);
//     memcpy(decode_packet->data + codec_context->extradata_size, m_packet->data, m_packet->size);
//     decode_packet->size = codec_context->extradata_size + m_packet->size;

//     std::this_thread::sleep_for(std::chrono::milliseconds(100));

//     auto time4 = std::chrono::high_resolution_clock::now();

//     res = avcodec_send_packet(decode_codec_context, decode_packet);
//     ASSERT_TRUE(res == 0);

//     res = avcodec_receive_frame(decode_codec_context, decode_frame);
//     ASSERT_TRUE(res == 0);

//     auto time5 = std::chrono::high_resolution_clock::now();

//     std::cout << "Decoding time: " << std::chrono::duration_cast<std::chrono::microseconds>(time5 - time4).count() << " us"
//               << std::endl;

//     // Save Y plane with STBI
//     std::vector<uint8_t> y_plane;
//     y_plane.resize(width * height);
//     for (int i = 0; i < height; i++)
//     {
//         memcpy(y_plane.data() + i * width, decode_frame->data[0] + i * decode_frame->linesize[0], width);
//     }
//     stbi_write_png("y_plane.png", width, height, 1, y_plane.data(), width);

//     res = avcodec_receive_packet(codec_context, m_packet);
//     ASSERT_EQ(res, 0);
//     auto time7 = std::chrono::high_resolution_clock::now();
//     //    std::cout << "Encoding time: " << std::chrono::duration_cast<std::chrono::microseconds>(time7 - time6).count() << " us" <<
//     //    std::endl;

//     // Decode again
//     auto time8 = std::chrono::high_resolution_clock::now();
//     res        = avcodec_send_packet(decode_codec_context, m_packet);
//     ASSERT_EQ(res, 0);

//     res = avcodec_receive_frame(decode_codec_context, decode_frame);
//     ASSERT_EQ(res, 0);
//     auto time9 = std::chrono::high_resolution_clock::now();
//     std::cout << "Decoding time: " << std::chrono::duration_cast<std::chrono::microseconds>(time9 - time8).count() << " us"
//               << std::endl;

//     // Send frame
//     frame->pts++;
//     res = avcodec_send_frame(codec_context, frame);
//     ASSERT_EQ(res, 0);

//     // Get next m_packet
//     res = avcodec_receive_packet(codec_context, m_packet);
//     ASSERT_EQ(res, 0);

//     // Decode again
//     auto time10 = std::chrono::high_resolution_clock::now();
//     res         = avcodec_send_packet(decode_codec_context, m_packet);
//     ASSERT_EQ(res, 0);

//     res = avcodec_receive_frame(decode_codec_context, decode_frame);
//     ASSERT_EQ(res, 0);
//     auto time11 = std::chrono::high_resolution_clock::now();
//     std::cout << "Decoding time: " << std::chrono::duration_cast<std::chrono::microseconds>(time11 - time10).count() << " us"
//               << std::endl;

    // endregion

    // Cleanup
    av_packet_free(&m_packet);
    av_frame_free(&frame);
    //    avcodec_free_context(&codec_context);
    av_buffer_unref(&hw_frames_ctx);
    av_buffer_unref(&hw_device_ctx);
    gpu_texture_srv->Release();
    gpu_texture->Release();
    staging_texture->Release();
    device_context->Release();
    device->Release();
}

#else
TEST {}
#endif
