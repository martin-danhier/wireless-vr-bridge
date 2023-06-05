#include "wvb_common/formats/h264.h"
#include <wvb_common/network_utils.h>
#include <wvb_common/rtp.h>

#include <cstring>
#include <random>

namespace wvb
{

#define RTP_MARGIN             100
#define NALU_NRI(nalu_header)  (((nalu_header) &0x60u) >> 5)
#define NALU_TYPE(nalu_header) ((nalu_header) &0x1Fu)
// FU indicator: keep start of NALU header and set type to FU-A
#define FU_INDICATOR(nalu_header) ((nalu_header) &0b11100000u) | 28u
#define FU_HEADER_START_BIT       0b10000000u
#define FU_HEADER_END_BIT         0b01000000u

    // ---- H264RtpPacketizer ----

    class H264RtpPacketizer : public IPacketizer
    {
      private:
        uint8_t        m_rtp_data[WVB_RTP_MTU] = {0};
        uint16_t       m_sequence_number       = 0;
        const uint8_t *m_h264_head             = nullptr;
        const uint8_t *m_h264_tail             = nullptr;
        bool           m_last                  = false;
        uint8_t        m_current_nalu_header   = 0;

      public:
        explicit H264RtpPacketizer(uint32_t ssrc);
        void add_frame_data(const uint8_t *h264_data,
                            size_t         h264_size,
                            uint32_t       frame_id,
                            bool           end_of_stream,
                            uint32_t       rtp_timestamp,
                            uint32_t       rtp_pose_timestamp,
                            bool           last,
                            bool           save_frame) override;
        bool create_next_packet(const uint8_t **__restrict out_packet_data, size_t *__restrict out_size) override;

        [[nodiscard]] const char *name() const override { return "H264RtpPacketizer"; }

        // Getters
        [[nodiscard]] inline rtp::RTPHeader *packet() { return reinterpret_cast<rtp::RTPHeader *>(m_rtp_data); };
        [[nodiscard]] inline uint8_t        *payload() { return m_rtp_data + sizeof(rtp::RTPHeader); };
        [[nodiscard]] inline uint8_t        *packet_data() { return m_rtp_data; }
    };

    // ---- Implementation ----

    std::shared_ptr<IPacketizer> create_h264_rtp_packetizer(uint32_t ssrc)
    {
        return std::make_shared<H264RtpPacketizer>(ssrc);
    }

    H264RtpPacketizer::H264RtpPacketizer(uint32_t ssrc)
    {
        packet()->first_byte = RTP_FIRST_BYTE_BASE;
        packet()->set_payload(rtp::RTPPayloadType::H264);
        packet()->ssrc = htonl(ssrc);

        // Choose random base sequence number
        std::random_device              rd;
        std::mt19937                    gen(rd());
        std::uniform_int_distribution<> dis(0, 65535);
        m_sequence_number         = dis(gen);
        packet()->sequence_number = htons(m_sequence_number);
    }

    void H264RtpPacketizer::add_frame_data(const uint8_t *h264_data,
                                           size_t         h264_size,
                                           uint32_t       frame_id,
                                           bool           end_of_stream,
                                           uint32_t       rtp_timestamp,
                                           uint32_t       rtp_pose_timestamp,
                                           bool           last,
                                           bool           save_frame)
    {
        m_h264_head           = h264_data;
        m_h264_tail           = h264_data + h264_size;
        m_current_nalu_header = 0;
        m_last                = last;

        packet()->timestamp          = htonl(rtp_timestamp);
        packet()->pose_timestamp_ext = htonl(rtp_pose_timestamp);
        packet()->frame_id_ext       = htonl(frame_id);
    }

    bool H264RtpPacketizer::create_next_packet(const uint8_t **__restrict out_packet_data, size_t *__restrict out_size)
    {
        if (m_h264_head == nullptr || m_h264_tail == nullptr)
        {
            *out_size = 0;
            return false;
        }

        // First, analyze the m_packet to find the length of the NAL unit
        // -> NAL unit starts after starting bytes (00) 00 00 01, until the starting bytes of the next NAL unit
        // -> If the current NAL unit is larger that the MTU, fragment it
        // So, we stop:
        // - if the end of the NAL unit is reached (next starting bytes)
        // - if the end of the stream is reached
        // - if the current NAL unit is the size of the MTU

        bool           is_nalu_start     = false;
        const bool     in_fragmented_nal = m_current_nalu_header != 0;
        size_t         nal_size          = 0;
        const uint8_t *current           = m_h264_head;
        const uint8_t *nal_start         = (in_fragmented_nal) ? m_h264_head : nullptr;

        while (nal_size < WVB_RTP_MTU - RTP_MARGIN)
        {
            const auto bytes_left = m_h264_tail - current;

            // Stop at the end of the stream
            if (bytes_left == 0)
            {
                break;
            }

            // We have a guarantee of 1 byte

            // Analyse start codes if enough space
            if (current[0] == 0x00 && bytes_left >= 3 && current[1] == 0x00)
            {
                // 3 bytes start code
                if (current[2] == 0x01)
                {
                    is_nalu_start = true;

                    // Already in a NAL
                    if (nal_start != nullptr)
                    {
                        break;
                    }

                    current += 3;
                    continue;
                }
                // 4 bytes start code
                else if (current[2] == 0x00 && bytes_left >= 4 && current[3] == 0x01)
                {
                    is_nalu_start = true;

                    // Already in a NAL
                    if (nal_start != nullptr)
                    {
                        break;
                    }

                    current += 4;
                    continue;
                }
            }

            // First byte after a start code, that is not a start code, is a NAL unit header
            if (is_nalu_start)
            {
                m_current_nalu_header = current[0];
                nal_start             = current;

                is_nalu_start = false;
            }

            current++;
            nal_size++;
        }

        if (nal_size == 0 || nal_start == nullptr)
        {
            *out_size        = 0;
            *out_packet_data = nullptr;
            return false;
        }

        // Cases:
        // - Found a second start code: is_nalu_start is true
        // - EOF reached: current == m_h264_tail
        // - MTU reached: otherwise

        // Packetization 1: fits in a single RTP m_packet
        // TODO maybe compound smallest packets into a single RTP m_packet
        const bool end_of_nal_reached = is_nalu_start || current == m_h264_tail;
        if (!in_fragmented_nal && end_of_nal_reached)
        {
            memcpy(payload(), nal_start, nal_size);
            *out_size             = sizeof(rtp::RTPHeader) + nal_size;
            *out_packet_data      = packet_data();
            m_current_nalu_header = 0; // NALU finished
        }
        // Packetization 2: fragment m_packet
        else
        {
            uint8_t *payload_data = payload();
            payload_data[0]       = FU_INDICATOR(m_current_nalu_header);
            payload_data[1]       = NALU_TYPE(m_current_nalu_header);
            if (!in_fragmented_nal)
            {
                // First fragment
                payload_data[1] |= FU_HEADER_START_BIT;

                // Copy data, but don't copy the header
                memcpy(&payload_data[2], nal_start + 1, nal_size - 1);
                // We added 1 indicator byte
                *out_size = sizeof(rtp::RTPHeader) + 1 + nal_size;
            }
            else
            {
                if (end_of_nal_reached)
                {
                    payload_data[1] |= FU_HEADER_END_BIT;
                    m_current_nalu_header = 0;
                }

                // Copy data. There is no header to copy
                memcpy(&payload_data[2], nal_start, nal_size);
                // We added 2 header bytes
                *out_size = sizeof(rtp::RTPHeader) + 2 + nal_size;
            }
        }

        // Finalize the m_packet
        packet()->sequence_number = htons(m_sequence_number++);
        *out_packet_data          = packet_data();

        // EOF reached ?
        if (current == m_h264_tail)
        {
            packet()->set_marker(m_last);
            m_h264_head = nullptr;
            m_h264_tail = nullptr;

            return false;
        }
        else
        {
            packet()->set_marker(false);
            m_h264_head = current;
            return true; // call again
        }
    }

} // namespace wvb