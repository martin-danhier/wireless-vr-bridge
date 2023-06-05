#include "wvb_common/formats/simple_packetizer.h"

#include <wvb_common/network_utils.h>

#include <iostream>
#include <mutex>
#include <vector>

#define MARKER_BIT        (1u << 31u)
#define FRAMEBUFFER_COUNT 10 // Capacity of the framebuffer. Once full, incoming frames will block until a slot is freed.
#define ENABLE_FRAME_DROP_CATCHUP    0 // If 1, we will always take the most recent received frame. If 0, frames are submitted in order.
#define CATCHUP_THRESHOLD 2 // Number of frames we can miss before we start dropping frames.

namespace wvb
{
    // --- Types ---

    /** Simple RTP packetizer designed for use over TCP:
     * - assume reliable and ordered m_packet delivery
     * - no clear length definition in each m_packet
     *
     * There is no need to reorder packets nor to encode the bitstream in a particular format.
     * Thus, this packetizer only appends a small header to the provided bitstream that contains:
     * - length of the m_packet
     * - RTP timestamp of the frame
     * - Whether this sub-m_packet is the last of the frame (useful if we want to send extra data before the frame)
     */
    class SimplePacketizer : public IPacketizer
    {
      private:
        friend class SimpleDepacketizer;

        // Combine various bool fields into a single byte
        enum class SimpleHeaderFlags : uint8_t
        {
            NONE          = 0b000,
            END_OF_FRAME  = 0b001,
            SAVE_FRAME    = 0b010,
            END_OF_STREAM = 0b100,
        };
        constexpr friend SimpleHeaderFlags operator|(SimpleHeaderFlags a, SimpleHeaderFlags b)
        {
            return static_cast<SimpleHeaderFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
        }
        constexpr friend SimpleHeaderFlags operator&(SimpleHeaderFlags a, SimpleHeaderFlags b)
        {
            return static_cast<SimpleHeaderFlags>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
        }

        /** Simplified header that doesn't redefine what TCP already handles. */
        struct SimpleHeader
        {
            /** Size of the m_packet, including this header.
             */
            uint32_t size = 0;
            /** Timestamp of the sampling time of the frame. */
            uint32_t rtp_sample_timestamp = 0;
            /** Timestamp of the pose that was used to generate the frame. */
            uint32_t          rtp_pose_timestamp = 0;
            uint32_t          frame_id           = 0;
            SimpleHeaderFlags flags              = SimpleHeaderFlags::NONE;
        };

        const uint8_t *m_data        = nullptr;
        SimpleHeader   m_header      = {};
        bool           m_header_sent = true;

      public:
        ~SimplePacketizer() override = default;

        [[nodiscard]] const char *name() const override { return "SimplePacketizer"; }

        void add_frame_data(const uint8_t *data,
                            size_t         size,
                            uint32_t       frame_id,
                            bool           end_of_stream,
                            uint32_t       rtp_sampling_timestamp,
                            uint32_t       rtp_pose_timestamp,
                            bool           save_frame,
                            bool           last_of_frame) override;

        bool create_next_packet(const uint8_t **__restrict out_packet_data, size_t *__restrict out_size) override;
    };

    class SimpleDepacketizer : public IDepacketizer
    {
      private:
        struct Framebuffer
        {
            std::vector<uint8_t>                  buffer;
            uint32_t                              size = 0;
            std::chrono::steady_clock::time_point last_packet_received_time;
            bool                                  ready = false;
            std::mutex                            mutex;

            [[nodiscard]] inline SimplePacketizer::SimpleHeader *header()
            {
                return reinterpret_cast<SimplePacketizer::SimpleHeader *>(buffer.data());
            }
        };

        Framebuffer          m_buffers[FRAMEBUFFER_COUNT] = {};
        std::atomic<uint8_t> m_tail                       = 0;
        std::atomic<uint8_t> m_head                       = 0;

      public:
        SimpleDepacketizer();
        ~SimpleDepacketizer() override = default;

        [[nodiscard]] const char *name() const override { return "SimpleDepacketizer"; }

        void add_packet(const uint8_t *packet_data, size_t packet_size) override;

        bool receive_frame_data(const uint8_t **__restrict out_frame_data,
                                size_t *__restrict out_frame_size,
                                uint32_t *__restrict out_frame_id,
                                bool *__restrict out_end_of_stream,
                                uint32_t *__restrict out_rtp_timestamp,
                                uint32_t *__restrict out_rtp_pose_timestamp,
                                std::chrono::steady_clock::time_point *out_last_packet_received_time,
                                bool *__restrict out_save_frame) override;
        void release_frame_data() override;
    };

    // --- Impl ---

    void SimplePacketizer::add_frame_data(const uint8_t *data,
                                          size_t         size,
                                          uint32_t       frame_id,
                                          bool           end_of_stream,
                                          uint32_t       rtp_sampling_timestamp,
                                          uint32_t       rtp_pose_timestamp,
                                          bool           save_frame,
                                          bool           last_of_frame)
    {
        // No processing to do, simply store the parameters
        m_data                        = data;
        m_header.size                 = htonl(sizeof(SimpleHeader) + size);
        m_header.rtp_sample_timestamp = htonl(rtp_sampling_timestamp);
        m_header.rtp_pose_timestamp   = htonl(rtp_pose_timestamp);
        m_header.frame_id             = htonl(frame_id);
        if (last_of_frame)
        {
            m_header.flags = m_header.flags | SimpleHeaderFlags::END_OF_FRAME;
        }
        if (save_frame)
        {
            m_header.flags = m_header.flags | SimpleHeaderFlags::SAVE_FRAME;
        }
        if (end_of_stream)
        {
            m_header.flags = m_header.flags | SimpleHeaderFlags::END_OF_STREAM;
        }
        m_header_sent = false;
    }

    bool SimplePacketizer::create_next_packet(const uint8_t **__restrict out_packet_data, size_t *__restrict out_size)
    {
        // Send first the header
        if (!m_header_sent)
        {
            *out_packet_data = reinterpret_cast<const uint8_t *>(&m_header);
            *out_size        = sizeof(SimpleHeader);
            m_header_sent    = true;
            // Return true if there is more data to send
            return ntohl(m_header.size) > sizeof(SimpleHeader);
        }

        if (m_data)
        {
            // Then send the data
            *out_packet_data = m_data;
            *out_size        = ntohl(m_header.size) - sizeof(SimpleHeader);
            m_data           = nullptr;
            return (m_header.flags & SimpleHeaderFlags::END_OF_FRAME) != SimpleHeaderFlags::END_OF_FRAME;
        }
        return false;
    }

    SimpleDepacketizer::SimpleDepacketizer()
    {
        for (auto &buf : m_buffers)
        {
            buf.buffer.reserve(32 * 1024); // 32 KB
        }
    }

    void wvb::SimpleDepacketizer::add_packet(const uint8_t *packet_data, size_t packet_size)
    {
        // Get current buffer
        Framebuffer &buf = m_buffers[m_tail];

        std::lock_guard<std::mutex> lock(buf.mutex);

        // Append the packet to the buffer
        buf.buffer.insert(buf.buffer.end(), packet_data, packet_data + packet_size);

        if (buf.size == 0)
        {
            // No size info yet on this packet. Try to get it from header
            const auto *header = buf.header();
            if (buf.buffer.size() < sizeof(wvb::SimplePacketizer::SimpleHeader))
            {
                // Not enough data for a full header
                return;
            }
            buf.size = ntohl(header->size);
        }

        if (buf.size > buf.buffer.size())
        {
            // Not a full packet yet
            return;
        }

        // Got a full packet
        buf.ready                     = true;
        buf.last_packet_received_time = std::chrono::steady_clock::now();
        m_tail                        = (m_tail + 1) % FRAMEBUFFER_COUNT;

        if (m_tail == m_head)
        {
            // Looped around and reached head
            auto &head_buf = m_buffers[m_head];

            std::lock_guard<std::mutex> head_lock(head_buf.mutex);

            if (m_tail == m_head)
            {
                // If the head is still the same, we can move it forward
                // The head could also be moved forward if the frame was processed before we got the lock
                m_head = (m_head + 1) % FRAMEBUFFER_COUNT;
            }

            // Reset the head buffer
            head_buf.ready = false;
            head_buf.size  = 0;
            head_buf.buffer.clear();
        }

        // TCP can sometimes start sending the next packet after the first one is received
        // So, we have to check if we got more data than the size of the last frame
        const auto remainder = (buf.buffer.size() - buf.size);
        if (remainder > 0)
        {
            // If so, already start processing the next frame
            Framebuffer                &next_buf = m_buffers[m_tail];
            std::lock_guard<std::mutex> next_lock(next_buf.mutex);

            next_buf.buffer.insert(next_buf.buffer.end(), buf.buffer.data() + buf.size, buf.buffer.data() + buf.size + remainder);
        }
    }

    uint8_t ring_buffer_distance(uint8_t head, uint8_t tail, uint8_t capacity)
    {
        if (head <= tail)
        {
            return tail - head;
        }
        else
        {
            // distance from head to the end + distance from beginning to tail
            return (capacity - head) + tail;
        }
    }

    bool SimpleDepacketizer::receive_frame_data(const uint8_t **__restrict out_frame_data,
                                                size_t *__restrict out_frame_size,
                                                uint32_t *__restrict out_frame_id,
                                                bool *__restrict out_end_of_stream,
                                                uint32_t *__restrict out_rtp_timestamp,
                                                uint32_t *__restrict out_rtp_pose_timestamp,
                                                std::chrono::steady_clock::time_point *out_last_packet_received_time,
                                                bool *__restrict out_save_frame)
    {
        uint8_t head = m_head;
        uint8_t tail = m_tail;
        while (head != tail)
        {
            // Get the head
            Framebuffer &buf = m_buffers[head];

            buf.mutex.lock();

            // Skip invalid frames
            if (!buf.ready)
            {
                head   = (head + 1) % FRAMEBUFFER_COUNT;
                m_head = head;
                buf.mutex.unlock();
                continue;
            }
            if (buf.buffer.size() < sizeof(wvb::SimplePacketizer::SimpleHeader))
            {
                head   = (head + 1) % FRAMEBUFFER_COUNT;
                m_head = head;

                // Reset the head buffer
                buf.ready = false;
                buf.size  = 0;
                buf.buffer.clear();
                buf.mutex.unlock();
                continue;
            }

            // Catch up if there are too many frames in queue in order to reduce latency
            if (ENABLE_FRAME_DROP_CATCHUP && ring_buffer_distance(head, tail, FRAMEBUFFER_COUNT) > CATCHUP_THRESHOLD)
            {
                // We have at least 2 frames in queue, so we can skip this one
                head   = (head + 1) % FRAMEBUFFER_COUNT;
                m_head = head;

                // Reset the head buffer
                buf.ready = false;
                buf.size  = 0;
                buf.buffer.clear();
                buf.mutex.unlock();
                continue;
            }

            const auto *header = buf.header();
            const auto  size   = ntohl(header->size);

            if (buf.buffer.size() < size)
            {
                head   = (head + 1) % FRAMEBUFFER_COUNT;
                m_head = head;

                // Reset the head buffer
                buf.ready = false;
                buf.size  = 0;
                buf.buffer.clear();
                buf.mutex.unlock();
                continue;
            }

            // We have enough data to read the fragment.
            *out_frame_data                = buf.buffer.data() + sizeof(wvb::SimplePacketizer::SimpleHeader);
            *out_frame_size                = size - sizeof(wvb::SimplePacketizer::SimpleHeader);
            *out_frame_id                  = ntohl(header->frame_id);
            *out_rtp_timestamp             = ntohl(header->rtp_sample_timestamp);
            *out_rtp_pose_timestamp        = ntohl(header->rtp_pose_timestamp);
            *out_last_packet_received_time = buf.last_packet_received_time;
            *out_save_frame                = (header->flags & wvb::SimplePacketizer::SimpleHeaderFlags::SAVE_FRAME)
                              == wvb::SimplePacketizer::SimpleHeaderFlags::SAVE_FRAME;
            *out_end_of_stream = (header->flags & wvb::SimplePacketizer::SimpleHeaderFlags::END_OF_STREAM)
                                 == wvb::SimplePacketizer::SimpleHeaderFlags::END_OF_STREAM;

            // Don't free the lock yet until we are done with the data
            return true;
        }

        return false;
    }

    void SimpleDepacketizer::release_frame_data()
    {
        uint8_t      head = m_head;
        Framebuffer &buf  = m_buffers[head];

        // Reset the head buffer
        buf.ready = false;
        buf.size  = 0;
        buf.buffer.clear();

        head   = (head + 1) % FRAMEBUFFER_COUNT;
        m_head = head;
        buf.mutex.unlock();
    }

    // --- Factory ---

    std::shared_ptr<IPacketizer> create_simple_packetizer()
    {
        return std::make_shared<SimplePacketizer>();
    }

    std::shared_ptr<IDepacketizer> create_simple_depacketizer()
    {
        return std::make_shared<SimpleDepacketizer>();
    }

} // namespace wvb