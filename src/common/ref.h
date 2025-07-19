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

#include <string>
#include <vector>
#include <stdexcept>

class Reference {
  public:
    Reference() {}
    Reference(const std::string &str) { set(str); }

    const std::string &ident() const { return ident_; }
    const std::vector<std::string> &scope() const { return scope_; }

    void setIdent(const std::string &ident) { ident_ = ident; }
    void addScope(const std::string &scope) { scope_.push_back(scope); }
    void clearScope() { scope_.clear(); }

    void set(const std::string &str) {
        scope_.clear();
        size_t pos = 0;
        size_t nextPos = 0;
        while ((nextPos = str.find("::", pos)) != std::string::npos) {
            scope_.push_back(str.substr(pos, nextPos - pos));
            pos = nextPos + 2; // Skip over "::"
        }
        if (pos < str.size()) {
            ident_ = str.substr(pos);
        } else {
            throw std::runtime_error("Invalid reference name: " + str);
        }
    }

    bool isNull() const { return ident_.empty(); }
    bool isAlone() const { return scope_.empty(); }

    const std::string toString() const {
        std::string result;
        if (!scope_.empty()) {
            for (const auto &s : scope_) {
                result += s + "::";
            }
        }
        result += ident_;
        return result;
    }

    operator std::string() const { return toString(); }

    bool operator==(const Reference &other) const { return ident_ == other.ident_ && scope_ == other.scope_; }

    bool operator<(const Reference &other) const {
        if (scope_ != other.scope_) {
            return scope_ < other.scope_;
        }
        return ident_ < other.ident_;
    }

  private:
    std::vector<std::string> scope_;
    std::string ident_;
};

namespace std {
template <> struct hash<Reference> {
    std::size_t operator()(const Reference &ref) const {
        std::size_t h = 0;
        std::hash<std::string> hashStr;

        for (const auto &s : ref.scope()) {
            h ^= hashStr(s) + 0x9e3779b9 + (h << 6) + (h >> 2); // boost-style hash combine
        }

        h ^= hashStr(ref.ident()) + 0x9e3779b9 + (h << 6) + (h >> 2);

        return h;
    }
};
} // namespace std