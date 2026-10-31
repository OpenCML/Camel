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
#include "camel/compile/gir/static_function.h"
#include "camel/core/rtdata/tuple.h"
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

namespace {

/// The smallest container the two steps of a replacement fit in: a `main` of four things (a
/// constant `2.0`, a call of a new `f`, `:io/println`, `return 0`) and that new `f` (the value of
/// `3/x`, the closure its rule builds, the call of that closure with the tangents `1` and `0`).
/// Both graphs are made from nothing: the program this pass was given cannot have a graph replaced
/// in it, so a rewrite makes a new program, and the group's first member is its root.
const camel::runtime::Program *buildContainer(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    const camel::compile::gir::GenericRuleInstance &instance, std::ostream &os) {
    using camel::core::type::Type;
    using camel::runtime::GraphDraftGroup;
    using camel::runtime::GraphId;
    using camel::runtime::gc_node_ref_t;

    const camel::runtime::GCGraph *fGraph    = nullptr;
    const camel::runtime::GCGraph *mainGraph = nullptr;
    for (size_t i = 0; i <= program->tableSize(); ++i) {
        camel::runtime::GCGraph *graph =
            program->graph(static_cast<camel::runtime::GraphId>(i));
        if (graph == nullptr) {
            continue;
        }
        if (graph->name() == "f") {
            fGraph = graph;
        } else if (graph->name() == "main") {
            mainGraph = graph;
        }
    }
    if (fGraph == nullptr || mainGraph == nullptr) {
        os << "REINTERP container skip=no-f-or-main\n";
        return nullptr;
    }
    // The operator handles the two graphs name, read from the graphs themselves: a rebuilt graph
    // uses the same implementations, and nothing here has to know how an operator is looked up.
    // The operator a node uses is named by its URI, not by the implementation handle: that handle is
    // a scheduler's cache and is null until a scheduler resolves it
    // (`GCOperBody::op`, include/camel/runtime/graph.h). A rebuilt node is given the URI and the
    // same null handle, the way the autodiff module builds one (modules/autodiff/pullback.cpp,
    // `addOper`).
    bool hasDiv            = false;
    bool hasPrintln        = false;
    camel::core::type::Type *printlnType = nullptr;
    for (camel::runtime::gc_node_ref_t ref = 1; ref <= fGraph->nodeCount(); ++ref) {
        const camel::runtime::GCNode *node = fGraph->node(ref);
        if (node != nullptr && node->kind == camel::runtime::GCNodeKind::Oper &&
            fGraph->nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri() == ":op/div_d") {
            hasDiv = true;
        }
    }
    for (camel::runtime::gc_node_ref_t ref = 1; ref <= mainGraph->nodeCount(); ++ref) {
        const camel::runtime::GCNode *node = mainGraph->node(ref);
        if (node != nullptr && node->kind == camel::runtime::GCNodeKind::Oper &&
            mainGraph->nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri() == ":io/println") {
            hasPrintln   = true;
            printlnType = node->dataType();
        }
    }
    if (!hasDiv || !hasPrintln) {
        os << "REINTERP container skip=no-operators\n";
        return nullptr;
    }

    Type *F = Type::Float64();
    auto *pairType = camel::core::type::TupleType::create(std::vector<Type *>{F, F});

    GraphDraftGroup group(context, program);
    const GraphId mainId = group.reserve("main__min", "main__min");
    const GraphId fId    = group.reserve("f__min", "f__min");

    {
        auto &draft = group.draft(fId);
        // A member is a graph without a signature until one is written: a graph the program cannot
        // call, which is what the first attempt's Type=- was. Both members take the signature the
        // graph they stand in for has.
        draft.setFuncType(fGraph->funcType());
        const gc_node_ref_t x = draft.addPortNode(F, draft.allocateRuntimeSlot(F));
        const gc_node_ref_t three = draft.materializeStaticValue(camel::core::rtdata::toSlot<double>(3.0), F);
        const gc_node_ref_t value = draft.addOperNode(F, nullptr, ":op/div_d");
        const std::array<gc_node_ref_t, 2> divIn{x, three};
        draft.setNormInputs(value, divIn);
        // The closure the rule builds: its captured values are the rule's variables, in the order
        // the left side writes them (`x,w`), and its own parameters are the tangents the second
        // step supplies.
        auto *function = camel::compile::gir::StaticFunction::create(
            instance.rhsBuilder, pairType, camel::core::mm::autoSpace());
        const size_t fnSlot = draft.appendStaticSlot(
            camel::core::rtdata::toSlot<camel::compile::gir::StaticFunction *>(function), instance.rhs->funcType());
        const gc_node_ref_t fnSource =
            draft.addDataNode(instance.rhs->funcType(), fnSlot, camel::runtime::kGCNodeFlagConstant);
        const gc_node_ref_t closure = draft.addFillNode(
            instance.rhs->funcType(),
            camel::runtime::makeFillPayload(
                camel::runtime::GCFillKind::FunctionClosure, std::vector<size_t>{0, 1}));
        draft.setNormInputs(closure, std::span<const gc_node_ref_t>(&fnSource, 1));
        const std::array<gc_node_ref_t, 2> captures{x, three};
        draft.setWithInputs(closure, captures);
        const gc_node_ref_t dx = draft.materializeStaticValue(camel::core::rtdata::toSlot<double>(1.0), F);
        const gc_node_ref_t dw = draft.materializeStaticValue(camel::core::rtdata::toSlot<double>(0.0), F);
        const gc_node_ref_t tangent = draft.addCallNode(F);
        draft.setWithInputs(tangent, std::span<const gc_node_ref_t>(&closure, 1));
        const std::array<gc_node_ref_t, 2> tangentIn{dx, dw};
        draft.setNormInputs(tangent, tangentIn);
        ::Tuple *emptyPair = ::Tuple::create(2, camel::core::mm::autoSpace());
        const gc_node_ref_t pairSource = draft.materializeStaticValue(
            camel::core::rtdata::toSlot<camel::core::rtdata::Object *>(emptyPair), pairType);
        const gc_node_ref_t out = draft.addFillNode(
            pairType, camel::runtime::makeFillPayload(
                          camel::runtime::GCFillKind::Tuple, std::vector<size_t>{0, 1}));
        draft.setNormInputs(out, std::span<const gc_node_ref_t>(&pairSource, 1));
        const std::array<gc_node_ref_t, 2> pairIn{value, tangent};
        draft.setWithInputs(out, pairIn);
        draft.setOutputNode(out);
        draft.setReturnNode(out, camel::runtime::GCReturnKind::Self);
    }

    {
        auto &draft = group.draft(mainId);
        draft.setFuncType(mainGraph->funcType());
        const gc_node_ref_t two = draft.materializeStaticValue(camel::core::rtdata::toSlot<double>(2.0), F);
        const gc_node_ref_t probeValue =
            draft.materializeStaticValue(camel::core::rtdata::toSlot<double>(42.0), F);
        const gc_node_ref_t probe = draft.addOperNode(printlnType, nullptr, ":io/println");
        draft.setNormInputs(probe, std::span<const gc_node_ref_t>(&probeValue, 1));
        auto *emptyClosure = camel::core::type::TupleType::create();
        const gc_node_ref_t fValue = draft.materializeStaticValue(
            camel::core::rtdata::toSlot<::Function *>(::Function::create(
                group.program(), fId, emptyClosure, camel::core::mm::autoSpace())),
            fGraph->funcType());
        const gc_node_ref_t call = draft.addCallNode(pairType);
        draft.setWithInputs(call, std::span<const gc_node_ref_t>(&fValue, 1));
        draft.setNormInputs(call, std::span<const gc_node_ref_t>(&two, 1));
        const gc_node_ref_t print =
            draft.addOperNode(printlnType, nullptr, ":io/println");
        draft.setNormInputs(print, std::span<const gc_node_ref_t>(&call, 1));
        const gc_node_ref_t zero =
            draft.materializeStaticValue(camel::core::rtdata::toSlot<int64_t>(0), Type::Int64());
        // println is a statement, not the value of anything: the exit has to follow it, or the
        // program may schedule the print after it has stopped, or not at all.
        draft.appendOrderDependency(call, probe);
        draft.appendOrderDependency(print, call);
        draft.appendOrderDependency(zero, print);
        draft.setOutputNode(zero);
        draft.setReturnNode(zero, camel::runtime::GCReturnKind::Self);
    }

    group.encode();
    // A member that was just encoded may have no signature of its own yet: the type is a
    // reading, and a reading that dereferences a null is a crash, not a measurement.
    const auto typeText = [](const camel::runtime::GCGraph *graph) -> std::string {
        const auto *fn = graph != nullptr ? graph->funcType() : nullptr;
        return fn != nullptr ? fn->toString() : std::string("-");
    };
    os << "REINTERP container mainType=" << typeText(group.encoded(mainId))
       << " oldMainType=" << typeText(mainGraph) << " fType=" << typeText(group.encoded(fId))
       << " oldFType=" << typeText(fGraph) << '\n';
    os << "REINTERP container root=" << camel::runtime::toValue(mainId)
       << " f=" << camel::runtime::toValue(fId) << " call=" << camel::runtime::toValue(fId)
       << " edge=main__min->f__min\n";
    return group.program();
}

} // namespace

namespace {

/// The instance of the `/` rule a replacement is built from, found the way the report finds it: by
/// the rule name the index holds for the operator's URI, then by the instance the context made of
/// it for `float`.
const camel::runtime::Program *buildContainerFrom(
    const camel::core::context::context_ptr_t &contextPtr, const camel::runtime::Program *program,
    std::ostream &os) {
    using camel::core::type::Type;
    // Held back: since the merge with develop the container segfaults while it is built, and a
    // container that crashes the pass cannot be reported as a reading. The shape below is what the
    // next step fixes; the pipeline's own rule is the one it has to satisfy, and that rule is a
    // graph id: src/execute/camel_pass.cpp, "the function a pass returns must be the root
    // function it was given, transformed".
    return nullptr;
    camel::core::context::Context &context = *contextPtr;
    const auto heads = camel::parse::ast::rulesOf("rulesets.jvp");
    const std::string uri = ":op/div_d";
    for (const camel::parse::ast::RuleEntry &head : heads) {
        auto &table = camel::compile::gir::genericRuleTable(context);
        for (size_t i = 0; i < table.size(); ++i) {
            if (table[i]->name != head.rule) {
                continue;
            }
            const uint64_t ruleId = static_cast<uint64_t>(i) + 1;
            const size_t at =
                camel::compile::gir::genericRuleInstance(context, ruleId, {Type::Float64()}, {});
            auto *entry = camel::compile::gir::genericRuleEntry(context, ruleId);
            if (entry == nullptr || at >= entry->instances.size()) {
                continue;
            }
            const camel::compile::gir::GenericRuleInstance &instance = entry->instances[at];
            if (!instance.made || instance.rhs == nullptr ||
                instance.rhsBuilder == nullptr || instance.lhsOperatorUri != uri) {
                continue;
            }
            const camel::runtime::Program *container =
                buildContainer(contextPtr, program, instance, os);
            if (container != nullptr) {
                return container;
            }
            os << "REINTERP container skip=build\n";
        }
    }
    os << "REINTERP container skip=no-instance\n";
    return nullptr;
}

} // namespace

/// The smallest thing that proves the transform's own code runs: one statement added to the graph
/// the program already enters -- a `println` of a constant -- committed in place, so the graph the
/// pipeline runs keeps its id. That id is the contract a pass answers to: the program it returns
/// must have the graph it was given as its root (src/execute/camel_pass.cpp). A container built as
/// a new program cannot meet it, however it is rooted.
const camel::runtime::Program *probeRoot(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    std::ostream &os) {
    using camel::core::type::Type;
    camel::runtime::GCGraph *root = program != nullptr ? program->root() : nullptr;
    if (root == nullptr) {
        os << "REINTERP probe root=none\n";
        return nullptr;
    }
    Type *F = Type::Float64();
    camel::runtime::RuntimeGraphDraftSession session(context, program);
    auto &transaction = session.edit(root);
    auto &draft       = transaction.edit();
    // The graph the program enters is not the graph that holds the statement: the entry calls the
    // function, and the println is in that function's graph (reading: the pass's own report says
    // REINTERP target=main ... opers=:io/println, and that target is not graph 1). So the node is
    // looked for in every graph of the program, and the one that has it is the one edited.
    camel::runtime::GCGraph *holder        = nullptr;
    camel::runtime::gc_node_ref_t printRef = 0;
    for (size_t i = 0; i <= program->tableSize(); ++i) {
        camel::runtime::GCGraph *candidate =
            program->graph(static_cast<camel::runtime::GraphId>(i));
        if (candidate == nullptr) {
            continue;
        }
        for (camel::runtime::gc_node_ref_t ref = 1; ref <= candidate->nodeCount(); ++ref) {
            const camel::runtime::GCNode *node = candidate->node(ref);
            if (node != nullptr && node->kind == camel::runtime::GCNodeKind::Oper &&
                candidate->nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri() == ":io/println") {
                holder   = candidate;
                printRef = ref;
                break;
            }
        }
        if (holder != nullptr) {
            break;
        }
    }
    if (holder == nullptr) {
        os << "REINTERP probe print=none\n";
        return nullptr;
    }
    auto &editedTransaction = holder == root ? transaction : session.edit(holder);
    auto &editedDraft       = editedTransaction.edit();
    const std::span<const camel::runtime::gc_node_ref_t> args = holder->normInputsOf(printRef);
    if (args.empty()) {
        os << "REINTERP probe graph=" << camel::runtime::toValue(holder->id())
           << " print=" << printRef << " args=none\n";
        return nullptr;
    }
    const camel::runtime::gc_node_ref_t argument = args.front();
    (void)argument;
    (void)editedDraft;

    // The rule's right side, made for `float`: one instance, already built by the rule table.
    const camel::compile::gir::GenericRuleInstance *instance = nullptr;
    const auto heads = camel::parse::ast::rulesOf("rulesets.jvp");
    for (size_t s = 0; s < heads.size() && instance == nullptr; ++s) {
        auto &table = camel::compile::gir::genericRuleTable(*context);
        for (size_t i = 0; i < table.size(); ++i) {
            if (table[i]->name != heads[s].rule) {
                continue;
            }
            const uint64_t ruleId = static_cast<uint64_t>(i) + 1;
            const size_t at = camel::compile::gir::genericRuleInstance(*context, ruleId, {F}, {});
            auto *entry     = camel::compile::gir::genericRuleEntry(*context, ruleId);
            if (entry == nullptr || at >= entry->instances.size()) {
                continue;
            }
            const camel::compile::gir::GenericRuleInstance &candidate = entry->instances[at];
            if (candidate.made && candidate.rhs != nullptr && candidate.rhsBuilder != nullptr &&
                candidate.lhsOperatorUri == ":op/div_d") {
                instance = &candidate;
                break;
            }
        }
    }
    if (instance == nullptr) {
        os << "REINTERP closure instance=none\n";
        return nullptr;
    }

    // The smallest case: build the closure the rule's right side is made of, in the graph that
    // holds the `/` site, and change nothing else. Two types are what this is about -- the type the
    // static value is written under, and the type of the thing written. The reference construction
    // (modules/autodiff/pullback.cpp, `addClosure`) makes a `::Function` over the rule's graph and
    // materializes it under that same graph's signature in one call; the first attempt here wrote a
    // `StaticFunction *` into a slot typed by the rule's signature, and the size of the value was
    // read off a type that describes a function rather than a pointer.
    camel::runtime::GCGraph *siteGraph = nullptr;
    camel::runtime::gc_node_ref_t divRef = 0;
    for (size_t i = 0; i <= program->tableSize() && siteGraph == nullptr; ++i) {
        camel::runtime::GCGraph *candidate =
            program->graph(static_cast<camel::runtime::GraphId>(i));
        if (candidate == nullptr) {
            continue;
        }
        for (camel::runtime::gc_node_ref_t ref = 1; ref <= candidate->nodeCount(); ++ref) {
            const camel::runtime::GCNode *node = candidate->node(ref);
            if (node != nullptr && node->kind == camel::runtime::GCNodeKind::Oper &&
                candidate->nodeBodyAs<camel::runtime::GCOperBody>(ref)->uri() == ":op/div_d") {
                siteGraph = candidate;
                divRef    = ref;
                break;
            }
        }
    }
    if (siteGraph == nullptr) {
        os << "REINTERP closure site=none\n";
        return nullptr;
    }
    // The type a closure value has is the tuple of what it captures, not the signature of the
    // function inside it: pullback's addClosure takes a TupleType *closureType and writes the
    // ::Function under that type. Writing it under the rule's signature made the runtime read a
    // value's size off a type that describes a function -- the allocation asked for a billion
    // megabytes, which is a size computed from the wrong type, not a shortage of memory.
    auto *closureType = camel::core::type::TupleType::create(std::vector<Type *>{F, F});
    auto &closureTransaction = session.edit(siteGraph);
    auto &closureDraft       = closureTransaction.edit();
    auto *function           = ::Function::create(
        session.program(), instance->rhs->id(), closureType, camel::core::mm::autoSpace());
    const camel::runtime::gc_node_ref_t source = closureDraft.materializeStaticValue(
        camel::core::rtdata::toSlot<::Function *>(function), closureType);
    const auto divIns = siteGraph->normInputsOf(divRef);
    if (divIns.size() < 2) {
        os << "REINTERP closure div=" << divRef << " ins=" << divIns.size() << '\n';
        return nullptr;
    }
    const camel::runtime::gc_node_ref_t closure = closureDraft.addFillNode(
        closureType,
        camel::runtime::makeFillPayload(
            camel::runtime::GCFillKind::FunctionClosure, std::vector<size_t>{0, 1}));
    closureDraft.setNormInputs(closure, std::span<const camel::runtime::gc_node_ref_t>(&source, 1));
    const std::array<camel::runtime::gc_node_ref_t, 2> captures{divIns[0], divIns[1]};
    closureDraft.setWithInputs(closure, captures);
    os << "REINTERP closure graph=" << camel::runtime::toValue(siteGraph->id())
       << " div=" << divRef << " slotType=" << closureType->toString()
       << " valueType=Function* source=" << source << " closure=" << closure
       << " captures=2\n";
    const camel::runtime::Program *committed = session.commit();
    os << "REINTERP closure committed=" << (committed != nullptr ? "yes" : "no") << '\n';
    return committed;
}

class ReinterpPass : public RuntimeGraphRewritePass {
  public:
    explicit ReinterpPass(const context_ptr_t &ctx) : RuntimeGraphRewritePass(ctx) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override {
        printRuleUris(*context_, program, os);
        // The container below is kept as the record of a direction that was measured and dropped:
        // its root is a new graph, and the pipeline wants the graph it was given, by id.
        // The two steps of the replacement, in the smallest container they fit in. The program this
        // pass was given cannot have a graph replaced in it (a rewrite makes a new program), so the
        // container is a new program whose root is its first member.
        // The container is built and reported, but the program the pass answers with is still the
        // one it was given: the container's root printed nothing when it was run, so installing it
        // would replace a working entry with a silent one. The line above says it exists; the next
        // step is making it speak.
        if (const camel::runtime::Program *container = buildContainerFrom(context_, program, os)) {
            // Where the container died, for whoever picks this up: the group builds both members,
            // prints its types and its call edge (`REINTERP container mainType=... fType=...`),
            // and then the program is run as a root of its own -- and the run ends with
            // SIGSEGV (exit 139) right after the last `REINTERP` line, before the `println`
            // inside `main__min` prints anything. So the crash is in the entry the group makes,
            // not in the graphs it built: those two lines print the same signatures the original
            // graphs have. The direction is dropped for now (see the pass's apply): a program
            // whose root is a new graph cannot satisfy src/execute/camel_pass.cpp, which wants the
            // graph the pass was given, by id.
            (void)container;
        }
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
        }
        // The probe runs after the reports above, so the readings this pass already had stay as
        // they were: one added statement, in place, and the program that carries it.
        if (const camel::runtime::Program *probed = probeRoot(context_, program, os)) {
            return probed;
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