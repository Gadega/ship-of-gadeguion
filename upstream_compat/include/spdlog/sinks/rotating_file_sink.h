#pragma once
#include "../spdlog.h"
namespace spdlog {
namespace sinks {
class rotating_file_sink_mt {};
} // namespace sinks
inline std::shared_ptr<logger> rotating_logger_mt(const std::string&, const std::string&, size_t, size_t) {
    return default_logger();
}
} // namespace spdlog
