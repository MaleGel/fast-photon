#pragma once
#include "Core/Log/Log.h"
#include <filesystem>

#if defined(_MSC_VER) || defined(__clang__)
    #define FP_DEBUGBREAK() __debugbreak()
#elif defined(__GNUC__)
    #define FP_DEBUGBREAK() __builtin_trap()
#else
    #include <csignal>
    #define FP_DEBUGBREAK() raise(SIGTRAP)
#endif

#define FP_FILENAME \
    std::filesystem::path(__FILE__).filename().string().c_str()

#define FP_INTERNAL_ASSERT_IMPL(logger, condition, msg, ...)        \
    do {                                                            \
        if (!(condition)) {                                         \
            logger("[ASSERT FAILED] Condition: " #condition "\n"   \
                   "  Message : " msg "\n"                         \
                   "  File    : {}:{}" ,                           \
                   ##__VA_ARGS__, FP_FILENAME, __LINE__);          \
            FP_DEBUGBREAK();                                        \
        }                                                           \
    } while(0)

#ifdef FP_DEBUG

    #define FP_CORE_ASSERT(condition, msg, ...) \
        FP_INTERNAL_ASSERT_IMPL(FP_CORE_CRITICAL, condition, msg, ##__VA_ARGS__)

    #define FP_ASSERT(condition, msg, ...) \
        FP_INTERNAL_ASSERT_IMPL(FP_CRITICAL, condition, msg, ##__VA_ARGS__)

#else
    #define FP_CORE_ASSERT(condition, msg, ...)                         \
        do {                                                            \
            if (!(condition)) {                                         \
                FP_CORE_ERROR("[ASSERT] Condition: " #condition "\n"   \
                              "  Message : " msg "\n"                  \
                              "  File    : {}:{}",                     \
                              ##__VA_ARGS__, FP_FILENAME, __LINE__);   \
            }                                                           \
        } while(0)

    #define FP_ASSERT(condition, msg, ...)                              \
        do {                                                            \
            if (!(condition)) {                                         \
                FP_ERROR("[ASSERT] Condition: " #condition "\n"        \
                         "  Message : " msg "\n"                       \
                         "  File    : {}:{}",                          \
                         ##__VA_ARGS__, FP_FILENAME, __LINE__);        \
            }                                                           \
        } while(0)

#endif // FP_DEBUG