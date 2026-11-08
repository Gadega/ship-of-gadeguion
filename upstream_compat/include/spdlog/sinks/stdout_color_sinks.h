#pragma once
#include "../spdlog.h"
namespace spdlog {
namespace sinks {
class stdout_color_sink_mt {};
} // namespace sinks
inline std::shared_ptr<logger> stdout_color_mt(const std::string&) { return default_logger(); }
} // namespace spdlog
