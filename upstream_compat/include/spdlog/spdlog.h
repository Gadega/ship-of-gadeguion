#pragma once
#include <memory>
#include <string>

namespace spdlog {
namespace fmt_lib {
template <typename... Args> std::string format(const char*, Args&&...) { return std::string(); }
} // namespace fmt_lib
namespace details {
class thread_pool {};
} // namespace details
namespace level {
enum level_enum { trace, debug, info, warn, err, critical, off };
} // namespace level

class logger {
  public:
    template <typename... Args> void trace(const char*, Args&&...) {}
    template <typename... Args> void debug(const char*, Args&&...) {}
    template <typename... Args> void info(const char*, Args&&...) {}
    template <typename... Args> void warn(const char*, Args&&...) {}
    template <typename... Args> void error(const char*, Args&&...) {}
    template <typename... Args> void critical(const char*, Args&&...) {}
    void set_level(level::level_enum) {}
    void flush_on(level::level_enum) {}
};

inline std::shared_ptr<logger> default_logger() {
    static std::shared_ptr<logger> value = std::make_shared<logger>();
    return value;
}
inline void set_default_logger(std::shared_ptr<logger>) {}
inline void set_level(level::level_enum) {}
inline void flush_on(level::level_enum) {}
inline void drop_all() {}
template <typename... Args> inline void trace(const char*, Args&&...) {}
template <typename... Args> inline void debug(const char*, Args&&...) {}
template <typename... Args> inline void info(const char*, Args&&...) {}
template <typename... Args> inline void warn(const char*, Args&&...) {}
template <typename... Args> inline void error(const char*, Args&&...) {}
template <typename... Args> inline void critical(const char*, Args&&...) {}
} // namespace spdlog

#ifndef SPDLOG_ERROR
#define SPDLOG_ERROR(...) do { } while (0)
#endif
#ifndef SPDLOG_WARN
#define SPDLOG_WARN(...) do { } while (0)
#endif
#ifndef SPDLOG_INFO
#define SPDLOG_INFO(...) do { } while (0)
#endif
#ifndef SPDLOG_DEBUG
#define SPDLOG_DEBUG(...) do { } while (0)
#endif
#ifndef SPDLOG_TRACE
#define SPDLOG_TRACE(...) do { } while (0)
#endif
