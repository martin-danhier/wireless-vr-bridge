#pragma once

#include <chrono>
#include <cstdint>
#ifdef __linux__
#include <stdexcept>
#include <time.h>

#endif

// Number of seconds between 1/1/1900 and 1/1/1970
#define UNIX_EPOCH_NTP   2208988800
#define NS_PER_SEC       1000000000
#define RTP_EPOCH_OFFSET std::chrono::minutes(30) // Put epoch 30min before now, so that if we need to adjust it we don't underflow

namespace wvb::rtp
{
    /**
     * Steady 90kHz clock for RTP timestamps.
     * It needs to be syncable between devices, so its epoch needs to be configurable.
     */
    class RTPClock
    {
      public:
        static constexpr bool is_steady = true;
        using rep                       = int64_t;
        using period                    = std::ratio<1, 90000>;
        using ticks                     = std::chrono::duration<rep, period>;
        using duration                  = ticks;
        using time_point                = std::chrono::time_point<std::chrono::steady_clock, duration>;

        // Offset for RTP timestamps
        int64_t offset = 0;

      private:
        std::chrono::system_clock::time_point m_system_epoch;
        std::chrono::steady_clock::time_point m_steady_epoch;
#ifdef __linux__
        timespec m_timespec_epoch {};
#endif

      public:
        RTPClock() { reset_epoch(); }

        explicit RTPClock(uint64_t ntp_epoch) { set_epoch(ntp_epoch); }

        /** Set the epoch to the current time */
        void reset_epoch()
        {
            // Get current system time (secs since 1/1/1970)
            const auto system_now = std::chrono::system_clock::now();
            // Get current steady clock time (nb of ticks since steady_clock epoch, typically since boot)
            const auto steady_now = std::chrono::steady_clock::now();
#ifdef __linux__
            timespec timespec_now {};
            clock_gettime(CLOCK_MONOTONIC, &timespec_now);
#endif

            m_system_epoch = std::chrono::time_point_cast<std::chrono::seconds>(system_now) - RTP_EPOCH_OFFSET;
            // Delay between system_now and the nearest second
            const auto delay = system_now - m_system_epoch;

            // Remove delay from steady_now to get the same time as rounded system_now
            m_steady_epoch = steady_now - delay;
#ifdef __linux__
            // Remove delay from timespec_now to get the same time as rounded system_now
            const std::chrono::nanoseconds delay_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(delay);
            m_timespec_epoch.tv_sec                 = timespec_now.tv_sec - (delay_ns.count() / NS_PER_SEC);
            m_timespec_epoch.tv_nsec                = timespec_now.tv_nsec - (delay_ns.count() % NS_PER_SEC);
            if (m_timespec_epoch.tv_nsec < 0)
            {
                m_timespec_epoch.tv_sec--;
                m_timespec_epoch.tv_nsec += NS_PER_SEC;
            }
            timespec ts = to_timespec(now());

#endif
        }

        /** Set the epoch to the given NTP time */
        void set_epoch(uint64_t ntp_epoch)
        {
            if (ntp_epoch < UNIX_EPOCH_NTP)
            {
                throw std::runtime_error("RTPClock: NTP epoch is too far in the past");
            }

            // Compute system epoch from ntp epoch
            m_system_epoch = std::chrono::system_clock::time_point(std::chrono::seconds(ntp_epoch - UNIX_EPOCH_NTP));
            // Get current steady clock time (nb of 90Khz ticks since steady_clock epoch, typically since boot)
            const auto steady_now   = std::chrono::steady_clock::now();
            const auto system_now   = std::chrono::system_clock::now();
            const auto system_delay = system_now - m_system_epoch;
            m_steady_epoch          = steady_now - system_delay;
#ifdef __linux__
            // Get current monotonic clock time (nb of ticks since monotonic_clock epoch, typically since boot)
            timespec timespec_now {};
            clock_gettime(CLOCK_MONOTONIC, &timespec_now);
            // Remove delay from timespec_now to get the same time as rounded system_now
            const std::chrono::nanoseconds delay_ns     = std::chrono::duration_cast<std::chrono::nanoseconds>(system_delay);
            const auto                     delay_s      = delay_ns.count() / NS_PER_SEC;
            const auto                     remainder_ns = delay_ns.count() % NS_PER_SEC;
            if (delay_s > timespec_now.tv_sec || (delay_s == timespec_now.tv_sec && remainder_ns > timespec_now.tv_nsec))
            {
                throw std::runtime_error("RTPClock: NTP epoch is too far in the past");
            }

            m_timespec_epoch.tv_sec  = timespec_now.tv_sec - delay_s;
            m_timespec_epoch.tv_nsec = timespec_now.tv_nsec - remainder_ns;
            if (m_timespec_epoch.tv_nsec < 0)
            {
                m_timespec_epoch.tv_sec--;
                m_timespec_epoch.tv_nsec += NS_PER_SEC;
            }
#endif
        }

        void move_epoch(std::chrono::microseconds amount)
        {
            m_system_epoch += amount;
            m_steady_epoch += amount;

#ifdef __linux__
            const auto delay_s      = amount.count() / NS_PER_SEC;
            const auto remainder_ns = (amount.count() * 1000) % NS_PER_SEC;

            m_timespec_epoch.tv_sec += delay_s;
            m_timespec_epoch.tv_nsec += remainder_ns;
            if (m_timespec_epoch.tv_nsec < 0)
            {
                m_timespec_epoch.tv_sec--;
                m_timespec_epoch.tv_nsec += NS_PER_SEC;
            }
            else if (m_timespec_epoch.tv_nsec >= NS_PER_SEC)
            {
                m_timespec_epoch.tv_sec++;
                m_timespec_epoch.tv_nsec -= NS_PER_SEC;
            }
#endif
        }

        [[nodiscard]] inline std::chrono::system_clock::time_point system_time_epoch() const { return m_system_epoch; }

        [[nodiscard]] inline std::chrono::steady_clock::time_point steady_time_epoch() const { return m_steady_epoch; }

#ifdef __linux__
        [[nodiscard]] inline timespec timespec_epoch() const { return m_timespec_epoch; }
#endif

        /** Return the number of seconds of epoch since 1/1/1900 */
        [[nodiscard]] inline uint64_t ntp_epoch() const
        {
            // Nb of seconds since 1/1/1970
            uint64_t system_epoch_sec = std::chrono::duration_cast<std::chrono::seconds>(m_system_epoch.time_since_epoch()).count();

            // Nb of seconds since 1/1/1900
            return system_epoch_sec + UNIX_EPOCH_NTP;
        }

        /** Returns the current time since RTP steady_epoch */
        [[nodiscard]] inline time_point now() const noexcept
        {
            const auto steady_now = std::chrono::steady_clock::now();
            return time_point(std::chrono::duration_cast<duration>(steady_now - m_steady_epoch));
        }

        /** Returns the current time since RTP steady_epoch */
        [[nodiscard]] inline uint32_t now_rtp_timestamp() const noexcept
        {
            const auto now = std::chrono::steady_clock::now();
            // Return the time in 90kHz ticks, wrapped around 32 bits if needed
            return static_cast<uint32_t>(std::chrono::duration_cast<duration>(now - m_steady_epoch).count()) + offset;
        }

        /** Returns a time point from a RTP timestamp */
        [[nodiscard]] inline time_point from_rtp_timestamp(uint32_t rtp_timestamp) const noexcept
        {
            return time_point(duration(rtp_timestamp - offset));
        }

        [[nodiscard]] inline uint32_t to_rtp_timestamp(time_point tp) const noexcept
        {
            return static_cast<uint32_t>(std::chrono::duration_cast<duration>(tp.time_since_epoch()).count()) + offset;
        }

        [[nodiscard]] inline time_point from_steady_timepoint(std::chrono::steady_clock::time_point tp) const noexcept
        {
            return time_point(std::chrono::duration_cast<duration>(tp - m_steady_epoch));
        }

#ifdef __linux__

        /** Converts a RTP time point to a timespec */
        [[nodiscard]] inline timespec to_timespec(time_point tp) const noexcept
        {
            const auto duration    = tp.time_since_epoch();
            const auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration);

            timespec ts;
            ts.tv_sec  = m_timespec_epoch.tv_sec + (duration_ns.count() / NS_PER_SEC);
            ts.tv_nsec = m_timespec_epoch.tv_nsec + (duration_ns.count() % NS_PER_SEC);
            if (ts.tv_nsec >= NS_PER_SEC)
            {
                ts.tv_sec++;
                ts.tv_nsec -= NS_PER_SEC;
            }
            return ts;
        }

        /** Converts a RTP time point to a timespec */
        [[nodiscard]] inline timespec to_timespec(uint32_t rtp_timestamp) const noexcept
        {
            return to_timespec(from_rtp_timestamp(rtp_timestamp));
        }

        /** Converts a timespec to a RTP time point */
        [[nodiscard]] inline time_point from_timespec(timespec ts) const noexcept
        {
            const auto seconds     = std::chrono::seconds(ts.tv_sec - m_timespec_epoch.tv_sec);
            const auto nanoseconds = std::chrono::nanoseconds(ts.tv_nsec - m_timespec_epoch.tv_nsec);

            return time_point(std::chrono::duration_cast<duration>(seconds + nanoseconds));
        }

        /** Converts a timespec to a RTP time stamp */
        [[nodiscard]] inline uint32_t rtp_timestamp_from_timespec(timespec ts) const noexcept
        {
            return static_cast<uint32_t>(from_timespec(ts).time_since_epoch().count()) + offset;
        }

#endif
    };

} // namespace wvb::rtp