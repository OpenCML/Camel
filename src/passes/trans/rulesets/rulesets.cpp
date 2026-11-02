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
               << entry.rule;
            // The function the rule calls is what a replacement has to call, and it is written in
            // the rule: printing it shows the two halves of a replacement -- which rule, and what
            // to call -- without either half being copied into C++.
            const std::string callee = camel::parse::ast::rhsCalleeOf(family, entry.rule);
            if (!callee.empty()) {
                os << " (rhs " << callee << ')';
            }
            os << '\n';
        }
    }
    // The keys a rule is filed under are read off the operator, so there is no per-operator table to
    // print. What is worth printing is the other half of that: the operators whose key is *not*
    // derivable, and why. With `forward=true` -- the reading of what the three transform families
    // resolve -- those lines come out, because a table a transform reads has to be a table a reading
    // can show, exceptions included.
    if (forward_) {
        for (const camel::core::RuleKeyException &exception : camel::core::ruleKeyExceptions()) {
            os << "RULEKEY-EXCEPTION " << exception.uri << " -> " << exception.head << '/'
               << exception.arity << " because " << exception.why << '\n';
        }
        os << "RULEKEY derivable: an operator's key is its own spelling -- the five tokens of the "
              "arithmetic, and a module operator's own name at the operand count its rule is filed "
              "under\n";
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