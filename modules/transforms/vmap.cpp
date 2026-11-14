// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "vmap.h"

#include "node_order.h"
#include "refusal_readings.h"
#include "rule_expand.h"
#include "rule_types.h"

#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/compile/gir/rule_templates.h"
#include "camel/core/forward.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/extent.h"
#include "camel/parse/ast/rule_head.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph_txn.h"

#include <algorithm>
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
using camel::transforms::rules::firstOperandTypeOf;
using camel::transforms::rules::ruleElementTypeOf;

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


/// The rule `rulesets.vmap` files for `uri`, or nullptr and why not. The key -- the spelling the
/// rule's left side is written with, and its operand count -- is read off the operator itself
/// (`camel/core/forward.h#ruleKeyOf`): `tensor:matmul` is written `matmul(x, w)` because that is the
/// operator's name, and the two operators that cannot be spelled that way are named, with their
/// reasons, in `ruleKeyExceptions()`. Nothing is registered per operator.
std::optional<camel::parse::ast::RuleMatch> ruleOfUri(const std::string &uri, std::string &why) {
    const auto answer = camel::core::vmapRuleOf(uri);
    if (!answer.has_value()) {
        why = "the operator's own name says no key (no rule is written for a call of it)";
        return std::nullopt;
    }
    if (answer->status == camel::parse::ast::RuleLookup::NoRuleForPrimitive) {
        why = "the family holds no rule under that key";
    }
    return answer;
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

/// The rule of `rulesets.vmap` that answers `uri` here: the key's one law, or -- when a key holds
/// several -- the law the operands' own types pick.
///
/// This family is the same choice the transpose family makes (`modules/transforms/
/// transpose.cpp#chooseTransposedRule`) with one difference in what is compared: an instance here is
/// made at the **element** type of the operand, because that is the type a rule of this family is
/// generic in, and its ports are then compared with the operands' *batched* types -- `vmap_rule_idx::
/// <T>(xs: T[], i: int)` at `T = float` takes a `float[2, 3]` batch through its `float[]` port. The
/// relation is the call's (`Type::assignableFrom`, the port takes the operand), and among the laws
/// that take them the narrowest wins (`narrowerPortsThan`); two that both take them with neither
/// narrower are refused by name rather than picked by filing order.
std::optional<camel::parse::ast::RuleMatch> chooseRuleOfUri(
    camel::core::context::Context &context, const camel::runtime::GCGraph &function,
    gc_node_ref_t ref, const std::string &uri, std::string &why) {
    const std::vector<camel::parse::ast::RuleMatch> candidates = camel::core::vmapRulesOf(uri);
    if (candidates.empty()) {
        // One law, or no key, or no law: `ruleOfUri` says which, in the two reasons it already
        // spells.
        return ruleOfUri(uri, why);
    }
    const Type *operand = firstOperandTypeOf(function, ref);
    Type *elementType   = ruleElementTypeOf(
        operand != nullptr
                ? const_cast<Type *>(operand)
                : (function.node(ref) != nullptr ? function.node(ref)->dataType() : nullptr));
    std::vector<Type *> actual;
    for (const gc_node_ref_t input : function.normInputsOf(ref)) {
        const camel::runtime::GCNode *node = function.node(input);
        actual.push_back(node != nullptr ? node->dataType() : nullptr);
    }
    // The ports past the data operands are the operator's own configuration -- `reshape`'s target
    // shape comes in `<>` position -- and a rule names them beside the value it batches, in port
    // order (`transpose.cpp` says the same about the pullback's list). Without them a law that names
    // its shape would look like a law that names more ports than the operator has.
    for (const gc_node_ref_t input : function.withInputsOf(ref)) {
        const camel::runtime::GCNode *node = function.node(input);
        actual.push_back(node != nullptr ? node->dataType() : nullptr);
    }
    // Which of the key's laws answers this batch is one question, asked in one place
    // (`rule_expand.cpp#chooseRuleAmong`); the batching family's own part is the type an instance is
    // made at -- the *element* of the operand, which is what makes `vmap_mul<float>` a law for a
    // `float[]` batch -- and the operand list, which carries the `<>` configuration beside the value
    // (so no lower bound is asked for here: `dataOperandCount` 0).
    const RuleChoice choice = chooseRuleAmong(
        context, candidates, std::vector<Type *>{elementType}, actual, 0,
        RuleChoiceWords{"law", "operands"});
    if (!choice.found()) {
        why = choice.why;
        return std::nullopt;
    }
    return camel::parse::ast::RuleMatch{camel::parse::ast::RuleLookup::Found, choice.rule};
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
    camel::runtime::GCGraph *function, const ::Tuple *captured, std::ostream &os) {
    if (program == nullptr || function == nullptr) {
        return VmapGraph{};
    }
    FunctionType *signature = vmapSignature(function->funcType());
    if (signature == nullptr) {
        os << "REINTERP vmap function=" << function->name()
           << " skip=" << refusalCauseName(RefusalCause::Signature) << '\n';
        noteRefusal(RefusalReading("vmap", function->name(), RefusalCause::Signature));
        return VmapGraph{};
    }
    const Type *liftedParameter = signature->normTypeAt(0);
    // The values the function under the transform captured where it was written. They are part of
    // the function *value* -- its closure tuple -- and they are a *constant* of the batched graph:
    // the transform copies the value in, lifted the way a constant written in the body is lifted,
    // so the batched body reads the capture and not the batch element. Before this, every port of
    // the source graph was answered with the batch parameter, the capture port among them, so
    // `x + c` computed `2x` and answered `[2.0, 4.0, 6.0]` where `[4.0, 5.0, 6.0]` is the answer
    // (author's review of 2026-11-02, high-2).
    const auto captures         = function->closureNodes();
    const auto sourceParameters = function->normPorts();
    // Where a capture port's value is in the closure tuple: the tuple is in capture order, so the
    // port that is the i-th entry of `closureNodes` is the i-th slot.
    std::vector<size_t> captureSlot(function->nodeCount(), static_cast<size_t>(-1));
    for (size_t at = 0; at < captures.size(); ++at) {
        captureSlot[captures[at]] = at;
    }

    VmapGraph refused;
    size_t copiedKinds = 0;
    size_t seenKinds   = 0;
    std::vector<std::string> usedRules;
    std::vector<std::string> usedElements;
    std::vector<gc_node_ref_t> liftOf;
    /// Whether a node's lift is the **batch of one row** `liftConstant` makes of a constant, rather
    /// than a batch of the caller's rows. Such a value is a fine operand (every rule reads one row as
    /// that row at every index, `rulesets/vmap.cml#at`), and it is a wrong *answer*: the caller's
    /// batch has as many rows as it has, and nothing in this graph spreads one row over it. Asked of
    /// the exit below, so that a body whose whole answer is a constant is refused instead of
    /// answering a one-row array (`docs/handover/questions/2026-11-14-a-vmap-of-a-constant-body-answers-one-line.cn.md`).
    std::vector<char> oneRowOf;

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

        // The batched graph captures nothing: every capture is a constant of it (see the walk
        // below), so the value made of it carries an empty closure.

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

        // A node the family answered for: the rule's right side names the function that does the
        // batched work, and the transform calls it with the batches of this node's operands. The
        // caller has already asked (and counted the ask); what is left is the instance, at the
        // primitive's element type -- the type a rule of this family is generic in, whatever rank the
        // value itself has.
        std::vector<gc_node_ref_t> copyOf(function->nodeCount(), rt::kInvalidNodeRef);
        const auto batchThroughRule = [&](gc_node_ref_t ref, const std::string &uri,
                                          const camel::parse::ast::RuleMatch &match)
            -> gc_node_ref_t {
            // The instance is made at the element type the primitive works on, because that is the
            // type the rule is generic in -- read from an operand, not from the node, whose type is
            // what the primitive answers.
            Type *operandType = firstOperandTypeOf(*function, ref);
            Type *elementType = ruleElementTypeOf(
                operandType != nullptr ? operandType : function->node(ref)->dataType());
            // What the instance was made at, in the reading: a rule is generic in its operands'
            // element, so this is the one fact a wrong reading of the element would show.
            const std::string element = elementType != nullptr ? elementType->toString() : "-";
            bool seenElement          = false;
            for (const std::string &known : usedElements) {
                seenElement = seenElement || known == element;
            }
            if (!seenElement) {
                usedElements.push_back(element);
            }
            const uint64_t ruleId = ruleIdOf(*context, match.rule);
            if (ruleId == 0) {
                refused.refusedOrigin = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                refused.refusedUri    = uri;
                refused.refusedWhy    = "the rule is named in the index but not registered";
                return rt::kInvalidNodeRef;
            }
            const size_t at = camel::compile::gir::genericRuleInstance(
                *context, ruleId, std::vector<Type *>{elementType}, {});
            const auto *entry = camel::compile::gir::genericRuleEntry(*context, ruleId);
            if (entry == nullptr || at >= entry->instances.size() || !entry->instances[at].made) {
                refused.refusedOrigin = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                refused.refusedUri    = uri;
                refused.refusedWhy    = entry != nullptr && at < entry->instances.size() &&
                                             !entry->instances[at].why.empty()
                                         ? entry->instances[at].why
                                         : "no instance of the rule at this type";
                return rt::kInvalidNodeRef;
            }
            std::string calleeWhy;
            const rt::GCGraph *callee = calleeGraphOf(
                entry->instances[at], camel::parse::ast::rhsCalleeOf(kFamily, match.rule),
                calleeWhy);
            if (callee == nullptr || entry->instances[at].program == nullptr) {
                refused.refusedOrigin = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                refused.refusedUri    = uri;
                refused.refusedWhy    = calleeWhy;
                return rt::kInvalidNodeRef;
            }
            const auto *closureType = camel::core::type::TupleType::create(std::vector<Type *>{});
            auto *functionValue     = ::Function::create(
                entry->instances[at].program, callee->id(), closureType, mm::autoSpace());
            if (functionValue == nullptr) {
                refused.refusedOrigin = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                refused.refusedUri    = uri;
                refused.refusedWhy    = "the rule's function is not a graph a value can name";
                return rt::kInvalidNodeRef;
            }
            const gc_node_ref_t named = draft.materializeStaticValue(
                toSlot<::Function *>(functionValue), const_cast<FunctionType *>(callee->funcType()));
            // The arguments are the node's ports in port order: the normal ones, then the context
            // (`with`) ones -- `dataPortsOf` is that one list and `portRoleOf` says where the role
            // changes (camel/runtime/draft.h), and an operator that writes `withAfterNorm` declares
            // its parameters in the same order (`OpDef::params`: `reshape` is `t` then `shape`,
            // `unsqueeze` is `t` then `axis`). A `with` argument is a rule's argument too, just not
            // one read by name: the target shape of a `reshape` and the axis of an `unsqueeze` are
            // values the batched body has to be given, and a rule's right side is a function whose
            // ports follow the same order as the primitive's. They are lifted like any operand -- a
            // constant becomes the batch of one row -- so the callee receives a batch for every
            // port, exactly as it does for the normal ones.
            std::vector<gc_node_ref_t> arguments;
            for (gc_node_ref_t input : function->normInputsOf(ref)) {
                if (input >= function->nodeCount() || copyOf[input] == rt::kInvalidNodeRef) {
                    refused.refusedOrigin = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                    refused.refusedUri    = uri;
                    refused.refusedWhy    = "an operand was not lifted";
                    return rt::kInvalidNodeRef;
                }
                arguments.push_back(copyOf[input]);
            }
            for (gc_node_ref_t input : function->withInputsOf(ref)) {
                if (input >= function->nodeCount() || copyOf[input] == rt::kInvalidNodeRef) {
                    refused.refusedOrigin = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                    refused.refusedUri    = uri;
                    refused.refusedWhy    = "a `with` argument was not lifted";
                    return rt::kInvalidNodeRef;
                }
                arguments.push_back(copyOf[input]);
            }
            Type *resultType = rowsOfType(function->node(ref)->dataType());
            const gc_node_ref_t call = draft.addCallNode(resultType);
            draft.setNormInputs(call, arguments);
            draft.setWithInputs(call, std::span<const gc_node_ref_t>(&named, 1));
            return call;
        };

        liftOf.assign(function->nodeCount(), rt::kInvalidNodeRef);
        oneRowOf.assign(function->nodeCount(), 0);
        std::string why;
        bool complete = true;
        // The body is read in the order its own edges impose, not in the order of its places: a
        // rewrite appends what it writes after the readers it repoints and leaves the places of the
        // nodes it removed `Dead` (node_order.h). Reading `0 .. nodeCount()` here refused two
        // bodies `std::opt` had rewritten -- `asked=1 found=1` on `x*2.0 + x/2.0 - 1.0`, where the
        // walk met a `Dead` place, and `asked=0 found=0` on `width(r) = dim<1>(r)`, whose whole body
        // the pass folds to a constant and whose operators are then `Dead` places.
        const std::vector<gc_node_ref_t> order = dependencyOrderOf(*function);
        for (gc_node_ref_t ref : order) {
            if (!complete) {
                break;
            }
            const rt::GCNode *node = function->node(ref);
            if (node == nullptr) {
                continue;
            }
            ++seenKinds;
            switch (node->kind) {
            case rt::GCNodeKind::Port: {
                // A port of the body is one of two things, and where the source graph lists it
                // decides which. The ordinary parameter is the batch the caller passes. A capture
                // is a value the function value carries, so it enters the batched graph as a
                // *constant* -- the value read out of the closure tuple, lifted the way a constant
                // written in the body is lifted -- and the body reads it, in the source graph's own
                // capture order. Anything else is a port this tier has no reading for, and it is
                // refused instead of being answered with the batch: a silent `2x` is worse than a
                // refusal (design principles 9.9).
                if (copyOf[ref] != rt::kInvalidNodeRef) {
                    break;
                }
                if (captureSlot[ref] != static_cast<size_t>(-1)) {
                    const size_t at = captureSlot[ref];
                    if (captured == nullptr || at >= captured->size()) {
                        refused.refusedOrigin = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                        refused.refusedUri    = "a capture";
                        refused.refusedWhy    = "the function value does not carry the value it captures";
                        complete              = false;
                        break;
                    }
                    copyOf[ref] =
                        liftConstant(node->dataType(), rowsOfType(node->dataType()),
                                     captured->get<slot_t>(at), why);
                    if (copyOf[ref] == rt::kInvalidNodeRef) {
                        refused.refusedOrigin = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                        refused.refusedUri    = "a capture";
                        refused.refusedWhy    = why;
                        complete              = false;
                        break;
                    }
                    // A capture enters as the batch of one row: it is a constant of the batched body.
                    oneRowOf[ref] = 1;
                    ++copiedKinds;
                    break;
                }
                if (std::find(sourceParameters.begin(), sourceParameters.end(), ref) ==
                    sourceParameters.end()) {
                    refused.refusedOrigin = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                    refused.refusedConstruct = "a port";
                    refused.refusedUri       = "a port";
                    refused.refusedWhy =
                        "a port that is neither the parameter nor a capture of the function has no "
                        "reading in the elementwise tier";
                    complete = false;
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
                // A constant is the batch of one row; a rule broadcasts it over the batch it meets.
                oneRowOf[ref] = 1;
                ++copiedKinds;
                break;
            }
            case rt::GCNodeKind::Oper: {
                const auto *body   = function->nodeBodyAs<rt::GCOperBody>(ref);
                const std::string uri(body->uri());
                ++refused.asked;
                const auto match = chooseRuleOfUri(*context, *function, ref, uri, why);
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
                copyOf[ref] = batchThroughRule(ref, uri, *match);
                if (copyOf[ref] == rt::kInvalidNodeRef) {
                    complete = false;
                    break;
                }
                ++copiedKinds;
                break;
            }
            case rt::GCNodeKind::Func: {
                // A `Func` node is one of two things, and its value type says which. A *direct call*
                // names its callee's graph, carries the arguments as its norm inputs, and its value
                // is the callee's result (`f(x)` is one node, not a value plus a call). A *function
                // value* is a function type, and carries nothing into a batched graph: the node that
                // reads it is the one that names a use.
                Type *valueType = function->node(ref)->dataType();
                if (valueType != nullptr && valueType->code() == TypeCode::Function) {
                    ++copiedKinds;
                    break;
                }
                // A call is asked about like a primitive, by the key the family files a function
                // under: the callee's name and how many arguments the call passes. A family may hold
                // a rule for a function -- the function's own name is a head like any other -- and
                // then the rule says which function does the batched work, exactly as it does for an
                // operator. What is never done is walking past a call unasked: a body whose callee
                // the family has no rule for stops here, with the ask counted (`asked=1 found=0`),
                // because "there is no rule for this call" is an answer and "nothing was asked" is
                // not (design principles 9.9).
                const rt::GCGraph *callee = program->calleeOf(*function, ref);
                const std::string name =
                    callee != nullptr ? leafNameOf(callee->name()) : std::string();
                const size_t operands = function->normInputsOf(ref).size();
                ++refused.asked;
                if (name.empty()) {
                    refused.refusedOrigin    = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                    refused.refusedConstruct = "a call";
                    refused.refusedUri       = "a call";
                    refused.refusedWhy       = "the call names no graph of the program";
                    complete                 = false;
                    break;
                }
                const auto match = camel::parse::ast::lookupRule(
                    kFamily, name, static_cast<uint32_t>(operands));
                if (match.status != camel::parse::ast::RuleLookup::Found) {
                    refused.refusedOrigin    = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                    refused.refusedConstruct = "a call";
                    refused.refusedUri       = name;
                    refused.refusedWhy =
                        match.status == camel::parse::ast::RuleLookup::NoSuchFamily
                            ? "no ruleset module of that name is loaded"
                            : "the ruleset holds no rule for a call to `" + name + "` with " +
                                  std::to_string(operands) +
                                  (operands == 1 ? " argument" : " arguments");
                    complete = false;
                    break;
                }
                ++refused.found;
                usedRules.push_back(match.rule);
                copyOf[ref] = batchThroughRule(ref, name, match);
                if (copyOf[ref] == rt::kInvalidNodeRef) {
                    refused.refusedConstruct = "a call";
                    complete                 = false;
                    break;
                }
                ++copiedKinds;
                break;
            }
            case rt::GCNodeKind::Call: {
                // An indirect call: the callee is a *value* the graph computed, so there is no name to
                // ask a family about, and a rule is filed under a name. Counting it as asked is not a
                // fiction: the family is the thing that would have to answer, and it cannot be asked
                // -- which is a hole in the body, reported like any other (asked=1 found=0).
                ++refused.asked;
                refused.refusedOrigin    = static_cast<camel::source::origin_id_t>(function->nodeOrigin(ref));
                refused.refusedConstruct = "a call";
                refused.refusedUri       = "a call through a value";
                refused.refusedWhy       = "the callee is a value, not a name: no rule can be filed "
                                           "under it";
                complete                 = false;
                break;
            }
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
            case rt::GCNodeKind::Dead:
                // A place a rewrite removed is not a node: it holds no value and reads none. The
                // walk's order does not name it (node_order.h), and a walk that met one anyway is
                // looking at the places of the graph, not at the graph.
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
               << " skip=" << refusalCauseName(RefusalCause::Body) << " construct="
               << (refused.refusedConstruct.empty() ? "-" : refused.refusedConstruct)
               << " uri=" << (refused.refusedUri.empty() ? "-" : refused.refusedUri)
               << askedFoundMissing(refused) << '\n';
            noteRefusal(
                RefusalReading("vmap", function->name(), RefusalCause::Body)
                    .atUri(refused.refusedUri)
                    .atConstruct(refused.refusedConstruct));
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
            os << "REINTERP vmap function=" << function->name()
               << " skip=" << refusalCauseName(RefusalCause::Exit) << '\n';
            noteRefusal(
                RefusalReading("vmap", function->name(), RefusalCause::Exit)
                    .atConstruct("the exit"));
            return refused;
        }
        const gc_node_ref_t liftedValue = copyOf[out];
        if (oneRowOf[out] != 0) {
            // The body's whole answer is a constant, so its batch is the one row `liftConstant`
            // wrote and not the batch the caller has: `vmap((x: float) => 3.0)` over a `float[4]`
            // answered `[3.0]`, silently, with the pass and without it. A constant is a fine operand
            // -- every rule reads one row as that row at every index -- but as an answer it is a
            // length the caller did not ask for, and everything downstream (`len`, `sum`, an index)
            // is then off by the batch's extent. Refused by name instead, which is design principles
            // 9.9's rule and the rule the port above already states; what the answer *should* be is
            // the author's (docs/handover/questions/2026-11-14-a-vmap-of-a-constant-body-answers-one-line.cn.md).
            refused.refusedOrigin    = static_cast<camel::source::origin_id_t>(function->nodeOrigin(out));
            refused.refusedConstruct = "an answer of one row";
            refused.refusedWhy =
                "the body's answer is a constant, whose batch is one row, and the batched graph has "
                "no node that spreads one row over the caller's batch";
            os << "REINTERP vmap function=" << function->name()
               << " skip=" << refusalCauseName(RefusalCause::Body)
               << " construct=" << refused.refusedConstruct << " uri=the exit"
               << askedFoundMissing(refused) << '\n';
            noteRefusal(
                RefusalReading("vmap", function->name(), RefusalCause::Body)
                    .atConstruct(refused.refusedConstruct));
            return refused;
        }
        const rt::DraftNodeHeader *header = draft.header(liftedValue);
        if (header == nullptr || header->dataType == nullptr) {
            refused.refusedConstruct = "the exit";
            refused.refusedWhy       = "the lifted value has no type";
            os << "REINTERP vmap function=" << function->name()
               << " skip=" << refusalCauseName(RefusalCause::ExitType) << '\n';
            noteRefusal(
                RefusalReading("vmap", function->name(), RefusalCause::ExitType)
                    .atConstruct("the exit"));
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
    std::string elements;
    for (const std::string &element : usedElements) {
        elements += (elements.empty() ? "" : ",") + element;
    }
    os << "REINTERP vmap function=" << function->name() << " graph=" << rt::toValue(member)
       << " type=" << (madeType != nullptr ? madeType->toString() : std::string("-"))
       << " nodes=" << function->nodeCount() << " kinds=" << copiedKinds << '/' << seenKinds
       << " " << askedFoundMissing(refused)
       << " rules=" << (rules.empty() ? "-" : rules)
       << " elements=" << (elements.empty() ? "-" : elements) << '\n';
    VmapGraph built = std::move(refused);
    built.program   = camel::runtime::ProgramHold(*context, group.program());
    built.graph     = const_cast<rt::GCGraph *>(made);
    built.rules     = std::move(usedRules);
    return built;
}

} // namespace camel::transforms
