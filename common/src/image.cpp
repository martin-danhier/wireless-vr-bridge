#include "wvb_common/image.h"

#include <stdexcept>
#ifdef __linux__
#include <cstring>
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))

namespace wvb
{
    RawImage::RawImage(uint32_t width, uint32_t height, uint32_t pitch)
    {
        allocate(width, height, pitch);
    }

    RawImage::RawImage(RawImage &&other) noexcept
        : m_data(other.m_data),
          m_max_width(other.m_max_width),
          m_max_height(other.m_max_height),
          m_pitch(other.m_pitch)
    {
        other.m_data       = nullptr;
        other.m_max_width  = 0;
        other.m_max_height = 0;
        other.m_pitch      = 0;
    }

    RawImage &RawImage::operator=(RawImage &&other) noexcept
    {
        if (this != &other)
        {
            delete[] m_data;
            m_data             = other.m_data;
            m_max_width        = other.m_max_width;
            m_max_height       = other.m_max_height;
            m_pitch            = other.m_pitch;
            other.m_data       = nullptr;
            other.m_max_width  = 0;
            other.m_max_height = 0;
            other.m_pitch      = 0;
        }
        return *this;
    }

    RawImage::~RawImage()
    {
        delete[] m_data;
        m_data       = nullptr;
        m_max_width  = 0;
        m_max_height = 0;
        m_pitch      = 0;
    }

    void RawImage::allocate(uint32_t width, uint32_t height, uint32_t pitch)
    {
        if (is_valid())
        {
            throw std::runtime_error("RawFrame::allocate: Frame is already allocated");
        }

        m_max_width  = width;
        m_max_height = height;
        m_pitch      = pitch;

        // Each pixel is pitch bytes
        // The data is a large array of bytes. When width is reached, the next line starts.
        m_data = new uint8_t[m_max_width * m_max_height * m_pitch];
    }

    bool RawImage::copy_from_dx11(uint8_t *src_data,
                                  uint32_t src_width,
                                  uint32_t src_height,
                                  uint32_t src_row_pitch,
                                  uint32_t dest_pitch)
    {


        // Copy the data to the buffer.
        // Inspired by Valve's virtual display example:
        // https://github.com/ValveSoftware/virtual_display/blob/master/shared/d3drender.cpp

        // If the frame is not allocated, allocate it using the same size as the texture
        if (!is_valid())
        {
            // Create the buffer based on the first received frame
            allocate(src_width, src_height, dest_pitch);
        }

        m_width  = MIN(m_max_width, src_width);
        m_height = MIN(m_max_height, src_height);
        const auto dst_row_pitch = m_width * m_pitch;

        // Copy the texture to the buffer
        if (dst_row_pitch == src_row_pitch)
        {
            // No need to copy line by line because the images have the same layout
            void * res = memcpy(m_data, src_data, dst_row_pitch * m_height);
            if (res == nullptr)
            {
                return false;
            }
        }
        else
        {
            const auto smallest_row_pitch = MIN(dst_row_pitch, src_row_pitch);
            // For each line
            for (uint64_t y = 0; y < m_height; y++)
            {
                // Copy the line: iterate on each line and copy the smallest number of bytes. Since lines are not the same size in
                // each texture, we need to increment them by their own respective row pitch
                void * res = memcpy(m_data + y * dst_row_pitch, src_data + y * src_row_pitch, smallest_row_pitch);
                if (res == nullptr)
                {
                    return false;
                }
            }
        }

        return true;
    }

} // namespace wvb