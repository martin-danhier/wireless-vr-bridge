/**
 * @file Simple lightweight testing framework for C or C++ projects, inspired by Google Tests API.
 * @author Martin Danhier
 */

#pragma once

// C++ compatibility
#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// --- Macros ---

// First macro: add the counter macro
#define TEST TF_TEST1(__COUNTER__)
// Second: get the value of the counter
#define TF_TEST1(id) TF_TEST2(id)
// Third: generate the function definition and register it
#define TF_TEST2(id)                                               \
    void __test_##id(tf_context *___context___, void *___info___); \
    int main(int argc, char **argv)                                \
    {                                                              \
        return tf_main(__test_##id, NULL, argc, argv);             \
    }                                                              \
    void __test_##id(tf_context *___context___, void *___info___)

    // Macros for assertions

    // clang-format off
    // keep the formatting as below: we don't want line breaks
    // It needs to be in a single line so that the __LINE__ macro is accurate

// Expects: recoverable even if false (just a test)
#define EXPECT_TRUE(condition) do { if (!tf_assert_true(___context___, __LINE__, __FILE__, (condition), true)) return; } while (0)

#define EXPECT_FALSE(condition) do { if (!tf_assert_false(___context___, __LINE__, __FILE__, (condition), true)) return; } while (0)

#define EXPECT_NOT_NULL(pointer) do { if (!tf_assert_not_null(___context___, __LINE__, __FILE__, (pointer), true)) return; } while (0)

#define EXPECT_NULL(pointer) do { if (!tf_assert_null(___context___, __LINE__, __FILE__, (pointer), true)) return; } while (0)

#define ERROR(message) do { if (!tf_assert_error(___context___, __LINE__, __FILE__, (message), true)) return; } while (0)

// Asserts: non-recoverable if false
#define ASSERT_TRUE(condition) do { if (!tf_assert_true(___context___, __LINE__, __FILE__, (condition), false)) return; } while (0)

#define ASSERT_FALSE(condition) do { if (!tf_assert_false(___context___, __LINE__, __FILE__, (condition), false)) return; } while (0)

#define ASSERT_NOT_NULL(pointer) do { if (!tf_assert_not_null(___context___, __LINE__, __FILE__, (void*)(pointer), false)) return; } while (0)

#define ASSERT_NULL(pointer) do { if (!tf_assert_null(___context___, __LINE__, __FILE__, (void*)(pointer), false)) return; } while (0)

#define ERROR_AND_QUIT(message) do { if (!tf_assert_error(___context___, __LINE__, __FILE__, (message), false)) return; } while (0)

    // clang-format on

    // --- Types ---

    typedef struct tf_context tf_context;

    typedef struct tf_message
    {
        const char *message;
        bool message_is_dynamic;
    } tf_message;

    typedef void (*tf_test_function)(tf_context *ctx, void *pfn_next);

    // --- Functions ---

    int tf_main(tf_test_function pfn_test, void *info, int argc, char **argv);

    tf_message tf_const_msg(const char *message);

    tf_message tf_dynamic_msg(const char *message);

    bool tf_assert_common(tf_context *context, size_t line_number, const char *file, bool condition, tf_message message, bool recoverable);

    bool tf_assert_true(tf_context *context, size_t line_number, const char *file, bool condition, bool recoverable);

    bool tf_assert_false(tf_context *context, size_t line_number, const char *file, bool condition, bool recoverable);

    bool tf_assert_not_null(tf_context *context, size_t line_number, const char *file, void *pointer, bool recoverable);

    bool tf_assert_null(tf_context *context, size_t line_number, const char *file, void *pointer, bool recoverable);

    bool tf_assert_error(tf_context *context, size_t line_number, const char *file, const char *message, bool recoverable);

#ifdef __cplusplus
}
#endif