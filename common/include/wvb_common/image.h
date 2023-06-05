#pragma once

#include <cstdint>

namespace wvb
{
    class RawImage
    {
      private:
        uint8_t *m_data       = nullptr;
        uint32_t m_max_width  = 0;
        uint32_t m_max_height = 0;
        uint32_t m_pitch      = 0;
        uint32_t m_width      = 0;
        uint32_t m_height     = 0;

        void allocate(uint32_t width, uint32_t height, uint32_t pitch);

      public:
        RawImage() = default;
        RawImage(uint32_t width, uint32_t height, uint32_t pitch = 4);
        RawImage(RawImage &&other) noexcept;
        RawImage &operator=(RawImage &&other) noexcept;
        RawImage(const RawImage &other)            = delete;
        RawImage &operator=(const RawImage &other) = delete;
        ~RawImage();

        [[nodiscard]] inline bool     is_valid() const { return m_data != nullptr; };
        [[nodiscard]] inline uint8_t *data() const { return m_data; };
        [[nodiscard]] inline uint32_t max_width() const { return m_max_width; };
        [[nodiscard]] inline uint32_t max_height() const { return m_max_height; };
        [[nodiscard]] inline uint32_t width() const { return m_width; };
        [[nodiscard]] inline uint32_t height() const { return m_height; };
        [[nodiscard]] inline uint32_t pitch() const { return m_pitch; };
        [[nodiscard]] inline uint32_t row_pitch() const { return m_pitch * m_width; };

        /**
         * @brief Copies the data from a DirectX 11 texture to the RawImage.
         * @param src_data Pointer to the data of the DirectX 11 texture.
         * @param src_width Width of the DirectX 11 texture.
         * @param src_height Height of the DirectX 11 texture.
         * @param src_row_pitch Row pitch of the DirectX 11 texture (Width * Bytes per pixel)
         * @param dest_pitch Bytes per pixel of the RawImage.
         * @return True if the copy was successful, false otherwise.
         */
        bool copy_from_dx11(uint8_t *src_data,
                            uint32_t src_width,
                            uint32_t src_height,
                            uint32_t src_row_pitch,
                            uint32_t dest_pitch = 4);
    };
} // namespace wvb