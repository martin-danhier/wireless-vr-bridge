#include "wvb_common/rtp.h"

namespace wvb::rtp
{
    std::chrono::microseconds rtp_timestamps_distance_us(uint32_t a, uint32_t b, const RTPClock &clock)
    {
        auto distance_ticks = rtp_timestamps_distance(a, b);
        bool negative       = distance_ticks < 0;
        if (negative)
        {
            distance_ticks = -distance_ticks;
            return -std::chrono::duration_cast<std::chrono::microseconds>(clock.from_rtp_timestamp(distance_ticks).time_since_epoch());
        }
        else
        {
            return std::chrono::duration_cast<std::chrono::microseconds>(clock.from_rtp_timestamp(distance_ticks).time_since_epoch());
        }
    }
} // namespace wvb::rtp