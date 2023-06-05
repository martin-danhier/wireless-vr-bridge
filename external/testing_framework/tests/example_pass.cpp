#include <test_framework.hpp>

#include <stdexcept>

// This test should pass
TEST
{
    int value = 17;

    // --- Expectations---
    
    // If it fails, the execution continues. Errors are reported at the end.
    // Use this if possible, because it allows to detect multiple errors at once.
    EXPECT_TRUE(value == 17);
    EXPECT_FALSE(value == 2);
    EXPECT_NULL(nullptr);
    EXPECT_NOT_NULL(&value);
    // Only available for C++
    EXPECT_EQ(value, 17);
    EXPECT_NEQ(value, 2);
    EXPECT_THROWS(throw std::runtime_error("Error"));
    EXPECT_NO_THROWS(value = 17);
    EXPECT_EQ(std::string("Hello"), std::string("Hello"));
    EXPECT_NEQ(std::string("Hello"), std::string("World"));

    // --- Assertions ---

    // If it fails, the execution stops. Only the first error is thus reported.
    // Use this for critical issues (for example a pointer that is null, that would cause segfaults later)
    ASSERT_TRUE(value == 17);
    ASSERT_FALSE(value == 2);
    ASSERT_NULL(nullptr);
    ASSERT_NOT_NULL(&value);
    // Only available for C++
    ASSERT_EQ(value, 17);
    ASSERT_NEQ(value, 2);
    ASSERT_THROWS(throw std::runtime_error("Error"));
    ASSERT_NO_THROWS(value = 17);
    ASSERT_EQ(std::string("Hello"), std::string("Hello"));
    ASSERT_NEQ(std::string("Hello"), std::string("World"));
}