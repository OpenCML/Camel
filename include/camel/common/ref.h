// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
    Reference(const std::vector<std::string> &paths, const std::string &ident)
        : paths_(paths), ident_(ident) {
        if (ident_.empty()) {
            throw std::runtime_error("Identifier cannot be empty");
        }
    }
    Reference(const std::vector<std::string> &parts) {
        if (parts.empty()) {
            throw std::runtime_error("Reference parts cannot be empty");
        }
        ident_ = parts.back();
        if (ident_.empty()) {
            throw std::runtime_error("Identifier cannot be empty");
        }
        paths_.assign(parts.begin(), parts.end() - 1);
    }

    const std::string &ident() const { return ident_; }
    const std::vector<std::string> &paths() const { return paths_; }

    void parse(const std::string &str, const std::string &delimiter = "::") {
        paths_.clear();
        size_t pos     = 0;
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
