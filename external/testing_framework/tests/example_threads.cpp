#include <test_framework.hpp>
#include <stdexcept>

TEST
{

    int value = 2;

    // If a test requires several processes, such as a network test,
    // you should use the START_THREAD macro to start a new thread.
    START_THREAD(t1, [&] {

        // Similarly to the main test, threads started that way will
        // catch uncaught exceptions and report them as test errors.
        EXPECT_EQ(value, 2);
    });

    START_THREAD(t2, [&] {
        EXPECT_EQ(value, 2);
    });

    // Don't forget to join the threads at the end of the test.
    t1.join();
    t2.join();
}