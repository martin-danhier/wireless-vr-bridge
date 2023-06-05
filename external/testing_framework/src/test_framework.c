/**
 * @file Simple lightweight testing framework for C or C++ projects, inspired by Google Tests API.
 * @author Martin Danhier
 */

#include "test_framework.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Macros for pretty printing ---

bool TF_INITIALIZED  = false;
bool TF_COLOR_OUTPUT = true;

// If WIN32 or _WIN32 or WIN64 or _WIN64 is defined, we are on a Windows platform.
#if WIN32 || _WIN32 || WIN64 || _WIN64
#include <windows.h>

// On Windows, without Windows Terminal, ANSI color codes are not supported

// On Windows, we need to store the previous color in order to reset it
HANDLE TF_CONSOLE_HANDLE          = 0;
WORD   TF_DEFAULT_COLOR_ATTRIBUTE = 0;

// Inits the stored variables
#define TF_INIT_FORMATTING                                                            \
    do                                                                                \
    {                                                                                 \
        if (TF_COLOR_OUTPUT) {                                                        \
            TF_CONSOLE_HANDLE                      = GetStdHandle(STD_OUTPUT_HANDLE); \
            CONSOLE_SCREEN_BUFFER_INFO consoleInfo = {0};                             \
            GetConsoleScreenBufferInfo(TF_CONSOLE_HANDLE, &consoleInfo);              \
            TF_DEFAULT_COLOR_ATTRIBUTE = consoleInfo.wAttributes;                     \
        }                                                                             \
    } while (0)

// Applies the various colors and settings
#define TF_FORMAT_BOLD_RED   if (TF_COLOR_OUTPUT) SetConsoleTextAttribute(TF_CONSOLE_HANDLE, 0xC)
#define TF_FORMAT_BOLD_GREEN if (TF_COLOR_OUTPUT) SetConsoleTextAttribute(TF_CONSOLE_HANDLE, 0xA)
#define TF_FORMAT_RED        if (TF_COLOR_OUTPUT) SetConsoleTextAttribute(TF_CONSOLE_HANDLE, 0x4)
#define TF_FORMAT_YELLOW     if (TF_COLOR_OUTPUT) SetConsoleTextAttribute(TF_CONSOLE_HANDLE, 0x6)
#define TF_FORMAT_BOLD       if (TF_COLOR_OUTPUT) SetConsoleTextAttribute(TF_CONSOLE_HANDLE, 0xF)
#define TF_FORMAT_RESET      if (TF_COLOR_OUTPUT) SetConsoleTextAttribute(TF_CONSOLE_HANDLE, TF_DEFAULT_COLOR_ATTRIBUTE)

#else

// On civilized terminals, we just need to print an ANSI color code.
#define TF_INIT_FORMATTING
#define TF_FORMAT_BOLD_RED   if (TF_COLOR_OUTPUT) printf("\033[31;1m")
#define TF_FORMAT_BOLD_GREEN if (TF_COLOR_OUTPUT) printf("\033[32;1m")
#define TF_FORMAT_RED        if (TF_COLOR_OUTPUT) printf("\033[31m")
#define TF_FORMAT_YELLOW     if (TF_COLOR_OUTPUT) printf("\033[93m")
#define TF_FORMAT_BOLD       if (TF_COLOR_OUTPUT) printf("\033[1m")
#define TF_FORMAT_RESET      if (TF_COLOR_OUTPUT) printf("\033[0m")
#endif


// --- Types ---

typedef enum tf_error_severity
{
    TF_ERROR,
    TF_WARNING
} tf_error_severity;

typedef struct tf_error
{
    tf_error_severity severity;
    size_t            line_number;
    tf_message        message;
    const char       *file;
} tf_error;

typedef struct tf_linked_list_element
{
    struct tf_linked_list_element *next;
    void                          *data;
} tf_linked_list_element;

typedef struct tf_linked_list
{
    tf_linked_list_element *head;
    tf_linked_list_element *tail;
    size_t                  count;
} tf_linked_list;

typedef struct tf_linked_list_it
{
    tf_linked_list_element *current;
    size_t                  index;
    void                   *value;
} tf_linked_list_it;

typedef struct tf_context
{
    tf_linked_list errors;
} tf_context;

// --- Functions ---

void tf_linked_list_push(tf_linked_list *linked_list, void *value, size_t value_size)
{
    // Allocate element
    void                   *value_alloc = NULL;
    tf_linked_list_element *element     = calloc(1, sizeof(tf_linked_list_element));
    bool                    success     = element != NULL;

    // Allocate value
    if (success)
    {
        value_alloc = calloc(1, value_size);
        success     = value_alloc != NULL;
    }

    // Copy value
    if (success)
    {
        success = memcpy(value_alloc, value, value_size) != NULL;
    }

    // Handle any error above
    if (!success)
    {
        fprintf(stderr, "Test Framework: an error occurred while creating an error.");
        abort();
    }

    // Save data to element
    element->data = value_alloc;

    // Empty: add it to head
    if (linked_list->count == 0)
    {
        linked_list->head = element;
    }
    // Not empty: add it in the tail's next field
    else
    {
        linked_list->tail->next = element;
    }

    linked_list->tail = element;
    linked_list->count++;
}

tf_linked_list_it tf_linked_list_iterator(tf_linked_list *linked_list)
{
    return (tf_linked_list_it) {
        .current = linked_list->head,
        .value   = NULL,
        .index   = -1,
    };
}

bool tf_linked_list_next(tf_linked_list_it *it)
{
    if (it->current != NULL)
    {
        it->index++;

        // For the index 0, we want to return the head, so don't go to the next one yet
        if (it->index > 0)
        {
            it->current = it->current->next;
        }

        // Get value
        if (it->current != NULL)
        {
            it->value = it->current->data;
            return true;
        }
        else
        {
            return false;
        }
    }

    return false;
}

void tf_linked_list_clear(tf_linked_list *linked_list)
{
    // Free values in linked list
    tf_linked_list_element *current = linked_list->head;
    while (current != NULL)
    {
        tf_linked_list_element *next = current->next;
        if (current->data != NULL)
        {
            free(current->data);
        }
        free(current);
        current = next;
    }

    // Clear the error list itself
    linked_list->head  = NULL;
    linked_list->tail  = NULL;
    linked_list->count = 0;
}

void tf_context_add_error(tf_context *context, tf_error *error)
{
    tf_linked_list_push(&context->errors, error, sizeof(tf_error));
}

tf_context *tf_create_context(void)
{
    tf_context *context = calloc(1, sizeof(tf_context));
    return context;
}

void tf_delete_context(tf_context *context)
{
    if (context == NULL)
    {
        return;
    }

    // Free dynamic messages
    tf_linked_list_it it = tf_linked_list_iterator(&context->errors);
    while (tf_linked_list_next(&it))
    {
        tf_error *error = it.value;

        // Free message
        if (error->message.message_is_dynamic)
        {
            free((void *) error->message.message);
            error->message.message = NULL;
        }
    }

    tf_linked_list_clear(&context->errors);
    free(context);
}

// Helper

tf_message tf_const_msg(const char *message)
{
    return (tf_message) {
        .message_is_dynamic = false,
        .message            = message,
    };
}

tf_message tf_dynamic_msg(const char *message)
{
    // Creates a message that is dynamically allocated.
    char *message_alloc = malloc(strlen(message) + 1);
    strcpy(message_alloc, message);

    return (tf_message) {
        .message_is_dynamic = true,
        .message            = message_alloc,
    };
}

// Asserts

bool tf_assert_common(tf_context *context, size_t line_number, const char *file, bool condition, tf_message message, bool recoverable)
{
    if (condition != true)
    {
        // Save error in context
        tf_error error = {
            .severity    = TF_ERROR,
            .line_number = line_number,
            .file        = file,
            .message     = message,
        };
        tf_context_add_error(context, &error);

        // Return a bool telling it the execution can continue
        return recoverable;
    }
    return true;
}

bool tf_assert_true(tf_context *context, size_t line_number, const char *file, bool condition, bool recoverable)
{
    return tf_assert_common(context,
                            line_number,
                            file,
                            condition,
                            recoverable
                                ? tf_const_msg("Condition failed. Expected [true], got [false].")
                                : tf_const_msg("Assertion failed. Expected [true], got [false]. Unable to continue execution."),
                            recoverable);
}

bool tf_assert_false(tf_context *context, size_t line_number, const char *file, bool condition, bool recoverable)
{
    return tf_assert_common(context,
                            line_number,
                            file,
                            !condition,
                            recoverable
                                ? tf_const_msg("Condition failed. Expected [false], got [true].")
                                : tf_const_msg("Assertion failed. Expected [false], got [true]. Unable to continue execution."),
                            recoverable);
}

bool tf_assert_not_null(tf_context *context, size_t line_number, const char *file, void *pointer, bool recoverable)
{
    return tf_assert_common(context,
                            line_number,
                            file,
                            pointer != NULL,
                            recoverable
                                ? tf_const_msg("Condition failed. Got [NULL], expected something else.")
                                : tf_const_msg("Assertion failed. Got [NULL], expected something else. Unable to continue execution."),
                            recoverable);
}

bool tf_assert_null(tf_context *context, size_t line_number, const char *file, void *pointer, bool recoverable)
{
    return tf_assert_common(context,
                            line_number,
                            file,
                            pointer == NULL,
                            recoverable
                                ? tf_const_msg("Condition failed. Got [NULL], expected something else.")
                                : tf_const_msg("Assertion failed. Got [NULL], expected something else. Unable to continue execution."),
                            recoverable);
}

bool tf_assert_error(tf_context *context, size_t line_number, const char *file, const char *message, bool recoverable)
{
    return tf_assert_common(context,
                            line_number,
                            file,
                            false, // Always false
                            tf_const_msg(message),
                            recoverable);
}

// Main

bool tf_run_test(tf_test_function pfn_test, void *next)
{
    printf("\n");

    // Create context to hold errors
    tf_context *context = tf_create_context();

    // Run test
    pfn_test(context, next);

    // Print result

    printf(" ---> ");
    bool success = context->errors.count == 0;
    if (success)
    {
        TF_FORMAT_BOLD_GREEN;
        printf("PASSED");
    }
    else
    {
        TF_FORMAT_BOLD_RED;
        printf("FAILED");
    }
    TF_FORMAT_RESET;
    printf("\n");

    // Print errors if there is any
    tf_linked_list_it err_it = tf_linked_list_iterator(&context->errors);
    while (tf_linked_list_next(&err_it))
    {
        tf_error *current_error = err_it.value;

        // Convert the severity to string
        printf("  - [");
        switch (current_error->severity)
        {
            case TF_ERROR:
                TF_FORMAT_RED;
                printf("Error");
                break;
            case TF_WARNING:
                TF_FORMAT_YELLOW;
                printf("Warning");
                break;
        }
        TF_FORMAT_RESET;

        printf("] %s:%zu\n    %s\n", current_error->file, current_error->line_number, current_error->message.message);
    }

    printf("\n");

    // Destroy context
    tf_delete_context(context);

    return success;
}

int tf_main(tf_test_function pfn_test, void *info, int argc, char **argv)
{
    if (!TF_INITIALIZED)
    {
        // If --no-color is passed, disable colors
        for (int i = 0; i < argc; i++)
        {
            if (strcmp(argv[i], "--no-color") == 0)
            {
                TF_COLOR_OUTPUT = false;
                break;
            }
        }

        // Alternatively, check if NO_COLOR environment variable is set
        if (TF_COLOR_OUTPUT && getenv("NO_COLOR") != NULL)
        {
            TF_COLOR_OUTPUT = false;
        }
        
        TF_INIT_FORMATTING;
        TF_INITIALIZED = true;
    }

    bool result = tf_run_test(pfn_test, info);

    return result == true ? EXIT_SUCCESS : EXIT_FAILURE;
}
