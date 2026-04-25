#include "Log.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include <spdlog/spdlog.h>
#include <vector>
#include <chrono>

namespace engine {

std::shared_ptr<spdlog::logger> Log::s_coreLogger;
std::shared_ptr<spdlog::logger> Log::s_clientLogger;

void Log::init() {

    const std::string pattern = "%^[%T] [%n] [%l]: %v%$";

    std::vector<spdlog::sink_ptr> coreSinks = {
        std::make_shared<spdlog::sinks::stdout_color_sink_mt>(),
        std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/engine.log", true)
    };
    std::vector<spdlog::sink_ptr> clientSinks = {
        std::make_shared<spdlog::sinks::stdout_color_sink_mt>(),
        std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/game.log", true)
    };

    s_coreLogger = std::make_shared<spdlog::logger>("ENGINE", coreSinks.begin(), coreSinks.end());
    s_coreLogger->set_pattern(pattern);
    s_coreLogger->set_level(spdlog::level::trace);
    s_coreLogger->flush_on(spdlog::level::trace);
    spdlog::register_logger(s_coreLogger);

    s_clientLogger = std::make_shared<spdlog::logger>("GAME", clientSinks.begin(), clientSinks.end());
    s_clientLogger->set_pattern(pattern);
    s_clientLogger->set_level(spdlog::level::trace);
    s_clientLogger->flush_on(spdlog::level::trace);
    spdlog::register_logger(s_clientLogger);

    spdlog::flush_every(std::chrono::seconds(1));

    FP_CORE_INFO("Log system initialized");
}

void Log::shutdown() {
    FP_CORE_INFO("Log system shutting down");
    spdlog::shutdown();
}

} // namespace engine