/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Jul. 03, 2025
 * Updated: Jul. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <stdexcept>
#include <string>
#include <vector>

class Reference {
  public:
    Reference() {}
    Reference(const std::string &str) { parse(str); }
    Reference(const char *cstr) {
        if (cstr) {
            parse(std::string(cstr));
        }
    }

    const std::string &ident() const { return ident_; }
    const std::vector<std::string> &paths() const { return paths_; }

    void parse(const std::string &str, const std::string &delimiter = "::") {
        paths_.clear();
        size_t pos = 0;
        size_t nextPos = 0;
        while ((nextPos = str.find(delimiter, pos)) != std::string::npos) {
            paths_.push_back(str.substr(pos, nextPos - pos));
            pos = nextPos + delimiter.size();
        }
        if (pos < str.size()) {
            ident_ = str.substr(pos);
        } else {
            throw std::runtime_error("Invalid reference name: " + str);
        }
    }

    bool empty() const { return ident_.empty(); }
    bool plain() const { return paths_.empty(); }

    const std::string toString() const {
        std::string result;
        if (!paths_.empty()) {
            for (const auto &s : paths_) {
                result += s + "::";
            }
        }
        result += ident_;
        return result;
    }

    operator std::string() const { return toString(); }

    bool operator==(const Reference &other) const {
        return ident_ == other.ident_ && paths_ == other.paths_;
    }

    bool operator<(const Reference &other) const {
        if (paths_ != other.paths_) {
            return paths_ < other.paths_;
        }
        return ident_ < other.ident_;
    }

  private:
    std::vector<std::string> paths_;
    std::string ident_;
};

namespace std {
template <> struct hash<Reference> {
    std::size_t operator()(const Reference &ref) const {
        std::size_t h = 0;
        std::hash<std::string> hashStr;

        for (const auto &s : ref.paths()) {
            h ^= hashStr(s) + 0x9e3779b9 + (h << 6) + (h >> 2); // boost-style hash combine
        }

        h ^= hashStr(ref.ident()) + 0x9e3779b9 + (h << 6) + (h >> 2);

        return h;
    }
};
} // namespace std