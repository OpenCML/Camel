// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "vmap.h"

#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/compile/gir/rule_templates.h"
#include "camel/core/forward.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/extent.h"
#include "camel/parse/ast/rule_head.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph_txn.h"

#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace camel::transforms {

namespace mm = camel::core::mm;
namespace rt = camel::runtime;
using ::Array;
using camel::core::rtdata::fromSlot;
using camel::core::rtdata::toSlot;
using camel::core::type::ArrayType;
using camel::core::type::ExtentForm;
using camel::core::type::FunctionType;
using camel::core::type::Type;
using camel::core::type::TypeCode;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GraphId;

namespace {

/// The ruleset this transform reads. A family is named by the module that declares it
/// (`stdlib/rulesets/vmap.cml`), which is the name `lookupRule` and `rhsCalleeOf` are keyed by.
constexpr const char *kFamily = "rulesets.vmap";

/// `RowsOf<T>`: the value with one dimension of unknown length in front of it. The same shape
/// `stdlib/types.cml` declares and `src/core/type/typecall.cpp` computes -- one element in front of
/// what the type states, so `T` with no dimension is one row of the batch.
Type *rowsOfType(Type *type) {
    if (type == nullptr) {
        return nullptr;
    }
    if (type->code() != TypeCode::Array) {
        return ArrayType::create(type);
    }
    const auto *array = tt::as_ptr<ArrayType>(type);
    std::vector<ExtentForm> forms;
    forms.reserve(array->rank() + 1);
    forms.push_back(ExtentForm::unknown());
    forms.insert(forms.end(), array->extents().begin(), array->extents().end());
    if (array->anyRank()) {
        return ArrayType::openRank(array->elemType(), forms);
    }
    return ArrayType::create(array->elemType(), forms, array->packName());
}

/// The type a rule is generic in: the type the primitive works on. A primitive over scalars is its
/// own type (`+` on two `f32`), and one over arrays works on the element of the array -- `matmul`
/// multiplies elements, `sum_axis` adds them -- so the rule is filed once per element type and says
/// which axis the batch is on, never what shape a row has. Asking for the array type instead would
/// file one instance per shape and would name the rows in type of the rule's `T`.
Type *ruleElementTypeOf(Type *type) {
    while (type != nullptr && type->code() == TypeCode::Array) {
        type = tt::as_ptr<ArrayType>(type)->elemType();
    }
    return type;
}

/// The rule `rulesets.vmap` files for `uri`, or nullptr and why not. The family is read through the
/// operator's key: `vmapBindingOf` answers the key the operator's batching rule is filed under, and
/// the head and the operand count in it are the operator's, not this family's. The key comes from
/// the operator's own module (`camel/core/forward.h#bindVmapRule`), because the spelling a rule is
/// written with (`matmul(x, w)`, `sum_axis(xs, axis, keepdims)`) is not the URI the node holds
/// (`tensor:matmul`, `tensor:sum_axis`).
std::optional<camel::parse::ast::RuleMatch> ruleOfUri(const std::string &uri, std::string &why) {
    const auto binding = camel::core::vmapBindingOf(uri);
    if (!binding.has_value()) {
        why = "the operator has no batching rule key bound at all";
        return std::nullopt;
    }
    return camel::parse::ast::lookupRule(kFamily, binding->head, binding->arity);
}

/// The rule's templates, by the name the index answered: what `genericRuleInstance` is asked for.
uint64_t ruleIdOf(camel::core::context::Context &context, const std::string &name) {
    const auto &table = camel::compile::gir::genericRuleTable(context);
    for (size_t i = 0; i < table.size(); ++i) {
        if (table[i] != nullptr && table[i]->name == name) {
            return static_cast<uint64_t>(i) + 1;
        }
    }
    return 0;
}

// The lifting of a constant is a lambda beside the draft it writes through, not a free function:
// naming the draft's type in a signature would be one more transaction-external reference to it,
// and the ledger of direct connections to the graph store only lets those numbers fall
// (test/cases/runtime/graph_draft_ledger). The draft is written the same either way.

/// The name a graph is filed under, without its module qualifier and its type arguments: the leaf a
/// declaration writes. `rulesets.vmap::vmap_mul<float>` answers `vmap_mul`.
std::string leafNameOf(const std::string &filed) {
    size_t start = filed.rfind("::");
    std::string leaf = start == std::string::npos ? filed : filed.substr(start + 2);
    const size_t cut = leaf.find_first_of("<$(");
    if (cut != std::string::npos) {
        leaf.resize(cut);
    }
    return leaf;
}

/// The function a rule's right side names, as a graph of the instance's program. The name is the
/// one the rule's syntax writes (`rhsCallee`, read by the same walk that files the rule); the graph
/// is what a function value is made from, and the instance is the program to make it in. A generic
/// function is filed under its declared name, so the graph is the one with that name.
const rt::GCGraph *calleeGraphOf(
    const camel::compile::gir::GenericRuleInstance &instance, const std::string &name,
    std::string &why) {
    if (instance.program == nullptr) {
        why = "the rule has no right side to read";
        return nullptr;
    }
    if (name.empty()) {
        why = "the rule's right side calls no named function";
        return nullptr;
    }
    for (const rt::GCGraph *graph : instance.program->graphs()) {
        if (graph != nullptr && graph->name() == name) {
            return graph;
        }
    }
    // A function of a module is filed under its qualified name, with the type arguments of a generic
    // instance after its own name: `rulesets.vmap::vmap_mul<float>` is the graph the rule's right
    // side names by `vmap_mul`. So the leaf of the filed name, without its qualifier and its type
    // arguments, is what the rule's syntax writes.
    for (const rt::GCGraph *graph : instance.program->graphs()) {
        if (graph != nullptr && leafNameOf(graph->name()) == name) {
            return graph;
        }
    }
    std::string listed;
    size_t shown = 0;
    for (const rt::GCGraph *graph : instance.program->graphs()) {
        if (graph == nullptr || shown++ >= 12) {
            continue;
        }
        listed += (listed.empty() ? "" : ", ") + graph->name();
    }
    why = "no graph of the rule's instance is named `" + name + "` (it holds: " + listed + ")";
    return nullptr;
}

} // namespace

FunctionType *vmapSignature(FunctionType *function) {
    if (function == nullptr) {
        return nullptr;
    }
    // One body is copied, so one ordinary parameter and no captured one: a `with` parameter has no
    // port to enter the copied graph by (the same shape `jvp` states).
    if (function->withTypesCount() != 0 || function->normTypesCount() != 1) {
        return nullptr;
    }
    Type *parameter = rowsOfType(function->normTypeAt(0));
    Type *result    = rowsOfType(function->exitType());
    if (parameter == nullptr || result == nullptr) {
        return nullptr;
    }
    camel::core::type::param_vec_t params;
    params.emplace_back(parameter, false);
    return FunctionType::create(camel::core::type::param_vec_t{}, params, result);
}

/// The reading of the ruleset's answers: how many primitives the walk asked about, how many the
/// family had a rule for, and how many it did not -- `missing` is the hole, and `asked=4 found=3
/// missing=1` is a rule that is missing, not a rule that was never asked for.
///
/// It is part of the transform's output, on stdout, always: a ruleset that is written is not a
/// ruleset that is used, so the count is what a reader checks, and a count behind an environment
/// variable would be a count nobody reads (the author's requirement of 2026-11-01 05:10).
std::string askedFoundMissing(const VmapGraph &graph) {
    const size_t missing = graph.asked > graph.found ? graph.asked - graph.found : 0;
    return std::format("asked={} found={} missing={}", graph.asked, graph.found, missing);
}

VmapGraph buildVmapGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, std::ostream &os) {
    if (program == nullptr || function == nullptr) {
        return VmapGraph{};
    }
    FunctionType *signature = vmapSignature(function->funcType());
    if (signature == nullptr) {
        os << "REINTERP vmap function=" << function->name() << " skip=signature\n";
        return VmapGraph{};
    }
    const Type *liftedParameter = signature->normTypeAt(0);
    const Type *liftedResult    = signature->exitType();

    VmapGraph refused;
    size_t copiedKinds = 0;
    size_t seenKinds   = 0;
    std::vector<std::string> usedRules;
    std::vector<gc_node_ref_t> liftOf;

    camel::runtime::GraphDraftGroup group(context, program);
    const GraphId member = group.reserve(
        "vmap_of_" + std::string(function->name()), "vmap_of_" + std::string(function->name()));
    {
        auto &transaction = group.transaction(member);
        auto &draft       = transaction.edit();
        draft.setFuncType(signature);
        const gc_node_ref_t port = draft.addPortNode(
            const_cast<Type *>(liftedParameter), draft.allocateRuntimeSlot(const_cast<Type *>(liftedParameter)));
        draft.appendNormPort(port);

        // A constant lifted: the batch of one row. `[3.0]` is what a constant becomes, and every
        // rule broadcasts it over the batch it meets (`at` in `rulesets/vmap.cml` reads one row as
        // that row at every index). An array constant lifts the same way -- the array is the row,
        // and the batch is that one row -- which is what a body that multiplies by a written matrix
        // needs: `matmul(a, w)` with `w` written in the body is one row of `w` at every index.
        const auto liftConstant = [&draft](
                                      Type *constantType, Type *liftedType, slot_t value,
                                      std::string &why) -> gc_node_ref_t {
            if (liftedType == nullptr || liftedType->code() != TypeCode::Array) {
                why = "the lifted type of a constant is not an array";
                return rt::kInvalidNodeRef;
            }
            auto *one = Array::create(
                mm::autoSpace(), tt::as_ptr<ArrayType>(liftedType), static_cast<size_t>(1));
            if (one == nullptr) {
                why = "no array for the lifted constant";
                return rt::kInvalidNodeRef;
            }
            if (constantType->code() == TypeCode::Array) {
                // The row is the constant's own elements, so the batch is those bytes under one
                // leading extent of length one, and the shape is read from the value rather than
                // from the type: the type states no extent the value does not.
                const auto *source = fromSlot<Array *>(value);
                if (source == nullptr || source->isSlots()) {
                    why = "a constant array whose elements are not stored as bytes";
                    return rt::kInvalidNodeRef;
                }
                std::vector<int64_t> extents;
                extents.reserve(source->rank() + 1);
                extents.push_back(1);
                extents.insert(extents.end(), source->shape(), source->shape() + source->rank());
                auto *batch = Array::create(
                    mm::autoSpace(), tt::as_ptr<ArrayType>(liftedType),
                    std::span<const int64_t>(extents));
                if (batch == nullptr || batch->byteSize() != source->byteSize()) {
                    why = "no array for the lifted constant";
                    return rt::kInvalidNodeRef;
                }
                std::memcpy(batch->rawData(), source->rawData(), source->byteSize());
                return draft.materializeStaticValue(
                    toSlot<Array *>(batch), liftedType, rt::kGCNodeFlagConstant);
            }
            switch (constantType->code()) {
            case TypeCode::Float64:
                one->dataAs<double>()[0] = fromSlot<double>(value);
                break;
            case TypeCode::Float32:
                one->dataAs<float>()[0] = fromSlot<float>(value);
                break;
            case TypeCode::Int64:
                one->dataAs<int64_t>()[0] = fromSlot<int64_t>(value);
                break;
            case TypeCode::Int32:
                one->dataAs<int32_t>()[0] = fromSlot<int32_t>(value);
                break;
            case TypeCode::Bool:
                one->dataAs<bool>()[0] = fromSlot<bool>(value);
                break;
            default:
                why = "a constant of a type this transform has no batch for";
                return rt::kInvalidNodeRef;
            }
            return draft.materializeStaticValue(
                toSlot<Array *>(one), liftedType, rt::kGCNodeFlagConstant);
        };

        std::vector<gc_node_ref_t> copyOf(function->nodeCount(), rt::kInvalidNodeRef);
        liftOf.assign(function->nodeCount(), rt::kInvalidNodeRef);
        std::string why;
        bool complete = true;
        for (gc_node_ref_t ref = 0; ref < function->nodeCount() && complete; ++ref) {
            const rt::GCNode *node = function->node(ref);
            if (node == nullptr) {
                continue;
            }
            ++seenKinds;
            switch (node->kind) {
            case rt::GCNodeKind::Port: {
                // The one parameter of the body is the batch the caller passes; a second port that
                // is not a parameter has no reading in this tier.
                if (copyOf[ref] != rt::kInvalidNodeRef) {
                    break;
                }
                copyOf[ref] = port;
                ++copiedKinds;
                break;
            }
            case rt::GCNodeKind::Gate: {
                // A gate only forwards; the walk reaches what it carries through the exit.
                ++copiedKinds;
                break;
            }
            case rt::GCNodeKind::Data: {
                const std::span<const slot_t> slots = function->staticSlots();
                const std::ptrdiff_t index          = -static_cast<std::ptrdiff_t>(node->dataIndex);
                if (node->dataIndex >= 0 || index < 0 ||
                    static_cast<size_t>(index) >= slots.size()) {
                    refused.refusedOrigin = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                    refused.refusedUri    = "a constant";
                    refused.refusedWhy    = "the slot of the constant is not readable";
                    complete              = false;
                    break;
                }
                copyOf[ref] = liftConstant(
                    node->dataType(), rowsOfType(node->dataType()),
                    slots[static_cast<size_t>(index)], why);
                if (copyOf[ref] == rt::kInvalidNodeRef) {
                    refused.refusedOrigin = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                    refused.refusedUri    = "a constant";
                    refused.refusedWhy    = why;
                    complete              = false;
                    break;
                }
                ++copiedKinds;
                break;
            }
            case rt::GCNodeKind::Oper: {
                const auto *body   = function->nodeBodyAs<rt::GCOperBody>(ref);
                const std::string uri(body->uri());
                ++refused.asked;
                const auto match = ruleOfUri(uri, why);
                if (!match.has_value() || match->status != camel::parse::ast::RuleLookup::Found) {
                    refused.refusedOrigin = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                    refused.refusedUri    = uri;
                    refused.refusedWhy    = !match.has_value()
                                                ? why
                                                : (match->status ==
                                                           camel::parse::ast::RuleLookup::NoSuchFamily
                                                       ? "no ruleset module of that name is loaded"
                                                       : "the ruleset holds no rule for this "
                                                         "operator and operand count");
                    complete              = false;
                    break;
                }
                ++refused.found;
                usedRules.push_back(match->rule);
                // The rule's right side names the function that does the batched arithmetic; the
                // transform calls it with the batches. The instance is made at the element type the
                // primitive works on, because that is the type the rule is generic in.
                Type *elementType = ruleElementTypeOf(node->dataType());
                const uint64_t ruleId = ruleIdOf(*context, match->rule);
                if (ruleId == 0) {
                    refused.refusedOrigin = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                    refused.refusedUri    = uri;
                    refused.refusedWhy    = "the rule is named in the index but not registered";
                    complete              = false;
                    break;
                }
                const size_t at = camel::compile::gir::genericRuleInstance(
                    *context, ruleId, std::vector<Type *>{elementType}, {});
                const auto *entry = camel::compile::gir::genericRuleEntry(*context, ruleId);
                if (entry == nullptr || at >= entry->instances.size() ||
                    !entry->instances[at].made) {
                    refused.refusedOrigin = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                    refused.refusedUri    = uri;
                    refused.refusedWhy    = entry != nullptr && at < entry->instances.size() &&
                                                 !entry->instances[at].why.empty()
                                             ? entry->instances[at].why
                                             : "no instance of the rule at this type";
                    complete              = false;
                    break;
                }
                const rt::GCGraph *callee = calleeGraphOf(
                    entry->instances[at],
                    camel::parse::ast::rhsCalleeOf(kFamily, match->rule), why);
                if (callee == nullptr || entry->instances[at].program == nullptr) {
                    refused.refusedOrigin = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                    refused.refusedUri    = uri;
                    refused.refusedWhy    = why;
                    complete              = false;
                    break;
                }
                const auto *closureType = camel::core::type::TupleType::create(
                    std::vector<Type *>{});
                auto *functionValue = ::Function::create(
                    entry->instances[at].program, callee->id(), closureType, mm::autoSpace());
                if (functionValue == nullptr) {
                    refused.refusedOrigin = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                    refused.refusedUri    = uri;
                    refused.refusedWhy    = "the rule's function is not a graph a value can name";
                    complete              = false;
                    break;
                }
                const gc_node_ref_t named = draft.materializeStaticValue(
                    toSlot<::Function *>(functionValue),
                    const_cast<FunctionType *>(callee->funcType()));
                std::vector<gc_node_ref_t> arguments;
                for (gc_node_ref_t input : function->normInputsOf(ref)) {
                    if (input >= function->nodeCount() ||
                        copyOf[input] == rt::kInvalidNodeRef) {
                        refused.refusedOrigin = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                        refused.refusedUri    = uri;
                        refused.refusedWhy    = "an operand was not lifted";
                        complete              = false;
                        break;
                    }
                    arguments.push_back(copyOf[input]);
                }
                if (!complete) {
                    break;
                }
                Type *resultType = rowsOfType(node->dataType());
                const gc_node_ref_t call = draft.addCallNode(resultType);
                draft.setNormInputs(call, arguments);
                draft.setWithInputs(call, std::span<const gc_node_ref_t>(&named, 1));
                copyOf[ref] = call;
                ++copiedKinds;
                break;
            }
            case rt::GCNodeKind::Call:
            case rt::GCNodeKind::Func:
                // A call is a `Func` node naming the callee beside a `Call` node reading it; either
                // one is the same refusal, reported at the callee's own origin.
                refused.refusedOrigin   = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                refused.refusedConstruct = "a call";
                refused.refusedWhy      = "the elementwise tier batches ports, constants and "
                                          "primitives; a call is a later tier";
                complete                = false;
                break;
            case rt::GCNodeKind::Brch:
            case rt::GCNodeKind::Join:
                refused.refusedOrigin    = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                refused.refusedConstruct = "a branch";
                refused.refusedWhy       = "the elementwise tier batches straight-line bodies";
                complete                 = false;
                break;
            case rt::GCNodeKind::Loop:
                refused.refusedOrigin    = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                refused.refusedConstruct = "a loop";
                refused.refusedWhy       = "the elementwise tier batches straight-line bodies";
                complete                 = false;
                break;
            default:
                refused.refusedOrigin    = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                refused.refusedConstruct = "a node of another kind";
                refused.refusedWhy =
                    std::string("node kind ") + std::to_string(static_cast<int>(node->kind)) +
                    " has no reading in the elementwise tier";
                complete = false;
                break;
            }
        }
        if (!complete) {
            // The reading of a refusal: which construct (or primitive) stopped the walk, and what
            // the ruleset answered up to there. `asked=0 found=0` is a body the walk never reached a
            // primitive in, and `asked=3 found=2` is a ruleset with a hole in it.
            os << "REINTERP vmap function=" << function->name()
               << " skip=body construct="
               << (refused.refusedConstruct.empty() ? "-" : refused.refusedConstruct)
               << " uri=" << (refused.refusedUri.empty() ? "-" : refused.refusedUri)
               << askedFoundMissing(refused) << '\n';
            return refused;
        }
        // The value the lifted chain produces: the node the exit names, followed through the gates
        // that only forward it.
        gc_node_ref_t out = function->exitNodeRef();
        while (out != rt::kInvalidNodeRef) {
            const rt::GCNode *node = function->node(out);
            if (node == nullptr || node->kind != rt::GCNodeKind::Gate) {
                break;
            }
            const auto norm = function->normInputsOf(out);
            if (!norm.empty()) {
                out = norm.back();
                continue;
            }
            const auto with = function->withInputsOf(out);
            out             = with.empty() ? rt::kInvalidNodeRef : with.back();
        }
        if (out >= function->nodeCount() || copyOf[out] == rt::kInvalidNodeRef) {
            refused.refusedOrigin = out < function->nodeCount() ? static_cast<camel::source::origin_id_t>(function->nodeOrigin(out))
                                                                : camel::source::kInvalidOriginId;
            refused.refusedConstruct = "the exit";
            refused.refusedWhy       = "the value of the body was not lifted";
            os << "REINTERP vmap function=" << function->name() << " skip=exit\n";
            return refused;
        }
        const gc_node_ref_t liftedValue = copyOf[out];
        const rt::DraftNodeHeader *header = draft.header(liftedValue);
        if (header == nullptr || header->dataType == nullptr) {
            refused.refusedConstruct = "the exit";
            refused.refusedWhy       = "the lifted value has no type";
            os << "REINTERP vmap function=" << function->name() << " skip=exit-type\n";
            return refused;
        }
        draft.setExitNode(liftedValue);
        draft.setOutputNode(liftedValue);
        draft.setReturnNode(liftedValue, rt::GCReturnKind::Self);
    }
    group.encode();
    const rt::GCGraph *made = group.encoded(member);
    const auto *madeType    = made != nullptr ? made->funcType() : nullptr;
    std::string rules;
    for (const std::string &rule : usedRules) {
        rules += (rules.empty() ? "" : ",") + rule;
    }
    os << "REINTERP vmap function=" << function->name() << " graph=" << rt::toValue(member)
       << " type=" << (madeType != nullptr ? madeType->toString() : std::string("-"))
       << " nodes=" << function->nodeCount() << " kinds=" << copiedKinds << '/' << seenKinds
       << " " << askedFoundMissing(refused)
       << " rules=" << (rules.empty() ? "-" : rules) << '\n';
    VmapGraph built = refused;
    built.program   = group.program();
    built.graph     = const_cast<rt::GCGraph *>(made);
    built.rules     = std::move(usedRules);
    return built;
}

} // namespace camel::transforms