#include <fstream>
#include <iostream>
#include <stb_image_write.h>
#include <test_framework.hpp>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

void save_image(AVFrame *frame, int i)
{
    // Convert from NV12 to RGB
    AVFrame *rgb_frame = av_frame_alloc();
    rgb_frame->format  = AV_PIX_FMT_RGB24;
    rgb_frame->width   = frame->width;
    rgb_frame->height  = frame->height;
    av_frame_get_buffer(rgb_frame, 0);
    av_frame_make_writable(rgb_frame);
    auto sws_ctx = sws_getContext(frame->width,
                                  frame->height,
                                  AV_PIX_FMT_NV12,
                                  frame->width,
                                  frame->height,
                                  AV_PIX_FMT_RGB24,
                                  0,
                                  NULL,
                                  NULL,
                                  NULL);
    sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height, rgb_frame->data, rgb_frame->linesize);
    sws_freeContext(sws_ctx);

    // Save the image
    std::string filename = "frame_1000_" + std::to_string(i) + "_rgb.png";
    stbi_write_png(filename.c_str(), frame->width, frame->height, 3, rgb_frame->data[0], frame->width * 3);
}

TEST
{
    // Load H.264 files
    std::ifstream file("frame_1000_nv12_header.h264", std::ios::binary);
    ASSERT_TRUE(file.is_open());
    file.seekg(0, std::ios::end);
    const std::size_t header_size = file.tellg();
    file.seekg(0, std::ios::beg);
    auto header_data = new uint8_t[header_size];
    file.read(reinterpret_cast<char *>(header_data), header_size);
    file.close();

    file.open("frame_1000_nv12_0.h264", std::ios::binary);
    ASSERT_TRUE(file.is_open());
    file.seekg(0, std::ios::end);
    const std::size_t size0 = file.tellg();
    file.seekg(0, std::ios::beg);
    auto data0 = new uint8_t[size0 + header_size];
    file.read(reinterpret_cast<char *>(data0) + header_size, size0);
    file.close();
    memcpy(data0, header_data, header_size);

    file.open("frame_1000_nv12_1.h264", std::ios::binary);
    ASSERT_TRUE(file.is_open());
    file.seekg(0, std::ios::end);
    const std::size_t size1 = file.tellg();
    file.seekg(0, std::ios::beg);
    auto data1 = new uint8_t[size1];
    file.read(reinterpret_cast<char *>(data1), size1);
    file.close();

    // Create CPU decoder
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    ASSERT_NOT_NULL(codec);
    std::cout << "Codec name: " << codec->name << std::endl;
    AVCodecContext *context = avcodec_alloc_context3(codec);
    ASSERT_NOT_NULL(context);
    auto ret                 = avcodec_open2(context, codec, nullptr);
    ASSERT_EQ(ret, 0);

    // Decode H.264 file
    AVPacket *packet = av_packet_alloc();
    packet->data     = data0;
    packet->size     = size0 + header_size;
    ret              = avcodec_send_packet(context, packet);
    ASSERT_EQ(ret, 0);

    AVFrame *frame = av_frame_alloc();
    ASSERT_NOT_NULL(frame);
    ret = avcodec_receive_frame(context, frame);
    ASSERT_EQ(ret, 0);

    // Save frame to png
    save_image(frame, 0);

    packet->data = data1;
    packet->size = size1;
    ret          = avcodec_send_packet(context, packet);
    ASSERT_EQ(ret, 0);

    ret = avcodec_receive_frame(context, frame);
    ASSERT_EQ(ret, 0);

    // Save frame to png
    save_image(frame, 1);

    // Free resources
    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&context);
    delete[] data0;
    delete[] data1;
    delete[] header_data;
}