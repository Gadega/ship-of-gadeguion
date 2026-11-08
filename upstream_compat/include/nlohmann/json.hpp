#pragma once
#include <map>
#include <string>
#include <vector>
namespace nlohmann {
class json {
  public:
    using iterator = json;
    using const_iterator = json;
    json() = default;
    json(std::nullptr_t) {}
    template <typename T> json(const T&) {}
    static json parse(const std::string&) { return json(); }
    static json parse(const std::string&, std::nullptr_t, bool, bool) { return json(); }
    bool is_null() const { return true; }
    bool is_object() const { return false; }
    bool is_array() const { return false; }
    bool contains(const std::string&) const { return false; }
    bool contains(const char*) const { return false; }
    json& operator[](const std::string&) { return *this; }
    const json& operator[](const std::string&) const { return *this; }
    json& operator[](const char*) { return *this; }
    const json& operator[](const char*) const { return *this; }
    json& operator[](size_t) { return *this; }
    const json& operator[](size_t) const { return *this; }
    template <typename T> T get() const { return T(); }
    operator std::string() const { return std::string(); }
    template <typename T> T value(const std::string&, const T& fallback) const { return fallback; }
    std::string dump(int = -1) const { return "{}"; }
    size_t size() const { return 0; }
    json begin() const { return json(); }
    json end() const { return json(); }
    bool operator!=(const json&) const { return false; }
    bool operator==(const char*) const { return false; }
    bool operator!=(const char*) const { return true; }
};
using ordered_json = json;
namespace detail {
template <typename T> class iter_impl {};
template <typename T> class iteration_proxy {};
template <typename T> class iteration_proxy_value {};
} // namespace detail
} // namespace nlohmann
