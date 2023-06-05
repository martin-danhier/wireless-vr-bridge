/**
 * Extension of the testing framework with more types of expectations, for C++.
 * @author Martin Danhier
 */
#pragma once

#include <functional>
#include <string>
#include <memory>
#include <thread>
#include "test_framework.h"

// Macros

// clang-format off
// keep the formatting as below: we don't want line breaks
// It needs to be in a single line so that the __LINE__ macro is accurate

#define EXPECT_THROWS(fn) do { if (!tf_assert_throws(___context___, __LINE__, __FILE__, [&](){fn;}, true)) return; } while (0)
#define EXPECT_NO_THROWS(fn) do { if (!tf_assert_no_throws(___context___, __LINE__, __FILE__, [&](){fn;}, true)) return; } while (0)
#define EXPECT_EQ(actual, expected) do { if (!tf_assert_equal(___context___, __LINE__, __FILE__, (actual), (expected), true)) return; } while (0)
#define EXPECT_NEQ(actual, not_expected) do { if (!tf_assert_not_equal(___context___, __LINE__, __FILE__, (actual), (not_expected), true)) return; } while (0)
#define ASSERT_THROWS(fn) do { if (!tf_assert_throws(___context___, __LINE__, __FILE__, [&](){fn;}, false)) return; } while (0)
#define ASSERT_NO_THROWS(fn) do { if (!tf_assert_no_throws(___context___, __LINE__, __FILE__, [&](){fn;}, false)) return; } while (0)
#define ASSERT_EQ(actual, expected) do { if (!tf_assert_equal(___context___, __LINE__, __FILE__, (actual), (expected), false)) return; } while (0)
#define ASSERT_NEQ(actual, not_expected) do { if (!tf_assert_not_equal(___context___, __LINE__, __FILE__, (actual), (not_expected), false)) return; } while (0)

// clang-format on

#undef TF_TEST2
#define TF_TEST2(id)                                                     \
    void __test_##id(tf_context *___context___, void* ___info___);       \
    int  main(int argc, char **argv)                                     \
    {                                                                    \
        return tf_main_cpp(__test_##id, __LINE__, __FILE__, argc, argv); \
    }                                                                    \
    void __test_##id(tf_context *___context___, void* ___info___)

/** Starts a std::thread that catches uncaught exceptions and report them as test errors.
 * @param id The name of the thread variable.
 * @param fn The function to run in the thread. Should take a reference [&]
 * Note: the thread should be joined at the end of the test.
*/
#define START_THREAD(id, fn) std::thread id([&] { tf_thread_wrapper(fn, ___context___, __LINE__, __FILE__); })

// Functions

// lambda type
using tf_callback = std::function<void()>;

void tf_thread_wrapper(tf_callback fn, tf_context *context, size_t line, const char *file);
int tf_main_cpp(tf_test_function pfn_test, size_t main_line_number, const char *main_file, int argc = 0, char **argv = nullptr);
bool tf_assert_throws(tf_context *context, size_t line_number, const char *file, const tf_callback& fn, bool recoverable);
bool tf_assert_no_throws(tf_context *context, size_t line_number, const char *file, const tf_callback& fn, bool recoverable);

template<typename T>
std::string tf_to_string(const T &value)
{
    // If it is a string
    if constexpr (std::is_same<T, std::string>::value)
    {
        return "\"" + value + "\"";
    }
    else
    {
        return std::to_string(value);
    }
}

template<typename T>
bool tf_assert_equal(tf_context *context, size_t line_number, const char *file, const T &actual, const T &expected, bool recoverable)
{
    if (actual != expected)
    {
        std::string s = recoverable ? "Condition" : "Assertion";
        s += " failed. Expected: ";
        s += tf_to_string(expected);
        s += ", got: ";
        s += tf_to_string(actual);
        s += ".";
        s += recoverable ? "" : " Unable to continue execution.";


        return tf_assert_common(context, line_number, file, false, tf_dynamic_msg(s.c_str()), recoverable);
    }
    return true;
}

template<typename T>
bool tf_assert_not_equal(tf_context *context, size_t line_number, const char *file, const T &actual, const T &not_expected, bool recoverable)
{
    if (actual == not_expected)
    {
        std::string s = recoverable ? "Condition" : "Assertion";
        s += " failed. Expected something different than ";
        s += tf_to_string(not_expected);
        s += ", but got the same.";
        s += recoverable ? "" : " Unable to continue execution.";

        return tf_assert_common(context, line_number, file, false, tf_dynamic_msg(s.c_str()), recoverable);
    }
    return true;
}