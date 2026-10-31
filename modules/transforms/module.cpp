// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * transforms module wiring.
 *
 * `reinterp<H>(f)` is written in docs/design/program-transforms/README.cn.md 4.1 as
 * `func reinterp::<F, H>(f: F): Reinterp<F, H> when is_function::<F>() && is_rule_set::<H>()`,
 * and neither `Reinterp` nor `is_rule_set` exists in the core or the library, so that signature
 * cannot be written today.
 *
 * What is here instead is the first thing the transform has to be able to do, as a pass:
 * `transforms::reinterp` walks every function of the program that is made of operators, counts what
 * it is made of, and builds a graph of the transform's own for it (reserved in a `GraphDraftGroup`,
 * since a transform's graphs are members of one) -- then prints the new graph's `GraphId` and node
 * count, one line per function.
 *
 * Building a graph is not transforming one. The body of the new graph today is a single port
 * handed straight to the output; what fills it is the next step, and the reading that step has to
 * keep is the one this pass prints: the graph exists, and it is the transform's own graph, not the
 * function's.
 */

#include "module.h"

#include "camel/compile/gir/rule_templates.h"
#include "camel/execute/pass/opt.h"
#include "camel/execute/pass/params.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/graph_txn.h"
#include "camel/parse/ast/rule_head.h"
#include "camel/runtime/program.h"

#include <algorithm>
#include <string>

using namespace camel::core::context;
using namespace camel::core::module;

namespace {

/// The other key the index holds: the rule's left side is written `a + b`, and the node a transform
/// stands on names its operator by URI (`:op/add_d`). One line per operator, read from the rule's
/// instance -- `URI -> rule -> the GraphId of its right side` -- because that is the pair a
/// replacement has to have in hand before it can build anything.
void printRuleUris(camel::core::context::Context &context, std::ostream &os) {
    std::vector<std::string> printed;
    for (const camel::parse::ast::RuleEntry &head : camel::parse::ast::rulesOf("rulesets.jvp")) {
        auto &table = camel::compile::gir::genericRuleTable(context);
        for (size_t i = 0; i < table.size(); ++i) {
            if (table[i]->name != head.rule) {
                continue;
            }
            const uint64_t ruleId = static_cast<uint64_t>(i) + 1;
            const size_t at       = camel::compile::gir::genericRuleInstance(
                context, ruleId, {camel::core::type::Type::Float64()}, {});
            auto *entry = camel::compile::gir::genericRuleEntry(context, ruleId);
            if (entry == nullptr || at >= entry->instances.size()) {
                break;
            }
            const camel::compile::gir::GenericRuleInstance &instance = entry->instances[at];
            if (!instance.made || instance.rhs == nullptr) {
                break;
            }
            const std::string &uri = instance.lhsOperatorUri;
            if (std::find(printed.begin(), printed.end(), uri) != printed.end()) {
                break;
            }
            printed.push_back(uri);
            os << "REINTERP uri " << uri << " -> rule " << head.rule
               << " -> rhs " << camel::runtime::toValue(instance.rhs->id()) << '\n';
            break;
        }
    }
}

class ReinterpPass : public RuntimeGraphRewritePass {
  public:
    explicit ReinterpPass(const context_ptr_t &ctx) : RuntimeGraphRewritePass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        printRuleUris(*context_, os);
        // Every function of the program that is made of operators: those are the ones a
        // replacement has something to replace in. A function of calls and constants alone is
        // simply not visited.
        for (size_t i = 0; i <= program->tableSize(); ++i) {
            const camel::runtime::GraphId id = static_cast<camel::runtime::GraphId>(i);
            camel::runtime::GCGraph *function  = program->graph(id);
            if (function == nullptr) {
                continue;
            }
            size_t primitives = 0;
            size_t other      = 0;
            for (auto it = function->nodes().begin(); it != function->nodes().end(); ++it) {
                const camel::runtime::GCNode *node = *it;
                if (node->kind == camel::runtime::GCNodeKind::Oper) {
                    ++primitives;
                } else {
                    ++other;
                }
            }
            if (primitives == 0) {
                continue;
            }
            // The transform's own graph: a member of a group, reserved first so that every graph
            // the group makes can name it, then built in its draft and encoded.
            camel::runtime::GraphDraftGroup group(context_, program);
            const std::string name             = function->name() + "__reinterp";
            const camel::runtime::GraphId made = group.reserve(name, name);
            {
                auto &draft                = group.draft(made);
                camel::core::type::Type *t = camel::core::type::Type::Float64();
                const camel::runtime::gc_node_ref_t port =
                    draft.addPortNode(t, draft.allocateRuntimeSlot(t));
                draft.setOutputNode(port);
                draft.setExitNode(port);
                draft.setReturnNode(port, camel::runtime::GCReturnKind::Self);
            }
            group.encode();
            const camel::runtime::GCGraph *encoded = group.encoded(made);
            os << "REINTERP target=" << function->name() << " primitives=" << primitives
               << " other=" << other << " graph=" << camel::runtime::toValue(made)
               << " nodes=" << (encoded != nullptr ? encoded->nodeCount() : 0) << '\n';
        }
        return program;
    }
};

} // namespace

TransformsModule::TransformsModule(context_ptr_t ctx) : BuiltinModule("transforms", ctx) {}

module_ptr_t TransformsModule::create(context_ptr_t ctx) {
    return std::make_shared<TransformsModule>(ctx);
}

bool TransformsModule::load() {
    if (loaded_) {
        return true;
    }
    PassDescriptor descriptor;
    // No parameters: the pass walks every function of the program that is made of operators.
    descriptor.make = [](const context_ptr_t &ctx, const PassArgs &) {
        return std::make_unique<ReinterpPass>(ctx);
    };
    registerModulePass("transforms::reinterp", std::move(descriptor));
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new TransformsModule(ctx->shared_from_this()); }
}