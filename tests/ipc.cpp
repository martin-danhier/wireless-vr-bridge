#include <wvb_common/ipc.h>

#include <test_framework.hpp>
#include <thread>

struct SharedState
{
    int value = 0;
};

TEST
{
    // The classes are designed for inter process communication, but it should work similarly with threads

    // Create two threads
    std::thread t1(
        [&]()
        {
            // Create a shared data object
            wvb::SharedData<SharedState> shared_data("test_mutex", "test_mapping");
            EXPECT_TRUE(shared_data.is_valid());

            // Lock the data
            {
                auto locked_data = shared_data.lock();
                EXPECT_TRUE(locked_data.is_valid());
                locked_data->value = 42;

                // Sleep for a while before unlocking
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }

            // Sleep for a while before locking again
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

            {
                // The other thread should have locked the data by now
                auto locked_data = shared_data.lock(5);
                EXPECT_FALSE(locked_data.is_valid());
            }

            // But without a timeout, it should work
            {
                auto locked_data = shared_data.lock();
                EXPECT_TRUE(locked_data.is_valid());
                EXPECT_EQ(locked_data->value, 55);
            }
        });

    std::thread t2(
        [&]()
        {
            // Create a shared data object
            wvb::SharedData<SharedState> shared_data("test_mutex", "test_mapping");
            EXPECT_TRUE(shared_data.is_valid());

            // Wait a bit before locking so that we are sure that t1 has locked the data
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            {
                auto locked_data = shared_data.lock(5);
                // It should not be possible to lock the data within 5ms
                EXPECT_FALSE(locked_data.is_valid());
            }
            {
                // Without a timeout, it should be possible to lock the data
                auto locked_data = shared_data.lock();
                EXPECT_TRUE(locked_data.is_valid());
                EXPECT_EQ(locked_data->value, 42);
                locked_data->value = 55;

                // Sleep for a while before unlocking
                std::this_thread::sleep_for(std::chrono::milliseconds(20));

            }
        });

    // Wait for the threads to finish
    t1.join();
    t2.join();
}