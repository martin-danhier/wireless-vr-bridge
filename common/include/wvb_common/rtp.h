#pragma once

#include <wvb_common/rtp_clock.h>

#include <cstdint>

namespace wvb::rtp
{

    // =============================================================
    // =                       Data types                          =
    // =============================================================

// Version 2, no padding, no extension, no CSRC
#define RTP_FIRST_BYTE_BASE   (uint8_t)(0b10000000u)
#define RTP_MARKER_BIT        (uint8_t)(0b10000000u)
#define RTP_PAYLOAD_TYPE_MASK (uint8_t)(0b01111111u)

    enum class RTPPayloadType : uint8_t
    {
        INVALID = 0u,

        // In RFC3551, values between 96 and 127 are typically used for dynamic payloads types.
        // Even if we don't really follow the profile, using this range may help traffic analysis tools like Wireshark

        // Add payload types here when new supported codecs are added

        /** H-264 video, RFC6184 */
        H264 = 97u,

        /** OPUS audio, RFC7587 */
        OPUS = 143u,
    };

#pragma pack(push, 1)

    struct RTPHeader
    {
        // +-+-+-+-+-+-+-+-+
        // |V=2|P|X|  CC   |
        // +-+-+-+-+-+-+-+-+
        // V: version. Always 2
        // P: padding. Set to 1 if the m_packet is padded. Default to no padding.
        // X: header extension present. We don't use any
        // CC: number of CSRC. We don't use any
        uint8_t first_byte = RTP_FIRST_BYTE_BASE;
        // +-+-+-+-+-+-+-+-+
        // |M|     PT      |
        // +-+-+-+-+-+-+-+-+
        // M: marker. Set to 1 if this m_packet is the last of the frame.
        // PT: payload type. One of the values of the RTPPayloadType enum.
        uint8_t  profile_byte    = 0;
        uint16_t sequence_number = 0;
        uint32_t timestamp       = 0;
        uint32_t ssrc            = 0;
        // Custom extension to the header for additional VR data
        uint32_t pose_timestamp_ext = 0;
        uint32_t frame_id_ext       = 0;

        // === Helper functions ===

        /** Set payload and marker byte */
        constexpr inline void set_payload(RTPPayloadType payload_type, bool last_frame = false)
        {
            if (last_frame)
            {
                profile_byte = static_cast<uint8_t>(payload_type) | RTP_MARKER_BIT;
            }
            else
            {
                profile_byte = static_cast<uint8_t>(payload_type);
            }
        }

        constexpr inline void set_marker(bool last_frame)
        {
            if (last_frame)
            {
                profile_byte |= RTP_MARKER_BIT;
            }
            else
            {
                profile_byte &= ~RTP_MARKER_BIT;
            }
        }

        [[nodiscard]] inline RTPPayloadType payload_type() const
        {
            switch (profile_byte & RTP_PAYLOAD_TYPE_MASK)
            {
                case 97: return RTPPayloadType::H264;
                case 143: return RTPPayloadType::OPUS;
                default: return RTPPayloadType::INVALID;
            }
        }

        [[nodiscard]] bool is_marker() const { return (profile_byte & RTP_MARKER_BIT) != 0; }
    };

#pragma pack(pop)

    /**
     * Compare two RTP timestamps, taking into account the wrap-around
     * @return true if a < b
     */
    [[nodiscard]] constexpr bool compare_rtp_timestamps(uint32_t a, uint32_t b)
    {
        // a is smaller than b if a - b underflows
        return (a - b) > (UINT32_MAX / 2u);
    }

    [[nodiscard]] constexpr bool compare_rtp_seq(uint16_t a, uint16_t b)
    {
        // a is smaller than b if a - b underflows
        return (a - b) > (UINT16_MAX / 2u);
    }

    /** Absolute distance between two RTP timestamps, taking into account the wrap-around */
    [[nodiscard]] constexpr uint32_t rtp_timestamps_distance_absolute(uint32_t a, uint32_t b)
    {
        // Distance between two timestamps, taking into account the wrap-around
        if (a <= 0x40000000 && b >= 0xC0000000)
        {
            // Consider that b < a
            // Wrapped around: distance = distance from b to max + distance from min to a
            return (UINT32_MAX - b) + a + 1;
        }
        else if (b <= 0x40000000 && a >= 0xC0000000)
        {
            // Consider that a < b
            // Wrapped around: distance = distance from a to max + distance from min to b
            return (UINT32_MAX - a) + b + 1;
        }
        else if (a > b)
        {
            // No wrap-around
            return a - b;
        }
        else
        {
            // No wrap-around
            return b - a;
        }
    }

    [[nodiscard]] constexpr int64_t rtp_timestamps_distance(uint32_t small, uint32_t high)
    {
        // Distance between two timestamps, taking into account the wrap-around
        if (small <= 0x40000000 && high >= 0xC0000000)
        {
            // Consider that high < small, so return a negative value
            // Wrapped around: distance = distance from b to max + distance from min to a
            return -static_cast<uint64_t>((UINT32_MAX - high) + small + 1);
        }
        else if (high <= 0x40000000 && small >= 0xC0000000)
        {
            // Consider that a < b
            // Wrapped around: distance = distance from a to max + distance from min to b
            return static_cast<uint64_t>((UINT32_MAX - small) + high + 1);
        }
        else
        {
            // No wrap-around
            return static_cast<int64_t>(high) - static_cast<int64_t>(small);
        }
    }

    [[nodiscard]] std::chrono::microseconds rtp_timestamps_distance_us(uint32_t a, uint32_t b, const RTPClock &clock);

    [[nodiscard]] constexpr uint16_t rtp_seq_distance(uint16_t small, uint16_t high)
    {
        // Distance between two sequence numbers, taking into account the wrap-around
        if (small <= high)
        {
            return high - small;
        }
        else
        {
            // Wrapped around: distance = distance from small to max + distance from min to high
            return (UINT16_MAX - small) + high + 1;
        }
    }

} // namespace wvb::rtp