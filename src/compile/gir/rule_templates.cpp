// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The templates of a generic rule, the instances made from them, and the program each instance is
 * published in (rewrite design 2 (second version) 8.5; see camel/compile/gir/rule_templates.h).
 *
 * The table lives on the context, so a rule value can keep an id instead of a pointer and an
 * instance outlives the pass that made it. The instance itself is built by the Builder that holds
 * the templates -- the one of the module that declared the rule -- because a generic function is
 * instantiated where it was declared, never in the program that matches it.
 */

#include "camel/compile/gir/rule_templates.h"

#include "builder.h"

#include "camel/compile/gir/encode.h"
#include "camel/core/context/context.h"
#include "camel/core/operator_table.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"
#include "camel/utils/assert.h"
#include "camel/utils/str.h"

namespace camel::compile::gir {

using camel::core::type::Type;

namespace {

using Table = std::vector<std::shared_ptr<GenericRuleEntry>>;

/// The context's table, made on first use.
Table &rulesOf(camel::core::context::Context &context) {
    std::shared_ptr<void> &slot = context.genericRuleTemplates();
    if (slot == nullptr) {
        slot = std::make_shared<Table>();
    }
    return *static_cast<Table *>(slot.get());
}

/// One type tuple as a reader writes it (`int`, `i32`), which is also the name an instance is
/// reported under (`add_zero<i32>`).
std::string tupleText(const std::vector<Type *> &types) {
    return strutil::join(types, ", ", [](Type *type) { return type->toString(); });
}

/// Types are interned, so the same type is the same object; `equals` is the fallback for a type a
/// program built for itself.
bool sameTuple(const std::vector<Type *> &left, const std::vector<Type *> &right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (size_t i = 0; i < left.size(); ++i) {
        if (left[i] != right[i] && !left[i]->equals(right[i])) {
            return false;
        }
    }
    return true;
}

/// The `with` values an instance was asked for, as the key they stand for: what makes two asks the
/// same instance (design principles 5.4, 5.5).
std::string withArgumentsKey(const RuleWithArguments &with) {
    std::string key;
    for (const auto &value : with) {
        key += (key.empty() ? "" : "|") + constasKeyOf(value);
    }
    return key;
}

/// The same values as the library writes them, each under the parameter it is bound to
/// (`tile = 64`), for the reports: what a reader needs to tell two instances of one rule apart.
std::string withArgumentsText(
    const RuleWithArguments &with, const std::vector<std::string> &names) {
    std::string text;
    for (size_t i = 0; i < with.size(); ++i) {
        text += (text.empty() ? "" : ", ");
        if (i < names.size()) {
            text += names[i] + " = ";
        }
        text += constasTextOf(with[i]);
    }
    return text;
}

/// Publishes one instance: its two sides and its condition are encoded into a program of the
/// context that is never released, which is the route an operator's definition graphs take
/// (`reserveProgram` then `pinRuntimeProgram`, src/runtime/definitions.cpp). A pinned program and
/// the graphs in it stay where they are, so the engine can read them for as long as the context
/// lives.
void publish(
    camel::core::context::Context &context, const compile_graph_ptr_t &lhs,
    const compile_graph_ptr_t &rhs, const compile_graph_ptr_t &when, GenericRuleInstance &instance) {
    camel::runtime::Program *program = context.reserveProgram();
    ASSERT(program != nullptr, "The context reserves the program an instance is published in.");
    encodeToRuntimeProgram(lhs, *program, &context.graphIds());
    instance.program = program;
    instance.lhs     = program->root();
    encodeToRuntimeProgram(rhs, *program, &context.graphIds());
    instance.rhs = program->root();
    if (when != nullptr) {
        encodeToRuntimeProgram(when, *program, &context.graphIds());
        instance.when      = program->root();
        instance.whenArity = static_cast<uint32_t>(when->funcType()->normTypesCount());
    }
    // The graphs an instance is made of are the context's from here on, like the definition
    // graphs of an operator: nothing of the pass that asked for them is left in them.
    context.pinRuntimeProgram(program);
}

} // namespace

std::vector<std::shared_ptr<GenericRuleEntry>> &
genericRuleTable(camel::core::context::Context &context) {
    return rulesOf(context);
}

GenericRuleEntry *genericRuleEntry(camel::core::context::Context &context, uint64_t ruleId) {
    Table &table = rulesOf(context);
    if (ruleId < 1 || ruleId > table.size()) {
        return nullptr;
    }
    return table[static_cast<size_t>(ruleId) - 1].get();
}

uint64_t registerGenericRule(
    camel::core::context::Context &context, void *owner, std::string name,
    std::vector<std::string> typeParams, std::vector<std::string> withParams,
    const std::shared_ptr<void> &lhs, const std::shared_ptr<void> &rhs,
    const std::shared_ptr<void> &when) {
    Table &table           = rulesOf(context);
    auto entry             = std::make_shared<GenericRuleEntry>();
    entry->name            = std::move(name);
    entry->typeParams      = std::move(typeParams);
    entry->withParams      = std::move(withParams);
    entry->owner           = owner;
    entry->lhs             = lhs;
    entry->rhs             = rhs;
    entry->when            = when;
    table.push_back(std::move(entry));
    // Ids start at 1: a rule value that keeps 0 is an ordinary rule, whose two sides are its own
    // functions, and every path that reads a rule value goes on reading it the way it did.
    return table.size();
}

RuleConditionAt ruleConditionAt(
    camel::core::context::Context &context, uint64_t ruleId,
    const std::vector<camel::core::type::Type *> &types, std::string &why) {
    GenericRuleEntry *entry = genericRuleEntry(context, ruleId);
    if (entry == nullptr) {
        why = "the rule is not registered in this context";
        return RuleConditionAt::Unknown;
    }
    auto *owner = static_cast<Builder *>(entry->owner);
    if (owner == nullptr) {
        why = "the builder that declared it is gone";
        return RuleConditionAt::Unknown;
    }
    return owner->ruleConditionVerdict(*entry, types, why);
}

std::vector<Type *> ruleInstanceTypes() {
    return camel::core::OperatorTable::instance().allRegisteredTypes();
}

size_t genericRuleInstance(
    camel::core::context::Context &context, uint64_t ruleId,
    const std::vector<camel::core::type::Type *> &types, const RuleWithArguments &with) {
    Table &table = rulesOf(context);
    ASSERT(
        ruleId >= 1 && ruleId <= table.size(),
        "The id of a generic rule names a rule the context has registered.");
    GenericRuleEntry &entry = *table[static_cast<size_t>(ruleId) - 1];
    const std::string withKey = withArgumentsKey(with);
    const std::string withText = withArgumentsText(with, entry.withParams);
    for (size_t i = 0; i < entry.instances.size(); ++i) {
        if (sameTuple(entry.instances[i].types, types) && entry.instances[i].withKey == withKey) {
            // One instance per type tuple and set of `with` values: the second ask for a pair is
            // the first instance, and the pair is remembered even when it did not come about, so
            // it is tried once.
            return i;
        }
    }
    GenericRuleInstance instance;
    instance.types    = types;
    instance.typeText = tupleText(types);
    instance.withKey  = withKey;
    instance.withText = withText;
    auto *owner       = static_cast<Builder *>(entry.owner);
    compile_graph_ptr_t lhs, rhs, when;
    if (owner == nullptr) {
        instance.why = "the builder that declared it is gone";
    } else if (!owner->instantiateRuleTemplates(entry, types, with, lhs, rhs, when, instance.why)) {
        // The attempt is remembered as one that did not come about, and nothing of what the
        // compiler said about it is reported (the rule on
        // generic-rule-instantiation-set-and-predicate).
    } else {
        publish(context, lhs, rhs, when, instance);
        instance.made = instance.lhs != nullptr && instance.rhs != nullptr;
        if (!instance.made) {
            instance.why = "its graphs could not be published in the program it runs in";
        } else {
            // The URI of the operator its left side is written with, read from the left side that
            // was just published: the one fact a transform on a node needs from the rule, since a
            // node names its operator by URI and the index names it by the way it is written.
            for (camel::runtime::gc_node_ref_t ref = 1; ref <= instance.lhs->nodeCount(); ++ref) {
                const camel::runtime::GCNode *node = instance.lhs->node(ref);
                if (node != nullptr && node->kind == camel::runtime::GCNodeKind::Oper) {
                    instance.lhsOperatorUri = std::string(
                        instance.lhs->nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri());
                    break;
                }
            }
        }
    }
    entry.instances.push_back(std::move(instance));
    return entry.instances.size() - 1;
}

} // namespace camel::compile::gir