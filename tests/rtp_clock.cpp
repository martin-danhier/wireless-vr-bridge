#include <wvb_common/rtp_clock.h>

#include <iostream>
#include <test_framework.hpp>

TEST
{
    wvb::rtp::RTPClock rtp_clock;
    auto               test = rtp_clock.now();

    // Print number of seconds since 1/1/1970
    std::cout << "system_clock: "
              << std::chrono::duration_cast<std::chrono::seconds>(rtp_clock.system_time_epoch().time_since_epoch()).count()
              << std::endl;
    // Print number of steady clock ticks since steady clock epoch
    std::cout << "steady_clock: " << std::chrono::steady_clock::now().time_since_epoch().count() << std::endl;
    // Print number of nanoseconds since steady clock epoch
    std::cout << "nanoseconds:  "
              << std::chrono::duration_cast<std::chrono::nanoseconds>(rtp_clock.steady_time_epoch().time_since_epoch()).count()
              << std::endl;

    // Print number of ticks since steady clock epoch
    std::cout << "ticks:        "
              << std::chrono::duration_cast<wvb::rtp::RTPClock::ticks>(rtp_clock.steady_time_epoch().time_since_epoch()).count()
              << std::endl;

    auto ticks_epoch = std::chrono::duration_cast<wvb::rtp::RTPClock::ticks>(rtp_clock.steady_time_epoch().time_since_epoch()).count();
    // Expect 90KHZ ticks
    auto expected = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(rtp_clock.steady_time_epoch().time_since_epoch()).count()
        / 11111.111111111);
    EXPECT_EQ(ticks_epoch, expected);

    // Measure an interval
    auto time0    = rtp_clock.now();
    auto hr_time0 = std::chrono::high_resolution_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    auto time1    = rtp_clock.now();
    auto hr_time1 = std::chrono::high_resolution_clock::now();

    // Print the interval in ticks
    std::cout << "elapsed ticks:  " << (time1 - time0).count() << std::endl;
    // Print the interval in nanoseconds
    std::cout << "elapsed nanos:  " << std::chrono::duration_cast<std::chrono::nanoseconds>(hr_time1 - hr_time0).count() << std::endl;
    // Print the expected number of ticks
    expected =
        static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(hr_time1 - hr_time0).count() / 11111.111111111);

    // Expect the interval to be within 1 tick of the expected value
    auto distance = std::abs(expected - (time1 - time0).count());
    EXPECT_TRUE(distance <= 1);

    // NTP time
    const uint64_t ntp_epoch = static_cast<int64_t>(rtp_clock.ntp_epoch());
    std::cout << "ntp epoch:      " << ntp_epoch << std::endl;
    EXPECT_EQ(ntp_epoch,
              (uint64_t) (std::chrono::duration_cast<std::chrono::seconds>(rtp_clock.system_time_epoch().time_since_epoch()).count() + 2208988800));

    // Create new clock based on NTP time
    wvb::rtp::RTPClock rtp_clock2(ntp_epoch);

    // Epochs should be the same
    std::cout << "ntp epoch 2:   " << rtp_clock2.ntp_epoch() << std::endl;
    std::cout << "system epoch2: "
              << std::chrono::duration_cast<std::chrono::seconds>(rtp_clock2.system_time_epoch().time_since_epoch()).count()
              << std::endl;
    std::cout << "steady epoch2: " << rtp_clock2.steady_time_epoch().time_since_epoch().count() << std::endl;
    
    // System clocks should be the same
    EXPECT_TRUE(rtp_clock2.ntp_epoch() == rtp_clock.ntp_epoch());

}