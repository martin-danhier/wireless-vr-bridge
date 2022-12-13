#include <wvb_common/ipc.h>

#include <iostream>
#include <test_framework.hpp>
#include <thread>
#ifdef __linux__
#include <fcntl.h>
#include <semaphore.h>
#elif _WIN32
#include <windows.h>
#endif

struct SharedState
{
    int value = 0;
};

#define MUTEX_NAME  "test_mutex"
#define MEMORY_NAME "test_memory"

TEST
{
    // Purposefully lock the mutex/semaphore to put the system under stress
    // Since IPC mutexes/semaphores are global kernel structures, it is possible that they will be in an initial locked state (for example if the
    // program crashed during a critical section).
    // The program needs to be able to detect such a situation and unlock so that it can
    // work properly
#ifdef __linux__
    // Get semaphore
    sem_unlink(MUTEX_NAME);
    sem_t *sem = sem_open(MUTEX_NAME, O_CREAT, 0644, 1);
    // To test stucked semaphore, lock it
    sem_wait(sem);

    sem_close(sem);
#elif _WIN32
    // Get mutex
    HANDLE mutex = CreateMutexA(nullptr, TRUE, MUTEX_NAME);
    // To test stucked mutex, lock it
    WaitForSingleObject(mutex, INFINITE);
    CloseHandle(mutex);
#endif

    // The classes are designed for inter process communication, but it should work similarly with threads

    // Create two threads
    std::thread t1(
        [&]()
        {
            // Create a shared data object
            wvb::SharedMemory<SharedState> shared_data(MUTEX_NAME, MEMORY_NAME);
            EXPECT_TRUE(shared_data.is_valid());

#ifdef __linux__
            // On Linux, a lock is done during the creation of the shared memory object to ensure that the semaphore is valid
            // We need an extra lock here to test the semaphore so that they are synchronized for our expected test results
            // In a real world scenario, it doesn't really matter if there is an additional lock
            {
                auto lock = shared_data.lock();
                EXPECT_TRUE(lock.is_valid());
                // wait so that the other thread starts, and wait during the initialization
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            // Then sleep to be sure thread 2 manages to finish initialization.
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
            // Thread 2 begins now
#endif
            std::cout << "Thread 1: start\n";

            // Lock the data
            {
                auto locked_data = shared_data.lock();
                ASSERT_TRUE(locked_data.is_valid());
                locked_data->value = 42;

                // Sleep for a while before unlocking
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                EXPECT_EQ(locked_data->value, 42);
            }

            // Sleep for a while before locking again
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            {
                // The other thread should have locked the data by now
                auto locked_data = shared_data.lock(5);
                EXPECT_FALSE(locked_data.is_valid());
            }

            // But without a timeout, it should work
            {
                auto locked_data = shared_data.lock();
                ASSERT_TRUE(locked_data.is_valid());
                EXPECT_EQ(locked_data->value, 55);
            }

            EXPECT_TRUE(shared_data.is_valid());
        });

    std::thread t2(
        [&]()
        {
            // Wait
            std::this_thread::sleep_for(std::chrono::milliseconds(5));

            // Create a shared data object
            wvb::SharedMemory<SharedState> shared_data(MUTEX_NAME, MEMORY_NAME);
            EXPECT_TRUE(shared_data.is_valid());


            // Wait a bit before locking so that we are sure that t1 has locked the data
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            std::cout << "Thread 2: start\n";

            {
                auto locked_data = shared_data.lock(5);
                // It should not be possible to lock the data within 5ms
                EXPECT_FALSE(locked_data.is_valid());
            }
            {
                // Without a timeout, it should be possible to lock the data
                auto locked_data = shared_data.lock();
                ASSERT_TRUE(locked_data.is_valid());
                EXPECT_EQ(locked_data->value, 42);
                locked_data->value = 55;

                // Sleep for a while before unlocking
                std::this_thread::sleep_for(std::chrono::milliseconds(40));

                EXPECT_EQ(locked_data->value, 55);
            }

            EXPECT_TRUE(shared_data.is_valid());
        });

    // Wait for the threads to finish
    t1.join();
    t2.join();
}