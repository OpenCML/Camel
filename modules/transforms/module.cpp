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

#include "jvp.h"
#include "jvp_operator.h"

#include "camel/compile/gir/rule_templates.h"
#include "camel/compile/gir/static_function.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/execute/executor.h"
#include "camel/runtime/draft.h"
#include "camel/execute/pass/opt.h"
#include "camel/execute/pass/params.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"

#include <array>
#include "camel/runtime/graph_txn.h"
#include "camel/parse/ast/rule_head.h"
#include "camel/runtime/program.h"

#include <algorithm>
#include <utility>
#include <string>

using namespace camel::core::context;
using namespace camel::core::module;

namespace {

/// The other key the index holds: the rule's left side is written `a + b`, and the node a transform
/// stands on names its operator by URI (`:op/add_d`). One line per operator, read from the rule's
/// instance -- `URI -> rule -> the GraphId of its right side` -- because that is the pair a
/// replacement has to have in hand before it can build anything.
void printRuleUris(
    camel::core::context::Context &context, const camel::runtime::Program *program,
    std::ostream &os) {
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
            // How many ports the right side takes, and how many nodes it has: whether a call can
            // stand where the operator stood is a question about the right side's own signature,
            // and a replacement that guessed it would be wrong at the first arity that differs.
            struct Shape {
                size_t ports = 0;
                std::string kinds;
                /// The type of the single value the graph takes: the packed tuple, when it has one.
                std::string portType;
                /// The norm inputs the graph's nodes consume between them: 0 means the operand is
                /// not wired into this graph at all.
                size_t inputs = 0;
            };
            const auto shapeOf = [](const camel::runtime::GCGraph *graph) {
                Shape shape;
                for (camel::runtime::gc_node_ref_t ref = 1; ref <= graph->nodeCount(); ++ref) {
                    const camel::runtime::GCNode *node = graph->node(ref);
                    if (node == nullptr) {
                        continue;
                    }
                    if (node->kind == camel::runtime::GCNodeKind::Port) {
                        ++shape.ports;
                        if (shape.portType.empty() && node->dataType() != nullptr) {
                            shape.portType = node->dataType()->toString();
                        }
                    }
                    shape.inputs += static_cast<size_t>(node->withBegin - node->inBegin);
                    if (!shape.kinds.empty()) {
                        shape.kinds += ',';
                    }
                    shape.kinds += std::to_string(static_cast<int>(node->kind));
                }
                return shape;
            };
            // The value the right side ends in: a closure, when it ends in a Fill of the
            // FunctionClosure kind. Its signature says what the replacement still has to supply.
            std::string closure;
            {
                const camel::runtime::GCGraph *graph = instance.rhs;
                for (camel::runtime::gc_node_ref_t ref = 1; ref <= graph->nodeCount(); ++ref) {
                    const camel::runtime::GCNode *node = graph->node(ref);
                    if (node == nullptr || node->kind != camel::runtime::GCNodeKind::Fill ||
                        node->payload == camel::runtime::kNoPayload) {
                        continue;
                    }
                    const auto *body = graph->nodeBodyAs<camel::runtime::GCFillBody>(ref);
                    if (body->fillKind != camel::runtime::GCFillKind::FunctionClosure) {
                        continue;
                    }
                    closure = "fill=FunctionClosure slots=" + std::to_string(body->slotCount);
                    if (auto *fnType =
                            dynamic_cast<camel::core::type::FunctionType *>(node->dataType())) {
                        closure += " type=" + fnType->toString();
                        closure += " norm=" + std::to_string(fnType->normTypesCount());
                        closure += " with=" + std::to_string(fnType->withTypesCount());
                        closure += " args=";
                        bool first = true;
                        for (const std::string_view name : fnType->argNames()) {
                            if (!first) {
                                closure += ',';
                            }
                            first = false;
                            closure += std::string(name);
                        }
                    } else if (node->dataType() != nullptr) {
                        closure += " type=" + node->dataType()->toString();
                    }
                    break;
                }
            }
            const Shape lhsShape = shapeOf(instance.lhs);
            const Shape rhsShape = shapeOf(instance.rhs);
            const size_t ports   = rhsShape.ports;
            os << "REINTERP uri " << uri << " -> rule " << head.rule
               << " -> rhs " << camel::runtime::toValue(instance.rhs->id()) << " ports=" << ports
               << " nodes=" << instance.rhs->nodeCount() << " lhsPorts=" << lhsShape.ports
               << " lhsKinds=" << lhsShape.kinds << " rhsKinds=" << rhsShape.kinds
               << " portType=" << (rhsShape.portType.empty() ? "-" : rhsShape.portType.c_str())
               << " lhsIns=" << lhsShape.inputs << " rhsIns=" << rhsShape.inputs
               << ' ' << (closure.empty() ? "closure=-" : closure.c_str())
               << " vars=" << head.variables << " count="
               << (head.variables.empty()
                       ? 0
                       : std::count(head.variables.begin(), head.variables.end(), ',') + 1)
               << '\n';
            // Step 1 of the replacement, for the operator the author named first. The right side
            // of the rule is a graph that *builds* a closure over the values the left side bound,
            // so what a call at the operator's site yields is a function value naming that graph.
            // It is built here -- with one closure slot per variable, in the order the left side
            // writes them -- and printed: it is the object the input tangents are later fed to,
            // and building it is the step that a one-step "replace the operator" would skip.
            if (uri == ":op/div_d" && instance.rhs != nullptr) {
                size_t count = head.variables.empty()
                                   ? 0
                                   : std::count(head.variables.begin(), head.variables.end(), ',') + 1;
                std::vector<camel::core::type::Type *> slots(
                    count, camel::core::type::Type::Float64());
                auto *layout = camel::core::type::TupleType::create(
                    std::span<camel::core::type::Type *const>(slots.data(), slots.size()));
                ::Function *built = ::Function::create(
                    program, instance.rhs->id(), layout, camel::core::mm::autoSpace());
                os << "REINTERP closure graph=" << camel::runtime::toValue(instance.rhs->id())
                   << " layout=" << layout->toString() << " built=" << (built != nullptr ? "yes" : "no")
                   << " slots=" << head.variables << '\n';
            }
            break;
        }
    }
}


class ReinterpPass : public RuntimeGraphRewritePass {
  public:
    explicit ReinterpPass(const context_ptr_t &ctx) : RuntimeGraphRewritePass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        printRuleUris(*context_, program, os);
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
            // What a rebuilt copy of this graph has to name, node by node: the graphs its FUNC nodes
            // call -- a rebuilt `main` calls the rebuilt `f`, and the rest (`println` among them)
            // keeps the graphs it already names -- and the operators its OPER nodes use.
            std::string calls;
            std::string uris;
            for (camel::runtime::gc_node_ref_t ref = 1; ref <= function->nodeCount(); ++ref) {
                const camel::runtime::GCNode *node = function->node(ref);
                if (node == nullptr) {
                    continue;
                }
                if (node->kind == camel::runtime::GCNodeKind::Func) {
                    calls += (calls.empty() ? "" : ",") +
                             std::to_string(
                                 camel::runtime::toValue(function->calleeIdOf(ref)));
                } else if (node->kind == camel::runtime::GCNodeKind::Oper) {
                    const auto *body = function->nodeBodyAs<camel::runtime::GCOperBody>(ref);
                    uris += (uris.empty() ? "" : ",") + std::string(body->uri());
                }
            }
            os << "REINTERP calls=" << (calls.empty() ? "-" : calls.c_str())
               << " opers=" << (uris.empty() ? "-" : uris.c_str()) << '\n';
            // What the forward transform would make of this function, so the graph it builds can be
            // named before anything is wired to a call site. Built and dropped: the program this
            // pass answers with is unchanged.
            if (camel::transforms::jvpSignature(function->funcType()) != nullptr) {
                camel::transforms::buildJvpGraph(context_, program, function, os);
            }
        }
        // Nothing is replaced in the program this pass was given: rewriting a graph that is already
        // published is the road measured at 9cb1c3a94, and it ends in SIGSEGV at the entry it makes.
        // `reinterp` produces a new function instead (the author's ruling, a824031aa): a new graph,
        // written node by node with the rule's right side in place of each primitive, handed to the
        // call site as a function value -- the shape `camel::autodiff::buildGradientGraph` builds
        // for the reverse direction. The same program is answered with until that graph exists.
        return program;
    }
};

} // namespace

/// The executors this module's operators run on: `jvp` is the only one, and its kernel builds the
/// transform's graph when the call runs (jvp_operator.cpp).
class TransformsExecutor : public Executor {
  public:
    explicit TransformsExecutor(context_ptr_t ctx)
        : Executor(std::move(ctx), camel::transforms::operatorKernels()) {}
};

TransformsModule::TransformsModule(context_ptr_t ctx) : BuiltinModule("transforms", ctx) {
    // `jvp` is one of the module's operators, beside the pass: `reinterp<jvp>(f)` is a value-level
    // transform (jvp.h), so it is reached as a call and not as a pass.
    camel::transforms::registerOperatorMetadata();
    for (const auto &group : camel::transforms::operatorGroups()) {
        exportEntity(group->name(), group);
    }
}

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
    context_->registerExecutorFactory("transforms", [ctx = context_]() -> executor_ptr_t {
        return std::make_shared<TransformsExecutor>(ctx);
    });
    loaded_ = true;
    return true;
}

extern "C" {
Module *camel_module_create(Context *ctx) { return new TransformsModule(ctx->shared_from_this()); }
}