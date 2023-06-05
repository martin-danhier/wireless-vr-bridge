#include <wvb_common/formats/rtp_packetizer.h>
#include <wvb_common/network_utils.h>

#include <cstring>
#include <iostream>

// ---- Common RTP logic ----

namespace wvb
{
    IRtpDepacketizer::IRtpDepacketizer()
    {
        m_jitter_buffer.reserve(WVB_EARLY_FRAME_TOLERANCE);
    }

    std::optional<size_t> IRtpDepacketizer::alloc_jitter_slot()
    {
        if (m_jitter_count == m_jitter_buffer.size())
        {
            // No room in buffer
            m_jitter_buffer.resize(m_jitter_count + 5);
            // Since it was full, the free slots are at the end
            const auto i             = m_jitter_count++;
            m_jitter_buffer[i].valid = true;
            return i;
        }

        // Find the first free slot
        for (size_t i = 0; i < m_jitter_buffer.size(); ++i)
        {
            if (!m_jitter_buffer[i].valid)
            {
                m_jitter_buffer[i].valid = true;
                m_jitter_count++;
                return i;
            }
        }

        return std::nullopt;
    }

    void IRtpDepacketizer::finish_packet(uint16_t sequence_number, bool is_marker)
    {
        m_last_processed_seq_id = sequence_number;
        m_desired_seq_id++;
        auto &view = m_packet_views[m_packet_view_head];
        if (view.is_valid())
        {
            // Free slot in jitter buffer
            auto &packet = m_jitter_buffer[view.index];
            if (packet.valid)
            {
                packet.valid = false;
                m_jitter_count--;
            }
            view.index = 0;
            view.size  = 0;
        }
        m_packet_view_head = (m_packet_view_head + 1) % WVB_EARLY_FRAME_TOLERANCE;

        // Last packet of the frame ?
        if (is_marker)
        {
            finish_frame();
        }
    }

    void IRtpDepacketizer::add_packet(const uint8_t *packet_data, size_t packet_size)
    {
        // Ignore invalid UDP packets
        if (packet_data == nullptr || packet_size < sizeof(rtp::RTPHeader) + 2 || packet_size >= WVB_RTP_MTU)
        {
            return;
        }

        const auto *rtp_packet = reinterpret_cast<const rtp::RTPHeader *>(packet_data);
        if (rtp_packet->first_byte != RTP_FIRST_BYTE_BASE)
        {
            return;
        }

        // If a frame is ready, then we consider that the user already used it, so we reset it
        if (m_has_frame)
        {
            reset_frame();

            // Reset buffer
            m_frame_data.clear();
        }

        const auto timestamp = ntohl(rtp_packet->timestamp);
        const auto seq       = ntohs(rtp_packet->sequence_number);
        if (m_first_packet)
        {
            m_desired_seq_id        = seq;
            m_current_rtp_timestamp = timestamp;
            m_first_packet          = false;
        }

        // If timestamp or seq is smaller than current one, drop it
        if (rtp::compare_rtp_seq(seq, m_desired_seq_id) || rtp::compare_rtp_timestamps(timestamp, m_current_rtp_timestamp))
        {
            // std::cout << "Dropped late packet\n";
            return;
        }

        auto distance = rtp::rtp_seq_distance(m_desired_seq_id, seq);
        while (distance >= WVB_EARLY_FRAME_TOLERANCE)
        {
            // Received packet beyond tolerance

            // Maybe we already have the next packet
            auto &head = m_packet_views[m_packet_view_head];
            if (!head.is_valid())
            {
                // If not, drop it
                m_desired_seq_id++;
                m_packet_view_head = (m_packet_view_head + 1) % WVB_EARLY_FRAME_TOLERANCE;
            }

            // Process all that we have then check again
            while (head.is_valid())
            {
                auto &packet = m_jitter_buffer[head.index];
                process_packet(reinterpret_cast<rtp::RTPHeader *>(packet.data), head.size);

                // Update head
                head = m_packet_views[m_packet_view_head];
            }

            // We moved as far as we could and now have a missing packet
            // Compute new distance
            // Worst case: no packet at all before this one, so we drop all packets
            // But we will terminate because we reduce the distance at each iteration
            // Best case: drop some packets, then encounter a large block of sequential packets, consume them and the current packet
            // becomes the desired one, and we can process it
            distance = rtp::rtp_seq_distance(m_desired_seq_id, seq);
        }

        // The m_packet is the one we need, no need to copy it to jitter buffer, we will use it now
        if (distance == 0)
        {
            process_packet(rtp_packet, packet_size);

            // Maybe we already have the next packet
            auto &head = m_packet_views[m_packet_view_head];
            while (head.is_valid())
            {
                auto &packet = m_jitter_buffer[head.index];
                process_packet(reinterpret_cast<rtp::RTPHeader *>(packet.data), head.size);
                // Update head
                head = m_packet_views[m_packet_view_head];
            }
        }
        // We need another packet first
        else
        {
            auto slot = alloc_jitter_slot();
            if (!slot.has_value())
            {
                return;
            }

            // Copy data to jitter slot to be able to access it later
            auto &slot_data = m_jitter_buffer[slot.value()];
            memcpy(slot_data.data, packet_data, packet_size);

            // Create view
            const RtpPacketView view {
                .index = slot.value(),
                .size  = packet_size,
            };

            // head of array is the position of the desired
            const size_t i    = (m_packet_view_head + distance) % WVB_EARLY_FRAME_TOLERANCE;
            m_packet_views[i] = view;
        }
    }

    bool IRtpDepacketizer::receive_frame_data(const uint8_t **__restrict out_frame_data,
                                              size_t *__restrict out_frame_size,
                                              uint32_t *__restrict out_frame_index,
                                              bool *__restrict out_eos,
                                              uint32_t *__restrict out_rtp_timestamp,
                                              uint32_t *__restrict out_rtp_pose_timestamp,
                                              std::chrono::steady_clock::time_point *out_last_packet_received_time,
                                              bool *__restrict out_save_frame)
    {
        if (!m_has_frame)
        {
            return false;
        }

        *out_frame_data                = m_frame_data.data();
        *out_frame_size                = m_frame_data.size();
        *out_rtp_timestamp             = m_current_rtp_timestamp;
        *out_rtp_pose_timestamp        = m_current_rtp_pose_timestamp;
        *out_last_packet_received_time = m_last_packet_received_time;

        return true;
    }
} // namespace wvb