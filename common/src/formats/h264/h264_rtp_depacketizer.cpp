#include "wvb_common/formats/h264.h"
#include <wvb_common/formats/rtp_packetizer.h>
#include <wvb_common/network_utils.h>

#include <cstring>
#include <optional>
#include <vector>

#define NALU_NRI(nalu_header)                         (((nalu_header) &0x60) >> 5)
#define NALU_TYPE(nalu_header)                        ((nalu_header) &0x1F)
#define FU_A_TYPE                                     28
#define FU_INDICATOR(nalu_header)                     (((nalu_header) &0b11100000) | FU_A_TYPE)
#define FU_REASSEMBLE_HEADER(fu_indicator, fu_header) (((fu_indicator) &0b11100000) | ((fu_header) &0b00011111))
#define FU_HEADER_START_BIT                           0b10000000
#define FU_HEADER_END_BIT                             0b01000000
#define NALU_HEADER_F_BIT                             0b10000000

namespace wvb
{

    // ---- H264RtpPacketizer ----

    class H264RtpDepacketizer : public IRtpDepacketizer
    {
      private:
        using super = IRtpDepacketizer;

        uint8_t *m_fu_header                   = nullptr;
        bool     m_should_drop_fragmented_unit = false;

        void                         add_start_code();
        void                         start_new_fu(const uint8_t *payload, size_t size);
        [[nodiscard]] constexpr bool in_fragmented_unit() { return m_fu_header != nullptr; }

      protected:
        void process_packet(const rtp::RTPHeader *const data, size_t size) override;
        void reset_frame() override;
        void finish_frame() override;

      public:
        const char *name() const override { return "H264RtpDepacketizer"; }

        H264RtpDepacketizer() : super() {}
    };

    // ---- Implementation ----

    std::shared_ptr<IDepacketizer> create_h264_rtp_depacketizer()
    {
        return std::make_shared<H264RtpDepacketizer>();
    }

    // ---- H264 logic ----

    void H264RtpDepacketizer::add_start_code()
    {
        if (m_frame_data.empty())
        {
            m_frame_data.insert(m_frame_data.end(), {0, 0, 0, 1});
        }
        else
        {
            m_frame_data.insert(m_frame_data.end(), {0, 0, 1});
        }
    }

    void H264RtpDepacketizer::start_new_fu(const uint8_t *payload, size_t size)
    {
        add_start_code();
        // Reassemble the NAL header
        m_frame_data.push_back(FU_REASSEMBLE_HEADER(payload[0], payload[1]));
        m_fu_header = &m_frame_data.back();
        // Copy the payload
        m_frame_data.insert(m_frame_data.end(), payload + 2, payload + size);
        m_should_drop_fragmented_unit = false;
    }

    void H264RtpDepacketizer::finish_frame()
    {
        m_has_frame = true;
    }

    void H264RtpDepacketizer::reset_frame()
    {
        // Reset the FU
        m_fu_header                   = nullptr;
        m_should_drop_fragmented_unit = false;
    }

    void H264RtpDepacketizer::process_packet(const rtp::RTPHeader *const data, size_t size)
    {
        const auto *payload      = reinterpret_cast<const uint8_t *>(data) + sizeof(rtp::RTPHeader);
        const auto  payload_size = size - sizeof(rtp::RTPHeader);

        if (m_current_rtp_timestamp != ntohl(data->timestamp))
        {
            if (!m_frame_data.empty())
            {
                // If we see a new timestamp, but the previous frame was not sent yet, send it
                // It also means that the m_packet with the marker bit was lost, so if the last m_packet was a FU, mark it as
                // syntactically incorrect
                if (in_fragmented_unit())
                {
                    *m_fu_header |= NALU_HEADER_F_BIT;
                }

                finish_frame();
                return;
            }

            m_current_rtp_timestamp      = ntohl(data->timestamp);
            m_current_rtp_pose_timestamp = ntohl(data->pose_timestamp_ext);
            m_current_frame_id           = ntohl(data->frame_id_ext);
        }

        // If the payload is a fragmented NAL
        if (NALU_TYPE(payload[0]) == FU_A_TYPE)
        {
            if (!in_fragmented_unit())
            {
                // First m_packet of this FU. Is there a start bit ?
                if (payload[1] & FU_HEADER_START_BIT)
                {
                    start_new_fu(payload, payload_size);
                }
                else
                {
                    // Start bit is not set, the first was lost. Drop all the packets of this FU
                    m_should_drop_fragmented_unit = true;
                }
            }
            else
            {
                // This is a continuation of a fragmented unit

                if (payload[1] & FU_HEADER_START_BIT)
                {
                    // Already in a FU that was not ended properly (last m_packet probably dropped), but a new FU started.
                    // Mark last FU as syntactically incorrect
                    *m_fu_header |= NALU_HEADER_F_BIT;
                    start_new_fu(payload, payload_size);
                }
                else
                {
                    if (m_last_processed_seq_id != ntohs(data->sequence_number) - 1)
                    {
                        // Previous m_packet was lost, drop subsequent packets of this FU
                        *m_fu_header |= NALU_HEADER_F_BIT;
                        m_should_drop_fragmented_unit = true;
                    }

                    if (!m_should_drop_fragmented_unit)
                    {
                        // Copy the payload
                        m_frame_data.insert(m_frame_data.end(), payload + 2, payload + payload_size);
                    }
                    if (payload[1] & FU_HEADER_END_BIT)
                    {
                        // This is the last m_packet of the FU, reset the state
                        m_fu_header                   = nullptr;
                        m_should_drop_fragmented_unit = false;
                    }
                }
            }
        }
        else
        {
            // Not a FU, the m_packet is a self-contained NAL.
            if (in_fragmented_unit())
            {
                // The previous FU didn't end properly (last m_packet probably dropped)
                *m_fu_header |= NALU_HEADER_F_BIT;
                m_fu_header = nullptr;
            }
            m_should_drop_fragmented_unit = false;

            // Save NAL
            add_start_code();
            m_frame_data.insert(m_frame_data.end(), payload, payload + payload_size);
        }

        // Update state
        finish_packet(ntohs(data->sequence_number), data->is_marker());
    }
} // namespace wvb
