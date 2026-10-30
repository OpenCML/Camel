// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "passes/trans/rulesets/rulesets.h"

#include "camel/parse/ast/rule_head.h"
#include "camel/core/forward.h"

#include <cstdlib>

namespace {

// One question of `query`: `family|head|arity`. The words are the program's, so a malformed one is
// reported rather than guessed at: guessing would answer a question nobody asked.
void answerOne(const std::string &question, std::ostream &os) {
    const size_t bar1 = question.find('|');
    const size_t bar2 = bar1 == std::string::npos ? std::string::npos : question.find('|', bar1 + 1);
    if (bar1 == std::string::npos || bar2 == std::string::npos) {
        os << "LOOKUP " << question << " -> malformed" << '\n';
        return;
    }
    const std::string family = question.substr(0, bar1);
    const std::string head   = question.substr(bar1 + 1, bar2 - bar1 - 1);
    const long arity         = std::strtol(question.c_str() + bar2 + 1, nullptr, 10);
    const auto answer =
        camel::parse::ast::lookupRule(family, head, static_cast<uint32_t>(arity < 0 ? 0 : arity));
    os << "LOOKUP " << family << ' ' << head << '/' << arity << " -> ";
    switch (answer.status) {
    case camel::parse::ast::RuleLookup::Found:
        os << answer.rule;
        break;
    case camel::parse::ast::RuleLookup::NoRuleForPrimitive:
        os << "no rule";
        break;
    case camel::parse::ast::RuleLookup::NoSuchFamily:
        os << "no such family";
        break;
    }
    os << '\n';
}

} // namespace

const camel::runtime::Program *
RulesetsPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    for (const std::string &family : camel::parse::ast::ruleFamilies()) {
        for (const auto &entry : camel::parse::ast::rulesOf(family)) {
            os << "RULESET " << family << ' ' << entry.head << '/' << entry.arity << " -> "
               << entry.rule << '\n';
        }
    }
    if (forward_) {
        for (const std::string &line : camel::core::forwardRuleDescriptions()) {
            os << "FORWARD " << line << '\n';
        }
    }
    size_t start = 0;
    while (start <= query_.size() && !query_.empty()) {
        const size_t end      = query_.find(';', start);
        const std::string one = query_.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? query_.size() + 1 : end + 1;
        if (!one.empty()) {
            answerOne(one, os);
        }
    }
    os.flush();
    // A reading of the index, not a rewrite: the graph goes on exactly as it came.
    return program;
}