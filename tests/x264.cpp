#include <fstream>
#include <iostream>
#include <stb_image.h>
#include <test_framework.hpp>

extern "C"
{
#include <libavcodec/avcodec.h>
}

void copy_frame(int i, AVFrame *frame, AVCodecContext *codec_ctx, uint8_t *src_img, int width, int height)
{
    // Create a new frame
    frame->format  = codec_ctx->pix_fmt;
    frame->width   = codec_ctx->width;
    frame->height  = codec_ctx->height;
    frame->pts     = i;
    auto ret       = av_frame_get_buffer(frame, 0);

    // Make writable
    

    // Convert from YUV 4:4:4 to NV12 4:2:0

    // For each pixel
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            // Get the YUV values
            uint8_t y_val = src_img[(y * width + x) * 4 + 0];
            uint8_t u_val = src_img[(y * width + x) * 4 + 1] + 128;
            uint8_t v_val = src_img[(y * width + x) * 4 + 2] + 128;

            // Copy the Y value to the Y plane
            frame->data[0][y * frame->linesize[0] + x] = y_val;

            // Copy the U and V values to the UV plane, interleaved
            if (y % 2 == 0 && x % 2 == 0)
            {
                frame->data[1][(y / 2) * frame->linesize[1] + (x / 2) * 2 + 0] = u_val;
                frame->data[1][(y / 2) * frame->linesize[1] + (x / 2) * 2 + 1] = v_val;
            }
        }
    }

}

TEST
{
    // Load test image
    int      width, height, n_channels;
    uint8_t *src_img = stbi_load("resources/frame_1000_yuv444.png", &width, &height, &n_channels, 4);
    ASSERT_NOT_NULL(src_img);
    EXPECT_EQ(width, 1832 * 2);
    EXPECT_EQ(height, 1920);
    EXPECT_EQ(n_channels, 4);

    // Create a new codec context
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    ASSERT_NOT_NULL(codec);

    // Create a new codec context
    // Video is a VR stream: 90fps, intra only
    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    ASSERT_NOT_NULL(codec_ctx);
    // Low latency preset
    codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    codec_ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    codec_ctx->flags |= AV_CODEC_FLAG2_FAST;
    codec_ctx->time_base = {1, 90};
    codec_ctx->framerate = {90, 1};
    codec_ctx->gop_size  = 1;
    codec_ctx->pix_fmt   = AV_PIX_FMT_NV12;
    codec_ctx->width     = width;
    codec_ctx->height    = height;
    // No P or B frames
    codec_ctx->max_b_frames = 0;
    codec_ctx->refs         = 1;
    codec_ctx->has_b_frames = 0;
    codec_ctx->delay        = 0;
    // We use BT.709 color space
    codec_ctx->color_primaries = AVCOL_PRI_BT709;
    codec_ctx->color_trc       = AVCOL_TRC_BT709;
    codec_ctx->colorspace      = AVCOL_SPC_BT709;
    codec_ctx->color_range     = AVCOL_RANGE_JPEG;
    // Set the profile to high
    codec_ctx->profile = FF_PROFILE_H264_HIGH;

    // Open the codec
    int ret = avcodec_open2(codec_ctx, codec, nullptr);
    ASSERT_EQ(ret, 0);

    AVFrame *frame = av_frame_alloc();
    ASSERT_NOT_NULL(frame);
    AVPacket *pkt = av_packet_alloc();
    ASSERT_NOT_NULL(pkt);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Send 20 frames
    for (int i = 0; i < 15; i++) {
        copy_frame(i, frame, codec_ctx, src_img, width, height);
        ret = avcodec_send_frame(codec_ctx, frame);
        if (ret == AVERROR(EAGAIN)) {
            break;
        }
    }

    // Write header to file
    std::string   filename = "frame_1000_nv12_header.h264";
    std::ofstream file(filename, std::ios::binary);
    file.write((char *) codec_ctx->extradata, codec_ctx->extradata_size);
    file.close();

    int i;
    for (i = 0; i < 100; i++)
    {
        // Wait for the m_packet to be ready
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR(EXIT_SUCCESS))
        {
            // Write the m_packet to a file
            std::string   filename = "frame_1000_nv12_" + std::to_string(pkt->pts) + ".h264";
            std::ofstream file(filename, std::ios::binary);
            file.write((char *) pkt->data, pkt->size);
            file.close();
        }
        else if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
        {
            break;
        }
        else
        {
            switch (ret)
            {
                case AVERROR(EINVAL): std::cerr << "Error encoding frame: EINVAL" << std::endl; break;
                case AVERROR(ENOMEM): std::cerr << "Error encoding frame: ENOMEM" << std::endl; break;
                default: std::cerr << "Error encoding frame: " << ret << std::endl; break;
            }
            ERROR("Error encoding frame");
            break;
        }
    }
    std::cout << "Done in " << i << " iterations." << std::endl;
    EXPECT_TRUE(i > 0);

    // Cleanup
    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&codec_ctx);
    stbi_image_free(src_img);
}