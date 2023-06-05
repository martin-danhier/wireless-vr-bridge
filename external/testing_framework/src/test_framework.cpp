/**
 * @file Simple lightweight testing framework for C or C++ projects, inspired by Google Tests API. Extension for C++.
 * @author Martin Danhier
 */

#include "test_framework.hpp"

#include <exception>
#include <string>

// --- Types ---

struct tf_cpp_wrapper_info
{
    tf_test_function pfn_test;
    size_t main_line_number;
    const char *main_file;
};

// --- Functions ---

bool tf_assert_throws(tf_context *context, size_t line_number, const char *file, const tf_callback &fn, bool recoverable)
{
    // Run the test
    bool caught = false;
    try
    {
        fn();
    }
    catch (std::exception &e)
    {
        caught = true;
    }

    if (!caught)
    {
        // Equivalent in C++
        std::string message = recoverable ? "Condition" : "Assertion";
        message += " failed. Expected the given function to throw an exception.";
        message += recoverable ? "" : " Unable to continue execution.";

        return tf_assert_common(context, line_number, file, false, tf_dynamic_msg(message.c_str()), recoverable);
    }
    return true;
}

bool tf_assert_no_throws(tf_context *context, size_t line_number, const char *file, const tf_callback &fn, bool recoverable)
{
    // Run the test
    try
    {
        fn();
    }
    catch (std::exception &e)
    {
        // Equivalent in C++
        std::string message = recoverable ? "Condition" : "Assertion";
        message += " failed. Caught unexpected exception: \"";
        message += e.what();
        message += "\".";
        message += recoverable ? "" : " Unable to continue execution.";

        return tf_assert_common(context, line_number, file, false, tf_dynamic_msg(message.c_str()), recoverable);
    }
    return true;
}

void tf_thread_wrapper(tf_callback fn, tf_context *context, size_t line, const char *file)
{
    try
    {
        fn();
    }
    catch (std::exception &e)
    {
        std::string message = "Uncaught exception: \"";
        message += e.what();
        message += "\". Unable to continue execution.";

        tf_assert_common(context, line, file, false, tf_dynamic_msg(message.c_str()), false);
    }
}

void tf_test_wrapper(tf_context *context, void *info)
{
    tf_cpp_wrapper_info *cpp_info = (tf_cpp_wrapper_info *)info;

    try
    {
        cpp_info->pfn_test(context, nullptr);
    }
    catch (std::exception &e)
    {
        std::string message = "Uncaught exception: \"";
        message += e.what();
        message += "\". Unable to continue execution.";

        tf_assert_common(context, cpp_info->main_line_number, cpp_info->main_file, false, tf_dynamic_msg(message.c_str()), false);
    }
}

int tf_main_cpp(tf_test_function pfn_test, size_t line_number, const char *file, int argc, char **argv)
{

    tf_cpp_wrapper_info tf_test_wrapper_info = {pfn_test, line_number, file};

    return tf_main(&tf_test_wrapper, &tf_test_wrapper_info, argc, argv);
}