#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/fmt/ostr.h>
#include <memory>

namespace engine {

class Log {
public:
    static void init();
    static void shutdown();

    static std::shared_ptr<spdlog::logger>& getCoreLogger()   { return s_coreLogger; }
    static std::shared_ptr<spdlog::logger>& getClientLogger() { return s_clientLogger; }

private:
    static std::shared_ptr<spdlog::logger> s_coreLogger;
    static std::shared_ptr<spdlog::logger> s_clientLogger;
};

} // namespace engine

// Engine
#define FP_CORE_TRACE(...)    ::engine::Log::getCoreLogger()->trace(__VA_ARGS__)
#define FP_CORE_INFO(...)     ::engine::Log::getCoreLogger()->info(__VA_ARGS__)
#define FP_CORE_WARN(...)     ::engine::Log::getCoreLogger()->warn(__VA_ARGS__)
#define FP_CORE_ERROR(...)    ::engine::Log::getCoreLogger()->error(__VA_ARGS__)
#define FP_CORE_CRITICAL(...) ::engine::Log::getCoreLogger()->critical(__VA_ARGS__)

// Client
#define FP_TRACE(...)         ::engine::Log::getClientLogger()->trace(__VA_ARGS__)
#define FP_INFO(...)          ::engine::Log::getClientLogger()->info(__VA_ARGS__)
#define FP_WARN(...)          ::engine::Log::getClientLogger()->warn(__VA_ARGS__)
#define FP_ERROR(...)         ::engine::Log::getClientLogger()->error(__VA_ARGS__)
#define FP_CRITICAL(...)      ::engine::Log::getClientLogger()->critical(__VA_ARGS__)