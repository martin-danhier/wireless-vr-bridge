#pragma once

/**
 * Boilerplate for a class using the Pimpl pattern (pointer to m_data private struct for internal variables).
 * When adding this macro to a class, you have to implement a destructor.
 * */
#define PIMPL_CLASS(Class)                                      \
  private:                                                      \
    struct Data;                                                \
    Data *m_data = nullptr;                                     \
                                                                \
  public:                                                       \
    Class(const Class &other) = delete;                         \
    inline Class(Class &&other) noexcept : m_data(other.m_data) \
    {                                                           \
        other.m_data = nullptr;                                 \
    }                                                           \
    Class        &operator=(Class &other) = delete;             \
    inline Class &operator=(Class &&other) noexcept             \
    {                                                           \
        this->~Class();                                         \
        m_data       = other.m_data;                            \
        other.m_data = nullptr;                                 \
        return *this;                                           \
    }                                                           \
    [[nodiscard]] inline bool is_valid() const noexcept         \
    {                                                           \
        return m_data != nullptr;                               \
    }                                                           \
    ~Class();

#define PIMPL_CLASS_COPIABLE(Class)                             \
  private:                                                      \
    struct Data;                                                \
    Data *m_data = nullptr;                                     \
                                                                \
  public:                                                       \
    inline Class(Class &&other) noexcept : m_data(other.m_data) \
    {                                                           \
        other.m_data = nullptr;                                 \
    }                                                           \
    inline Class &operator=(Class &&other) noexcept             \
    {                                                           \
        this->~Class();                                         \
        m_data       = other.m_data;                            \
        other.m_data = nullptr;                                 \
        return *this;                                           \
    }                                                           \
    [[nodiscard]] inline bool is_valid() const noexcept         \
    {                                                           \
        return m_data != nullptr;                               \
    }                                                           \
    ~Class();

#define PIMPL_VIRTUAL_CLASS(Class)                              \
  private:                                                      \
    struct Data;                                                \
    Data *m_data = nullptr;                                     \
                                                                \
  public:                                                       \
    Class(const Class &other) = delete;                         \
    inline Class(Class &&other) noexcept : m_data(other.m_data) \
    {                                                           \
        other.m_data = nullptr;                                 \
    }                                                           \
    Class        &operator=(Class &other) = delete;             \
    inline Class &operator=(Class &&other) noexcept             \
    {                                                           \
        this->~Class();                                         \
        m_data       = other.m_data;                            \
        other.m_data = nullptr;                                 \
        return *this;                                           \
    }                                                           \
    [[nodiscard]] inline bool is_valid() const noexcept         \
    {                                                           \
        return m_data != nullptr;                               \
    }                                                           \
    virtual ~Class();

#define DEFAULT_PIMPL_DESTRUCTOR(Class) \
    Class::~Class()                     \
    {                                   \
        if (m_data != nullptr)          \
        {                               \
            delete m_data;              \
            m_data = nullptr;           \
        }                               \
    }
#define DEFAULT_PIMPL_CONSTRUCTOR(Class) \
    Class::Class() : m_data(new Data) {}

#ifdef __ANDROID__
#include <android/log.h>
#define LOG(...)  __android_log_print(ANDROID_LOG_INFO, "WVB", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "WVB", __VA_ARGS__)
#define FLUSH_LOG()
#define FLUSH_LOGE()
#else
#define LOG(...)     printf(__VA_ARGS__)
#define LOGE(...)    fprintf(stderr, __VA_ARGS__)
#define FLUSH_LOG()  fflush(stdout)
#define FLUSH_LOGE() fflush(stderr)
#endif