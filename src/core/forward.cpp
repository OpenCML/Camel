// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/forward.h"

#include <map>
#include <mutex>

namespace camel::core {

namespace {

std::mutex &bindingMutex() {
    static std::mutex mutex;
    return mutex;
}

/// One binding per operator, in uri order so that a reading of them is stable.
std::map<std::string, ForwardRuleBinding> &bindings() {
    static std::map<std::string, ForwardRuleBinding> table;
    return table;
}

} // namespace

void bindForwardRule(ForwardRuleBinding binding) {
    std::lock_guard<std::mutex> lock(bindingMutex());
    bindings()[binding.uri] = std::move(binding);
}

std::optional<camel::parse::ast::RuleMatch> forwardRuleOf(const std::string &uri) {
    ForwardRuleBinding binding;
    {
        std::lock_guard<std::mutex> lock(bindingMutex());
        const auto at = bindings().find(uri);
        if (at == bindings().end()) {
            return std::nullopt;
        }
        binding = at->second;
    }
    return camel::parse::ast::lookupRule(binding.family, binding.head, binding.arity);
}

std::vector<std::string> forwardRuleDescriptions() {
    std::vector<std::string> lines;
    std::vector<ForwardRuleBinding> all;
    {
        std::lock_guard<std::mutex> lock(bindingMutex());
        all.reserve(bindings().size());
        for (const auto &[uri, binding] : bindings()) {
            (void)uri;
            all.push_back(binding);
        }
    }
    for (const ForwardRuleBinding &binding : all) {
        std::string line = binding.uri + " " + binding.family + " " + binding.head + "/" +
                           std::to_string(binding.arity) + " -> ";
        const auto answer = camel::parse::ast::lookupRule(
            binding.family, binding.head, binding.arity);
        switch (answer.status) {
        case camel::parse::ast::RuleLookup::Found:
            line += answer.rule;
            break;
        case camel::parse::ast::RuleLookup::NoRuleForPrimitive:
            line += "no rule";
            break;
        case camel::parse::ast::RuleLookup::NoSuchFamily:
            line += "no such family";
            break;
        }
        lines.push_back(std::move(line));
    }
    return lines;
}

} // namespace camel::core