#include <test_framework.hpp>
#include <iostream>

#define TEST_FN(name) void name(tf_context *___context___, void *_)


// Define "sub tests" to ensure that the tests fail when they should
TEST_FN(failing_expect_true) {
    EXPECT_TRUE(false);
}

TEST_FN(failing_expect_false) {
    EXPECT_FALSE(true);
}

TEST_FN(failing_expect_not_null) {
    EXPECT_NOT_NULL(nullptr);
}

TEST_FN(failing_expect_null) {
    EXPECT_NULL((void *)0x1234);
}

TEST_FN(failing_expect_error) {
    ERROR("This is an error");
}

TEST_FN(failing_assert_true) {
    ASSERT_TRUE(false);
}

TEST_FN(failing_assert_false) {
    ASSERT_FALSE(true);
}

TEST_FN(failing_assert_not_null) {
    ASSERT_NOT_NULL(nullptr);
}

TEST_FN(failing_assert_null) {
    ASSERT_NULL((void *)0x1234);
}

TEST_FN(failing_assert_error) {
    ERROR_AND_QUIT("This is an error");
}

// C++ specific
TEST_FN(failing_expect_eq) {
    EXPECT_EQ(1, 2);
}

TEST_FN(failing_expect_neq) {
    EXPECT_NEQ(1, 1);
}

TEST_FN(failing_expect_throws) {
    EXPECT_THROWS(int a = 2);
}

TEST_FN(failing_expect_no_throws) {
    EXPECT_NO_THROWS(throw std::runtime_error("Error"));
}

TEST_FN(failing_assert_eq) {
    ASSERT_EQ(1, 2);
}

TEST_FN(failing_assert_neq) {
    ASSERT_NEQ(1, 1);
}

TEST_FN(failing_assert_throws) {
    ASSERT_THROWS(int a = 2);
}

TEST_FN(failing_assert_no_throws) {
    ASSERT_NO_THROWS(throw std::runtime_error("Error"));
}

TEST_FN(uncaught_exception) {
    throw std::runtime_error("Error");
}

TEST_FN(failing_expect_eq_std_string) {
    ASSERT_EQ(std::string("Hello"), std::string("World"));
}

TEST_FN(failing_expect_neq_std_string) {
    ASSERT_NEQ(std::string("Hello"), std::string("Hello"));
}

TEST_FN(failing_assert_eq_std_string) {
    ASSERT_EQ(std::string("Hello"), std::string("World"));
}

TEST_FN(failing_assert_neq_std_string) {
    ASSERT_NEQ(std::string("Hello"), std::string("Hello"));
}




// Global test function

TEST {
    std::cout << "A bunch of tests is going to run. They should all fail, except the last one which should pass.\n\n";

    // List functions
    auto tests = {
        failing_expect_true,
        failing_expect_false,
        failing_expect_not_null,
        failing_expect_null,
        failing_expect_error,
        failing_assert_true,
        failing_assert_false,
        failing_assert_not_null,
        failing_assert_null,
        failing_assert_error,
        failing_expect_eq,
        failing_expect_neq,
        failing_expect_throws,
        failing_expect_no_throws,
        failing_assert_eq,
        failing_assert_neq,
        failing_assert_throws,
        failing_assert_no_throws,
        uncaught_exception,
        failing_expect_eq_std_string,
        failing_expect_neq_std_string,
        failing_assert_eq_std_string,
        failing_assert_neq_std_string
    };

    // Run tests
    for (auto test : tests) {
        auto res = tf_main_cpp(test, 0, "");
        EXPECT_EQ(res, 1);
    }
}