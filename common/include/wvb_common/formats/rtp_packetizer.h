#pragma once

#include <wvb_common/packetizer.h>
#include <wvb_common/rtp.h>

#include <optional>
#include <vector>

#define WVB_EARLY_FRAME_TOLERANCE 128
#define WVB_RTP_MTU               1500

namespace wvb
{
    /**
     * Contains the RTP depacketization logic that is common for all payload formats.
    */
    class IRtpDepacketizer : public IDepacketizer
    {
      protected:
        struct RtpPacket
        {
            bool    valid             = false;
            uint8_t data[WVB_RTP_MTU] = {0};
        };

        struct RtpPacketView
        {
            // Index of the packet in the jitter buffer
            // Cannot use pointer because the buffer can be reallocated
            size_t index = 0;
            size_t size  = 0;

            [[nodiscard]] constexpr bool is_valid() { return size != 0; }
        };

        bool m_first_packet = true;
        bool m_has_frame    = false;

        // Seq of the m_packet that the depacketizer is waiting for
        uint16_t m_desired_seq_id        = 0;
        uint16_t m_last_processed_seq_id = 0;

        // Views of arriving packets are placed here in sequence order
        RtpPacketView m_packet_views[WVB_EARLY_FRAME_TOLERANCE];

        uint16_t m_packet_view_head = 0; // Index of the desired packet

        // Arriving packet data is placed here in arrival order
        uint16_t               m_jitter_count = 0;
        std::vector<RtpPacket> m_jitter_buffer;

        uint32_t                              m_current_rtp_timestamp      = 0;
        uint32_t                              m_current_rtp_pose_timestamp = 0;
        uint32_t                              m_current_frame_id           = 0;
        std::chrono::steady_clock::time_point m_last_packet_received_time ;

        // Buffer where the packet will be reassembled
        std::vector<uint8_t> m_frame_data;

        std::optional<size_t> alloc_jitter_slot();
        void                  finish_packet(uint16_t sequence_number, bool is_marker);

        virtual void process_packet(const rtp::RTPHeader *const data, size_t size) = 0;
        virtual void reset_frame()                                                 = 0;
        virtual void finish_frame()                                                = 0;

      public:
        IRtpDepacketizer();
        ~IRtpDepacketizer() override = default;

        void add_packet(const uint8_t *packet_data, size_t packet_size) override;
        bool receive_frame_data(const uint8_t **__restrict out_frame_data,
                                size_t *__restrict out_frame_size,
                                uint32_t *__restrict out_frame_id,
                                bool *__restrict out_eos,
                                uint32_t *__restrict out_rtp_timestamp,
                                uint32_t *__restrict out_rtp_pose_timestamp,
                                std::chrono::steady_clock::time_point *out_last_packet_received_time,
                                bool *__restrict out_save_frame) override;
        void release_frame_data() override {/** TODO */};
    };
} // namespace wvb
