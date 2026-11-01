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

/// The transpose family's bindings, kept apart from the forward ones: one operator has a rule in
/// each family, and `bindForwardRule` must not overwrite the other fact.
std::map<std::string, ForwardRuleBinding> &transposeBindings() {
    static std::map<std::string, ForwardRuleBinding> table;
    return table;
}

/// The batching family's bindings, kept apart for the same reason as the transpose ones: a rule is
/// filed under its operator and operand count in one family, and the family a transform reads is
/// not a fact another transform may replace.
std::map<std::string, ForwardRuleBinding> &vmapBindings() {
    static std::map<std::string, ForwardRuleBinding> table;
    return table;
}

using BindingTable = std::map<std::string, ForwardRuleBinding>;

std::optional<camel::parse::ast::RuleMatch> ruleOf(BindingTable &table, const std::string &uri) {
    ForwardRuleBinding binding;
    {
        std::lock_guard<std::mutex> lock(bindingMutex());
        const auto at = table.find(uri);
        if (at == table.end()) {
            return std::nullopt;
        }
        binding = at->second;
    }
    return camel::parse::ast::lookupRule(binding.family, binding.head, binding.arity);
}

std::vector<std::string> descriptionsOf(BindingTable &table) {
    std::vector<std::string> lines;
    std::vector<ForwardRuleBinding> all;
    {
        std::lock_guard<std::mutex> lock(bindingMutex());
        all.reserve(table.size());
        for (const auto &[uri, binding] : table) {
            (void)uri;
            all.push_back(binding);
        }
    }
    for (const ForwardRuleBinding &binding : all) {
        std::string line = binding.uri + " " + binding.family + " " + binding.head + "/" +
                           std::to_string(binding.arity) + " -> ";
        const auto answer =
            camel::parse::ast::lookupRule(binding.family, binding.head, binding.arity);
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

} // namespace

void bindForwardRule(ForwardRuleBinding binding) {
    std::lock_guard<std::mutex> lock(bindingMutex());
    bindings()[binding.uri] = std::move(binding);
}

std::optional<camel::parse::ast::RuleMatch> forwardRuleOf(const std::string &uri) {
    return ruleOf(bindings(), uri);
}

std::optional<ForwardRuleBinding> forwardBindingOf(const std::string &uri) {
    std::lock_guard<std::mutex> lock(bindingMutex());
    const auto at = bindings().find(uri);
    if (at == bindings().end()) {
        return std::nullopt;
    }
    return at->second;
}

std::vector<std::string> forwardRuleDescriptions() { return descriptionsOf(bindings()); }

void bindTransposeRule(ForwardRuleBinding binding) {
    std::lock_guard<std::mutex> lock(bindingMutex());
    transposeBindings()[binding.uri] = std::move(binding);
}

std::optional<camel::parse::ast::RuleMatch> transposeRuleOf(const std::string &uri) {
    return ruleOf(transposeBindings(), uri);
}

std::vector<std::string> transposeRuleDescriptions() { return descriptionsOf(transposeBindings()); }

void bindVmapRule(ForwardRuleBinding binding) {
    std::lock_guard<std::mutex> lock(bindingMutex());
    vmapBindings()[binding.uri] = std::move(binding);
}

std::optional<ForwardRuleBinding> vmapBindingOf(const std::string &uri) {
    std::lock_guard<std::mutex> lock(bindingMutex());
    const auto at = vmapBindings().find(uri);
    if (at == vmapBindings().end()) {
        return std::nullopt;
    }
    return at->second;
}

std::vector<std::string> vmapRuleDescriptions() { return descriptionsOf(vmapBindings()); }

} // namespace camel::core
