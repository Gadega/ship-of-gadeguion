// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace oot::ps3_compat {

class Path {
public:
    Path() = default;
    explicit Path(std::string value) : value_(normalize(std::move(value))) {}

    const std::string& string() const {
        return value_;
    }

    bool empty() const {
        return value_.empty();
    }

    std::string filename() const {
        if (value_.empty()) {
            return {};
        }
        const std::string trimmed = trim_trailing_slashes(value_);
        const std::string::size_type slash = trimmed.find_last_of('/');
        return slash == std::string::npos ? trimmed : trimmed.substr(slash + 1U);
    }

    Path parent_path() const {
        const std::string trimmed = trim_trailing_slashes(value_);
        const std::string::size_type slash = trimmed.find_last_of('/');
        if (slash == std::string::npos) {
            return Path();
        }
        if (slash == 0U) {
            return Path("/");
        }
        return Path(trimmed.substr(0U, slash));
    }

    Path join(const std::string& child) const {
        if (value_.empty()) {
            return Path(child);
        }
        if (!child.empty() && child[0] == '/') {
            return Path(child);
        }
        return Path(value_ + "/" + child);
    }

    std::vector<std::string> components() const {
        std::vector<std::string> parts;
        std::string current;
        for (const char c : value_) {
            if (c == '/') {
                if (!current.empty()) {
                    parts.push_back(current);
                    current.clear();
                }
                continue;
            }
            current.push_back(c);
        }
        if (!current.empty()) {
            parts.push_back(current);
        }
        return parts;
    }

private:
    static std::string normalize(std::string value) {
        for (char& c : value) {
            if (c == '\\') {
                c = '/';
            }
        }
        while (value.size() > 1U && value.back() == '/') {
            value.pop_back();
        }
        return value;
    }

    static std::string trim_trailing_slashes(std::string value) {
        while (value.size() > 1U && value.back() == '/') {
            value.pop_back();
        }
        return value;
    }

    std::string value_;
};

} // namespace oot::ps3_compat
