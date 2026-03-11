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
 * Created: Mar. 11, 2026
 * Updated: Mar. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/compile/gir.h"

#include <cctype>
#include <regex>
#include <string>
#include <unordered_map>
#include <unordered_set>

class CppSymbolNamer {
  public:
    std::string graphSymbol(const GIR::Graph *graph) {
        auto it = graphNames_.find(graph);
        if (it != graphNames_.end()) {
            return it->second;
        }

        std::string rawName = graph->name().empty() ? "graph" : graph->name();
        std::string base;
        static const std::regex anonymousBranchPattern("^__([0-9]+)__$");
        std::smatch match;
        if (std::regex_match(rawName, match, anonymousBranchPattern)) {
            base = "l_" + match[1].str();
        } else {
            base = "f_" + sanitize(rawName);
        }
        if (base.empty()) {
            base = "f_graph";
        }
        return graphNames_[graph] = makeUnique(base);
    }

    std::string valueSymbol(const GIR::Node *node, const std::string &hint = "v") {
        auto it = valueNames_.find(node);
        if (it != valueNames_.end()) {
            return it->second;
        }

        std::string base = collapseUnderscores(sanitize(hint));
        if (base.empty()) {
            base = "value";
        }
        return valueNames_[node] = makeUnique("v_" + base);
    }

    std::string valueSymbolStmt(const GIR::Node *node, const std::string &hint, size_t stmtIndex) {
        auto it = valueNames_.find(node);
        if (it != valueNames_.end()) {
            return it->second;
        }
        std::string base = collapseUnderscores(sanitize(hint));
        if (base.empty()) {
            base = "v";
        }
        std::string name = "v" + std::to_string(stmtIndex) + "_" + base;
        usedNames_.insert(name);
        return valueNames_[node] = name;
    }

  private:
    std::string makeUnique(const std::string &base) {
        std::string candidate = base;
        size_t suffix         = 0;
        while (!usedNames_.insert(candidate).second) {
            candidate = base + "_" + std::to_string(++suffix);
        }
        return candidate;
    }

    static std::string collapseUnderscores(std::string s) {
        std::string out;
        out.reserve(s.size());
        bool lastUnderscore = false;
        for (char ch : s) {
            if (ch == '_') {
                if (!lastUnderscore) {
                    out.push_back('_');
                    lastUnderscore = true;
                }
            } else {
                out.push_back(ch);
                lastUnderscore = false;
            }
        }
        return out;
    }

    static std::string sanitize(const std::string &text) {
        std::string out;
        out.reserve(text.size());
        for (unsigned char ch : text) {
            if (std::isalnum(ch)) {
                out.push_back(static_cast<char>(std::tolower(ch)));
            } else {
                out.push_back('_');
            }
        }
        while (!out.empty() && out.front() == '_') {
            out.erase(out.begin());
        }
        while (!out.empty() && out.back() == '_') {
            out.pop_back();
        }
        if (!out.empty() && std::isdigit(static_cast<unsigned char>(out.front()))) {
            out.insert(out.begin(), '_');
        }
        return out;
    }

    std::unordered_map<const GIR::Graph *, std::string> graphNames_;
    std::unordered_map<const GIR::Node *, std::string> valueNames_;
    std::unordered_set<std::string> usedNames_;
};
