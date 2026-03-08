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
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Mar. 08, 2026
 * Updated: Mar. 08, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "opencmlrc.h"

#include "nlohmann/json.hpp"

#include <cstddef>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <filesystem>

namespace fs = std::filesystem;

namespace opencmlrc {

namespace {

constexpr const char *OPENCMLRC_FILENAME = ".opencmlrc";

// Defensive limits
constexpr std::size_t MAX_CONFIG_FILE_SIZE = 512 * 1024; // 512 KiB
constexpr std::size_t MAX_PASSES_PER_LIST  = 512;
constexpr std::size_t MAX_PATH_LENGTH      = 8192;
constexpr std::size_t MAX_TOTAL_PASSES     = 1024;

struct RcPassesConfig {
    std::vector<std::string> prefix;
    std::vector<std::string> suffix;
    std::vector<std::string> fallback;
};

bool isPathAcceptable(const fs::path &p) {
    try {
        std::string s = p.string();
        return s.size() <= MAX_PATH_LENGTH;
    } catch (...) {
        return false;
    }
}

// Returns at most MAX_PASSES_PER_LIST entries; ignores non-strings and empty strings.
std::vector<std::string> parsePassList(const nlohmann::json &j) {
    std::vector<std::string> out;
    out.reserve(std::min(static_cast<std::size_t>(32), MAX_PASSES_PER_LIST));
    if (j.is_string()) {
        std::string s = j.get<std::string>();
        if (!s.empty())
            out.push_back(std::move(s));
        return out;
    }
    if (j.is_array()) {
        for (const auto &e : j) {
            if (out.size() >= MAX_PASSES_PER_LIST)
                break;
            if (e.is_string()) {
                std::string s = e.get<std::string>();
                if (!s.empty())
                    out.push_back(std::move(s));
            }
        }
    }
    return out;
}

// Strip // line comments; do not strip inside JSON string literals.
void stripLineComments(std::string &s) {
    bool inString = false;
    bool escape   = false;
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (escape) {
            out += c;
            escape = false;
            continue;
        }
        if (inString) {
            if (c == '\\') {
                escape = true;
                out += c;
            } else if (c == '"') {
                inString = false;
                out += c;
            } else {
                out += c;
            }
            continue;
        }
        if (c == '"') {
            inString = true;
            out += c;
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            while (i < s.size() && s[i] != '\n')
                ++i;
            if (i < s.size())
                out += '\n';
            continue;
        }
        out += c;
    }
    s = std::move(out);
}

// Read file with size limit to avoid reading huge files.
bool readLimitedFile(const fs::path &path, std::string &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return false;
    f.seekg(0, std::ios::end);
    std::streamsize size = f.tellg();
    if (size < 0 || static_cast<std::size_t>(size) > MAX_CONFIG_FILE_SIZE)
        return false;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<std::size_t>(size));
    if (size > 0 && !f.read(&out[0], size))
        return false;
    return true;
}

RcPassesConfig loadOneRc(const fs::path &path) {
    RcPassesConfig cfg;
    if (!isPathAcceptable(path))
        return cfg;
    if (!fs::is_regular_file(path))
        return cfg;
    std::string content;
    if (!readLimitedFile(path, content))
        return cfg;
    stripLineComments(content);
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(content);
    } catch (...) {
        return cfg;
    }
    if (!j.is_object())
        return cfg;
    auto it = j.find("passes");
    if (it == j.end() || !it->is_object())
        return cfg;
    const auto &passes = *it;
    auto p             = passes.find("prefix");
    if (p != passes.end())
        cfg.prefix = parsePassList(*p);
    p = passes.find("suffix");
    if (p != passes.end())
        cfg.suffix = parsePassList(*p);
    p = passes.find("fallback");
    if (p != passes.end())
        cfg.fallback = parsePassList(*p);
    return cfg;
}

void mergeRcConfig(RcPassesConfig &base, const RcPassesConfig &over) {
    if (!over.prefix.empty())
        base.prefix = over.prefix;
    if (!over.suffix.empty())
        base.suffix = over.suffix;
    if (!over.fallback.empty())
        base.fallback = over.fallback;
}

RcPassesConfig loadOpenCmlRc(const std::string &cwd, const std::string &scriptDir) {
    RcPassesConfig merged;
    if (cwd.size() > MAX_PATH_LENGTH)
        return merged;
    fs::path cwdPath(cwd);
    if (!cwd.empty() && isPathAcceptable(cwdPath)) {
        fs::path p = cwdPath / OPENCMLRC_FILENAME;
        if (isPathAcceptable(p)) {
            RcPassesConfig fromCwd = loadOneRc(p);
            merged.prefix          = std::move(fromCwd.prefix);
            merged.suffix          = std::move(fromCwd.suffix);
            merged.fallback        = std::move(fromCwd.fallback);
        }
    }
    if (!scriptDir.empty() && scriptDir != cwd && scriptDir.size() <= MAX_PATH_LENGTH) {
        fs::path scriptPath(scriptDir);
        if (isPathAcceptable(scriptPath)) {
            fs::path p = scriptPath / OPENCMLRC_FILENAME;
            if (isPathAcceptable(p)) {
                mergeRcConfig(merged, loadOneRc(p));
            }
        }
    }
    return merged;
}

} // namespace

constexpr const char *DEFAULT_FALLBACK_PASS = "std::default";

std::vector<std::string> resolvePassListWithRc(
    const std::string &cwd, const std::string &scriptDir,
    const std::vector<std::string> &cliPassList, std::vector<std::string> *outFallbackPasses) {

    RcPassesConfig rc = loadOpenCmlRc(cwd, scriptDir);
    if (outFallbackPasses)
        *outFallbackPasses =
            rc.fallback.empty() ? std::vector<std::string>{DEFAULT_FALLBACK_PASS} : rc.fallback;

    // Pass list is always prefix + cliPassList + suffix; deduplicate by pass name (first occurrence
    // wins).
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    auto appendUnique = [&out, &seen](const std::string &p) {
        if (seen.insert(p).second)
            out.push_back(p);
    };
    for (const auto &p : rc.prefix)
        appendUnique(p);
    for (const auto &p : cliPassList)
        appendUnique(p);
    for (const auto &p : rc.suffix)
        appendUnique(p);
    if (out.size() > MAX_TOTAL_PASSES)
        out.resize(MAX_TOTAL_PASSES);
    return out;
}

} // namespace opencmlrc
