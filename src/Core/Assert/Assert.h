#pragma once
#include "Core/Log/Log.h"
#include <filesystem>
#include <cstdio>
#include <cstdlib>

#if defined(_MSC_VER) || defined(__clang__)
    #define FP_DEBUGBREAK() __debugbreak()
#elif defined(__GNUC__)
    #define FP_DEBUGBREAK() __builtin_trap()
#else
    #include <csignal>
    #define FP_DEBUGBREAK() raise(SIGTRAP)
#endif

#ifdef FP_DEBUG
    #define FP_ASSERT_HALT() FP_DEBUGBREAK()
#else
    #define FP_ASSERT_HALT() std::abort()
#endif

#define FP_FILENAME \
    std::filesystem::path(__FILE__).filename().string().c_str()

#define FP_INTERNAL_ASSERT_IMPL(logger, condition, msg, ...)                       \
    do {                                                                           \
        if (!(condition)) {                                                        \
            if (::engine::Log::isInitialized()) {                                 \
                logger("[ASSERT FAILED] Condition: " #condition "\n"              \
                       "  Message : " msg "\n"                                    \
                       "  File    : {}:{}",                                       \
                       ##__VA_ARGS__, FP_FILENAME, __LINE__);                     \
            } else {                                                               \
                std::fprintf(stderr,                                               \
                    "[ASSERT FAILED] Condition: " #condition "\n"                 \
                    "  File    : %s:%d\n",                                        \
                    FP_FILENAME, __LINE__);                                       \
            }                                                                      \
            FP_ASSERT_HALT();                                                      \
        }                                                                          \
    } while(0)

#define FP_CORE_ASSERT(condition, msg, ...) \
    FP_INTERNAL_ASSERT_IMPL(FP_CORE_CRITICAL, condition, msg, ##__VA_ARGS__)

#define FP_ASSERT(condition, msg, ...) \
    FP_INTERNAL_ASSERT_IMPL(FP_CRITICAL, condition, msg, ##__VA_ARGS__)