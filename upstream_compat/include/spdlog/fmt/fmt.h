#pragma once
#include <string>
namespace fmt {
template <typename... Args> std::string format(const char*, Args&&...) { return std::string(); }
} // namespace fmt
