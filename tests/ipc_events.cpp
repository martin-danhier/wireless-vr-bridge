#include <wvb_common/ipc.h>

#include <iostream>
#include <test_framework.hpp>
#include <thread>

struct SharedState
{
    int value = 0;
};

#define MUTEX_NAME  "TEST_MUTEX"
#define MEMORY_NAME "TEST_MEMORY"

TEST
{
    std::thread t1(
        [&]
        {
            wvb::SharedMemory<SharedState> memory(MUTEX_NAME, MEMORY_NAME);

            // We will use two events for signaling
            wvb::InterProcessEvent new_t1_data("NEW_T1_DATA", true);
            wvb::InterProcessEvent new_t2_data("NEW_T2_DATA", false);

            // Sleep
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            // Start by setting the value
            {
                auto lock   = memory.lock();
                lock->value = 1;
            }
            // Signal the other thread that we have set the value
            std::cout << "T1: Set value from thread 1\n";
            new_t1_data.signal();

            // Wait for the other thread to set the value
            new_t2_data.wait();

            std::cout << "T1: Received value from thread 2\n";

            // Read the value
            {
                auto lock = memory.lock();
                EXPECT_EQ(lock->value, 2);

                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                lock->value = 42;
            }
            // Signal again
            std::cout << "T1: Set value from thread 1\n";
            new_t1_data.signal();

            // Active wait to test is_triggered() function
            while (!new_t2_data.is_signaled())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            EXPECT_TRUE(new_t2_data.is_signaled());
            {
                auto lock = memory.lock();
                EXPECT_EQ(lock->value, 1337);
            }
            // Since no wait was used, we need to manually reset
            std::cout << "T1: Received value from thread 2\n";
            new_t2_data.reset();
            EXPECT_FALSE(new_t2_data.is_signaled());
        });

    std::thread t2(
        [&]
        {
            wvb::SharedMemory<SharedState> memory(MUTEX_NAME, MEMORY_NAME);

            // We will use two events for signaling
            wvb::InterProcessEvent new_t1_data("NEW_T1_DATA", false);
            wvb::InterProcessEvent new_t2_data("NEW_T2_DATA", true);

            // Wait for the other thread to set the value
            new_t1_data.wait();
            std::cout << "T2: Received value from thread 1\n";

            // Value should be 1
            {
                auto lock = memory.lock();
                EXPECT_EQ(lock->value, 1);

                // Sleep
                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                // Update data
                lock->value = 2;
            }
            // Signal the other thread that we have set the value
            std::cout << "T2: Set value from thread 2\n";
            new_t2_data.signal();

            // Wait again
            new_t1_data.wait();
            std::cout << "T2: Received value from thread 1\n";
            {
                auto lock = memory.lock();
                EXPECT_EQ(lock->value, 42);

                // Sleep
                std::this_thread::sleep_for(std::chrono::milliseconds(10));

                lock->value = 1337;
            }

            // Signal again
            std::cout << "T2: Set value from thread 2\n";
            new_t2_data.signal();
        });

    // Join threads
    t1.join();
    t2.join();
}