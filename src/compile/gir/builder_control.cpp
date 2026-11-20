// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Control flow: branches, the arms of a match, loops, and the exit each of them gives.
 */

#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"

#include "camel/compile/gir/static_function.h"
#include "camel/core/data/composite/tuple.h"
#include "camel/core/data/primary.h"
#include "camel/core/data/ref.h"
#include "camel/core/numeric_convert.h"
#include "camel/core/operator_names.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/type/composite/stream.h"
#include "camel/execute/entry_args.h"
#include "camel/runtime/control.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"
#include "core/module/entity_internal.h"

#include "camel/core/operator_traits.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/other.h"

#include <atomic>
#include <cctype>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {

node_handle_t
Builder::castNodeTo(node_handle_t value, Type *target, const GCT::node_ptr_t &origin) {
    node_handle_t castNode = currGraph_->addCastNode(target);
    linkNodes(LinkType::Norm, value, castNode);
    registerNodeOrigin(context_, castNode, origin, "gir.cast");
    return castNode;
}

namespace {

/// The value a body hands on, read on the graph rather than by how the source spelled it.
///
/// A name is not a computation: `let r = (xs->append<7>, n + 1)` followed by `r` leaves the
/// tuple's own FILL as the body's value, exactly as writing the tuple in place does, so a name
/// needs no case of its own here. What a block can add around the value is an anchor: a GATE
/// that orders the value after the block's effects forwards it unchanged (`sync { …; r }` ends
/// in one), and the criterion is the value behind it. A CALL is not looked through: what a call
/// returns is computed in another graph (a function's), whose state
/// this is not — a loop state is only written in place when the graph that returns it also
/// builds it.
node_handle_t loopStateValueOf(node_handle_t node, bool preserveWaits = true) {
    // A gate that carries a wait is the block's execution anchor, not only a value forwarder:
    // keep it so the effects behind the state remain reachable. Only a forwarder with no wait is
    // looked through. A gate chain is at most the number of blocks the value passed through.
    for (size_t depth = 0; node != nullptr && depth < 16; ++depth) {
        if (!nodeIsKind(node, runtime::GCNodeKind::Gate)) {
            return node;
        }
        if (preserveWaits && (!syncInputsOf(node).empty() || !ctrlInputsOf(node).empty())) {
            return node;
        }
        const auto inputs = normInputsOf(node);
        if (inputs.size() != 1) {
            return node; // a gate that waits on nothing has no value to forward
        }
        node = nodeDraftOf(node)->ref(inputs.front());
    }
    return node;
}

/// The results of the arms a loop body ends in: the value itself, or, behind a JOIN (an `if` or a
/// `match`), the result of each arm, looking through the JOINs of an `else if` chain.
std::vector<node_handle_t> loopArmResultsOf(node_handle_t node) {
    node = loopStateValueOf(node, false);
    if (node == nullptr || !nodeIsKind(node, runtime::GCNodeKind::Join)) {
        return {node};
    }
    std::vector<node_handle_t> arms;
    for (runtime::gc_node_ref_t input : withInputsOf(node)) {
        const std::vector<node_handle_t> inner = loopArmResultsOf(nodeDraftOf(node)->ref(input));
        arms.insert(arms.end(), inner.begin(), inner.end());
    }
    return arms;
}

/// Whether a node that reads a value can keep it after its own run: a call, a branch, a closure, a
/// capture, and every operator that stores what it is given (an append into another array, a
/// field, a collection slot). Only a pure computation whose result holds no reference is not such
/// a holder, which is the answer `Builder::markNonRetainingReads` uses as its condition 3 for the
/// reads that may keep a value. An operator of unknown purity is a holder: the safe direction.
bool nodeKeepsItsInput(node_handle_t node) {
    if (!nodeIsKind(node, runtime::GCNodeKind::Oper)) {
        return true;
    }
    const auto op = nodeGraphOf(node)->operTarget(node);
    if (op == nullptr || !camel::core::OperatorTable::instance().isPure(op->uri())) {
        return true;
    }
    const Type *result = valueTypeOf(nodeTypeOf(node));
    // A string is not an array: a computation that makes one cannot hand out what it read.
    return result == nullptr || (result->code() != TypeCode::String && isGCTraced(result->code()));
}

/// The values a round hands on as the next state, on the graph: the results of the arms the body
/// can end in and the GATEs that forward one of them to the body's value (the walk
/// `loopStateValueOf` makes, here collected). A state's next value is such a value or a part of
/// one, and nothing else in the round may keep it.
std::vector<node_handle_t>
loopStateValues(node_handle_t bodyValue, const std::vector<node_handle_t> &arms) {
    std::vector<node_handle_t> values = arms;
    node_handle_t at                  = bodyValue;
    for (size_t depth = 0; at != nullptr && depth < 16; ++depth) {
        values.push_back(at);
        if (!nodeIsKind(at, runtime::GCNodeKind::Gate)) {
            break;
        }
        const auto inputs = normInputsOf(at);
        if (inputs.size() != 1) {
            break;
        }
        at = nodeDraftOf(at)->ref(inputs.front());
    }
    return values;
}

/// Gives `node` a new value type, moving it to a slot of that type. Used when the element type
/// of an empty-array loop state is learned from the body's append after the graph was built.
void retypeValueNode(DraftGraphBuilder *graph, node_handle_t node, Type *type) {
    if (graph == nullptr || node == nullptr || type == nullptr) {
        return;
    }
    auto &draft   = graph->transaction().edit();
    const auto id = nodeIdOf(node);
    draft.setNodeDataType(id, type);
    if (nodeIsKind(node, runtime::GCNodeKind::Data)) {
        return; // a static slot, not a frame slot
    }
    draft.setNodeDataIndex(id, graph->addRuntimeSlot(type));
}

/// A state initialised by the empty array literal and nothing else: the only shape whose
/// element type the loop may still have to learn from the body.
bool isEmptyArrayLiteral(node_handle_t node) {
    if (!nodeIsKind(node, runtime::GCNodeKind::Fill) &&
        !nodeIsKind(node, runtime::GCNodeKind::Data)) {
        return false;
    }
    const Type *type = valueTypeOf(nodeTypeOf(node));
    if (type == nullptr || type->code() != TypeCode::Array) {
        return false;
    }
    if (isUntypedArrayType(type)) {
        return true; // the untyped `[]`: it has no elements by construction
    }
    if (nodeIsKind(node, runtime::GCNodeKind::Data)) {
        // A typed `[]` is a static array: its length says whether the literal was empty.
        const auto &draft       = *nodeDraftOf(node);
        const int32_t dataIndex = draft.node(nodeIdOf(node))->header.dataIndex;
        if (dataIndex < 0 && static_cast<size_t>(-dataIndex) < draft.staticSlots().size()) {
            const auto *array = camel::core::rtdata::fromSlot<::Array *>(
                draft.staticSlots()[static_cast<size_t>(-dataIndex)]);
            return array == nullptr || array->size() == 0;
        }
        return false;
    }
    return withInputsOf(node).empty();
}

/// Whether a value of this type is a struct or tuple whose parts can be the components of a
/// loop state. A cell and the tagged forms are representations, not collections of values:
/// their parts are not independent values of the programme.
bool isSplittableAggregate(const Type *type) {
    if (type == nullptr) {
        return false;
    }
    if (type->code() == TypeCode::Struct) {
        const auto *strct = tt::as_ptr<const StructType>(type);
        return strct->resolved() && strct->size() > 0;
    }
    if (type->code() == TypeCode::Tuple) {
        const auto *tuple = tt::as_ptr<const TupleType>(type);
        return tuple->resolved() && tuple->size() > 0 && !tuple->isCell() && !tuple->isTagged();
    }
    return false;
}

size_t aggregatePartCount(const Type *type) {
    if (type->code() == TypeCode::Struct) {
        return tt::as_ptr<const StructType>(type)->size();
    }
    return tt::as_ptr<const TupleType>(type)->size();
}

Type *aggregatePartType(const Type *type, size_t index) {
    if (type->code() == TypeCode::Struct) {
        return tt::as_ptr<const StructType>(type)->typeAt(index);
    }
    return tt::as_ptr<const TupleType>(type)->typeAt(index);
}

std::string aggregatePartName(const Type *type, size_t index) {
    if (type->code() == TypeCode::Struct) {
        return std::string(tt::as_ptr<const StructType>(type)->fieldName(index));
    }
    return std::to_string(index);
}

/// The part of `type` an ACCS node reads, when it reads one: `s.x` of a struct, `t.0` of a
/// tuple, of exactly the part's type.
std::optional<size_t> accessedPartOf(node_handle_t node, const Type *type) {
    if (!nodeIsKind(node, runtime::GCNodeKind::Accs) || type == nullptr) {
        return std::nullopt;
    }
    const auto norm = normInputsOf(node);
    if (norm.size() != 1) {
        return std::nullopt;
    }
    const auto payload = nodeDraftOf(node)->payloadOf(nodeIdOf(node));
    if (payload.size() < sizeof(runtime::GCAccsBody)) {
        return std::nullopt;
    }
    const auto *body = reinterpret_cast<const runtime::GCAccsBody *>(payload.data());
    size_t index     = 0;
    if (type->code() == TypeCode::Struct) {
        if (body->accsKind != runtime::GCAccsKind::StructKey) {
            return std::nullopt;
        }
        const auto field = tt::as_ptr<const StructType>(type)->findField(body->key());
        if (!field.has_value()) {
            return std::nullopt;
        }
        index = *field;
    } else if (type->code() == TypeCode::Tuple) {
        if (body->accsKind != runtime::GCAccsKind::TupleIndex ||
            body->value >= tt::as_ptr<const TupleType>(type)->size()) {
            return std::nullopt;
        }
        index = body->value;
    } else {
        return std::nullopt;
    }
    Type *part = aggregatePartType(type, index);
    Type *read = valueTypeOf(nodeTypeOf(node));
    if (part == nullptr || read == nullptr || (read != part && !read->equals(part))) {
        return std::nullopt;
    }
    return index;
}

/// The next value of the part `step` of `value` (`value` is a struct or tuple FILL, or any value
/// of that type). A FILL's parts are its With inputs, which is what lets an `append` among them
/// keep growing the state's array in place; a part the FILL does not write is read from it.
node_handle_t projectPart(node_handle_t value, const Type *type, size_t step) {
    if (value == nullptr || type == nullptr) {
        return nullptr;
    }
    node_handle_t source = value;
    // A gate forwards its value unchanged; an identity CAST between two spellings of the same
    // struct layout does not move a field (design principles 5.2). Both are looked through, so
    // that a struct written in a `sync` body or under a named type still gives its parts.
    for (size_t depth = 0; depth < 16; ++depth) {
        if (nodeIsKind(source, runtime::GCNodeKind::Gate)) {
            const auto inputs = normInputsOf(source);
            if (inputs.size() != 1) {
                break;
            }
            source = nodeDraftOf(source)->ref(inputs.front());
            continue;
        }
        if (nodeIsKind(source, runtime::GCNodeKind::Cast)) {
            const auto inputs = normInputsOf(source);
            Type *from = inputs.empty()
                             ? nullptr
                             : valueTypeOf(nodeTypeOf(nodeDraftOf(source)->ref(inputs.front())));
            Type *to   = valueTypeOf(nodeTypeOf(source));
            if (inputs.size() == 1 && from != nullptr && to != nullptr &&
                from->code() == TypeCode::Struct && to->code() == TypeCode::Struct &&
                to->castSafetyFrom(from) == CastSafety::Safe) {
                source = nodeDraftOf(source)->ref(inputs.front());
                continue;
            }
        }
        break;
    }
    if (nodeIsKind(source, runtime::GCNodeKind::Fill)) {
        const auto payload = nodeDraftOf(source)->payloadOf(nodeIdOf(source));
        if (payload.size() >= sizeof(runtime::GCFillBody)) {
            const auto *fill = reinterpret_cast<const runtime::GCFillBody *>(payload.data());
            const bool structFill =
                fill->fillKind == runtime::GCFillKind::Struct && type->code() == TypeCode::Struct;
            const bool tupleFill =
                fill->fillKind == runtime::GCFillKind::Tuple && type->code() == TypeCode::Tuple;
            if (structFill || tupleFill) {
                const auto slots = fill->slots();
                const auto with  = withInputsOf(source);
                for (size_t i = 0; i < slots.size() && i < with.size(); ++i) {
                    if (slots[i] == step) {
                        return nodeDraftOf(source)->ref(with[i]);
                    }
                }
            }
        }
    }
    return nullptr;
}

} // namespace


void Builder::coerceArmExit(const arm_ptr_t &arm, Type *target, const GCT::node_ptr_t &origin) {
    Type *exitType = arm->exitType();
    if (exitType != nullptr && target->assignableFrom(exitType)) {
        return;
    }
    // The nodes are added to the arm's own region; no name is looked up, so the scopes of the
    // position the branch stands at do no harm.
    auto &draft                = arm->graph->transaction().edit();
    const uint32_t outerRegion = draft.insertRegion();
    draft.setInsertRegion(arm->region);
    node_handle_t output  = arm->output;
    node_handle_t coerced = coerceTo(output, target, origin);
    if (coerced == nullptr) {
        // The caller asks with `entersImplicitly`, which is the check `coerceTo` makes last, so
        // this is a compiler defect and not an error in the program: the assertion below is the
        // debug build's, and a release build reports it at the branch instead of handing a null
        // node to `normInputsOf` and the join (that was a segmentation fault).
        GUARD(coerced != nullptr, "A branch arm is joined only with a type its value enters.");
        diags_->of(InternalDiag::UnknownInternalError)
            .atOrigin(origin != nullptr ? origin->origin() : arm->origin)
            .commit(
                "the value of a branch arm is not joined: the arm gives '" +
                (exitType != nullptr ? exitType->toString() : std::string("?")) +
                "' and the join takes '" +
                (target != nullptr ? target->toString() : std::string("?")) +
                "', which the value does not enter");
        throw BuildAbortException();
    }
    if (normInputsOf(coerced).empty() && !ctrlInputsOf(output).empty()) {
        // A literal took the place of the output (`[]`): what the output waited for (the
        // statements of a sync arm) is still waited for.
        node_handle_t gate = currGraph_->addGateNode(target);
        linkNodes(LinkType::Norm, coerced, gate);
        linkNodes(LinkType::Sync, output, gate);
        coerced = gate;
    }
    draft.setInsertRegion(outerRegion);
    arm->output = coerced;
    arm->value  = coerced;
    arm->type   = target;
}

bool Builder::branchTakesNoValue(const GCT::node_ptr_t &gct) const {
    // A `return` in a function declared `: void`, and the tail of such a body, stand in a
    // position whose type is `void`: nothing takes what the branch gives there (design
    // principles 3.7).
    if (expectedValueType_ != nullptr && expectedValueType_->code() == TypeCode::Void) {
        return true;
    }
    // A branch written as a statement is the statement whose value the block drops
    // (visitExecNode): the same position.
    return gct != nullptr && unusedStatement_ == gct;
}

void Builder::readArmCellValue(const arm_ptr_t &arm) {
    Type *type = arm->type;
    if (type == nullptr || valueTypeOf(type) == type) {
        return;
    }
    // A cell is not a value: an arm that gives one gives the value the cell holds, and the
    // cell's type never reaches the join. The
    // read takes the data edge every read has -- the version the last write made, or the cell
    // itself before any write (`valueVersionOf`), and the cell itself for a write that gives
    // the cell it wrote into (`__assn__`) -- and nothing else: it stands in the arm's region,
    // behind the GATE an arm with statements hands its value on through, so the arm's own chain
    // carries the order and the block's chain is not moved by a read that belongs to the arm
    // (readVariable would make this read the block's last node).
    node_handle_t cell = arm->output;
    node_handle_t version{};
    if (isVariable(cell)) {
        version = valueVersionOf(cell);
    }
    auto &draft                = arm->graph->transaction().edit();
    const uint32_t outerRegion = draft.insertRegion();
    draft.setInsertRegion(arm->region);
    node_handle_t read =
        variableOperator(camel::core::oper::VarGet, version != nullptr ? version : cell);
    if (version == nullptr) {
        // The write gave no version of the cell, so the cell is the read's data port and the
        // write that must execute first is what a control input stands for (readVariableThrough,
        // orderAfterLastWriter; design principles 3.5, F3).
        if (auto modifier = modifierOf(cell); modifier.has_value()) {
            if (sameGraph(*modifier, currGraph_) && linkCheek(*modifier, read)) {
                writeControlLink(*modifier, read);
            }
        }
    }
    draft.setInsertRegion(outerRegion);
    arm->output = read;
    arm->value  = read;
    arm->type   = valueTypeOf(type);
}

Type *Builder::declaredExitTypeHere() const {
    if (activeRestBody_ != nullptr && activeStaticBody_ == nullptr) {
        // The body of a rest function declares its own result type, whichever graph it is
        // expanded into.
        return activeRestBody_->exitType;
    }
    FunctionType *funcType = currGraph_ != nullptr ? currGraph_->funcType() : nullptr;
    // An undeclared return type is set from the first value returned (see
    // checkDeclaredReturn): before that, a type that is present was declared.
    if (funcType == nullptr || !funcType->hasExitType() ||
        currGraph_->view().exitNode() != runtime::kInvalidNodeRef) {
        return nullptr;
    }
    return funcType->exitType();
}

Type *Builder::ownDeclaredExitType() const {
    if (activeArm() != nullptr) {
        return nullptr; // an arm declares no type of its own
    }
    FunctionType *funcType = currGraph_ != nullptr ? currGraph_->funcType() : nullptr;
    if (funcType == nullptr || !funcType->hasExitType() ||
        currGraph_->view().exitNode() != runtime::kInvalidNodeRef) {
        return nullptr;
    }
    return funcType->exitType();
}

Type *Builder::tailPositionType() const {
    // A function's last expression is the value it returns; a lambda's is its own. An arm's
    // body has no declared type of its own: the branch's position is what the arm stands at.
    Type *declared = ownDeclaredExitType();
    return declared != nullptr ? declared : expectedValueType_;
}

Type *Builder::expectedExitTypeHere() const {
    if (activeArm() != nullptr || activeStaticBody_ != nullptr || currentBlock_ != functionBody_) {
        // Inside a branch arm, the selected body of a `match type`, or a block written inside a
        // body, the value of a `return` is that construct's value, so its position is the one the
        // construct stands at; the function's declared return type has nothing to do with it
        // (design principles 5.1, language reference 7.3).
        return expectedValueType_;
    }
    return declaredExitTypeHere();
}

node_handle_t Builder::adaptExitValue(const GCT::node_ptr_t &gct, node_handle_t value) {
    Type *declared  = declaredExitTypeHere();
    Type *valueType = valueTypeOf(nodeTypeOf(value));
    if (valueType != nullptr && valueType->equals(errorType())) {
        // An error value handed out by a function that declares a `T!` is that type's failed
        // arm (design principles 3.9): `return error('DivByZero', '...')` fails with that
        // error. This holds wherever the exit stands, an arm of a branch included -- there the
        // arm's own position may be nothing at all (the branch is a statement whose value
        // nothing uses), and the function's declaration is what the error value is read
        // against. OPER __throw__ makes the failed value and the CAST gives it the declared
        // type; both are the same object. A function that declares no `T!` leaves the value as
        // it is, and the diagnostic for an exit that does not fit its position is the one the
        // position gives.
        if (TupleType *fallible = asFallible(declared); fallible != nullptr) {
            node_handle_t failed =
                applyTagOperator(camel::core::oper::TagThrow, bindTemporary(value), gct->at(0));
            return fallible->equals(nodeTypeOf(failed)) ? failed
                                                        : castNodeTo(failed, fallible, gct->at(0));
        }
    }
    // `return v` under a declared `T?` / `T!` puts `v` into that form (design principles 3.8;
    // coerceTo): it is the position a value enters by itself, and an error value written there
    // becomes the failed arm of that `T!`. The declared type is also the name the value is used
    // as: an unnamed
    // struct value in a declared named type is given that type here (a CAST that moves
    // nothing, or a copy when the fields are written in another order; design principles
    // 5.1, 5.2), so a named type is never erased to its shape. Applying the position's type
    // unless the value already has it is the same rule checkValue applies everywhere else;
    // a value that fits neither way is left alone for checkDeclaredReturn to report. In a
    // branch arm the position is the arm's (design principles 5.1: a `return` there is not the
    // function's return).
    Type *position = expectedExitTypeHere();
    if (position != nullptr && !position->equals(valueType)) {
        if (node_handle_t entered = coerceTo(value, position, gct->at(0))) {
            return entered;
        }
    }
    return value;
}

void Builder::enterBlockScope() {
    nodeScope_      = nodeScope_->enter("");
    graphScope_     = graphScope_->enter("");
    decoratedScope_ = decoratedScope_->enter("");
    templateScope_  = templateScope_->enter("");
}

void Builder::leaveBlockScope() {
    nodeScope_      = nodeScope_->leave();
    graphScope_     = graphScope_->leave();
    decoratedScope_ = decoratedScope_->leave();
    templateScope_  = templateScope_->leave();
}

void Builder::noteVariable(node_handle_t variable) {
    ArmBuild *arm = activeArm();
    if (arm == nullptr || nodeGraphOf(variable) != arm->graph) {
        return;
    }
    const auto [at, fresh] = arm->touched.try_emplace(variable);
    if (!fresh) {
        return;
    }
    arm->touchedOrder.push_back(variable);
    if (auto writer = modifierOf(variable); writer.has_value()) {
        at->second.writerBefore = nodeIdOf(*writer);
    }
    if (auto uses = pendingUses_.find(variable); uses != pendingUses_.end()) {
        at->second.usesBefore = uses->second;
    }
}

Builder::arm_ptr_t Builder::buildArm(
    const GCT::node_ptr_t &exec, const std::function<node_handle_t()> &body,
    runtime::GCRegionKind kind, const std::function<void(ArmBuild &)> &declare) {
    auto arm                   = std::make_shared<ArmBuild>();
    auto &draft                = currGraph_->transaction().edit();
    const uint32_t outerRegion = draft.insertRegion();
    arm->bodyId                = nextBodyId_++;
    arm->graph                 = currGraph_.get();
    arm->region                = draft.addRegion(kind, outerRegion);
    arm->kind                  = kind;
    arm->origin                = exec != nullptr ? exec->origin() : camel::source::kInvalidOriginId;
    arm->firstNode             = static_cast<draft_node_ref_t>(draft.nodeSlotCount());
    if (context_ && context_->sourceContext()) {
        const auto origin = deriveGirOrigin(
            context_,
            exec,
            camel::source::OriginKind::Graph,
            kind == runtime::GCRegionKind::Arm ? "gir.brch.case.graph" : "gir.loop.graph",
            {});
        if (origin != camel::source::kInvalidOriginId) {
            draft.setRegionOrigin(arm->region, static_cast<uint32_t>(origin));
        }
    }
    enterBlockScope();
    draft.setInsertRegion(arm->region);
    armStack_.push_back(arm.get());
    // A body given by the caller is a body of its own like any other (see visitBodyNode).
    const bool outerSynced        = synced_;
    const node_handle_t outerLast = lastSyncedNode_;
    const auto finish             = [&] {
        for (node_handle_t variable : arm->touchedOrder) {
            ArmBuild::Touched &touched = arm->touched[variable];
            if (auto writer = modifierOf(variable);
                writer.has_value() &&
                (!touched.writerBefore.has_value() || *touched.writerBefore != nodeIdOf(*writer))) {
                touched.writerAfter = *writer;
            }
            if (auto uses = pendingUses_.find(variable); uses != pendingUses_.end()) {
                touched.usesAfter = uses->second;
            }
            auto &modifiers = nodeModifierMaps_[nodeGraphOf(variable)];
            if (touched.writerBefore.has_value()) {
                modifiers[nodeIdOf(variable)] = *touched.writerBefore;
            } else {
                modifiers.erase(nodeIdOf(variable));
            }
            if (touched.usesBefore.empty()) {
                pendingUses_.erase(variable);
            } else {
                pendingUses_[variable] = touched.usesBefore;
            }
        }
        synced_         = outerSynced;
        lastSyncedNode_ = outerLast;
        armStack_.pop_back();
        draft.setInsertRegion(outerRegion);
        leaveBlockScope();
    };
    try {
        if (declare) {
            declare(*arm);
        }
        if (body) {
            synced_         = false;
            lastSyncedNode_ = {};
            setArmResult(*arm, body());
        } else {
            setArmResult(*arm, visitBodyNode(exec));
        }
    } catch (...) {
        finish();
        throw;
    }
    finish();
    return arm;
}

void Builder::setArmResult(ArmBuild &arm, node_handle_t result) {
    if (arm.returned) {
        return;
    }
    if (result == nullptr) {
        result     = currGraph_->addStaticDataNode(Data::null());
        arm.output = result;
        arm.value  = result;
        arm.type   = Type::Void();
        return;
    }
    // A sync block that ends in a value hands it back behind a GATE (see visitExecNode): the
    // GATE is what the arm gives, the value it forwards is the arm's value.
    node_handle_t value = result;
    if (nodeIsKind(result, runtime::GCNodeKind::Gate) && !normInputsOf(result).empty()) {
        value = nodeDraftOf(result)->ref(normInputsOf(result).front());
    }
    arm.output = result;
    arm.value  = value;
    arm.type   = nodeTypeOf(value);
}

node_handle_t Builder::visitTaggedBrchNode(const GCT::node_ptr_t &gct) {
    using Kind         = GCT::BranchKind;
    const Kind kind    = gct->branchKind();
    const bool onNull  = kind == Kind::NullThen;
    const char *symbol = onNull ? "??" : "!!";

    // The left side is a `T?` (`T!`) as the name it is, and the right side is a `T` or the
    // same `T?` (design principles 3.8): neither is in the position the whole expression
    // stands at, so neither takes that position's type. The position's type applies to the
    // branch's value once, where the branch stands (`checkValue`); leaking it down here gave
    // the subject's arms a `float?` and the right side the form of the whole result.
    node_handle_t subject = nullptr;
    {
        ExpectedValueScope subjectScope(expectedValueType_, nullptr);
        subject = valueNodeOf(visit(gct->at(0)));
    }
    // `a ?? d` where `a` cannot be null is allowed, with a warning (design principles 3.8): a
    // plain value has no null case at all, and a name an `if` around it already narrowed to a
    // `T` is that same case one step later, so both are put back into `T?` and built as an
    // ordinary `??`. A `void` left side is not a value and a `T!` carries the other mark:
    // both keep the refusal below.
    Type *unopened    = valueTypeOf(nodeTypeOf(subject));
    const bool noTag  = asTagged(unopened) == nullptr;
    const bool valued = unopened != nullptr && unopened->code() != TypeCode::Void;
    if (onNull && noTag && valued) {
        subject = reopenKnownNotNull(gct->at(0), subject, gct, "??");
    }
    Type *subjectType = valueTypeOf(nodeTypeOf(subject));
    TupleType *tagged = asTagged(subjectType);
    if (tagged == nullptr || tagged->isNullable() != onNull) {
        diags_->of(SemanticDiag::TaggedOperandMismatch)
            .atOrigin(gct->origin())
            .commit(
                symbol,
                "its left side has type '" + subjectType->toString() + "', which is not a '" +
                    (onNull ? "T?" : "T!") + "'");
        throw BuildAbortException();
    }
    Type *payload          = tagged->payloadType();
    const std::string name = bindTemporary(subject);
    node_handle_t tagNode  = applyTagOperator(
        onNull ? camel::core::oper::TagIsNull : camel::core::oper::TagIsError,
        name,
        gct);
    const camel::core::OperId valueOp =
        onNull ? camel::core::oper::TagNullValue : camel::core::oper::TagErrorValue;
    const GCT::node_ptr_t exec = gct->at(1)->at(0);
    std::vector<arm_ptr_t> arms(2);

    // Arm 0 is the right-hand side, a subgraph of its own: it is entered, and so
    // evaluated, only when the tag selects it. Arm 1 takes the value out. The right
    // side's position is the payload type: a plain `T` is what it is, and a `T?` is what
    // the chained form hands on (a value of another form is left as it is, and the test
    // below names it).
    {
        ExpectedValueScope fallbackScope(expectedValueType_, payload);
        arms[0] = buildArm(exec);
    }
    // Design principles 3.8: the right-hand side is a `T`, and the result is a `T`; or
    // it is itself a `T?` (`T!` for `!!`), and the result is that: `a ?? b ?? 9` tries
    // `a`, then `b`, then gives the default. Arm 1 then hands on `a` as it is instead of
    // taking the value out. Either way it is one branch.
    Type *fallback   = arms[0]->exitType();
    const bool plain = fallback != nullptr &&
                       (payload->unify(fallback) != nullptr || entersImplicitly(payload, fallback));
    const bool chained = !plain && fallback != nullptr && entersImplicitly(subjectType, fallback);
    if (plain) {
        if (payload->unify(fallback) == nullptr) {
            coerceArmExit(arms[0], payload, gct);
        }
        arms[1] = buildArm(exec, [&] { return applyTagOperator(valueOp, name, gct); });
    } else if (chained) {
        coerceArmExit(arms[0], subjectType, gct);
        arms[1] = buildArm(exec, [&] {
            const GCT::node_ptr_t dref = tree_->makeDRef(name);
            dref->setOrigin(gct->origin());
            return valueNodeOf(visitDRefNode(dref));
        });
    } else {
        diags_->of(SemanticDiag::TaggedOperandMismatch)
            .atOrigin(gct->origin())
            .commit(
                symbol,
                "its right side has type '" +
                    (fallback ? fallback->toString() : std::string("void")) +
                    "', but the left side holds a '" + payload->toString() + "'");
        throw BuildAbortException();
    }
    return buildBranch(gct, tagNode, &arms);
}

// `x -> f` where `x` is a `T?` or a `T!` and no overload of `f` takes it as it is (design
// principles 3.9): a branch on the tag of `x`. The arm taken when `x` is null (has failed) hands it
// on as the result; the other arm calls `f` with the value taken out, and gives a `U?` (`U!`; a
// `U` that is already of that form is kept). The value taken out may itself be tagged (`T?!`), in
// which case the call there takes it apart again.
node_handle_t Builder::peelPipeReceiver(const GCT::node_ptr_t &call, node_handle_t receiver) {
    Type *subjectType = valueTypeOf(nodeTypeOf(receiver));
    TupleType *tagged = asTagged(subjectType);
    GUARD(tagged != nullptr, "A pipe takes a receiver apart only when it is tagged.");
    const bool onNull      = tagged->isNullable();
    const std::string name = bindTemporary(receiver);
    node_handle_t tagNode  = applyTagOperator(
        onNull ? camel::core::oper::TagIsNull : camel::core::oper::TagIsError,
        name,
        call);
    const camel::core::OperId valueOp =
        onNull ? camel::core::oper::TagNullValue : camel::core::oper::TagErrorValue;
    GCT::node_ptr_t passed = tree_->makeDRef(GCT::kPassedValueName);
    passed->setOrigin(call->at(1)->origin());

    std::vector<arm_ptr_t> arms(2);
    Type *resultType = nullptr;
    arms[1]          = buildArm(call, [&] {
        insertNode(GCT::kPassedValueName, applyTagOperator(valueOp, name, call));
        ExpectedValueScope noPosition(expectedValueType_, nullptr);
        node_handle_t result = visitPipeLink(call, passed);
        if (nodeIsKind(result, runtime::GCNodeKind::Gate) && !normInputsOf(result).empty()) {
            result = nodeDraftOf(result)->ref(normInputsOf(result).front());
        }
        Type *type = valueTypeOf(nodeTypeOf(result));
        if (onNull) {
            resultType = type->code() == TypeCode::Void || asNullable(type) != nullptr
                             ? type
                             : TupleType::nullable(type);
        } else {
            resultType = asFallible(type) != nullptr ? type : TupleType::fallible(type);
        }
        return resultType == type ? result : castNodeTo(result, resultType, call);
    });
    arms[0]          = buildArm(call, [&] {
        if (onNull) {
            node_handle_t none = currGraph_->addStaticDataNode(Data::null());
            registerNodeOrigin(context_, none, call, "gir.data");
            return resultType->code() == TypeCode::Void ? none : castNodeTo(none, resultType, call);
        }
        const GCT::node_ptr_t dref = tree_->makeDRef(name);
        dref->setOrigin(call->origin());
        node_handle_t failed = valueNodeOf(visitDRefNode(dref));
        return resultType->equals(subjectType) ? failed : castNodeTo(failed, resultType, call);
    });
    return buildBranch(call, tagNode, &arms);
}

node_handle_t
Builder::visitTaggedMatch(const GCT::node_ptr_t &gct, node_handle_t scrutinee, TupleType *type) {
    using CaseType       = GCT::CaseType;
    const auto malformed = [&](const std::string &why) {
        diags_->of(SemanticDiag::TaggedMatchCases)
            .atOrigin(gct->origin())
            .commit(type->toString(), why);
        throw BuildAbortException();
    };
    // DESIGN-QUESTION(nullable-error) Q4: exactly the tagged case and one other.
    // A case with a guard is taken only where its guard holds, so it is not the case that
    // opens the value: each side needs one case without a guard, and the cases written before
    // it may carry guards.
    std::vector<GCT::node_ptr_t> taggedSide, restSide;
    for (size_t i = 1; i < gct->size(); ++i) {
        const auto &caseNode = gct->at(i);
        const auto caseType  = caseNode->caseType();
        const bool isNull    = caseType == CaseType::Value &&
                               caseNode->at(0)->type() == GCT::NodeKind::DATA &&
                               caseNode->at(0)->data()->type()->code() == TypeCode::Void;
        if (isNull || caseType == CaseType::Error) {
            if (isNull != type->isNullable()) {
                malformed(
                    isNull ? "'case null' needs a value that may be null"
                           : "'case error' needs a value that may have failed");
            }
            taggedSide.push_back(caseNode);
        } else if (caseType == CaseType::Bind || caseType == CaseType::Else) {
            restSide.push_back(caseNode);
        } else {
            malformed("a literal case cannot be compared with a value that is not opened yet");
        }
    }
    const bool anyGuard = hasGuardedCase(gct, 1);
    if (!anyGuard) {
        // Without a guard the cases of a side are one and the same case: what the old form
        // says, and what its reports name.
        if (taggedSide.size() > 1) {
            malformed(
                std::string("it has more than one '") +
                (type->isNullable() ? "case null" : "case error") + "'");
        }
        if (restSide.size() > 1) {
            malformed("it has more than one case for the value");
        }
    }
    const auto openingCase = [&](const std::vector<GCT::node_ptr_t> &side) -> GCT::node_ptr_t {
        // The case that opens the value of its side: the one without a guard. Every case
        // written after it for the same side can never be taken.
        GCT::node_ptr_t opening = nullptr;
        for (const GCT::node_ptr_t &caseNode : side) {
            const bool guarded = tree_->caseGuardOf(caseNode) != nullptr;
            if (opening != nullptr && !guarded) {
                malformed("a case of one side follows the case that takes that side");
            }
            if (!guarded && opening == nullptr) {
                opening = caseNode;
            }
        }
        return opening;
    };
    const GCT::node_ptr_t taggedCase = openingCase(taggedSide);
    const GCT::node_ptr_t restCase   = openingCase(restSide);
    if (taggedCase == nullptr) {
        malformed(type->isNullable() ? "'case null' is missing" : "'case error e' is missing");
    }
    if (restCase == nullptr) {
        malformed("the case for the value is missing ('case v' or 'case _')");
    }

    const std::string name = bindTemporary(scrutinee);
    const auto tagOf       = [&](const GCT::node_ptr_t &origin) {
        return applyTagOperator(
            type->isNullable() ? camel::core::oper::TagIsNull : camel::core::oper::TagIsError,
            name,
            origin);
    };
    if (anyGuard) {
        // The cases in written order, as one decision list: each tests the side it takes, and
        // the cases written after it are the `else` of its step, so a guard that does not hold
        // leaves the value to them.
        std::vector<CaseStep> steps;
        for (size_t i = 1; i < gct->size(); ++i) {
            const GCT::node_ptr_t &caseNode = gct->at(i);
            const auto caseType             = caseNode->caseType();
            const bool isNull   = caseType == CaseType::Value &&
                                  caseNode->at(0)->type() == GCT::NodeKind::DATA &&
                                  caseNode->at(0)->data()->type()->code() == TypeCode::Void;
            const bool onTagged = isNull || caseType == CaseType::Error;
            CaseStep caseStep;
            caseStep.guard = tree_->caseGuardOf(caseNode);
            caseStep.exec  = caseNode->at(caseNode->size() - 1);
            if (caseStep.guard != nullptr || i + 1 < gct->size()) {
                // The last case is the one the list ends at, and it needs no test: nothing
                // written after it can meet the value. Every other case says which side it
                // takes, so that the value a guard of a case before it refused reaches it.
                node_handle_t tag  = tagOf(caseNode);
                caseStep.condition = onTagged ? tag : negateCondition(tag, caseNode);
            }
            caseStep.bind = [&, caseNode, onTagged, isNull, caseType] {
                if (onTagged) {
                    if (type->isFallible()) {
                        insertNode(
                            caseNode->binding(),
                            applyTagOperator(camel::core::oper::TagErrorInfo, name, caseNode));
                    }
                } else if (caseType == CaseType::Bind) {
                    insertNode(
                        caseNode->binding(),
                        applyTagOperator(
                            type->isNullable() ? camel::core::oper::TagNullValue
                                               : camel::core::oper::TagErrorValue,
                            name,
                            caseNode));
                }
            };
            steps.push_back(std::move(caseStep));
        }
        return buildCaseChain(steps, gct);
    }

    node_handle_t tagNode            = tagOf(gct);
    const GCT::node_ptr_t taggedExec = taggedCase->at(taggedCase->size() - 1);
    const GCT::node_ptr_t restExec   = restCase->at(0);
    std::vector<arm_ptr_t> arms(2);
    arms[0] = buildArm(taggedExec, [&] {
        if (type->isFallible()) {
            insertNode(
                taggedCase->binding(),
                applyTagOperator(camel::core::oper::TagErrorInfo, name, taggedCase));
        }
        return visitBodyNode(taggedExec);
    });
    arms[1] = buildArm(restExec, [&] {
        if (restCase->caseType() == CaseType::Bind) {
            insertNode(
                restCase->binding(),
                applyTagOperator(
                    type->isNullable() ? camel::core::oper::TagNullValue
                                       : camel::core::oper::TagErrorValue,
                    name,
                    restCase));
        }
        return visitBodyNode(restExec);
    });
    return buildBranch(gct, tagNode, &arms);
}

namespace {

bool isNullLiteral(const GCT::node_ptr_t &expr) {
    return expr->type() == GCT::NodeKind::DATA && expr->data()->type()->code() == TypeCode::Void;
}

bool isLogicalBranch(const GCT::node_ptr_t &expr, GCT::BranchKind kind) {
    return expr->type() == GCT::NodeKind::BRCH && expr->branchKind() == kind;
}

/// The right side of the `&&` / `||` branch `brch` (see GCT::BranchKind for its layout).
GCT::node_ptr_t rightSideOf(const GCT::node_ptr_t &brch) {
    const bool isAnd = brch->branchKind() == GCT::BranchKind::LogicalAnd;
    return brch->at(isAnd ? 1 : 2)->at(0)->at(0);
}

/// One comparison of a name with `null` inside a condition.
struct NullTest {
    std::string name;
    GCT::node_ptr_t expr;    // the comparison
    GCT::node_ptr_t subject; // the name in it
    /// The name is not null where the condition the test is part of holds (`x != null`), as
    /// opposed to where it does not (`x == null`).
    bool nonNullWhenTrue;
    /// Not under a `!` (`!(x == null)` is not one of the forms narrowing follows).
    bool direct;
};

/// `expr` as the comparison of a name with `null`, in either order.
std::optional<NullTest> nullTestOf(const GCT::node_ptr_t &expr) {
    if (expr->type() != GCT::NodeKind::LINK || expr->size() != 3 ||
        expr->at(0)->type() != GCT::NodeKind::DREF) {
        return std::nullopt;
    }
    const std::string op = expr->at(0)->name();
    if (op != "__eq__" && op != "__neq__") {
        return std::nullopt;
    }
    const bool nullFirst     = isNullLiteral(expr->at(1));
    const auto &subject      = expr->at(nullFirst ? 2 : 1);
    const auto &comparedWith = expr->at(nullFirst ? 1 : 2);
    if (!isNullLiteral(comparedWith) || subject->type() != GCT::NodeKind::DREF) {
        return std::nullopt;
    }
    return NullTest{subject->name(), expr, subject, op == "__neq__", true};
}

/// Every comparison of a name with `null` the condition `expr` is made of, looking through
/// `&&`, `||` and `!`. This is wider than what narrowing follows (see conjunctsOf): the
/// tests found here and not followed are the ones a diagnostic explains.
void collectNullTests(const GCT::node_ptr_t &expr, bool negated, std::vector<NullTest> &tests) {
    if (auto test = nullTestOf(expr)) {
        test->nonNullWhenTrue = test->nonNullWhenTrue != negated;
        test->direct          = test->direct && !negated;
        tests.push_back(std::move(*test));
    } else if (
        expr->type() == GCT::NodeKind::LINK && expr->size() == 2 &&
        expr->at(0)->type() == GCT::NodeKind::DREF && expr->at(0)->name() == "__not__") {
        collectNullTests(expr->at(1), !negated, tests);
    } else if (
        isLogicalBranch(expr, GCT::BranchKind::LogicalAnd) ||
        isLogicalBranch(expr, GCT::BranchKind::LogicalOr)) {
        collectNullTests(expr->at(0), negated, tests);
        collectNullTests(rightSideOf(expr), negated, tests);
    }
}

/// The operands that `&&` joins in `expr` (`expr` itself when it is not an `&&`). Each of
/// them holds where `expr` does: a test against null among them narrows the then arm
/// (design principles 3.8).
void conjunctsOf(const GCT::node_ptr_t &expr, std::vector<const GCT::Node *> &conjuncts) {
    if (isLogicalBranch(expr, GCT::BranchKind::LogicalAnd)) {
        conjunctsOf(expr->at(0), conjuncts);
        conjunctsOf(rightSideOf(expr), conjuncts);
    } else {
        conjuncts.push_back(expr);
    }
}

/// Whether the name is written as a DREF anywhere in `node`. A use of another declaration of
/// the same name counts too, and a name kept in a constant (an element of an array literal)
/// is not seen: the answer only decides whether a diagnostic gets a note.
bool mentionsName(const GCT::node_ptr_t &node, const std::string &name) {
    if (node == nullptr) {
        return false;
    }
    if (node->type() == GCT::NodeKind::DREF && node->name() == name) {
        return true;
    }
    for (size_t i = 0; i < node->size(); ++i) {
        if (mentionsName(node->at(i), name)) {
            return true;
        }
    }
    return false;
}

} // namespace

node_handle_t Builder::nullComparison(
    const std::string &group, const GCT::node_ptr_t &gct, const std::vector<Argument> &operands,
    const type_vec_t &types) {
    if ((group != "__eq__" && group != "__neq__") || operands.size() != 2) {
        return nullptr;
    }
    const bool nullFirst = isNullLiteral(operands[0].expr);
    if (nullFirst == isNullLiteral(operands[1].expr)) {
        return nullptr; // no `null` written, or two
    }
    // Only a `T?` has the tag asked about. Two `T?` values are not compared (no `null` is
    // written then).
    const size_t subject = nullFirst ? 1 : 0;
    Type *subjectType    = valueTypeOf(types[subject]);
    if (asNullable(subjectType) == nullptr) {
        // A value whose type is never null (`5`, an `int`, a name already narrowed to a `T`):
        // the test is allowed and its result is fixed, which is worth a warning (design
        // principles 3.8). The value itself is not needed, so nothing evaluates it. `null`
        // itself (type `void`) is left to the comparison of two `null`s, which is refused.
        if (subjectType == nullptr || subjectType->code() == TypeCode::Void) {
            return nullptr;
        }
        const bool fixed      = group == "__neq__";
        const auto &written   = operands[subject].expr;
        const std::string who = written->type() == GCT::NodeKind::DREF
                                    ? std::format("'{}'", written->name())
                                    : std::string("This value");
        diags_->of(SemanticDiag::NullTestNeverNull)
            .atOrigin(gct->origin())
            .commit(who, subjectType->toString(), fixed ? "true" : "false");
        const GCT::node_ptr_t literal =
            tree_->makeData(tt::as_shared<Data>(std::make_shared<BoolData>(fixed)));
        literal->setOrigin(gct->origin());
        return visitDataNode(literal);
    }
    node_handle_t isNull = applyTagOperator(
        camel::core::oper::TagIsNull,
        bindTemporary(valueOf(operands[subject].node)),
        gct);
    return group == "__eq__" ? isNull : applyTagOperator("__not__", bindTemporary(isNull), gct);
}

bool Builder::isNarrowedName(const GCT::node_ptr_t &expr) {
    if (expr->type() != GCT::NodeKind::DREF) {
        return false;
    }
    // The scope maps the name to the value the narrowing `if` bound in its arm, also from an
    // arm or a function nested in that arm (which reaches it through a capture).
    const auto bound = nodeScope_->get(expr->name());
    return bound.has_value() && narrowedValues_.contains(*bound);
}

node_handle_t Builder::reopenKnownNotNull(
    const GCT::node_ptr_t &expr, node_handle_t subject, const GCT::node_ptr_t &at,
    const char *construct) {
    // Design principles 3.8: the construct is still allowed on a left side that cannot be
    // null; it is applied to the value put back into `T?`, so that it is checked and built
    // exactly as on a `T?` (the right side of `??` still has to fit, the cases of the `match`
    // still have to be its fixed patterns), and only its null arm can never be taken.
    Type *type = valueTypeOf(nodeTypeOf(subject));
    // A name is named as it is written; anything else (`a + 1`, `f()`) is named by the text
    // that produced it, so the reader is told which value cannot be null.
    std::string who = expr->type() == GCT::NodeKind::DREF ? expr->name() : sourceTextOf(expr);
    if (who.empty()) {
        who = "This value";
    }
    diags_->of(SemanticDiag::KnownNotNull)
        .atOrigin(at->origin())
        .commit(who, type->toString(), construct);
    node_handle_t reopened = coerceTo(subject, TupleType::nullable(type), at);
    GUARD(reopened != nullptr, "A value always enters the nullable form of its own type.");
    return reopened;
}

node_handle_t Builder::visitConditionBrchNode(const GCT::node_ptr_t &gct) {
    using Kind         = GCT::BranchKind;
    using CaseType     = GCT::CaseType;
    const Kind kind    = gct->branchKind();
    const bool logical = kind == Kind::LogicalAnd || kind == Kind::LogicalOr;
    if (gct->size() != 3 || gct->at(1)->caseType() != CaseType::True ||
        gct->at(2)->caseType() != CaseType::Else) {
        GUARD(!logical, "'&&' and '||' are lowered with a true arm and an else arm.");
        return nullptr; // a `match`
    }
    const GCT::node_ptr_t &cond = gct->at(0);
    std::vector<NullTest> tests;
    collectNullTests(cond, false, tests);
    if (!logical && tests.empty()) {
        return nullptr; // an `if` that tests no name against null
    }

    // `&&` and `||` are branches (design principles 4.2), but their operands are not in the
    // position the whole expression stands at: the condition and both sides are `bool`
    // whatever that position wants. The position's type applies to the branch's value once,
    // where the branch stands (`checkValue`); leaking it down here made a `bool?` operand out
    // of the literal `false` in `let o: bool? = true || false`.
    Type *const operandType = logical ? Type::Bool() : expectedValueType_;
    ExpectedValueScope operandScope(expectedValueType_, operandType);

    // Narrowing (design principles 3.8). In the arm of `if x != null then A else B`
    // that excludes null, `x` is a `T`: the arm starts by taking the value out of `x`, and
    // the name `x` stands for that value inside the arm, as `v` does in
    // `match x { case null => B  case v => A }`. The value is taken out inside the arm
    // because doing so has a meaning only where the arm is selected.
    //
    // It applies to a name bound by `let` or a parameter (a `var` may be rewritten between
    // the test and the use), and to the direct forms only: `x != null` or `null != x` that
    // is the whole condition or one of the operands `&&` joins narrows the then arm
    // (`if x != null && ready then ...`); `x == null` or `null == x` that is the whole
    // condition narrows the else arm. Nothing under `!` or `||` narrows the arms of the `if`,
    // and the else arm of a conjunction learns nothing. A test that is found but not
    // followed is kept as the explanation of an error in the arm it would have narrowed.
    struct Opened {
        std::string name;
        GCT::node_ptr_t subject; // the name as the condition wrote it
        std::string temporary;   // what the arm captures (bindTemporary)
    };
    struct Refused {
        std::string name;
        std::string type;
        DiagLabel label;
    };
    struct Arm {
        GCT::node_ptr_t exec;
        std::vector<Opened> opened;
        std::vector<Refused> refused;
    };
    Arm thenArm{gct->at(1)->at(0), {}, {}}, elseArm{gct->at(2)->at(0), {}, {}};
    const std::optional<NullTest> whole = nullTestOf(cond);
    std::vector<const GCT::Node *> conjuncts;
    conjunctsOf(cond, conjuncts);
    for (const NullTest &test : tests) {
        const auto bound    = nodeAt(test.name);
        TupleType *nullable = bound.has_value() && *bound != nullptr
                                  ? asNullable(valueTypeOf(nodeTypeOf(*bound)))
                                  : nullptr;
        if (nullable == nullptr) {
            continue; // not a `T?`: nothing to narrow (and the comparison itself is an error)
        }
        Arm &arm            = test.nonNullWhenTrue ? thenArm : elseArm;
        const bool conjunct = std::ranges::find(conjuncts, test.expr) != conjuncts.end();
        const bool follows =
            test.direct && conjunct && (test.nonNullWhenTrue || conjuncts.size() == 1);
        const bool mutableName = isVariable(*bound);
        // A name a closure captured is not a variable any more once the capture is by value
        // (design principles 3.5, 197): the port is the value the name had where the closure was
        // made, and every write of the name is refused from then on (197, 198), so nothing can
        // rewrite it between the test and the use. Narrowing it is therefore sound -- BENIGN on
        // purpose: the arm is kept, and the message below is the one a `var` that really can be
        // rewritten still gets. `ok_narrowing_a_captured_nullable_var` pins it.
        if (follows && !mutableName) {
            if (std::ranges::find(arm.opened, test.name, &Opened::name) == arm.opened.end()) {
                arm.opened.push_back({test.name, test.subject, {}});
            }
            continue;
        }
        const char *why =
            mutableName ? "a 'var' may be rewritten between the test and the use (bind its value "
                          "with 'let' and test that)"
            : test.direct && conjunct
                ? "where the operands of '&&' do not all hold, nothing is known about "
                  "any one of them"
                : "narrowing follows a test that is the whole condition or an operand "
                  "of '&&', not one under '||' or '!'";
        arm.refused.push_back(
            {test.name,
             nullable->toString(),
             DiagLabel{
                 SourceRange{test.subject->origin()},
                 std::format(
                     "'{}' still has the type '{}' in this arm; the test of it against null "
                     "does not narrow it: {}",
                     test.name,
                     nullable->toString(),
                     why),
                 ""}});
    }

    // The condition. One followed test that is the whole condition selects the arm by the
    // tag itself, which is the branch of the `match` above: arm 0 is the null arm. Any other
    // condition is a bool as in every `if`: arm 0 is the then arm.
    const bool byTag = whole.has_value() && thenArm.opened.size() + elseArm.opened.size() == 1;
    for (Arm *arm : {&thenArm, &elseArm}) {
        for (Opened &opened : arm->opened) {
            opened.temporary = bindTemporary(valueNodeOf(visit(opened.subject)));
        }
    }
    node_handle_t condNode =
        byTag ? applyTagOperator(
                    camel::core::oper::TagIsNull,
                    (thenArm.opened.empty() ? elseArm : thenArm).opened.front().temporary,
                    cond)
              : valueNodeOf(visit(cond));

    const char *symbol = kind == Kind::LogicalAnd ? "&&" : "||";
    // Reported at the operand that is not a bool.
    const auto requireBool = [&](Type *type, const char *side, const GCT::node_ptr_t &operand) {
        type = valueTypeOf(type);
        if (type == nullptr || type->code() != TypeCode::Bool) {
            const auto origin = operand->origin();
            diags_->of(SemanticDiag::LogicalOperandNotBool)
                .atOrigin(origin != 0 ? origin : gct->origin())
                .commit(symbol, side, type != nullptr ? type->toString() : std::string("void"));
            throw BuildAbortException();
        }
    };
    if (logical) {
        requireBool(nodeTypeOf(condNode), "left", cond);
    }

    const auto armOf = [&](const Arm &arm) {
        const size_t errorsBefore = diags_->count(Severity::Error);
        const size_t diagsBefore  = diags_->count();
        try {
            if (arm.opened.empty()) {
                return buildArm(arm.exec);
            }
            return buildArm(arm.exec, [&] {
                for (const Opened &opened : arm.opened) {
                    // Opened whether or not the arm seems to use the name: a name can be
                    // written where no DREF shows it (the elements of an array literal are
                    // names kept in the constant), and an unused node costs nothing at run
                    // time, as nothing depends on it.
                    node_handle_t value = applyTagOperator(
                        camel::core::oper::TagNullValue,
                        opened.temporary,
                        opened.subject);
                    [[maybe_unused]] const bool bound = insertNode(opened.name, value);
                    GUARD(bound, "A narrowed name is the first name of its arm.");
                    narrowedValues_.insert(value);
                }
                return visitBodyNode(arm.exec);
            });
        } catch (const BuildAbortException &) {
            // An error in an arm that uses a name the condition tested without narrowing it,
            // and that speaks of the name's type: most likely the name was used as a `T`.
            const auto errors = diags_->errors();
            const auto first =
                errors.begin() + static_cast<std::ptrdiff_t>(std::min(errorsBefore, errors.size()));
            for (const Refused &refused : arm.refused) {
                if (mentionsName(arm.exec, refused.name) &&
                    std::any_of(first, errors.end(), [&](const Diagnostic *d) {
                        return d->message.find(refused.type) != std::string::npos;
                    })) {
                    diags_->addRelatedToErrorsFrom(diagsBefore, refused.label, 3);
                }
            }
            throw;
        }
    };
    // Arm 0 is the one the condition selects first: the null arm by tag, the then arm else.
    const bool thenFirst = !byTag || thenArm.opened.empty();
    std::vector<arm_ptr_t> arms(2);
    arms[thenFirst ? 0 : 1] = armOf(thenArm);
    arms[thenFirst ? 1 : 0] = armOf(elseArm);
    if (logical) {
        // The right side is the then arm of `&&` and the else arm of `||`.
        const bool rightIsThen = kind == Kind::LogicalAnd;
        requireBool(arms[rightIsThen == thenFirst ? 0 : 1]->exitType(), "right", rightSideOf(gct));
    }
    // An `if` without `else` has no value (design principles 3.7; see buildBranch).
    const bool valueless = !logical && elseArm.exec->size() == 0;
    return buildBranch(gct, condNode, &arms, valueless);
}

node_handle_t Builder::visitBrchNode(const GCT::node_ptr_t &gct) {
    ENTER("BRCH");
    const auto kind = gct->branchKind();
    if (kind == GCT::BranchKind::NullThen || kind == GCT::BranchKind::ErrorThen) {
        node_handle_t node = visitTaggedBrchNode(gct);
        LEAVE("BRCH");
        return node;
    }
    // `&&`, `||`, and an `if` whose condition compares names with `null` (design principles
    // 3.8). Any other `if`, and every `match`, is built below.
    if (node_handle_t node = visitConditionBrchNode(gct)) {
        LEAVE("BRCH");
        return node;
    }
    // The condition of an `if` is a `bool` (design principles 3.7), and the value a `match`
    // selects on is of its own type: neither is in the position the branch stands at, so the
    // position's type must not leak into what is built there. The position applies to
    // the branch's value once, where the branch stands.
    const bool ifBranch = gct->size() > 1 && gct->at(1)->caseType() == GCT::CaseType::True;
    node_handle_t condNode{};
    if (ifBranch) {
        condNode = visitInPosition(gct->at(0), Type::Bool());
    } else {
        ExpectedValueScope scrutineeScope(expectedValueType_, nullptr);
        condNode = valueNodeOf(visit(gct->at(0)));
    }
    node_handle_t node{};
    // A `match` on a value that cannot be null is left to the plain match below: only the
    // narrowed name is put back into `T?` here, so that `case null` over it is still checked
    // as it is over a `T?` (`??` treats every value that cannot be null the same way, see
    // visitTaggedBrchNode).
    if (asTagged(valueTypeOf(nodeTypeOf(condNode))) == nullptr && isNarrowedName(gct->at(0))) {
        // `match x { case null => ...  case v => ... }` where an `if` around it already
        // narrowed `x` to a `T` (design principles 3.8): allowed, with a warning. A match
        // without `case null` is an ordinary match on the `T`.
        const bool nullCase = std::any_of(gct->begin() + 1, gct->end(), [](const auto &c) {
            return c->caseType() == GCT::CaseType::Value && isNullLiteral(c->at(0));
        });
        if (nullCase) {
            condNode = reopenKnownNotNull(gct->at(0), condNode, gct, "match");
        }
    }
    if (TupleType *tagged = asTagged(valueTypeOf(nodeTypeOf(condNode)))) {
        node = visitTaggedMatch(gct, condNode, tagged);
    } else if (EnumType *enumType = asEnumType(valueTypeOf(nodeTypeOf(condNode)))) {
        node = visitEnumMatch(gct, condNode, enumType);
    } else {
        for (size_t i = 1; i < gct->size(); ++i) {
            const GCT::node_ptr_t &boundCase = gct->at(i);
            if (boundCase->caseType() == GCT::CaseType::Bind ||
                boundCase->caseType() == GCT::CaseType::Error) {
                // The two patterns that bind are the fixed forms of `T?` and `T!` (design
                // principles 3.8): over any other type there is no mark to take apart.
                const bool isError = boundCase->caseType() == GCT::CaseType::Error;
                diags_->of(SemanticDiag::IdentifierPatternNeedsNullableOrErrorType)
                    .atOrigin(boundCase->origin())
                    .commit(
                        (isError ? "error " : "") + boundCase->binding(),
                        nodeTypeOf(condNode)->toString());
                throw BuildAbortException();
            }
        }
        requireMatchDefault(gct, condNode);
        node = buildBranch(gct, condNode);
    }
    LEAVE("BRCH");
    return node;
}

void Builder::requireMatchDefault(const GCT::node_ptr_t &gct, node_handle_t scrutinee) {
    using CaseType = GCT::CaseType;
    bool hasTrue = false, hasFalse = false, anyCase = false;
    for (size_t i = 1; i < gct->size(); ++i) {
        const auto &caseNode = gct->at(i);
        const auto caseType  = caseNode->caseType();
        if (caseType == CaseType::Else) {
            if (!caseNode->guardedCase()) {
                return; // an unguarded `case _` is the default of the match
            }
            // `case _ when c`: the branch is entered only when `c` holds, so it is not a
            // default. The cases written after it are still reached (the guarded wildcard does
            // not end the match), and the match still needs one.
            continue;
        }
        if (caseType != CaseType::Value) {
            continue;
        }
        anyCase = true;
        if (caseNode->guardedCase()) {
            // A guarded case covers nothing on its own (and Rust the same): the value a false guard
            // refuses is matched by the cases written after it, so it counts for neither `case _`
            // nor a bool's values.
            continue;
        }
        if (caseNode->at(0)->type() == GCT::NodeKind::DATA) {
            const auto literal = std::dynamic_pointer_cast<BoolData>(caseNode->at(0)->data());
            if (literal) {
                (literal->data() ? hasTrue : hasFalse) = true;
            }
        }
    }
    if (!anyCase) {
        return; // an `if`
    }
    Type *type = valueTypeOf(nodeTypeOf(scrutinee));
    if (type == Type::Bool() && hasTrue && hasFalse) {
        return; // every value of the type has a case
    }
    // Design principles 3.7: a `match` must have a default case, and the compiler adds no
    // arm the program does not show. A `match` without `case _` is therefore refused here
    // (the fixed patterns over `T?` / `T!` never reach this function).
    diags_->of(SemanticDiag::MatchWithoutDefault).atOrigin(gct->origin()).commit(type->toString());
    throw BuildAbortException();
}

bool Builder::hasGuardedCase(const GCT::node_ptr_t &gct, size_t first) {
    for (size_t i = first; i < gct->size(); ++i) {
        if (tree_->caseGuardOf(gct->at(i)) != nullptr) {
            return true;
        }
    }
    return false;
}

node_handle_t Builder::negateCondition(node_handle_t value, const GCT::node_ptr_t &origin) {
    // `!b` is the branch `if b then false else true`: `not` over a bool, with no new node kind.
    const auto constant = [&](bool each) {
        node_handle_t node = currGraph_->addStaticDataNode(
            tt::as_shared<Data>(std::make_shared<PrimaryData<bool>>(each)));
        registerNodeOrigin(context_, node, origin, "gir.data");
        return node;
    };
    std::vector<arm_ptr_t> two(2);
    two[0] = buildArm(origin, [&] { return constant(false); });
    two[1] = buildArm(origin, [&] { return constant(true); });
    return buildBranch(origin, value, &two);
}

node_handle_t Builder::buildCaseChain(std::vector<CaseStep> &steps, const GCT::node_ptr_t &origin) {
    node_handle_t always = currGraph_->addStaticDataNode(
        tt::as_shared<Data>(std::make_shared<PrimaryData<bool>>(true)));
    registerNodeOrigin(context_, always, origin, "gir.data");
    const auto boolConstant = [&](bool value) {
        node_handle_t node = currGraph_->addStaticDataNode(
            tt::as_shared<Data>(std::make_shared<PrimaryData<bool>>(value)));
        registerNodeOrigin(context_, node, origin, "gir.data");
        return node;
    };
    // The condition of a case: the test of its pattern and, when it has a guard, that guard as
    // well. The guard is read in the arm the pattern's test enters, so it sees the names the
    // pattern binds and is not evaluated at all for a value the pattern does not match
    // (design principles 4.2: `a && b` is `if a then b else false`).
    const auto conditionOf = [&](CaseStep &step) {
        const auto guardValue = [&] {
            node_handle_t guard = valueNodeOf(visitInPosition(step.guard, Type::Bool()));
            Type *guardType     = valueTypeOf(nodeTypeOf(guard));
            if (guardType == nullptr || guardType->code() != TypeCode::Bool) {
                diags_->of(SemanticDiag::ConditionNotBool)
                    .atOrigin(step.guard->origin())
                    .commit(guardType == nullptr ? "?" : guardType->toString());
                throw BuildAbortException();
            }
            return guard;
        };
        if (step.condition == nullptr) {
            // `case _ when c`: the case tests nothing itself, so the guard is its whole
            // condition. The names the pattern binds are taken out all the same.
            std::vector<arm_ptr_t> one(1);
            one[0] = buildArm(step.exec, [&] {
                step.bind();
                return guardValue();
            });
            return buildBranch(origin, always, &one);
        }
        std::vector<arm_ptr_t> held(2);
        held[0] = buildArm(step.exec, [&] {
            step.bind();
            return guardValue();
        });
        held[1] = buildArm(step.exec, [&] { return boolConstant(false); });
        return buildBranch(origin, step.condition, &held);
    };

    // `step(i)` is the value of the list from case i on. The cases after a case are the `else`
    // of its own step, and are built there: a guard that does not hold hands the value to them,
    // and they are reached no other way.
    std::function<node_handle_t(size_t)> step = [&](size_t i) -> node_handle_t {
        CaseStep &caseStep      = steps[i];
        node_handle_t condition = caseStep.condition;
        if (caseStep.guard != nullptr) {
            condition = conditionOf(caseStep);
        }
        if (condition == nullptr) {
            // The case that ends the list: an unguarded `case _` (the checks of the form
            // refused a `match` that can reach a value none of its cases takes). Its arm is the
            // only one, and the runtime takes it: the branch has no case value.
            std::vector<arm_ptr_t> one(1);
            one[0] = buildArm(caseStep.exec, [&] {
                caseStep.bind();
                return visitBodyNode(caseStep.exec);
            });
            return buildBranch(origin, always, &one);
        }
        std::vector<arm_ptr_t> two(2);
        two[0] = buildArm(caseStep.exec, [&] {
            caseStep.bind();
            return visitBodyNode(caseStep.exec);
        });
        two[1] = buildArm(caseStep.exec, [&] {
            if (i + 1 < steps.size()) {
                return step(i + 1);
            }
            // Nothing is written after this case: no value reaches here (the checks of the form
            // refused a `match` that can fall off its end), but the arm the test does not take
            // is still needed -- the branch offers the join one value of the arms' type. It is
            // the body of this case's own arm, as the arm an exhaustive `match` over an enum
            // adds for the value it cannot meet.
            caseStep.bind();
            return visitBodyNode(caseStep.exec);
        });
        return buildBranch(origin, condition, &two);
    };
    return step(0);
}

node_handle_t Builder::buildGuardedMatch(const GCT::node_ptr_t &gct, node_handle_t condNode) {
    // The value is read once, into a name every case is compared with: a case value is a
    // literal, and the comparison is the one `==` of that type makes, so `case 3` against an
    // `f32` compares it with 3.0 (design principles 5.1) exactly as the branch that dispatches
    // by case value does, and a case value of another type is the same report that branch gives.
    const std::string name = bindTemporary(condNode);
    Type *subjectType      = valueTypeOf(nodeTypeOf(condNode));
    std::vector<CaseStep> steps;
    for (size_t i = 1; i < gct->size(); ++i) {
        const GCT::node_ptr_t &caseNode = gct->at(i);
        CaseStep caseStep;
        caseStep.guard = tree_->caseGuardOf(caseNode);
        caseStep.bind  = [] {};
        switch (caseNode->caseType()) {
        case GCT::CaseType::Value: {
            // The comparison gets a node of its own: the CASE keeps the one it was built with,
            // and a node of this tree has one parent. The case value is a pattern constant -- a
            // literal, or a name that folds to one -- so the copy is the same value, and nothing
            // is evaluated twice by taking it.
            node_handle_t valueNode = valueNodeOf(visit(caseNode->at(0)));
            Type *caseType          = valueTypeOf(nodeTypeOf(valueNode));
            GCT::node_ptr_t operand = tree_->clone(caseNode->at(0));
            bool converted          = false;
            if (const std::optional<literal::Value> pattern = literalValueOfExpr(caseNode->at(0));
                pattern.has_value() && subjectType != nullptr && subjectType->isPrimitive() &&
                !subjectType->assignableFrom(caseType) &&
                literal::kindEnters(*pattern, subjectType)) {
                if (const auto data = pattern->toData(subjectType); data.has_value()) {
                    operand   = tree_->makeData(*data);
                    converted = true;
                }
            }
            if (!converted && (subjectType == nullptr || !subjectType->assignableFrom(caseType))) {
                diags_->of(SemanticDiag::MatchCaseTypeMismatch)
                    .atOrigin(caseNode->at(0)->origin())
                    .commit(
                        subjectType == nullptr ? std::string("?") : subjectType->toString(),
                        caseType->toString());
                throw BuildAbortException();
            }
            caseStep.condition = applyInternalOperator(
                "__eq__",
                nullptr,
                {tree_->makeDRef(name), operand},
                caseNode);
            caseStep.exec = caseNode->at(1);
            break;
        }
        case GCT::CaseType::Else:
            // `case _`, guarded or not: it tests nothing itself. A guarded one leaves the value
            // to the cases written after it when its guard does not hold.
            caseStep.exec = caseNode->at(0);
            break;
        default:
            throw std::runtime_error("A guarded match case is a value or `_`.");
        }
        steps.push_back(std::move(caseStep));
    }
    return buildCaseChain(steps, gct);
}

node_handle_t Builder::buildBranch(
    const GCT::node_ptr_t &gct, node_handle_t condNode, const std::vector<arm_ptr_t> *arms,
    bool valueless) {
    if (arms == nullptr && hasGuardedCase(gct, 1)) {
        return buildGuardedMatch(gct, condNode);
    }
    node_handle_t joinNode = currGraph_->addJoinNode(nullptr, nullptr, 0);
    node_handle_t brchNode = currGraph_->addBrchNode(Type::Int64(), joinNode, 0);
    registerNodeOrigin(context_, brchNode, gct, "gir.brch");
    registerNodeOrigin(
        context_,
        joinNode,
        gct,
        "gir.join",
        {semanticPart(
            camel::source::SemanticRole::ValueProducer,
            camel::source::kInvalidOriginId,
            -1,
            "join")},
        {});

    Type *joinType = nullptr;
    // The arm being added is the one an `if` without `else` has in its place. With arms the
    // caller built, the caller says whether the branch is such an `if` (`valueless`): the
    // join then takes nothing from any of them.
    bool absentElse = valueless;
    bool ifArmSeen  = false;
    // Whether the branch is an `if` without `else`, known before its arms are added: its arms
    // give values nothing takes (rejectDiscardedValue in addArm).
    bool dropsArmValues = valueless;
    if (arms == nullptr && gct->size() >= 3) {
        const auto &first = gct->at(1);
        const auto &last  = gct->at(gct->size() - 1);
        dropsArmValues    = first->caseType() == GCT::CaseType::True &&
                            last->caseType() == GCT::CaseType::Else && last->size() == 1 &&
                            last->at(0)->size() == 0;
    }
    // A position that takes no value joins nothing: an `if` without `else` is one (`valueless`,
    // `dropsArmValues`), and so is a branch at a `void` position or written as a statement whose
    // value the block drops. One rule (design principles 3.7):
    // the arms' effects happen, no value is joined,
    // and what the arms give is dropped. It is known before the arms are added, and an `if` that
    // does write an `else` does not undo it.
    const bool joinsNoValue = dropsArmValues || branchTakesNoValue(gct);

    linkNodes(LinkType::Norm, condNode, brchNode);
    std::vector<arm_ptr_t> armList;

    // Makes `arm` the next arm of the branch.
    const auto addArm = [&](const arm_ptr_t &arm) {
        const BodyKey key{true, arm->bodyId};
        if (dropsArmValues) {
            // The arm of an `if` without `else` gives a value nothing takes.
            rejectDiscardedValue(key);
        }
        bodyValueStatement_.erase(key);
        armList.push_back(arm);
        if (!joinsNoValue && !absentElse) {
            // A position that takes a value reads a cell out before the arms are joined: an arm
            // of `var T` joins as its `T` (design principles 3.8).
            // Nothing is read where the join takes no value.
            readArmCellValue(arm);
        }
        Type *exitType = arm->exitType();

        if (absentElse || joinsNoValue) {
            // Design principles 3.7: an `if` without `else` has no value, and neither has a
            // branch at a position that takes no value. Its arm may be of any type and what the
            // arm gives is dropped: the join takes nothing. The arm written is evaluated as any
            // arm is; the one not written is an empty arm.
            joinType = Type::Void();
            setNodeType(joinNode, joinType);
        } else if (joinType == nullptr) {
            joinType = exitType;
            setNodeType(joinNode, joinType);
        } else {
            // Arms must agree up to refinement: unify widens (for example two
            // tensors whose shapes differ in one extent) or fails. Failing that, one arm may
            // enter the form of the other (design principles 3.8): `if c then 1 else null`
            // is an `int?`, `if c then v else failed` a `T!`. The arm that is not of the
            // joined type gets the CAST at its exit once the type is known (below).
            Type *unified = joinTypes(joinType, exitType);
            if (unified != nullptr && unified != joinType) {
                joinType = unified;
                setNodeType(joinNode, joinType);
            }
            if (unified == nullptr) {
                // Two arrays whose element types do not unify have no common type either, and the
                // element type of a value is part of its type (design principles 5.1): the join
                // names the element type each arm gives, with the conversion that would make the
                // two one, rather than only reporting that the arms disagree.
                if (joinType->code() == TypeCode::Array && exitType->code() == TypeCode::Array) {
                    diags_->of(SemanticDiag::BranchElementTypeMismatch)
                        .atOrigin(gct->origin())
                        .commit(
                            tt::as_ptr<ArrayType>(joinType)->elemType()->toString(),
                            tt::as_ptr<ArrayType>(exitType)->elemType()->toString());
                    throw BuildAbortException();
                }
                diags_->of(SemanticDiag::BranchReturnTypeMismatch)
                    .atOrigin(gct->origin())
                    .commit(
                        currGraph_->name() + ": " + currGraph_->funcType()->toString(),
                        joinType->toString(),
                        exitType->toString());
                throw BuildAbortException();
            }
        }
    };
    const size_t armEnd = arms != nullptr ? arms->size() + 1 : gct->size();
    // The value a name had before this branch, as this branch found it. A branch built *inside* an
    // arm registers its own join as the name's version while that arm is being built, and that join
    // is not the value the name had before this branch: the arm that hands the branch nothing would
    // then join the nested join, which is only defined inside the other arm (a `var` written inside
    // a nested `if` came back as 0, and the C++ writer left the branch's temporary unassigned).
    const std::unordered_map<node_handle_t, node_handle_t> carriedVersionsBefore = carriedVersions_;
    // The pre-branch values are taken while the arms are built (SsaEnv::rebind). The gathering is
    // open for exactly as long as they are, and every branch that opens it closes it, nested or
    // not.
    ssa_.beginBranch();
    for (size_t i = 1; i < armEnd; i++) {
        arm_ptr_t subGraph;
        if (arms != nullptr) {
            // Arms the caller built (the tagged forms): a boolean condition and two arms.
            subGraph = (*arms)[i - 1];
        } else {
            const auto &caseNode         = gct->at(i);
            GCT::node_ptr_t caseExecNode = nullptr;

            switch (caseNode->caseType()) {
            case GCT::CaseType::True: {
                GUARD(caseNode->size() == 1, "True case should have only one child.");
                // An `if`: the condition is a bool (language reference 6.8.1: no truth-value
                // conversion). Anything else was compared bit for bit with `true`.
                Type *condType = valueTypeOf(nodeTypeOf(condNode));
                if (condType->code() != TypeCode::Bool) {
                    diags_->of(SemanticDiag::ConditionNotBool)
                        .atOrigin(gct->at(0)->origin())
                        .commit(condType->toString());
                    throw BuildAbortException();
                }
                caseExecNode = caseNode->at(0);
                ifArmSeen    = true;
                break;
            }
            case GCT::CaseType::Else:
                GUARD(i == gct->size() - 1, "Else case should be the last case.");
                GUARD(caseNode->size() == 1, "Else case should have only one child.");
                caseExecNode = caseNode->at(0);
                absentElse   = ifArmSeen && caseExecNode->size() == 0;
                break;
            case GCT::CaseType::Value: {
                GUARD(caseNode->size() == 2, "Value case should have two children.");
                node_handle_t valueNode = valueNodeOf(visit(caseNode->at(0)));
                // A case is compared with the value matched, by the comparison of that type:
                // a case of another type was compared bit for bit (`case 1` on a string read
                // the integer as a string, and crashed).
                Type *subjectType = valueTypeOf(nodeTypeOf(condNode));
                // A pattern literal takes the type of the value it is matched against (design
                // principles 5.1): `match x { case 3 => ... }` with `x: f32` compares the f32
                // with 3.0, not with a pattern of another numeric type.
                if (const std::optional<literal::Value> pattern =
                        literalValueOfExpr(caseNode->at(0));
                    pattern.has_value() && subjectType != nullptr && subjectType->isPrimitive() &&
                    !subjectType->assignableFrom(valueTypeOf(nodeTypeOf(valueNode))) &&
                    literal::kindEnters(*pattern, subjectType)) {
                    valueNode = literalInType(*pattern, subjectType, caseNode->at(0), true);
                }
                Type *caseType = valueTypeOf(nodeTypeOf(valueNode));
                if (!subjectType->assignableFrom(caseType)) {
                    diags_->of(SemanticDiag::MatchCaseTypeMismatch)
                        .atOrigin(caseNode->at(0)->origin())
                        .commit(subjectType->toString(), caseType->toString());
                    throw BuildAbortException();
                }
                linkNodes(LinkType::With, valueNode, brchNode);
                caseExecNode = caseNode->at(1);
                break;
            }
            default:
                throw std::runtime_error("Unknown case type in BRCH node.");
            }

            subGraph = buildArm(caseExecNode);
        }
        addArm(subGraph);
    }
    // Every branch that began the gathering ends it here, nested or not: the outermost one's
    // records are the ones the joins above read, and the depth is what says whether a branch is
    // open at all.
    ssa_.endBranch();
    // A `var` an arm writes as a value is carried by the branch's JOIN. The join has one
    // value slot, so the scan lets a branch write one declaration and no more; an arm that writes
    // nothing hands out the value the declaration had before the branch, which is why the
    // pre-branch value is taken while the arms are built (SsaEnv::rebind). What the arms wrote is
    // theirs; the question the join asks of it is the environment's.
    std::string ssaCarried;
    {
        std::vector<const SsaEnv::ArmWrites *> writes;
        writes.reserve(armList.size());
        for (const arm_ptr_t &arm : armList) {
            writes.push_back(&arm->writes);
        }
        const auto typeOf = [this](node_handle_t value) {
            return value != nullptr ? valueTypeOf(nodeTypeOf(value)) : nullptr;
        };
        if (const auto carry = ssa_.branchCarry(writes, nodeScope_, typeOf)) {
            // The arm that wrote hands the join its value, and the arm that wrote nothing hands the
            // value the declaration had before the branch: that is the arm's result, so a read
            // after the branch that takes the join reads what the branch gave the name.
            auto &carryDraft = currGraph_->transaction().edit();
            for (size_t a = 0; a < armList.size(); ++a) {
                carryDraft.setRegionResult(armList[a]->region, nodeIdOf(carry->values[a]));
            }
            joinType   = carry->type;
            ssaCarried = carry->name;
        }
    }
    if (joinType != nullptr && !absentElse && !joinsNoValue) {
        for (const arm_ptr_t &arm : armList) {
            Type *exitType = arm->exitType();
            if (exitType != nullptr && !joinType->assignableFrom(exitType) &&
                entersImplicitly(joinType, exitType)) {
                coerceArmExit(arm, joinType, gct);
            }
        }
    }
    // The JOIN takes what each arm gives; each arm is the region it was built in, which the
    // BRCH owns now that it is there.
    auto &draft = currGraph_->transaction().edit();
    for (size_t i = 0; i < armList.size(); ++i) {
        linkNodes(LinkType::With, armList[i]->output, joinNode);
        draft.setRegionOwner(armList[i]->region, nodeIdOf(brchNode), static_cast<uint32_t>(i));
        draft.setRegionResult(armList[i]->region, nodeIdOf(armList[i]->output));
    }
    const size_t armCount = armList.size();
    runtime::DraftBrchPayload payload{
        .join     = nodeIdOf(joinNode),
        .armCount = static_cast<runtime::gc_cnt_t>(armCount)};
    std::vector<std::byte> brchBytes(sizeof(runtime::DraftBrchPayload));
    std::memcpy(brchBytes.data(), &payload, sizeof(payload));
    std::vector<draft_node_ref_t> brchNormInputs(
        normInputsOf(brchNode).begin(),
        normInputsOf(brchNode).end());
    std::vector<draft_node_ref_t> brchWithInputs(
        withInputsOf(brchNode).begin(),
        withInputsOf(brchNode).end());
    std::vector<draft_node_ref_t> brchCtrlInputs(
        ctrlInputsOf(brchNode).begin(),
        ctrlInputsOf(brchNode).end());
    const auto *brchHeader   = nodeHeaderOf(brchNode);
    const auto brchDataIndex = brchHeader->dataIndex != 0
                                   ? brchHeader->dataIndex
                                   : currGraph_->addRuntimeSlot(Type::Int64());
    currGraph_->edit().rewriteNode(
        nodeIdOf(brchNode),
        runtime::DraftNodeInit{
            .dataIndex    = brchDataIndex,
            .dataType     = Type::Int64(),
            .runtimeFlags = brchHeader->runtimeFlags,
            .kind         = runtime::GCNodeKind::Brch,
            .payload      = std::span<const std::byte>(brchBytes.data(), brchBytes.size()),
            .normInputs   = brchNormInputs,
            .withInputs   = brchWithInputs,
            .ctrlInputs   = brchCtrlInputs});
    runtime::GCJoinBody joinBody{
        .brch     = nodeIdOf(brchNode),
        .armCount = static_cast<runtime::gc_cnt_t>(armCount)};
    std::vector<draft_node_ref_t> joinNormInputs(
        normInputsOf(joinNode).begin(),
        normInputsOf(joinNode).end());
    std::vector<draft_node_ref_t> joinWithInputs(
        withInputsOf(joinNode).begin(),
        withInputsOf(joinNode).end());
    // A `var` is a name for values, so a write inside a branch is the branch's result. A statement
    // branch joins no value, but the arms' writes are still that branch's value: each arm gives
    // what it wrote, which already stands in the join's WITH slot, and an arm that did not write
    // (`if c then { x = 1 }`) gives the value the `var` had before the branch. That is the shape
    // the rule names: `if c then { x = 1 }` is `x = if c then 1 else x`, and
    // `if c then { x = 1 } else { x = 2 }` is `x = if c then 1 else 2` (design principles 3.5,
    //  `var` is lowered to SSA, a branch assignment is a result of the
    // branch).
    //
    // The carry is one variable per branch, because the WITH slots are one per arm and not one per
    // variable: a branch whose arms write two or more `var`s has no slot to merge them in and keeps
    // its cells. A `var` that escapes through a closure or a `var` argument keeps its cell too, and
    // so does a `var` parameter. A write inside a loop is carried state, the third of the places
    // the rule names: this path sees a JOIN, never a LOOP.
    node_handle_t mergeVariable{};
    if (joinsNoValue && joinWithInputs.size() == armList.size() && !armList.empty()) {
        // For each distinct `var` the arms wrote: one entry per arm, holding the value that arm's
        // last write stored. The arm's own block value is not it -- `sync { x = 5; println(..) }`
        // writes `5` and gives the print's value -- and what the branch carries is the write.
        struct CarriedArm {
            bool wrote = false;
            std::optional<draft_node_ref_t> value;
        };
        std::vector<node_handle_t> written;
        std::unordered_map<node_handle_t, std::vector<CarriedArm>> perArm;
        for (size_t a = 0; a < armList.size(); ++a) {
            std::unordered_set<node_handle_t> here;
            for (node_handle_t variable : armList[a]->touchedOrder) {
                const auto &touched = armList[a]->touched.at(variable);
                if (!touched.writerAfter.has_value() || !here.insert(variable).second) {
                    continue;
                }
                if (!perArm.contains(variable)) {
                    written.push_back(variable);
                    perArm[variable].assign(armList.size(), CarriedArm{});
                }
                CarriedArm &arm = perArm[variable][a];
                // The last write of the arm, and the value it stored: a write's second operand.
                std::vector<draft_node_ref_t> operands(
                    normInputsOf(*touched.writerAfter).begin(),
                    normInputsOf(*touched.writerAfter).end());
                if (operands.size() >= 2) {
                    arm.wrote = true;
                    arm.value = operands[1];
                }
            }
        }
        if (written.size() == 1 && !escapingVariables_.contains(written.front()) &&
            nodeKindOf(written.front()) != camel::runtime::GCNodeKind::Port) {
            const node_handle_t variable  = written.front();
            Type *valueType               = valueTypeOf(nodeTypeOf(variable));
            std::vector<CarriedArm> &arms = perArm[variable];
            size_t wroteArms              = 0;
            for (const CarriedArm &arm : arms) {
                if (arm.wrote) {
                    ++wroteArms;
                }
            }
            // The value the `var` has before the branch: the one its last write wrote, or the one
            // the cell was made with when nothing had written it yet. Only an arm that did not
            // write needs it.
            std::optional<draft_node_ref_t> beforeValue;
            if (wroteArms != armList.size()) {
                node_handle_t version = nullptr;
                {
                    // The versions the names had before this branch, not the ones a branch built
                    // inside an arm registered while that arm was being built.
                    auto live        = std::move(carriedVersions_);
                    carriedVersions_ = carriedVersionsBefore;
                    version          = valueVersionOf(variable);
                    carriedVersions_ = std::move(live);
                }
                if (version != nullptr) {
                    // A version that carries the value itself -- a write, or a branch that carried
                    // an earlier write -- is the value from before this branch; one that carries
                    // the cell is a write, and what a read wants is the value it stored.
                    Type *versionType = nodeTypeOf(version);
                    if (versionType != nullptr && valueTypeOf(versionType) == versionType) {
                        beforeValue = nodeIdOf(version);
                    } else {
                        std::vector<draft_node_ref_t> operands(
                            normInputsOf(version).begin(),
                            normInputsOf(version).end());
                        if (operands.size() >= 2) {
                            beforeValue = operands[1];
                        }
                    }
                } else {
                    std::vector<draft_node_ref_t> made(
                        normInputsOf(variable).begin(),
                        normInputsOf(variable).end());
                    if (!made.empty()) {
                        beforeValue = made[0];
                    }
                }
            }
            // Each slot of the join takes the value that arm wrote, or -- for an arm that did not
            // write (an `if` without `else`) -- the value the `var` had before the branch. Every
            // one of them must be of the variable's value type, or the join would carry two types:
            // such a shape keeps its cell.
            bool carried = valueType != nullptr;
            for (const CarriedArm &arm : arms) {
                const std::optional<draft_node_ref_t> &value = arm.wrote ? arm.value : beforeValue;
                if (!value.has_value() ||
                    nodeTypeOf(nodeDraftOf(variable)->ref(*value)) != valueType) {
                    carried = false;
                    break;
                }
            }
            if (carried) {
                auto &mergeDraft = currGraph_->transaction().edit();
                for (size_t a = 0; a < arms.size(); ++a) {
                    joinWithInputs[a] = arms[a].wrote ? *arms[a].value : *beforeValue;
                    // The arm's result is the value it wrote: an arm that hands the branch no value
                    // of its own (`sync { x = 1 }`, a statement branch, whose result is the GATE
                    // handing out the statement's value) now hands out that value, and an arm with
                    // no result takes one. An arm that already has a value of its own -- a
                    // `return`, a block whose value is the branch's value -- keeps it.
                    const node_handle_t value     = nodeDraftOf(variable)->ref(joinWithInputs[a]);
                    const draft_node_ref_t result = mergeDraft.region(armList[a]->region).result;
                    const bool holdsOnlyTheWrite  = result != 0 && arms[a].wrote && [&] {
                        // What the arm hands out, with the GATEs it hands it out through taken
                        // away: a GATE computes nothing and passes its value on, so when the
                        // arm's value unwraps to the value the write wrote, that value is the
                        // arm's result and the GATEs that only carried it are left with no
                        // reader. A branch value of the arm's own (`if c then a else b` in the
                        // arm) does not unwrap to it, and the arm keeps what it had.
                        camel::runtime::gc_node_ref_t node = result;
                        for (int steps = 0; steps < 64; ++steps) {
                            const camel::runtime::DraftNodeHeader *header = mergeDraft.header(node);
                            if (header == nullptr ||
                                header->kind != camel::runtime::GCNodeKind::Gate) {
                                break;
                            }
                            const auto inputs = mergeDraft.normInputsOf(node);
                            if (inputs.empty()) {
                                break;
                            }
                            node = inputs[0];
                        }
                        return node == nodeIdOf(nodeDraftOf(variable)->ref(*arms[a].value));
                    }();
                    if (result == 0 || holdsOnlyTheWrite) {
                        mergeDraft.setRegionResult(armList[a]->region, nodeIdOf(value));
                    }
                }
                joinType      = valueType;
                mergeVariable = variable;
                // The branch is this variable's write from here on: a read written after it takes
                // the join, and the `sync` block that used to order such a read is not needed.
                carriedVersions_[variable] = joinNode;
            }
        }
    }
    std::vector<draft_node_ref_t> joinCtrlInputs(
        ctrlInputsOf(joinNode).begin(),
        ctrlInputsOf(joinNode).end());
    const auto *joinHeader = nodeHeaderOf(joinNode);
    const auto joinDataIndex =
        joinHeader->dataIndex != 0 ? joinHeader->dataIndex : currGraph_->addRuntimeSlot(joinType);
    currGraph_->edit().rewriteNode(
        nodeIdOf(joinNode),
        runtime::DraftNodeInit{
            .dataIndex    = joinDataIndex,
            .dataType     = joinType,
            .runtimeFlags = joinHeader->runtimeFlags,
            .kind         = runtime::GCNodeKind::Join,
            .payload      = std::as_bytes(std::span{&joinBody, 1}),
            .normInputs   = joinNormInputs,
            .withInputs   = joinWithInputs,
            .ctrlInputs   = joinCtrlInputs});

    linkNodes(LinkType::Norm, brchNode, joinNode);
    if (!ssaCarried.empty()) {
        // The join is the name's value from here on: an arm that wrote it hands that value out, and
        // the arm that wrote nothing hands out the value the name had before the branch. A loop
        // head around this branch carries the name as a value too, and the value it
        // carries is the join -- not the value of the arm that happened to be built last.
        ssa_.rebind(nodeScope_, ssaCarried, joinNode, armWritesHere());
        noteLoopStateWrite(ssaCarried, joinNode);
    }
    linkArmEntries(armList, brchNode);
    // The arms of this branch exclude each other (design principles 5.4): the check on the built
    // graph reads which arm a read stands in from the region of the BRCH node, so nothing is
    // recorded here.

    // The branch is the user of the variables its arms used, and the writer of those they
    // wrote. It begins at BRCH and is complete at JOIN.
    std::vector<node_handle_t> capturedWrites;
    mergeArmVariables(armList, joinNode, capturedWrites);
    if (mergeVariable != nullptr) {
        // The join is this variable's version, not only the node its write was recorded through.
        // mergeArmVariables records the write through the callee path, which leaves the variable's
        // modifier alone, so a read written after the branch would still ask for the cell and take
        // the value the cell was made with (`[DBG-I] var=3 read=13 at-read-time=3` while
        // `[DBG-H] var=3 recorded=4` says the join was recorded). The join carries the value now,
        // so it is the version, and what the read takes is what the branch gives.
        setModifier(mergeVariable, joinNode);
        // The write is what marked this variable as escaping (requireMutableArgument: the first
        // parameter of `__assn__` is a `var` parameter). Here the write is the branch's own value
        // and the cell never leaves the graph, so the mark is taken back for this one variable --
        // not for assignments in general, which is what broke a write inside a loop (carried state,
        // the third item of the schedule).
        escapingVariables_.erase(mergeVariable);
    }
    const auto originOf = [&](node_handle_t variable) {
        const auto origin = captureOriginOf_.find(variable);
        return origin != captureOriginOf_.end() ? origin->second : variable;
    };
    std::vector<DraftGraphBuilder *> armCallees;
    for (const arm_ptr_t &arm : armList) {
        armCallees.insert(armCallees.end(), arm->callees.begin(), arm->callees.end());
    }
    if (ArmBuild *outer = activeArm()) {
        // The arms of this branch are part of the arm it is in.
        outer->callees.insert(outer->callees.end(), armCallees.begin(), armCallees.end());
    }
    // A `var` an arm writes is the branch's value, not an effect (design principles 3.1): a write
    // to a `var` is a value, so the arm does not report one. What is reported here is a write by a
    // callee an arm calls to a variable that callee captured -- that write is not handed back where
    // the call stands (reportIgnoredCapturedWrites).
    if (!capturedWrites.empty() && !synced_) {
        std::vector<node_handle_t> origins;
        for (node_handle_t variable : capturedWrites) {
            origins.push_back(originOf(variable));
        }
        unit_->pendingIgnoredWrites.push_back(
            {armCallees, origins, gct->origin(), "The branch", "an arm writes"});
    }

    if (synced_) {
        // A branch is one statement: the chain enters its owning BRCH and continues from its JOIN.
        linkSyncStatement(lastSyncedNode_, brchNode);
        lastSyncedNode_ = joinNode;
        chainedNodes_.insert(brchNode);
        chainedNodes_.insert(joinNode);
    }

    return joinNode;
}

void Builder::linkArmEntries(const std::vector<arm_ptr_t> &arms, node_handle_t brchNode) {
    // Arm entry is conditional control, not a sync edge of the enclosing block: an outer sync block
    // does not turn the arm into a sync block.
    auto &draft                = currGraph_->transaction().edit();
    const draft_node_ref_t end = static_cast<draft_node_ref_t>(draft.nodeSlotCount());
    for (const arm_ptr_t &arm : arms) {
        const auto inArm = [&](draft_node_ref_t id) {
            return draft.alive(id) && draft.regionWithin(draft.nodeRegion(id), arm->region);
        };
        for (draft_node_ref_t id = arm->firstNode; id < end; ++id) {
            if (!draft.alive(id) || draft.nodeRegion(id) != arm->region) {
                continue;
            }
            switch (draft.header(id)->kind) {
            case runtime::GCNodeKind::Data:
            case runtime::GCNodeKind::Port:
            case runtime::GCNodeKind::Dref:
                continue;
            default:
                break;
            }
            bool computed = false;
            for (auto inputs :
                 {draft.normInputsOf(id), draft.withInputsOf(id), draft.ctrlInputsOf(id)}) {
                for (draft_node_ref_t input : inputs) {
                    computed = computed || inArm(input);
                }
            }
            node_handle_t node = draft.ref(id);
            if (!computed && linkCheek(brchNode, node)) {
                writeControlLink(brchNode, node);
            }
        }
    }
}

void Builder::mergeArmVariables(
    const std::vector<arm_ptr_t> &arms, node_handle_t joinNode,
    std::vector<node_handle_t> &written) {
    std::vector<node_handle_t> order;
    std::unordered_map<node_handle_t, bool> wrote;
    for (const arm_ptr_t &arm : arms) {
        for (node_handle_t variable : arm->touchedOrder) {
            const ArmBuild::Touched &touched = arm->touched.at(variable);
            auto known                       = wrote.find(variable);
            if (known == wrote.end()) {
                known = wrote.emplace(variable, false).first;
                order.push_back(variable);
            }
            if (touched.writerAfter.has_value()) {
                known->second = true;
                if (linkCheek(*touched.writerAfter, joinNode)) {
                    writeControlLink(*touched.writerAfter, joinNode);
                }
            }
        }
    }
    for (node_handle_t variable : order) {
        if (wrote[variable]) {
            written.push_back(variable);
            recordWrite(variable, joinNode, true);
        } else {
            pendingUsesOf(variable).push_back({joinNode, false});
        }
    }
}

node_handle_t
Builder::intervalSequenceValue(node_handle_t from, node_handle_t to, const GCT::node_ptr_t &expr) {
    // The interval in the sequence position of a loop is the pair of its two bounds, not the
    // array of its elements (design principles 4.2): the pair stands where any other sequence
    // stands — Norm input 0 of the LOOP node, one value — and a back end takes the element of the
    // round from the bounds and the round number, so nothing of the interval's size is ever made.
    // The interval is the `int[]` of `intervalValue` in every other position.
    std::vector<Type *> bounds{Type::Int64(), Type::Int64()};
    TupleType *boundsType = TupleType::create(bounds);
    const std::vector<size_t> slots{0, 1};
    auto &draft          = currGraph_->transaction().edit();
    node_handle_t result = currGraph_->addFillNode(
        boundsType,
        runtime::makeFillPayload(runtime::GCFillKind::Tuple, slots));
    const slot_t empty =
        camel::core::rtdata::toSlot(::Tuple::create(2, camel::core::mm::autoSpace()));
    node_handle_t source = draft.ref(draft.materializeStaticValue(empty, boundsType));
    linkNodes(LinkType::Norm, source, result);
    linkNodes(LinkType::With, from, result);
    linkNodes(LinkType::With, to, result);
    registerNodeOrigin(context_, result, expr, "gir.loop.interval");
    return result;
}

node_handle_t Builder::visitLoopNode(const GCT::node_ptr_t &gct) {
    ENTER("LOOP");
    using camel::core::LoopKind;
    const LoopKind kind    = gct->loopKind();
    const size_t k         = gct->loopStateCount();
    const bool hasIterable = camel::core::loopHasIterable(kind);

    // The names of the head are declared once, together (design principles 3.10).
    {
        std::unordered_set<std::string> names;
        for (size_t j = 0; j < k + (hasIterable ? 1 : 0); ++j) {
            const std::string &name = j < k ? gct->loopStateName(j) : gct->loopElementName();
            if (!names.insert(name).second) {
                diags_->of(SemanticDiag::LoopNameRepeated).atOrigin(gct->origin()).commit(name);
                throw BuildAbortException();
            }
        }
    }

    // Whether the first value written at `expr` is a numeric literal the position `type` holds.
    // Such a literal takes that type while it is made (visitInPosition, literalDataInType), as it
    // does in the arm of an `if`. A value that is not a literal is never given this type: the
    // loop's value is the state's type, and the position's own check reports the mismatch.
    //
    // A composite literal is not one of these, and neither is the empty array literal. A
    // composite has a shape of its own, and the loop's state keeps it: the position then converts
    // the loop's value (`loop s = {}` under `Nothing` keeps the state `{  }` and a CAST names it;
    // cases/loops/zero_leaf_state.cml). The empty array literal has no element type at all, and
    // takes the position's element type below, once the body is built and the elements it appends
    // are known (inferLoopStateFromAppend).
    const auto literalEnters = [&](const GCT::node_ptr_t &expr, Type *type) -> bool {
        if (expr == nullptr || type == nullptr || asTagged(type) != nullptr) {
            return false;
        }
        const std::optional<literal::Value> exact = literalValueOfExpr(expr);
        return exact.has_value() && literal::kindEnters(*exact, type);
    };

    // The first values and the array: evaluated once, where the loop is (they do not see the
    // names of the head). The loop's value is the state's type -- the tuple of the state types
    // when there are several -- so each first value stands at the loop's own position, and a
    // numeric literal written there takes the position's type (design principles 5.1): `let n:
    // f32 = loop s = 0.0 while s < 3.0 do s + 1.0` is a loop of `f32`, and the body's and the
    // condition's literals follow from the state's type. A state written as the empty literal is
    // the other way round: it takes the element type of the position the loop's value goes to
    // (`loop losses = [] for ...` where the loop gives a `float[]`; design principles 5.1, 5.2).
    // A state whose first value is anything else keeps its own type, whatever the position says.
    std::vector<Type *> stateExpected(k, nullptr);
    std::vector<bool> literalAtPosition(k, false);
    if (expectedValueType_ != nullptr) {
        Type *position = valueTypeOf(expectedValueType_);
        for (size_t j = 0; j < k; ++j) {
            Type *state = position;
            if (k > 1) {
                // The loop's value is a tuple of the state types: one state's position is one
                // element of it, and a position that is not that tuple says nothing per state.
                if (position == nullptr || position->code() != TypeCode::Tuple ||
                    tt::as_ptr<TupleType>(position)->size() != k) {
                    continue;
                }
                state = tt::as_ptr<TupleType>(position)->typeAt(j);
            }
            if (state == nullptr) {
                continue;
            }
            if (k == 1 && state->code() == TypeCode::Array) {
                stateExpected[j] = state;
                continue;
            }
            if (literalEnters(gct->loopInitial(j), state)) {
                stateExpected[j]     = state;
                literalAtPosition[j] = true;
            }
        }
    }
    std::vector<node_handle_t> initial;
    std::vector<Type *> stateTypes;
    std::vector<bool> emptyInitial(k, false);
    bool deferredEmpty = false;
    for (size_t j = 0; j < k; ++j) {
        const auto &expr   = gct->loopInitial(j);
        Type *loopExpected = stateExpected[j];
        node_handle_t node;
        {
            ExpectedValueScope stateScope(expectedValueType_, loopExpected);
            // A literal in the position is made in the position's type; the empty literal is
            // left as it is and takes that type below.
            node_handle_t built{};
            if (literalAtPosition[j]) {
                built = visitInPosition(expr, loopExpected);
            } else {
                built = valueNodeOf(visit(expr));
            }
            node = valueOf(functionValueOf(built, expr));
        }
        if (loopExpected != nullptr && !literalAtPosition[j]) {
            // A value that is not the empty literal is converted only when its position
            // accepts it; one that does not fit stays as it is and is reported below.
            if (node_handle_t typed = checkValue(node, loopExpected, expr); typed != nullptr) {
                node = typed;
            }
        }
        Type *type = valueTypeOf(nodeTypeOf(node));
        const bool untyped =
            type == nullptr || type->code() == TypeCode::Void || isUntypedArrayType(type);
        if (untyped) {
            // `loop losses = [] for ... do losses->append<l>`: the element type is the value
            // the body appends, and it is known once the body is built (design principles 5.1,
            // 5.2). One state gives the loop's value directly; with several, the loop's value
            // is a tuple and does not say what this state's element type is, so it must be
            // written.
            if (k == 1 && isEmptyArrayLiteral(node)) {
                deferredEmpty = true;
            } else {
                diags_->of(SemanticDiag::LoopStateWithoutType)
                    .atOrigin(expr->origin())
                    .commit(gct->loopStateName(j), type ? type->toString() : std::string("void"));
                throw BuildAbortException();
            }
        }
        emptyInitial[j] = isEmptyArrayLiteral(node);
        initial.push_back(node);
        stateTypes.push_back(type);
    }
    // The `var`s the body writes are states of the loop (design principles 3.5: "a write to an
    // outer `var` in a loop body becomes the loop's carried state"). They are found here, before
    // anything of the body is built, because a state is a port of the loop's regions and the ports
    // are declared before the body's names are resolved: `x = e` in the body then writes the port.
    //
    // What is claimed is a statement of the body's own block that assigns a whole `var` of this
    // graph (`x = e`, and nothing else): a compound assignment, a path (`x.f = e`), a `var`
    // parameter of the graph (whose cell is the caller's), a cell a closure captured or a call was
    // handed, and a name the body writes anywhere deeper than its own block are left to the
    // lowering that carries a branch's or a call's write, which is what they need.
    const size_t declaredStates = k;
    struct LoopCarriedState {
        node_handle_t cell  = nullptr;
        node_handle_t start = nullptr;
        std::string name;
        node_handle_t port = nullptr;
        /// The state is a value, not a cell: the body's name is the port, a write
        /// rebinds it and is the round's next value, and the head's phi is the port.
        bool ssa = false;
    };
    std::vector<LoopCarriedState> carried;
    {
        // The names assigned by a statement of the body's block, in the order they are written; the
        // names assigned below it in a region this loop can hand the value back out of -- an arm of
        // a statement branch (a JOIN) or a loop body (that head's value) -- in the order
        // they are first written there; and the names assigned in a region it cannot (a block of
        // its own, a closure's body, a static loop's), which are left to that region's own
        // lowering.
        std::vector<std::string> directWrites;
        std::vector<std::string> carriedWrites;
        std::unordered_set<std::string> seenWrites;
        std::unordered_set<std::string> seenCarriedWrites;
        std::unordered_set<std::string> nestedWrites;
        std::unordered_set<std::string> outsideWrites;
        // Names a compound assignment writes (`x += e` is `LINK(__assn_add__, x, e)`). It is a
        // write of the name exactly as `x = e` is: the body builds the same new value and hands it
        // to the name. A `var` the declaration built as a value is therefore carried by the head
        // when the body writes it this way too -- `total(node: Node)` in
        // `test/cases/compile/semantic/recursive_struct_array.cml` accumulates with `acc += ...`,
        // and a head that does not carry it leaves the read after the loop with a value from the
        // round that last ran (`450971566189` where the sum is `10`). A cell keeps the shape it
        // had: the store in the body is reachable there, so this only widens what a value carries.
        std::unordered_set<std::string> compoundWrites;
        const std::function<void(const GCT::node_ptr_t &, bool, bool, bool)> scan =
            [&](const GCT::node_ptr_t &node, bool direct, bool inCarriedRegion, bool inLoopBody) {
                if (node == nullptr) {
                    return;
                }
                // What the node is (NodeKind::ASSIGN, with its form), read off the node instead of
                // off a group's spelling: `x += e` writes the name exactly as `x = e` does. The
                // target has to be the name itself for this scan to have a name to carry -- a path
                // into a value (`v.f = e`) is the whole variable's write, which this scan is not
                // about, and the builder's own rebuilds (`Rebuilt`, `Owned`) are not the program's
                // writes.
                const bool nameWrite =
                    node->type() == GCT::NodeKind::ASSIGN &&
                    (node->assignmentForm() == GCT::AssignmentForm::Plain ||
                     node->assignmentForm() == GCT::AssignmentForm::Compound) &&
                    node->assignmentTarget() != nullptr &&
                    node->assignmentTarget()->type() == GCT::NodeKind::DREF;
                const bool plainAssign =
                    nameWrite && node->assignmentForm() == GCT::AssignmentForm::Plain;
                const bool compoundAssign =
                    nameWrite && node->assignmentForm() == GCT::AssignmentForm::Compound;
                if (plainAssign || compoundAssign) {
                    const std::string written = moduleLevelName(node->assignmentTarget()->name());
                    if (compoundAssign) {
                        compoundWrites.insert(written);
                        if (seenWrites.insert(written).second) {
                            directWrites.push_back(written);
                        }
                    } else if (direct) {
                        if (seenWrites.insert(written).second) {
                            directWrites.push_back(written);
                        }
                    } else {
                        nestedWrites.insert(written);
                        if (inLoopBody || inCarriedRegion) {
                            if (seenCarriedWrites.insert(written).second) {
                                carriedWrites.push_back(written);
                            }
                        } else {
                            outsideWrites.insert(written);
                        }
                    }
                }
                for (size_t i = 0; i < node->size(); ++i) {
                    const GCT::node_ptr_t &child = node->at(i);
                    const bool childLoopBody =
                        node->type() == GCT::NodeKind::LOOP && node->loopBody() == child;
                    const bool childArm =
                        child != nullptr && (child->type() == GCT::NodeKind::CASE ||
                                             child->type() == GCT::NodeKind::TCASE);
                    scan(child, false, inCarriedRegion || childArm, inLoopBody || childLoopBody);
                }
            };
        const GCT::node_ptr_t body = gct->loopBody();
        if (body != nullptr) {
            if (body->type() == GCT::NodeKind::EXEC) {
                for (size_t i = 0; i < body->size(); ++i) {
                    scan(body->at(i), true, false, false);
                }
            } else {
                scan(body, true, false, false);
            }
        }
        // A name a region this loop cannot hand a value back out of writes keeps its cell: that is
        // the cheap direction to be wrong in.
        for (const std::string &name : carriedWrites) {
            if (outsideWrites.contains(name) || seenWrites.contains(name)) {
                continue;
            }
            directWrites.push_back(name);
        }
        // The value the loop starts a `var` from is the value under the copy its declaration made:
        // a `var` holds a value of its own, so the store the unit would have made copies the
        // initialiser, and the `let` writing puts the value itself in the same place. The copy is
        // therefore not part of what the two writings say, and the state starts from the value.
        const auto underTheCopy = [&](node_handle_t value) -> node_handle_t {
            node_handle_t start = value;
            for (size_t depth = 0; start != nullptr && depth < 16; ++depth) {
                if (!nodeIsKind(start, runtime::GCNodeKind::Copy)) {
                    break;
                }
                const auto below = normInputsOf(start);
                if (below.size() != 1) {
                    break;
                }
                const node_handle_t next = nodeDraftOf(start)->ref(below.front());
                if (next == nullptr) {
                    break;
                }
                start = next;
            }
            return start;
        };
        for (const std::string &name : directWrites) {
            const auto resolved = nodeAt(name);
            if (!resolved.has_value()) {
                continue;
            }
            const node_handle_t bound = resolved.value();
            // A name the declaration built as a value is carried as a value: the
            // body's name is the port, and the head carries whatever the body wrote. Nothing of it
            // is a cell, so none of the cell questions below apply.
            const SsaEnv::Entry *ssa = ssa_.find(nodeScope_, name);
            const node_handle_t value = ssa_.valueOf(ssa);
            const bool asValue        = ssa != nullptr && value != nullptr && !isVariable(bound);
            // A name that is still a cell is carried only when the body's block writes it and no
            // region of its own does, which is the reason a
            // write inside a block of the body is left to that block: the port is read where the
            // write stands, so a write the round may skip cannot be the state. A value has no cell
            // to fall back to, and the regions the scan lets such a name be used across (an arm,
            // whose JOIN is the value, and a loop body, whose head is) are exactly the ones this
            // scan hands over, so it is carried wherever one of them writes it.
            if (!asValue && (compoundWrites.contains(name) || !seenWrites.contains(name) ||
                             nestedWrites.contains(name))) {
                continue;
            }
            if (asValue) {
                Type *stored = ssa->type != nullptr ? ssa->type : valueTypeOf(nodeTypeOf(value));
                if (stored == nullptr || stored->code() == TypeCode::Void) {
                    continue;
                }
                node_handle_t start = underTheCopy(value);
                if (start == nullptr) {
                    continue;
                }
                carried.push_back(LoopCarriedState{nullptr, start, name, nullptr, true});
                stateTypes.push_back(stored);
                initial.push_back(start);
                emptyInitial.push_back(false);
                continue;
            }
            if (!sameGraph(bound, currGraph_) || !isVariable(bound) ||
                nodeKindOf(bound) == runtime::GCNodeKind::Port) {
                continue; // not a cell of this graph, or a `var` parameter's (the caller's)
            }
            const node_handle_t cell = bound;
            if (escapingVariables_.contains(cell) || captureOriginOf_.contains(cell)) {
                continue; // a closure or a call holds the cell: it stays a resource
            }
            Type *stored = valueTypeOf(nodeTypeOf(cell));
            if (stored == nullptr || stored->code() == TypeCode::Void) {
                continue;
            }
            // Where the loop starts: the value the `var` has where the loop stands, which is the
            // version its last write made, taken the way a use site takes it -- `readVariable` is
            // that read, and it is also what orders the writes written after the loop against it
            // (`recordWrite` meets every read of the cell in `pendingUses_`). `:var/get` on the
            // version a call left, and the version itself when a branch's arms carried the value,
            // are the two shapes it answers with. A `var` nothing has written keeps the value the
            // cell was made with: the one under the copy `:var/new` was built over, which is what
            // the `let` writing puts in the same place. Reading the version is what makes
            // `var v = false; v = true;` enter the loop with `true`: the state is the `var` as it
            // stands, not the value its declaration gave it
            // (test/cases/compile/var/loop_state_starts_from_the_last_write.cml).
            node_handle_t start = nullptr;
            if (valueVersionOf(cell) != nullptr) {
                start = readVariable(cell);
            } else {
                const auto ins = normInputsOf(cell);
                if (ins.size() != 1) {
                    continue;
                }
                start = underTheCopy(nodeDraftOf(cell)->ref(ins.front()));
            }
            if (start == nullptr) {
                continue;
            }
            carried.push_back(LoopCarriedState{cell, start, name, nullptr, false});
            stateTypes.push_back(stored);
            initial.push_back(start);
            emptyInitial.push_back(false);
        }
    }
    node_handle_t sequence = nullptr;
    Type *elementType      = nullptr;
    bool intervalIterates  = false;
    // The element type of a sequence a loop iterates: an array gives its own elements along its
    // first dimension, and a string and a map give theirs through the `__iter__` operator their
    // type declares (design principles 3.10, 11.1, 11.3): the code points of a string as
    // one-character strings, and the (key, value) pairs of a map in key order. The operator is
    // resolved like any other, by the type of the sequence; the loop then iterates the array it
    // gives, on every back end, with one driver.
    //
    // A `for` over an array of any rank is the core's, and no module takes part in it: `t[i]`
    // takes the i-th along the first dimension (design principles 5.4), so the round of a rank-1
    // array is its element and the round of a higher-rank one is the row one dimension shorter
    // (ArrayType::firstDimensionElement). The tensor module used to declare `__iter__` for a
    // numeric array (`tensor:iter`, sub-arrays along the first dimension), and asking it here
    // made the iterable position an OPER node: that is what hid a constant array's row count
    // from `core.loopDomain` (runtime/core_facts.cpp, constantLength). Design principles 5.4
    // states it once, in the core, for every array; the module's `iter` went with the P4 alias
    // (one thing, one spelling).
    //
    // `__iter__` reaches a compilation unit only if it imports the module that declares it, and
    // the standard library's own array code compiles without it.
    const auto iterableElementType = [&](node_handle_t &iterable,
                                         const GCT::node_ptr_t &expr) -> Type * {
        Type *type = valueTypeOf(nodeTypeOf(iterable));
        if (type == nullptr) {
            diags_->of(SemanticDiag::LoopSequenceNotIterable)
                .atOrigin(expr->origin())
                .commit(sourceTextOf(expr), std::string("void"));
            throw BuildAbortException();
        }
        if (type->code() != TypeCode::Array) {
            const std::vector<StaticArg> statics(1);
            std::optional<oper_idx_ptr_t> elements;
            // `__iter__` is a group of several overloads (`:struct/iter_str`,
            // `:coll/map_items`), one per iterable type: no one OperId names it, so it is still
            // looked up by the group's name.
            if (const auto group = importedEntity("__iter__");
                group.has_value() && group->isOperGroup()) {
                elements = group->operGroup()->resolve({}, {type}, statics);
            }
            if (!elements.has_value()) {
                diags_->of(SemanticDiag::LoopSequenceNotIterable)
                    .atOrigin(expr->origin())
                    .commit(sourceTextOf(expr), type->toString());
                throw BuildAbortException();
            }
            node_handle_t array = currGraph_->addOperNode(*elements, 0);
            stampPermissions(array);
            registerNodeOrigin(context_, array, expr, "gir.loop.iter");
            linkNodes(LinkType::Norm, iterable, array);
            iterable = array;
            type     = valueTypeOf(nodeTypeOf(iterable));
            GUARD(
                type != nullptr && type->code() == TypeCode::Array,
                "The elements of an iterable must be an array.");
        }
        return tt::as_ptr<ArrayType>(type)->firstDimensionElement();
    };
    // `for i in a..b` (design principles 4.2: "`for i in 0..n` is fused by the compiler, without
    // materialising an array"). The interval in the sequence position of a loop is the loop's own
    // form: its two bounds are the loop's iterable, and a round takes `from + round`
    // (camel::core::LoopIterableForm::Interval). In every other position `a..b` is still the
    // `int[]` the rule gives it, and the loop iterates that value like any other sequence.
    if (hasIterable && isIntervalExpr(gct->loopSequence())) {
        const auto &interval = gct->loopSequence();
        // The sequence is not the loop's value, so the position the loop stands in does not reach
        // it (design principles 5.1): its literals have the types they are written with.
        node_handle_t from{};
        node_handle_t to{};
        {
            ExpectedValueScope sequenceScope(expectedValueType_, nullptr);
            from = argumentNodeOf(visit(interval->at(1)), interval->at(1));
            to   = argumentNodeOf(visit(interval->at(2)), interval->at(2));
        }
        if (intervalBounds(from, to)) {
            sequence         = intervalSequenceValue(from, to, interval);
            elementType      = Type::Int64();
            intervalIterates = true;
        } else {
            // Two functions, or two operands `..` has no reading for: the composition (or the
            // refusal it gives everywhere else), built from the operands already in hand so that
            // neither of them is built twice.
            sequence = valueOf(
                functionValueOf(valueNodeOf(buildCompositionWith(interval, from, to)), interval));
            elementType = iterableElementType(sequence, interval);
        }
    } else if (hasIterable) {
        const auto &expr = gct->loopSequence();
        {
            // The sequence is not the loop's value (see above).
            ExpectedValueScope sequenceScope(expectedValueType_, nullptr);
            sequence = valueOf(functionValueOf(valueNodeOf(visit(expr)), expr));
        }
        Type *sequenceType = valueTypeOf(nodeTypeOf(sequence));
        // `for x in s` where `s` is a stream: the rounds are the pulls of the stream's step
        // function, and the graph already has the loop for that (the `while`, with a condition
        // and no iterable), so nothing about the driver or a back end is new.
        if (sequenceType != nullptr && sequenceType->code() == TypeCode::Stream && k > 0) {
            LEAVE("LOOP");
            return buildStreamLoop(gct, sequence, initial, stateTypes);
        }
        elementType = iterableElementType(sequence, expr);
    }
    // The states the loop carries: the ones its head declares, and the `var`s its body writes.
    const size_t stateCount = stateTypes.size();
    Type *stateType =
        stateCount == 1 ? stateTypes.front() : static_cast<Type *>(TupleType::create(stateTypes));

    // The names of the state's components. They are the state's one name until splits a
    // struct state into its parts; then they are the paths of those parts (`s.inner.x`), which
    // is what the graph shows and the diagnostics name.
    std::vector<std::string> stateNames(stateCount);
    for (size_t j = 0; j < declaredStates; ++j) {
        stateNames[j] = gct->loopStateName(j);
    }
    for (size_t j = 0; j < carried.size(); ++j) {
        stateNames[declaredStates + j] = carried[j].name;
    }

    // The condition and the body are regions of the current graph, built where they are
    // written. Their ports (the element, the values of the state) are Port nodes of the graph
    // that are not its parameters; the captured values are read where they are.
    // The names and types the region declares for its ports, from the ports themselves.
    const auto refreshPorts = [&](ArmBuild &part, bool withElement) {
        FunctionTypeFactory params;
        std::vector<runtime::gc_node_ref_t> ids;
        for (size_t i = 0; i < part.ports.size(); ++i) {
            const size_t state = i - (withElement ? 1 : 0);
            const std::string &name =
                withElement && i == 0 ? gct->loopElementName() : stateNames[state];
            ids.push_back(nodeIdOf(part.ports[i]));
            params.addNormArg(name, nodeTypeOf(part.ports[i]), false);
        }
        params.setExitType(Type::Void());
        part.graph->edit().setRegionPorts(part.region, ids);
        part.graph->edit().setRegionParams(part.region, params.build());
    };
    const auto declarePorts = [&](ArmBuild &part, bool withElement, const GCT::node_ptr_t &exec) {
        const auto add = [&](const std::string &name, Type *type) -> node_handle_t {
            node_handle_t port = currGraph_->addRegionPortNode(type, name);
            // A port belongs to its region, as the nodes that read it do.
            part.graph->edit().setNodeRegion(nodeIdOf(port), part.region);
            insertNode(name, port);
            if (recorder_) {
                recordDeclaration(name, port, exec);
            }
            part.ports.push_back(port);
            return port;
        };
        if (withElement) {
            add(gct->loopElementName(), elementType);
        }
        for (size_t j = 0; j < stateCount; ++j) {
            node_handle_t port = add(stateNames[j], stateTypes[j]);
            // The value a state's port stands for: what the round starts from, which is the state's
            // initial value. A loop that carries a stream is the loop that reads it, so the port
            // and the value handed to the loop are one value (noteMachinePortOrigin).
            if (j < initial.size()) {
                noteMachinePortOrigin(port, initial[j]);
            }
        }
        refreshPorts(part, withElement);
    };
    arm_ptr_t condPart;
    arm_ptr_t bodyPart;
    // The body's own names of the `var`s it carries are ports of its region, and a write to one of
    // them is the round's next value (loopStateAssignmentOf). The two tables of that record are
    // this loop's, and are put back as they were when it is built: a loop written in a body of its
    // own has its own. Which head a name belongs to is not a table of its own -- the SSA
    // environment keeps the port on the declaration, so the innermost head is the one a write
    // inside the body finds and the enclosing one is what a write after the loop finds.
    std::unordered_set<node_handle_t> enclosingPorts;
    for (const auto &entry : loopStatePorts_) {
        enclosingPorts.insert(entry.first);
    }
    for (const node_handle_t port : ssa_.portsInScope(nodeScope_)) {
        enclosingPorts.insert(port);
    }
    struct LoopCarriedScope {
        Builder &self;
        decltype(loopStatePorts_) outerPorts;
        decltype(loopStateWrites_) outerWrites;
        std::unordered_set<node_handle_t> enclosing;
        ~LoopCarriedScope() {
            // A record this loop wrote made *on an enclosing head's port* is that head's next
            // value, not this loop's: the port outlives this loop (a loop written in a loop's
            // body), so its record goes back with the value the body wrote. A port of this loop's
            // own body dies with the loop, and the record on it is dropped with the rest of the
            // tables.
            for (const auto &entry : self.loopStateWrites_) {
                if (!enclosing.contains(entry.first)) {
                    continue;
                }
                outerWrites[entry.first] = entry.second;
            }
            self.loopStatePorts_  = std::move(outerPorts);
            self.loopStateWrites_ = std::move(outerWrites);
        }
    } carriedScope{*this, loopStatePorts_, loopStateWrites_, std::move(enclosingPorts)};
    {
        ExpectedValueScope partScope(expectedValueType_, nullptr);
        if (camel::core::loopHasCondition(kind)) {
            condPart = buildArm(
                gct->loopCondition(),
                {},
                runtime::GCRegionKind::LoopCond,
                [&](ArmBuild &part) { declarePorts(part, false, gct->loopCondition()); });
            Type *condType = condPart->exitType();
            if (condType == nullptr || condType->code() != TypeCode::Bool) {
                diags_->of(SemanticDiag::LoopConditionNotBool)
                    .atOrigin(gct->loopCondition()->origin())
                    .commit(condType ? condType->toString() : std::string("void"));
                throw BuildAbortException();
            }
        }
        bodyPart =
            buildArm(gct->loopBody(), {}, runtime::GCRegionKind::LoopBody, [&](ArmBuild &part) {
                declarePorts(part, hasIterable, gct->loopBody());
                // The name of a carried `var` is the port the body reads and writes. A state the
                // declaration built as a value binds the name to the port as that value:
                // a read takes the port, an assignment rebinds it, and the write is the round's
                // next state (noteLoopStateWrite). A state that is still a cell hands the name to
                // the port and the port stands for the cell (loopStateAssignmentOf).
                const size_t base = hasIterable ? 1 : 0;
                for (size_t j = 0; j < carried.size(); ++j) {
                    const size_t at = base + declaredStates + j;
                    if (at >= part.ports.size()) {
                        continue;
                    }
                    carried[j].port = part.ports[at];
                    if (carried[j].ssa) {
                        // The body's `x` is this head's port, and a write of it is the round's next
                        // state of that port. Anything still live outside this body keeps its own
                        // declaration and its own value: which port a name belongs to is not
                        // recorded by name, so this loop's port speaks for the name while its body
                        // is built and the enclosing head's speaks for it again once the body is
                        // done.
                        ssa_.declarePort(
                            nodeScope_,
                            carried[j].name,
                            carried[j].port,
                            stateTypes[declaredStates + j]);
                    } else {
                        loopStatePorts_[part.ports[at]] = carried[j].cell;
                    }
                }
            });
    }
    if (deferredEmpty) {
        node_handle_t next = loopStateValueOf(bodyPart->value);
        if (bodyPart->returned ||
            !inferLoopStateFromAppend(bodyPart, condPart, gct, initial, stateTypes, 0, next)) {
            diags_->of(SemanticDiag::LoopStateWithoutType)
                .atOrigin(gct->loopInitial(0)->origin())
                .commit(
                    gct->loopStateName(0),
                    stateTypes.front() ? stateTypes.front()->toString() : std::string("void"));
            throw BuildAbortException();
        }
        stateType = stateTypes.front();
        refreshPorts(*bodyPart, hasIterable);
        if (condPart) {
            refreshPorts(*condPart, false);
        }
    }
    // The next state the body hands on: the declared states' next values followed by the value
    // each carried `var` was written with this round (design principles 3.5). A `var` the body did
    // not write this round -- its write is the write of a block of its own, or the name was
    // declared again in the body -- goes on as the value the round started from, which is the port.
    if (!carried.empty()) {
        auto &bodyDraft = currGraph_->transaction().edit();
        std::vector<node_handle_t> components(stateCount, nullptr);
        // Whether the declared states' next values came from the body's tuple fill (below). A
        // component that fill does not name is read out of the tuple, and that read has to stand
        // where the tuple stands -- in the body's region -- or it is a node of the outer region
        // between the loop's own nodes and the LOOP that ends a round.
        bool readFromFill      = false;
        node_handle_t returned = loopStateValueOf(bodyPart->value);
        if (declaredStates == 1) {
            components[0] = returned;
        } else if (declaredStates > 1) {
            // The declared states' next values are the tuple the body gave: a FILL whose payload
            // says which component each value fills (a constant component has no value node).
            bool fromFill = false;
            if (returned != nullptr && nodeIsKind(returned, runtime::GCNodeKind::Fill)) {
                const auto withIns = withInputsOf(returned);
                const auto payload = nodeDraftOf(returned)->payloadOf(nodeIdOf(returned));
                if (payload.size() >= sizeof(runtime::GCFillBody)) {
                    const auto *fill =
                        reinterpret_cast<const runtime::GCFillBody *>(payload.data());
                    const auto slots = fill->slots();
                    if (fill->fillKind == runtime::GCFillKind::Tuple &&
                        slots.size() == withIns.size()) {
                        fromFill = true;
                        for (size_t i = 0; i < slots.size(); ++i) {
                            const size_t slot = static_cast<size_t>(slots[i]);
                            if (slot >= declaredStates) {
                                fromFill = false;
                                break;
                            }
                            components[slot] = nodeDraftOf(returned)->ref(withIns[i]);
                        }
                        // A fill names a component only when that component is one of its
                        // with-inputs: a component its static template already holds -- the
                        // constant of `(xs, 0.25)`, as opposed to the value of `(xs, y)` -- has no
                        // value node and therefore no slot, so its table is shorter than the
                        // states. One it leaves out is read out of the tuple below, which is how a
                        // body that gave no fill at all is read: the state's next value is the
                        // tuple's component in both shapes, and the loop's next state is not
                        // missing it. What the fill does name keeps the value it hands on, so a
                        // state the body handed on in place is still that value.
                        readFromFill = fromFill;
                    }
                }
            }
            if (!fromFill) {
                // A body that gives its states as something else than a tuple written there: each
                // state's next value is read out of what it gave.
                for (size_t j = 0; j < declaredStates; ++j) {
                    if (returned == nullptr) {
                        const size_t at = j + (camel::core::loopHasIterable(kind) ? 1 : 0);
                        components[j] = at < bodyPart->ports.size() ? bodyPart->ports[at] : nullptr;
                        continue;
                    }
                    node_handle_t part =
                        currGraph_->addAccsNode(stateTypes[j], static_cast<uint32_t>(j));
                    linkNodes(LinkType::Norm, returned, part);
                    registerNodeOrigin(context_, part, gct->loopBody(), "gir.loop.state");
                    components[j] = part;
                }
            }
        }
        for (size_t j = 0; j < carried.size(); ++j) {
            node_handle_t next = carried[j].port;
            if (const auto written = loopStateWrites_.find(carried[j].port);
                written != loopStateWrites_.end()) {
                next = written->second;
            }
            components[declaredStates + j] = next;
        }
        node_handle_t result = components.empty() ? nullptr : components.front();
        Type *nextType       = stateCount == 1 ? stateTypes.front() : nullptr;
        if (stateCount > 1) {
            nextType = TupleType::create(stateTypes);
            std::vector<size_t> slots(stateCount);
            std::iota(slots.begin(), slots.end(), size_t{0});
            const uint32_t outerRegion = bodyDraft.insertRegion();
            bodyDraft.setInsertRegion(bodyPart->region);
            // The component a tuple fill does not name is read out of the tuple, and the read is
            // the body's own computation, so it is made in the body's region: a node of the outer
            // region that the body's next-state fill reads stands between the loop's nodes and the
            // LOOP that ends a round, and the plan refuses that graph
            // (execute/exec_plan.cpp, "not one contiguous range").
            if (readFromFill && returned != nullptr) {
                for (size_t j = 0; j < declaredStates; ++j) {
                    if (components[j] != nullptr) {
                        continue;
                    }
                    node_handle_t part =
                        currGraph_->addAccsNode(stateTypes[j], static_cast<uint32_t>(j));
                    linkNodes(LinkType::Norm, returned, part);
                    registerNodeOrigin(context_, part, gct->loopBody(), "gir.loop.state");
                    components[j] = part;
                }
            }
            result = currGraph_->addFillNode(
                nextType,
                runtime::makeFillPayload(runtime::GCFillKind::Tuple, slots));
            const slot_t empty = camel::core::rtdata::toSlot(
                ::Tuple::create(stateCount, camel::core::mm::autoSpace()));
            node_handle_t source = bodyDraft.ref(bodyDraft.materializeStaticValue(empty, nextType));
            linkNodes(LinkType::Norm, source, result);
            for (node_handle_t component : components) {
                GUARD(
                    component != nullptr,
                    "A loop state's next value is missing from the body's value.");
                linkNodes(LinkType::With, component, result);
            }
            bodyDraft.setInsertRegion(outerRegion);
            registerNodeOrigin(context_, result, gct->loopBody(), "gir.loop.state");
        }
        GUARD(result != nullptr, "A loop state's next value is missing from the body's value.");
        if (bodyPart->output != nullptr && bodyPart->output != bodyPart->value) {
            // A `sync` body's effects are ordered by the gate around the old value; the state the
            // round ends with comes after them (the same reading splitStructState takes).
            writeControlLink(bodyPart->output, result);
        }
        bodyPart->output = result;
        bodyPart->value  = result;
        bodyPart->type   = nextType;
        bodyDraft.setRegionResult(bodyPart->region, nodeIdOf(result));
        stateType = nextType;
    }
    // The body gives the next state: of the state's type, or a value that enters it (an
    // `int` where the state is an `int?`, design principles 3.8).
    Type *bodyType = bodyPart->exitType();
    if (bodyType == nullptr || !stateType->assignableFrom(bodyType)) {
        if (bodyType != nullptr && entersImplicitly(stateType, bodyType)) {
            coerceArmExit(bodyPart, stateType, gct->loopBody());
        } else {
            diags_->of(SemanticDiag::LoopBodyTypeMismatch)
                .atOrigin(gct->loopBody()->origin())
                .commit(
                    bodyType ? bodyType->toString() : std::string("void"),
                    stateType->toString());
            throw BuildAbortException();
        }
    }

    // A struct's fields are the loop state's components. When the state is one struct (or
    // one tuple) and the whole value never escapes the loop's parts -- every use of the state,
    // in the body and in the condition, is a part access -- the parts of that value become the
    // state's components, as if the loop had declared one state per part. The body hands back a
    // tuple of their next values, and the struct is rebuilt once, after the loop.
    //
    // What this buys is the in-place guarantee of design principles 10.5 for an array field: the
    // field is a state of its own, so `s.xs->append<v>` grows the loop's own array each round
    // instead of copying the struct's array. A part that never changes is still handed on, and
    // nothing observable changes: an array part is copied once when the loop is entered, and the
    // struct a user sees is the one built at the end. A body that uses the struct as a whole (a
    // call, a branch, `return s`) is left alone.
    struct SplitPart {
        Type *type = nullptr;
        std::vector<size_t> steps;
        std::string name;
        bool split = false;
        std::vector<node_handle_t> bodyNodes;
        std::vector<node_handle_t> condNodes;
        std::vector<SplitPart> children;
        size_t component = 0;
    };
    SplitPart splitRoot;
    std::vector<SplitPart *> splitComponents;
    bool splitApplied    = false;
    Type *splitValueType = nullptr;
    std::vector<bool> privateInitial;

    // A part access (`s.x`, `t.0`) of the value a node holds; added to the insert region.
    const auto addPartAccess = [&](Type *type, node_handle_t value, size_t step) -> node_handle_t {
        Type *partType = aggregatePartType(type, step);
        node_handle_t access =
            type->code() == TypeCode::Struct
                ? currGraph_->addAccsNode(partType, aggregatePartName(type, step))
                : currGraph_->addAccsNode(partType, static_cast<uint32_t>(step));
        linkNodes(LinkType::Norm, value, access);
        registerNodeOrigin(context_, access, gct, "gir.loop.split");
        return access;
    };

    const auto splitStructState = [&]() -> bool {
        if (k != 1 || stateTypes.size() != 1 || bodyPart->returned || bodyPart->value == nullptr ||
            bodyPart->output == nullptr || !isSplittableAggregate(stateType)) {
            return false;
        }
        auto &draft              = currGraph_->transaction().edit();
        const size_t statePortAt = camel::core::loopHasIterable(kind) ? 1 : 0;
        if (bodyPart->ports.size() != statePortAt + 1) {
            return false;
        }
        if (condPart != nullptr && condPart->ports.size() != 1) {
            return false;
        }
        const node_handle_t bodyStatePort = bodyPart->ports[statePortAt];
        const node_handle_t condStatePort = condPart != nullptr ? condPart->ports.front() : nullptr;

        std::function<bool(
            SplitPart &,
            const std::vector<node_handle_t> &,
            const std::vector<node_handle_t> &)>
            buildPart;
        buildPart = [&](SplitPart &part,
                        const std::vector<node_handle_t> &bodyNodes,
                        const std::vector<node_handle_t> &condNodes) -> bool {
            part.bodyNodes = bodyNodes;
            part.condNodes = condNodes;
            if (!isSplittableAggregate(part.type) || (bodyNodes.empty() && condNodes.empty())) {
                return true; // a component, or a part nothing reads: kept whole
            }
            // Every use has to be a part access. One use of the whole value (as a call's
            // argument, a branch's arm, another struct) makes the parts inseparable, so this
            // part stays whole and the split stops here.
            std::map<size_t, std::vector<node_handle_t>> bodyUses;
            std::map<size_t, std::vector<node_handle_t>> condUses;
            std::vector<node_handle_t> all = bodyNodes;
            all.insert(all.end(), condNodes.begin(), condNodes.end());
            for (node_handle_t source : all) {
                for (const auto users : {normUsersOf(source), withUsersOf(source)}) {
                    for (draft_node_ref_t userRef : users) {
                        node_handle_t user = nodeDraftOf(source)->ref(userRef);
                        if (user == nullptr) {
                            return true;
                        }
                        const auto index = accessedPartOf(user, part.type);
                        const auto norms = normInputsOf(user);
                        if (!index.has_value() || norms.size() != 1 ||
                            nodeDraftOf(user)->ref(norms.front()) != source) {
                            return true;
                        }
                        const uint32_t region = draft.nodeRegion(nodeIdOf(user));
                        std::map<size_t, std::vector<node_handle_t>> *uses = nullptr;
                        if (draft.regionWithin(region, bodyPart->region)) {
                            uses = &bodyUses;
                        } else if (
                            condPart != nullptr && draft.regionWithin(region, condPart->region)) {
                            uses = &condUses;
                        } else {
                            return true;
                        }
                        auto &list = (*uses)[*index];
                        if (std::find(list.begin(), list.end(), user) == list.end()) {
                            list.push_back(user);
                        }
                    }
                }
            }
            part.split         = true;
            const size_t count = aggregatePartCount(part.type);
            for (size_t i = 0; i < count; ++i) {
                SplitPart child;
                child.type  = aggregatePartType(part.type, i);
                child.steps = part.steps;
                child.steps.push_back(i);
                const std::string stepName = aggregatePartName(part.type, i);
                child.name        = part.name.empty() ? stepName : part.name + "." + stepName;
                const auto bodyIt = bodyUses.find(i);
                const auto condIt = condUses.find(i);
                const std::vector<node_handle_t> none;
                if (!buildPart(
                        child,
                        bodyIt == bodyUses.end() ? none : bodyIt->second,
                        condIt == condUses.end() ? none : condIt->second)) {
                    return false;
                }
                part.children.push_back(std::move(child));
            }
            return true;
        };

        const Type *rootStateType = stateType;
        splitRoot.type            = stateType;
        splitRoot.name            = stateNames.front();
        splitRoot.steps.clear();
        splitRoot.children.clear();
        splitRoot.split = false;
        const std::vector<node_handle_t> noCond;
        const std::vector<node_handle_t> condRoot =
            condStatePort != nullptr ? std::vector<node_handle_t>{condStatePort} : noCond;
        if (!buildPart(splitRoot, {bodyStatePort}, condRoot) || !splitRoot.split) {
            return false;
        }
        splitComponents.clear();
        std::function<void(SplitPart &)> collectParts = [&](SplitPart &part) {
            if (!part.split) {
                part.component = splitComponents.size();
                splitComponents.push_back(&part);
                return;
            }
            for (SplitPart &child : part.children) {
                collectParts(child);
            }
        };
        collectParts(splitRoot);
        if (splitComponents.empty()) {
            return false;
        }
        std::vector<Type *> leafTypes;
        for (SplitPart *component : splitComponents) {
            leafTypes.push_back(component->type);
        }
        Type *valueType = leafTypes.size() == 1 ? leafTypes.front() : TupleType::create(leafTypes);

        const uint32_t outerRegion = draft.insertRegion();

        // The body's ports: the element, then one port per component, in the same order as the
        // states. The old state port goes; nothing but the accesses of its parts read it.
        draft.setInsertRegion(bodyPart->region);
        std::vector<node_handle_t> bodyPorts(bodyPart->ports.begin(), bodyPart->ports.end());
        bodyPorts.erase(bodyPorts.begin() + static_cast<std::ptrdiff_t>(statePortAt));
        std::vector<node_handle_t> bodyComponentPorts;
        for (SplitPart *component : splitComponents) {
            node_handle_t port = currGraph_->addRegionPortNode(component->type, component->name);
            draft.setNodeRegion(nodeIdOf(port), bodyPart->region);
            bodyComponentPorts.push_back(port);
            bodyPorts.push_back(port);
        }
        bodyPart->ports = bodyPorts;
        std::vector<node_handle_t> condComponentPorts;
        if (condPart != nullptr) {
            std::vector<node_handle_t> condPorts(condPart->ports.begin(), condPart->ports.end());
            condPorts.erase(condPorts.begin());
            for (SplitPart *component : splitComponents) {
                node_handle_t port =
                    currGraph_->addRegionPortNode(component->type, component->name);
                draft.setNodeRegion(nodeIdOf(port), condPart->region);
                condComponentPorts.push_back(port);
                condPorts.push_back(port);
            }
            condPart->ports = condPorts;
        }

        // The next value of each component, worked out from the body's next struct: a part of a
        // struct FILL is the value written there (so an `append` stays the next state itself,
        // and can grow in place); a part the FILL does not write, or a struct given by another
        // value (a call, a branch), is read from it.
        std::vector<node_handle_t> nextValues(splitComponents.size(), nullptr);
        std::function<void(const SplitPart &, node_handle_t, bool)> assignNext;
        assignNext = [&](const SplitPart &part, node_handle_t value, bool unchanged) {
            if (!part.split) {
                nextValues[part.component] = unchanged ? nullptr : value;
                return;
            }
            bool same = unchanged || value == nullptr;
            for (node_handle_t node : part.bodyNodes) {
                if (node == value) {
                    same = true;
                    break;
                }
            }
            for (const SplitPart &child : part.children) {
                const size_t step        = child.steps.back();
                node_handle_t childValue = nullptr;
                if (!same) {
                    childValue = projectPart(value, part.type, step);
                    if (childValue == nullptr) {
                        childValue = addPartAccess(part.type, value, step);
                    }
                }
                assignNext(child, childValue, same);
            }
        };
        assignNext(splitRoot, bodyPart->value, false);

        std::vector<node_handle_t> resolved;
        for (size_t i = 0; i < splitComponents.size(); ++i) {
            resolved.push_back(nextValues[i] != nullptr ? nextValues[i] : bodyComponentPorts[i]);
        }
        node_handle_t result = nullptr;
        if (splitComponents.size() == 1) {
            result = resolved.front();
        } else {
            const size_t count = resolved.size();
            std::vector<size_t> slots(count);
            std::iota(slots.begin(), slots.end(), size_t{0});
            result = currGraph_->addFillNode(
                valueType,
                runtime::makeFillPayload(runtime::GCFillKind::Tuple, slots));
            const slot_t empty =
                camel::core::rtdata::toSlot(::Tuple::create(count, camel::core::mm::autoSpace()));
            node_handle_t source = draft.ref(draft.materializeStaticValue(empty, valueType));
            linkNodes(LinkType::Norm, source, result);
            for (node_handle_t value : resolved) {
                linkNodes(LinkType::With, value, result);
            }
            registerNodeOrigin(context_, result, gct->loopBody(), "gir.loop.split");
        }
        if (bodyPart->output != bodyPart->value) {
            // A `sync` body's effects are ordered by the gate around the old value; the new
            // state comes after them.
            writeControlLink(bodyPart->output, result);
        }
        bodyPart->output = result;
        bodyPart->value  = result;
        bodyPart->type   = valueType;
        draft.setRegionResult(bodyPart->region, nodeIdOf(result));

        // The loop starts from the parts of its first value. The accesses are made outside the
        // loop (the insert region is the one around it).
        draft.setInsertRegion(outerRegion);
        std::vector<node_handle_t> projections;
        projections.reserve(splitComponents.size());
        for (SplitPart *component : splitComponents) {
            node_handle_t value = initial.front();
            Type *current       = const_cast<Type *>(rootStateType);
            for (size_t step : component->steps) {
                value   = addPartAccess(current, value, step);
                current = aggregatePartType(current, step);
            }
            projections.push_back(value);
        }

        // The reads of the old state are now reads of its parts. This is done after the next
        // values are written: a next value that is a read of the state (`n: s.n`) becomes the
        // part itself, which is the value it had.
        for (size_t i = 0; i < splitComponents.size(); ++i) {
            for (node_handle_t node : splitComponents[i]->bodyNodes) {
                draft.replaceAllValueUses(nodeIdOf(node), nodeIdOf(bodyComponentPorts[i]));
            }
            for (node_handle_t node : splitComponents[i]->condNodes) {
                draft.replaceAllValueUses(nodeIdOf(node), nodeIdOf(condComponentPorts[i]));
            }
        }

        stateTypes = std::move(leafTypes);
        stateNames.clear();
        for (SplitPart *component : splitComponents) {
            stateNames.push_back(component->name);
        }
        initial = std::move(projections);
        privateInitial.assign(splitComponents.size(), false);
        for (size_t i = 0; i < splitComponents.size(); ++i) {
            privateInitial[i] = splitComponents[i]->type != nullptr &&
                                splitComponents[i]->type->code() == TypeCode::Array;
        }
        stateType      = valueType;
        splitValueType = valueType;
        splitApplied   = true;
        refreshPorts(*bodyPart, hasIterable);
        if (condPart != nullptr) {
            refreshPorts(*condPart, false);
        }
        return true;
    };
    splitStructState();
    if (splitApplied) {
        // The empty-array collection is a slot beside one state; a struct state's parts are not
        // empty literals, so there is nothing to collect once it is split.
        emptyInitial.assign(stateTypes.size(), false);
    }

    markLoopStateInPlace(bodyPart, gct, initial, privateInitial);
    // An empty-array state whose only use is `append` also becomes a collection slot: the loop
    // then hands out the appended values as one array, built by the driver in one pass, beside
    // the state the program sees (design principles 3.10; the tape autodiff and the exporter
    // read). The state stays the program's value and its gradient channel; the slot is
    // internal, and the FILL below hides it from the loop's users.
    const std::vector<Type *> collected =
        collectLoopAppends(bodyPart, gct, stateTypes, emptyInitial);
    Type *loopValueType = splitApplied ? splitValueType : stateType;
    if (!collected.empty()) {
        std::vector<Type *> components = stateTypes;
        for (Type *element : collected) {
            components.push_back(ArrayType::create(element));
        }
        loopValueType = TupleType::create(components);
    }

    node_handle_t loopNode = currGraph_->addRegionLoopNode(kind, loopValueType);
    if (intervalIterates) {
        // The sequence the loop iterates is the pair of the interval's bounds, not an array
        // (camel::core::LoopIterableForm::Interval): a back end takes the element of a round from
        // the bounds and the round number, so nothing the size of the interval is ever made.
        currGraph_->edit().setLoopIterableInterval(nodeIdOf(loopNode));
    }
    if (!collected.empty()) {
        currGraph_->edit().setLoopCollectionCount(
            nodeIdOf(loopNode),
            static_cast<uint32_t>(collected.size()));
    }
    registerNodeOrigin(context_, loopNode, gct, "gir.loop");
    stampPermissions(loopNode);
    if (sequence != nullptr) {
        linkNodes(LinkType::Norm, sequence, loopNode);
    }
    for (node_handle_t node : initial) {
        linkNodes(LinkType::Norm, node, loopNode);
    }
    // The With inputs of the node are the results of its parts (the body's, then the
    // condition's) and the slot the rounds are counted in; the parts are its regions.
    std::vector<arm_ptr_t> parts{bodyPart};
    linkNodes(LinkType::With, bodyPart->output, loopNode);
    if (condPart) {
        parts.push_back(condPart);
        linkNodes(LinkType::With, condPart->output, loopNode);
    }
    linkNodes(LinkType::With, currGraph_->addRegionPortNode(Type::Int64(), ""), loopNode);
    auto &draft = currGraph_->transaction().edit();
    draft.setRegionOwner(bodyPart->region, nodeIdOf(loopNode), 1);
    draft.setRegionResult(bodyPart->region, nodeIdOf(bodyPart->output));
    if (condPart) {
        draft.setRegionOwner(condPart->region, nodeIdOf(loopNode), 0);
        draft.setRegionResult(condPart->region, nodeIdOf(condPart->output));
    }

    // Running the loop is like entering an arm: it uses every variable its parts used and
    // writes those they wrote.
    std::vector<node_handle_t> capturedWrites;
    mergeArmVariables(parts, loopNode, capturedWrites);
    const auto originOf = [&](node_handle_t variable) {
        const auto origin = captureOriginOf_.find(variable);
        return origin != captureOriginOf_.end() ? origin->second : variable;
    };
    std::vector<DraftGraphBuilder *> callees;
    for (const arm_ptr_t &part : parts) {
        callees.insert(callees.end(), part->callees.begin(), part->callees.end());
    }
    if (ArmBuild *outer = activeArm()) {
        // The parts of this loop are part of the arm it is in.
        outer->callees.insert(outer->callees.end(), callees.begin(), callees.end());
    }
    // What the loop's parts write is not an effect: a write to a `var` is a value (design
    // principles 3.1), and the body's writes are the states the LOOP hands on. What is left to
    // report here is a write by a callee the parts call to a variable that callee captured: that
    // write is not handed back where the call stands (reportIgnoredCapturedWrites).
    if (!capturedWrites.empty() && !synced_) {
        std::vector<node_handle_t> origins;
        for (node_handle_t variable : capturedWrites) {
            origins.push_back(originOf(variable));
        }
        unit_->pendingIgnoredWrites.push_back(
            {callees, origins, gct->origin(), "The loop", "its body writes"});
    }

    if (synced_) {
        // The loop is a statement of the block, like a call: the statement written before it
        // follows into it by a sync edge (the control edge the kind names the same pair with goes
        // beside it).
        linkSyncStatement(lastSyncedNode_, loopNode);
        lastSyncedNode_ = loopNode;
        chainedNodes_.insert(loopNode);
    }
    // The program sees the states its head declares: not the collection slots beside them, and not
    // the `var`s the body wrote, which are the loop's own state and no value of the program's (a
    // `loop i = 0 while …` whose body writes an outer `var` is still worth its `i`). One state is
    // its own value; several are a tuple, whose FILL reads the state components and leaves the
    // rest to autodiff and the exporter.
    std::vector<Type *> visibleStateTypes(stateTypes.begin(), stateTypes.begin() + declaredStates);
    Type *visibleStateType  = declaredStates == 1
                                  ? stateTypes.front()
                                  : static_cast<Type *>(TupleType::create(visibleStateTypes));
    node_handle_t loopValue = loopNode;
    if (!collected.empty() || !carried.empty()) {
        if (declaredStates == 1) {
            loopValue = currGraph_->addAccsNode(visibleStateType, 0);
            linkNodes(LinkType::Norm, loopNode, loopValue);
            registerNodeOrigin(context_, loopValue, gct, "gir.loop.value");
        } else {
            std::vector<size_t> slots(declaredStates);
            std::iota(slots.begin(), slots.end(), size_t{0});
            loopValue = currGraph_->addFillNode(
                visibleStateType,
                runtime::makeFillPayload(runtime::GCFillKind::Tuple, slots));
            const slot_t empty = camel::core::rtdata::toSlot(
                ::Tuple::create(declaredStates, camel::core::mm::autoSpace()));
            node_handle_t source = currGraph_->view().ref(
                currGraph_->edit().materializeStaticValue(empty, visibleStateType));
            linkNodes(LinkType::Norm, source, loopValue);
            for (size_t j = 0; j < declaredStates; ++j) {
                node_handle_t component =
                    currGraph_->addAccsNode(stateTypes[j], static_cast<uint32_t>(j));
                linkNodes(LinkType::Norm, loopNode, component);
                linkNodes(LinkType::With, component, loopValue);
            }
            registerNodeOrigin(context_, loopValue, gct, "gir.loop.value");
        }
    }
    // A `var` the body wrote is a state of the loop, so a read written after the loop reads the
    // state the loop hands out: the ACCS of the loop's value, which is what the `let` writing's
    // destructuring is. No `:var/get` is made, and nothing of the `var`'s unit is left to read
    // (dropCarriedVariableUnits).
    //
    // A state the declaration built as a value is the head's phi for the name: the name
    // is rebound to what the head hands out, so every read after the loop takes that value and no
    // cell is involved anywhere. The rebinding is a write of the name in the region the loop is
    // written in, so the head of a loop *around* this one takes it too (noteLoopStateWrite), and an
    // arm around it has the join carry it.
    for (size_t j = 0; j < carried.size(); ++j) {
        const size_t index  = declaredStates + j;
        node_handle_t value = loopNode;
        if (stateCount > 1) {
            value = currGraph_->addAccsNode(stateTypes[index], static_cast<uint32_t>(index));
            linkNodes(LinkType::Norm, loopNode, value);
            registerNodeOrigin(context_, value, gct, "gir.loop.value");
        }
        if (carried[j].ssa) {
            // The write the head hands out is recorded where the loop is written: on the head of a
            // loop around this one, if there is one, so this loop's write is that loop's next state
            // too. This loop's own port went back with the body it was declared in, so the name
            // here is the enclosing declaration's again -- the one whose port, if any, takes this
            // record.
            noteLoopStateWrite(carried[j].name, value);
            ssa_.rebind(nodeScope_, carried[j].name, value, armWritesHere());
        } else {
            carriedVersions_[carried[j].cell] = value;
        }
    }
    if (splitApplied) {
        // The programme sees the struct, not its parts: it is rebuilt from the loop's value,
        // once, in the layout of the type the state had (nested structs included). Nothing of
        // the loop's own arrays is copied here; the parts are read out of the loop's value.
        std::vector<node_handle_t> componentValues(splitComponents.size(), nullptr);
        for (size_t j = 0; j < splitComponents.size(); ++j) {
            if (splitComponents.size() == 1) {
                componentValues[j] = loopValue;
                continue;
            }
            node_handle_t access =
                currGraph_->addAccsNode(splitComponents[j]->type, static_cast<uint32_t>(j));
            linkNodes(LinkType::Norm, loopValue, access);
            registerNodeOrigin(context_, access, gct, "gir.loop.value");
            componentValues[j] = access;
        }
        std::function<node_handle_t(const SplitPart &)> rebuildPart =
            [&](const SplitPart &part) -> node_handle_t {
            if (!part.split) {
                return componentValues[part.component];
            }
            std::vector<node_handle_t> values;
            for (const SplitPart &child : part.children) {
                values.push_back(rebuildPart(child));
            }
            const size_t count = values.size();
            std::vector<size_t> slots(count);
            std::iota(slots.begin(), slots.end(), size_t{0});
            const bool isStruct = part.type->code() == TypeCode::Struct;
            node_handle_t fill  = currGraph_->addFillNode(
                part.type,
                runtime::makeFillPayload(
                    isStruct ? runtime::GCFillKind::Struct : runtime::GCFillKind::Tuple,
                    slots));
            const slot_t empty = isStruct
                                     ? camel::core::rtdata::toSlot(
                                           ::Struct::create(count, camel::core::mm::autoSpace()))
                                     : camel::core::rtdata::toSlot(
                                           ::Tuple::create(count, camel::core::mm::autoSpace()));
            node_handle_t source =
                currGraph_->view().ref(currGraph_->edit().materializeStaticValue(empty, part.type));
            linkNodes(LinkType::Norm, source, fill);
            for (node_handle_t value : values) {
                linkNodes(LinkType::With, value, fill);
            }
            registerNodeOrigin(context_, fill, gct, "gir.loop.split");
            return fill;
        };
        loopValue = rebuildPart(splitRoot);
    }
    LEAVE("LOOP");
    return loopValue;
}

// `for x in s` where `s` is a stream. The stream's value is its step function
// (StreamType::stepType), so a round is one pull of it, and the graph already has the loop that
// takes one step per round: the `while`, whose condition and body are regions and whose state is
// what the next round starts from. The loop is therefore written here as that `while`, with the
// state the program wrote carried beside the pull:
//
//   state (next, acc); next is the last pull, acc the program's state
//   entering:   (s(), a0)
//   condition:  next != null, and the condition the head wrote after it
//   body:       the element is next.0, the program's body gives acc', and the round ends by
//               pulling next.1() into the next next
//
// `next` is the last pull -- the element with the rest of the stream, or `null` -- and the body
// reads the element out of it and pulls the next one from the rest at its end, after the
// statements it wrote ran. `for ... while c` folds `c` into the condition, after the test that
// the pull has a value: the stream ending is the loop ending, and `c` is asked only while there
// is something to iterate. Nothing here is a new node kind, a new region kind or a new loop
// form: a back end sees the LOOP node it always did, and the driver the same.
node_handle_t Builder::buildStreamLoop(
    const GCT::node_ptr_t &gct, node_handle_t sequence, const std::vector<node_handle_t> &initial,
    const std::vector<Type *> &stateTypes) {
    using camel::core::LoopKind;
    using camel::core::oper::TagIsNull;
    using camel::core::oper::TagNullValue;
    const LoopKind kind    = gct->loopKind();
    const size_t k         = initial.size();
    StreamType *stream     = tt::as_ptr<StreamType>(valueTypeOf(nodeTypeOf(sequence)));
    FunctionType *stepType = stream->stepType();
    Type *pullType         = stepType->exitType();
    Type *elementType      = stream->elemType();
    // A `for` over a stream is one consumer of it (design principles 5.4: a stream is read once on
    // each execution path), and the loop reads the stream through the data edge the call below
    // writes, so the check on the built graph sees this read like any other.

    // The names of the machine's parts. They are no names a program writes -- the names of the
    // head are the states and the element -- and the parts read the round through them.
    const std::string streamName = "$stream";
    const std::string doneName   = "$done";
    std::vector<std::string> stateNames(k);
    for (size_t j = 0; j < k; ++j) {
        stateNames[j] = gct->loopStateName(j);
    }

    // The state of the loop: the stream this round pulls from, whether that stream is over, then
    // the state the program wrote. The pull itself stands inside the round, after the condition:
    // every round judges the condition first and takes its element afterwards (design principles
    // 3.10), and a pull may have effects, so the round a condition refuses reads nothing.
    std::vector<Type *> machineTypes{stream, Type::Bool()};
    machineTypes.insert(machineTypes.end(), stateTypes.begin(), stateTypes.end());
    Type *machineType = static_cast<Type *>(TupleType::create(machineTypes));
    Type *stateType =
        k == 1 ? stateTypes.front() : static_cast<Type *>(TupleType::create(stateTypes));

    // One pull, where the round stands. The value of `T..` is that function value, so the call
    // reads the step signature and nothing is converted.
    const auto pullOf = [&](node_handle_t value, const GCT::node_ptr_t &at) {
        node_handle_t call = addCallSite(currGraph_, pullType);
        linkNodes(LinkType::With, value, call);
        registerNodeOrigin(context_, call, at, "gir.loop.pull");
        return call;
    };
    // A value on its way into the loop's state: one of the state's type as it is, a value that
    // enters it (an `int` where the state is an `int?`, design principles 3.8) converted, and
    // anything else the body's mismatch.
    const auto intoState = [&](node_handle_t value, Type *type) {
        if (value == nullptr || type == nullptr) {
            return value;
        }
        Type *valueType = valueTypeOf(nodeTypeOf(value));
        if (valueType != nullptr && type->assignableFrom(valueType)) {
            return value;
        }
        if (valueType != nullptr && entersImplicitly(type, valueType)) {
            if (node_handle_t coerced = coerceTo(value, type, gct->loopBody());
                coerced != nullptr) {
                return coerced;
            }
        }
        diags_->of(SemanticDiag::LoopBodyTypeMismatch)
            .atOrigin(gct->loopBody()->origin())
            .commit(valueType ? valueType->toString() : std::string("void"), type->toString());
        throw BuildAbortException();
    };
    // The state the next round starts from, in the order the loop's state holds it.
    const auto machineStateOf = [&](const std::vector<node_handle_t> &components) {
        std::vector<size_t> slots(components.size());
        std::iota(slots.begin(), slots.end(), size_t{0});
        node_handle_t fill = currGraph_->addFillNode(
            machineType,
            runtime::makeFillPayload(runtime::GCFillKind::Tuple, slots));
        const slot_t empty = camel::core::rtdata::toSlot(
            ::Tuple::create(components.size(), camel::core::mm::autoSpace()));
        node_handle_t source =
            currGraph_->view().ref(currGraph_->edit().materializeStaticValue(empty, machineType));
        linkNodes(LinkType::Norm, source, fill);
        for (node_handle_t component : components) {
            linkNodes(LinkType::With, component, fill);
        }
        registerNodeOrigin(context_, fill, gct, "gir.loop.state");
        return fill;
    };
    const auto booleanConstant = [&](bool each) {
        node_handle_t node = currGraph_->addStaticDataNode(
            tt::as_shared<Data>(std::make_shared<PrimaryData<bool>>(each)));
        registerNodeOrigin(context_, node, gct, "gir.data");
        return node;
    };

    // Entering the loop takes the stream as it is: the first pull waits for the first round, so a
    // loop whose condition refuses every round reads nothing at all.
    std::vector<node_handle_t> machineInitial{sequence, booleanConstant(false)};
    machineInitial.insert(machineInitial.end(), initial.begin(), initial.end());

    // The parts declare the pull and the states they read; the element is bound inside the body,
    // out of the pull, so that the body reads it as the name the program wrote.
    const auto refreshPorts = [&](ArmBuild &part) {
        FunctionTypeFactory params;
        std::vector<runtime::gc_node_ref_t> ids;
        for (size_t i = 0; i < part.ports.size(); ++i) {
            const std::string &name = i == 0 ? streamName : (i == 1 ? doneName : stateNames[i - 2]);
            params.addNormArg(name, nodeTypeOf(part.ports[i]), false);
            ids.push_back(nodeIdOf(part.ports[i]));
        }
        params.setExitType(Type::Void());
        part.graph->edit().setRegionPorts(part.region, ids);
        part.graph->edit().setRegionParams(part.region, params.build());
    };
    const auto declarePorts = [&](ArmBuild &part, const GCT::node_ptr_t &exec) {
        // The value each port stands for: the machine is handed the sequence, the flag that says it
        // is over and the states, in that order (`machineInitial`), and inside a round those values
        // are the round's ports. The port is recorded against what it stands for, so the value the
        // program wrote outside and the slot that carries it inside are one value (design
        // principles 3.7 C1, `noteMachinePortOrigin`).
        size_t handed  = 0;
        const auto add = [&](const std::string &name, Type *type) {
            node_handle_t port = currGraph_->addRegionPortNode(type, name);
            part.graph->edit().setNodeRegion(nodeIdOf(port), part.region);
            insertNode(name, port);
            if (handed < machineInitial.size()) {
                noteMachinePortOrigin(port, machineInitial[handed]);
            }
            ++handed;
            if (recorder_) {
                recordDeclaration(name, port, exec);
            }
            part.ports.push_back(port);
        };
        add(streamName, stream);
        add(doneName, Type::Bool());
        for (size_t j = 0; j < k; ++j) {
            add(stateNames[j], stateTypes[j]);
        }
        refreshPorts(part);
    };

    arm_ptr_t condPart;
    arm_ptr_t bodyPart;
    {
        ExpectedValueScope partScope(expectedValueType_, nullptr);
        // The condition of the loop: where the stream is not over, the condition of the head --
        // and a `for` head wrote none, so there it is `true` and nothing is asked. A round whose
        // stream is over ends the loop without asking the head's condition again, so a condition
        // is asked once per round that has an element and once for the round that finds the end,
        // as the `while` of the array form is asked.
        //
        // The parts the condition is made of stand where the loop is: a `for` head wrote no
        // condition, so the body is what they take their position from.
        const GCT::node_ptr_t condExec =
            camel::core::loopHasCondition(kind) ? gct->loopCondition() : gct->loopBody();
        const auto conditionArms = [&]() -> node_handle_t {
            node_handle_t over = *nodeAt(doneName);
            std::vector<arm_ptr_t> arms(2);
            // The stream is over: the loop ends without asking the condition the head wrote.
            arms[0] = buildArm(condExec, [&] { return booleanConstant(false); });
            arms[1] = buildArm(condExec, [&] {
                return camel::core::loopHasCondition(kind) ? visitBodyNode(gct->loopCondition())
                                                           : booleanConstant(true);
            });
            return buildBranch(condExec, over, &arms);
        };
        condPart =
            buildArm(condExec, conditionArms, runtime::GCRegionKind::LoopCond, [&](ArmBuild &part) {
                declarePorts(part, condExec);
            });
        Type *condType = condPart->exitType();
        if (condType == nullptr || condType->code() != TypeCode::Bool) {
            diags_->of(SemanticDiag::LoopConditionNotBool)
                .atOrigin(condExec->origin())
                .commit(condType ? condType->toString() : std::string("void"));
            throw BuildAbortException();
        }
        // The body of the loop: the pull of this round -- the condition has already held, so this
        // is where the step function is called -- and then, where the pull answered an element,
        // the element, the program's body and the state of the next round.
        bodyPart = buildArm(
            gct->loopBody(),
            [&]() -> node_handle_t {
                node_handle_t pull         = pullOf(*nodeAt(streamName), gct->loopBody());
                const std::string pullTemp = bindTemporary(pull);
                node_handle_t isNull       = applyTagOperator(TagIsNull, pullTemp, gct->loopBody());
                std::vector<arm_ptr_t> arms(2);
                // The pull answered `null`: the stream is over. The program's body does not run,
                // the stream and the state stay as they are, and the round that begins after it
                // ends the loop without asking the condition again.
                arms[0] = buildArm(gct->loopBody(), [&] {
                    std::vector<node_handle_t> next{*nodeAt(streamName), booleanConstant(true)};
                    for (size_t j = 0; j < k; ++j) {
                        next.push_back(*nodeAt(stateNames[j]));
                    }
                    return machineStateOf(next);
                });
                arms[1] = buildArm(gct->loopBody(), [&] {
                    // The element this round is about and the rest of the stream, taken out of
                    // the pull that has just answered.
                    node_handle_t pair = applyTagOperator(TagNullValue, pullTemp, gct->loopBody());
                    node_handle_t element = currGraph_->addAccsNode(elementType, 0);
                    linkNodes(LinkType::Norm, pair, element);
                    registerNodeOrigin(context_, element, gct->loopBody(), "gir.loop.element");
                    insertNode(gct->loopElementName(), element);
                    if (recorder_) {
                        recordDeclaration(gct->loopElementName(), element, gct->loopBody());
                    }
                    node_handle_t rest = currGraph_->addAccsNode(stream, 1);
                    linkNodes(LinkType::Norm, pair, rest);
                    registerNodeOrigin(context_, rest, gct->loopBody(), "gir.loop.rest");

                    // What the program's body gives is the state of the next round.
                    node_handle_t state =
                        intoState(loopStateValueOf(visitBodyNode(gct->loopBody())), stateType);
                    if (state == nullptr) {
                        diags_->of(SemanticDiag::LoopBodyTypeMismatch)
                            .atOrigin(gct->loopBody()->origin())
                            .commit(std::string("void"), stateType->toString());
                        throw BuildAbortException();
                    }
                    // The body runs after the pull it reads its element from.
                    writeControlLink(pull, state);
                    std::vector<node_handle_t> next{rest, booleanConstant(false)};
                    if (k == 1) {
                        next.push_back(state);
                    } else {
                        for (size_t j = 0; j < k; ++j) {
                            node_handle_t component =
                                currGraph_->addAccsNode(stateTypes[j], static_cast<uint32_t>(j));
                            linkNodes(LinkType::Norm, state, component);
                            registerNodeOrigin(
                                context_,
                                component,
                                gct->loopBody(),
                                "gir.loop.state");
                            next.push_back(component);
                        }
                    }
                    return machineStateOf(next);
                });
                return buildBranch(gct->loopBody(), isNull, &arms);
            },
            runtime::GCRegionKind::LoopBody,
            [&](ArmBuild &part) { declarePorts(part, gct->loopBody()); });
    }

    node_handle_t loopNode = currGraph_->addRegionLoopNode(LoopKind::While, machineType);
    registerNodeOrigin(context_, loopNode, gct, "gir.loop");
    stampPermissions(loopNode);
    for (node_handle_t value : machineInitial) {
        linkNodes(LinkType::Norm, value, loopNode);
    }
    // The With inputs of the node are the results of its parts (the body's, then the
    // condition's) and the slot the rounds are counted in; the parts are its regions.
    std::vector<arm_ptr_t> parts{bodyPart};
    linkNodes(LinkType::With, bodyPart->output, loopNode);
    if (condPart) {
        parts.push_back(condPart);
        linkNodes(LinkType::With, condPart->output, loopNode);
    }
    linkNodes(LinkType::With, currGraph_->addRegionPortNode(Type::Int64(), ""), loopNode);
    auto &draft = currGraph_->transaction().edit();
    draft.setRegionOwner(bodyPart->region, nodeIdOf(loopNode), 1);
    draft.setRegionResult(bodyPart->region, nodeIdOf(bodyPart->output));
    if (condPart) {
        draft.setRegionOwner(condPart->region, nodeIdOf(loopNode), 0);
        draft.setRegionResult(condPart->region, nodeIdOf(condPart->output));
    }

    // Running the loop is like entering an arm: it uses every variable its parts used and
    // writes those they wrote.
    std::vector<node_handle_t> capturedWrites;
    mergeArmVariables(parts, loopNode, capturedWrites);
    const auto originOf = [&](node_handle_t variable) {
        const auto origin = captureOriginOf_.find(variable);
        return origin != captureOriginOf_.end() ? origin->second : variable;
    };
    std::vector<DraftGraphBuilder *> callees;
    for (const arm_ptr_t &part : parts) {
        callees.insert(callees.end(), part->callees.begin(), part->callees.end());
    }
    if (ArmBuild *outer = activeArm()) {
        outer->callees.insert(outer->callees.end(), callees.begin(), callees.end());
    }
    // What the loop's parts write is not an effect (design principles 3.1): a write to a `var` is
    // a value, so only a write by a callee the parts call to a variable that callee captured is
    // reported here (reportIgnoredCapturedWrites).
    if (!capturedWrites.empty() && !synced_) {
        std::vector<node_handle_t> origins;
        for (node_handle_t variable : capturedWrites) {
            origins.push_back(originOf(variable));
        }
        unit_->pendingIgnoredWrites.push_back(
            {callees, origins, gct->origin(), "The loop", "its body writes"});
    }
    if (synced_) {
        // The loop is a statement of the block, like a call: the statement written before it
        // follows into it by a sync edge (the control edge the kind names the same pair with goes
        // beside it).
        linkSyncStatement(lastSyncedNode_, loopNode);
        lastSyncedNode_ = loopNode;
        chainedNodes_.insert(loopNode);
    }

    // The program sees the state it wrote, not the stream and the flag beside it: one state is
    // its own value, several are a tuple of them.
    node_handle_t loopValue = nullptr;
    if (k == 1) {
        loopValue = currGraph_->addAccsNode(stateTypes.front(), 2);
        linkNodes(LinkType::Norm, loopNode, loopValue);
        registerNodeOrigin(context_, loopValue, gct, "gir.loop.value");
    } else {
        std::vector<size_t> slots(k);
        std::iota(slots.begin(), slots.end(), size_t{0});
        loopValue = currGraph_->addFillNode(
            stateType,
            runtime::makeFillPayload(runtime::GCFillKind::Tuple, slots));
        const slot_t empty =
            camel::core::rtdata::toSlot(::Tuple::create(k, camel::core::mm::autoSpace()));
        // Two steps, because the two halves are not the same side: writing the static value is an
        // edit, and reading the node it produced is a view.
        const auto emptyNode = currGraph_->edit().materializeStaticValue(empty, stateType);
        node_handle_t source = currGraph_->view().ref(emptyNode);
        linkNodes(LinkType::Norm, source, loopValue);
        for (size_t j = 0; j < k; ++j) {
            node_handle_t component =
                currGraph_->addAccsNode(stateTypes[j], static_cast<uint32_t>(j + 2));
            linkNodes(LinkType::Norm, loopNode, component);
            linkNodes(LinkType::With, component, loopValue);
        }
        registerNodeOrigin(context_, loopValue, gct, "gir.loop.value");
    }
    return loopValue;
}

node_handle_t Builder::visitAnnoNode(const GCT::node_ptr_t &gct) {
    ENTER("ANNO");
    GUARD(gct->size() == 1, "ANNO node should have exactly one child.");
    node_handle_t node = valueNodeOf(visit(gct->at(0)));
    LEAVE("ANNO");
    return node;
}

node_handle_t Builder::visitExitNode(const GCT::node_ptr_t &gct) {
    ENTER("EXIT");
    // A `yield` is a node of the construction tree: the generator body is built here, and the
    // machine splits the body into arms at the `yield` it finds -- and it can only find what the
    // tree holds. Exactly one machine claims such a node
    // (`buildGeneratorBody`, `builder_generator.cpp`); one that arrives here is a `yield` nobody
    // claimed, and the reading is:
    // "a `yield` reached the statement builder, so the generator machine did not claim it. Nothing
    // in the program is wrong: the machine missed a shape it builds". It is an internal error
    // all the same: a refusal is not a defect of the program, so the text never asks the
    // reader to send it.
    if (gct->exitType() != ExitType::Return) {
        diags_->of(InternalDiag::UnknownInternalError)
            .atOrigin(gct->origin())
            .commit(
                "a `yield` reached the translation, so no generator machine claimed it. Nothing in "
                "the program is wrong: the machine missed a shape it builds");
        throw BuildAbortException();
    }
    ExpectedValueScope expectedScope(expectedValueType_, expectedExitTypeHere());
    // `return f`: the function value of a function named here. The name itself is not a
    // value, and left in place it reached the end of compilation as a DREF node.
    node_handle_t resNode = functionValueOf(valueNodeOf(visit(gct->at(0))), gct);
    // The value a `return` hands back takes the declared return type: the return slot is a
    // position like a declaration, as `test/cases/compile/array_shape/rank_zero_slot_return.cml`
    // says in its own words, and a rank-zero return does take it. The position's type is already
    // in scope above (`expectedExitTypeHere`), but a literal whose elements are all written is a
    // finished constant by the time it is read here, so it is asked for the shape only where the
    // position states one and the value leaves it open (`compositeDataInType`); a value that
    // states more than the position does is left as it is, for the check below to speak about.
    if (const data_ptr_t written = constantOfNode(resNode); written != nullptr) {
        Type *declaredExit = expectedExitTypeHere();
        if (declaredExit != nullptr && declaredExit->code() == TypeCode::Array &&
            written->type()->code() == TypeCode::Array && !declaredExit->equals(written->type())) {
            if (const data_ptr_t built = compositeDataInType(written, declaredExit, gct->at(0));
                built != nullptr && declaredExit->equals(built->type())) {
                resNode = currGraph_->addStaticDataNode(built);
                rememberConstant(resNode, built);
                registerNodeOrigin(context_, resNode, gct, "gir.data");
            } else if (
                const auto *stated = tt::as_ptr<ArrayType>(declaredExit);
                stated != nullptr && stated->extents().size() == 1 &&
                stated->extents().front().isConstant()) {
                // A literal of another length is a length, reported with the extent it wrote and
                // the one the position states, rather than as a value that is merely less exact
                // than the return type.
                if (const auto array = std::dynamic_pointer_cast<ArrayData>(written);
                    array != nullptr && static_cast<int64_t>(array->raw().size()) !=
                                            stated->extents().front().constant()) {
                    rejectLiteralData(written, declaredExit, gct->origin(), gct->at(0));
                }
            }
        }
    }
    if (genericDrefs_.contains(resNode)) {
        // `return id` under a declared function type: that type is the function wanted.
        resNode = genericValueFor(resNode, expectedExitTypeHere(), gct->at(0));
    }
    if (nodeIsKind(resNode, runtime::GCNodeKind::Dref)) {
        const auto &returned = gct->at(0);
        rejectNameWithoutValue(
            resNode,
            returned->type() == GCT::NodeKind::DREF ? returned->name()
                                                    : std::string("the returned name"),
            gct);
    }
    // A function that declares T? or T! returns its plain value boxed (see adaptExitValue).
    resNode                    = adaptExitValue(gct, resNode);
    node_handle_t outputAnchor = resNode;
    node_vec_t pendingCtrlInputs;
    if (auto modifier = modifierOf(resNode); modifier.has_value()) {
        pendingCtrlInputs.push_back(*modifier);
    }
    if (synced_ && lastSyncedNode_ != nullptr) {
        pendingCtrlInputs.push_back(lastSyncedNode_);
    }

    // The exit takes the value by a data edge, and the statement the chain ends with reaches it by
    // a sync edge: the value's own node is what the graph hands on, and only a value that does not
    // already read that statement needs the edge -- with it, a GATE to carry the order. A value
    // that came out of that statement (the `v` of `v = 2; v`) is ordered after it already, so it
    // takes no control edge and no GATE, and the tail `return v` and a body whose last statement
    // is `v` ask this once and build one graph (docs/design/effects/edges.cn.md 5.3).
    node_vec_t syncInputs;
    for (node_handle_t ctrlInput : pendingCtrlInputs) {
        if (!valueNeedsSyncEdge(resNode, ctrlInput)) {
            continue;
        }
        syncInputs.push_back(ctrlInput);
    }
    // The chain tail's sync edge may only sit on a value node this block alone owns
    // (docs/design/effects/edges.cn.md 5.3): a node some other block already reaches by a sync edge
    // -- the value of a body the call at this position expanded (5.7) -- is not this block's to
    // wait on, so the wait is carried by a GATE of this block's own. An unread GATE stays either
    // way: it is what keeps the edge, and the chain it ends, able to reach the graph's exit.
    if (!syncInputs.empty() &&
        (!nodeIsKind(resNode, runtime::GCNodeKind::Gate) || !syncInputsOf(resNode).empty())) {
        node_handle_t gatedValue = currGraph_->addGateNode(nodeTypeOf(resNode));
        linkNodes(LinkType::Norm, resNode, gatedValue);
        outputAnchor = gatedValue;
    }
    for (node_handle_t ctrlInput : syncInputs) {
        if (linkCheek(ctrlInput, outputAnchor) && !runtime::hasControlDependency(
                                                      *nodeTransactionOf(outputAnchor),
                                                      nodeIdOf(ctrlInput),
                                                      nodeIdOf(outputAnchor))) {
            if (ctrlInput == lastSyncedNode_) {
                // The chain's tail: the statement the chain ends with reaches the value the graph
                // hands on by a sync edge (docs/design/effects/edges.cn.md 5.3).
                writeSyncStatement(ctrlInput, outputAnchor);
            } else {
                // The `var` modifier of the returned value: it only orders -- the modifier is
                // reached by the data port that reads it, so nothing here carries "used" -- and an
                // edge that only orders is an order edge (docs/design/effects/edges.cn.md 1, 5.6;
                // design principles 3.1).
                linkNodes(LinkType::Order, ctrlInput, outputAnchor);
            }
        }
    }
    if (activeStaticBody_ != nullptr) {
        // The selected body of a `match type` has no region of its own: its `return` gives the
        // value of the match (visitTypeMatchNode), not of the function around it.
        activeStaticBody_->value    = outputAnchor;
        activeStaticBody_->returned = true;
    } else if (activeRestBody_ != nullptr) {
        // A rest function is expanded into the caller's graph, so its `return` is not the
        // caller's: it gives the value of the expanded body (inlineRestCall).
        activeRestBody_->value    = outputAnchor;
        activeRestBody_->returned = true;
    } else if (ArmBuild *arm = activeArm()) {
        // The value of the arm, not of the function.
        arm->output = outputAnchor;
        arm->value  = resNode;
        arm->type   = isVoidGraphExitType(nodeTypeOf(resNode)) ? Type::Void() : nodeTypeOf(resNode);
        arm->returned = true;
    } else if (currentBlock_ != functionBody_) {
        // A `return` written in a block that is itself a statement gives that block's value, not
        // the function's (design principles 7.3): the block hands it to whatever takes the block,
        // and the statement it is gives it to what takes that. The function's exit is written by
        // the `return` of its own body only, so a block's `return` never overwrites the value the
        // function hands on.
    } else {
        checkDeclaredReturn(currGraph_, nodeTypeOf(resNode), gct->origin(), gct->at(0));
        if (!droppedGraphs_.contains(camel::runtime::toValue(currGraph_->graphId()))) {
            // The write the body leaves in a `var` parameter is used here, where the body leaves:
            // the cell is the caller's, and what the body last wrote into it is what the caller
            // finds there afterwards (design principles 3.1).
            outputAnchor = exitUsesVariableParameterWrites(currGraph_, outputAnchor);
            setGraphReturnFromResult(
                currGraph_,
                nodeIdOf(outputAnchor),
                nodeIdOf(resNode),
                nodeTypeOf(resNode));
        }
    }

    LEAVE("EXIT");
    return resNode;
}

node_handle_t Builder::visitBodyNode(const GCT::node_ptr_t &gct) {
    const bool outerSynced        = std::exchange(synced_, false);
    const node_handle_t outerLast = std::exchange(lastSyncedNode_, {});
    // The body of the region being built: a `var` declared in it is a candidate for SSA exactly
    // when every read and write of its name stays in this body (Builder::scanBodyForSsa).
    regionBodies_.push_back(gct);
    // The last statement of a body is the value the body hands on: a branch there (`if c then
    // … else …` as a function's whole body) stands in the position of that value, and so do
    // the `return`s written in its arms (design principles 5.1: an arm's `return` takes the type of
    // position the arm is at, never the declared type of the function around it). The position
    // is passed to visitExecNode, which builds that statement through the same expected-type
    // channel every other position uses.
    node_handle_t result{};
    try {
        result = visitExecNode(gct, tailPositionType());
        if (lastBlockStatement_.first != nullptr) {
            bodyValueStatement_[currentBodyKey()] = lastBlockStatement_;
        }
    } catch (...) {
        synced_         = outerSynced;
        lastSyncedNode_ = outerLast;
        regionBodies_.pop_back();
        throw;
    }
    synced_         = outerSynced;
    lastSyncedNode_ = outerLast;
    regionBodies_.pop_back();
    return result;
}

bool Builder::inferLoopStateFromAppend(
    const arm_ptr_t &body, const arm_ptr_t &cond, const GCT::node_ptr_t &loop,
    std::vector<node_handle_t> &initial, std::vector<Type *> &stateTypes, size_t index,
    node_handle_t next) {
    if (next == nullptr || !nodeIsKind(next, runtime::GCNodeKind::Oper)) {
        return false;
    }
    const oper_idx_ptr_t op = nodeGraphOf(next)->operTarget(next);
    if (op == nullptr || op->uri() != ":struct/append_arr") {
        return false;
    }
    const auto normIns = normInputsOf(next);
    if (normIns.size() != 1) {
        return false;
    }
    const size_t portAt = index + (camel::core::loopHasIterable(loop->loopKind()) ? 1 : 0);
    node_handle_t port  = nodeDraftOf(next)->ref(normIns.front());
    if (port == nullptr || portAt >= body->ports.size() || body->ports[portAt] != port) {
        return false;
    }
    const auto withIns = withInputsOf(next);
    if (withIns.size() != 1) {
        return false;
    }
    Type *element = nodeTypeOf(nodeDraftOf(next)->ref(withIns.front()));
    if (element == nullptr || element->code() == TypeCode::Void) {
        return false;
    }
    Type *newType = ArrayType::create(element);
    // The parts were built with the placeholder `void[]`: the append's operand, the ports it
    // reads and the empty literal all have to become the element array's type, and the call is
    // resolved again so its instance carries the element layout.
    DraftGraphBuilder *graph = currGraph_.get();
    retypeValueNode(graph, port, newType);
    retypeValueNode(graph, next, newType);
    retypeValueNode(nodeGraphOf(initial[index]), initial[index], newType);
    if (body->output != nullptr && body->output != next) {
        retypeValueNode(graph, body->output, newType);
    }
    body->type = newType;
    if (cond && index < cond->ports.size()) {
        retypeValueNode(graph, cond->ports[index], newType);
    }
    const auto group = importedOperatorGroupNamed("append");
    if (group != nullptr) {
        const std::vector<StaticArg> statics(1);
        const auto resolved = group->resolve({element}, {newType}, statics);
        if (resolved.has_value()) {
            graph->retargetOperNode(next, *resolved);
        }
    }
    stateTypes[index] = newType;
    return true;
}

std::vector<Type *> Builder::collectLoopAppends(
    const arm_ptr_t &body, const GCT::node_ptr_t &loop, const std::vector<Type *> &stateTypes,
    const std::vector<bool> &emptyInitial) {
    const size_t stateCount = stateTypes.size();
    if (stateCount == 0 || emptyInitial.size() != stateCount || body->returned) {
        return {};
    }
    auto &draft            = currGraph_->transaction().edit();
    node_handle_t returned = loopStateValueOf(body->value);
    if (returned == nullptr) {
        return {};
    }
    const size_t portBase = camel::core::loopHasIterable(loop->loopKind()) ? 1 : 0;
    std::vector<node_handle_t> next(stateCount, nullptr);
    if (stateCount == 1) {
        next[0] = returned;
    } else {
        if (!nodeIsKind(returned, runtime::GCNodeKind::Fill)) {
            return {};
        }
        const Type *returnType = valueTypeOf(nodeTypeOf(returned));
        if (returnType == nullptr || returnType->code() != TypeCode::Tuple) {
            return {};
        }
        const auto withIns = withInputsOf(returned);
        const auto payload = nodeDraftOf(returned)->payloadOf(nodeIdOf(returned));
        if (payload.size() < sizeof(runtime::GCFillBody)) {
            return {};
        }
        const auto *fill = reinterpret_cast<const runtime::GCFillBody *>(payload.data());
        if (fill->fillKind != runtime::GCFillKind::Tuple) {
            return {};
        }
        const auto slots = fill->slots();
        if (slots.size() != withIns.size()) {
            return {};
        }
        for (size_t i = 0; i < slots.size(); ++i) {
            if (static_cast<size_t>(slots[i]) >= stateCount) {
                return {};
            }
            next[slots[i]] = nodeDraftOf(returned)->ref(withIns[i]);
        }
    }
    std::vector<Type *> collected;
    std::vector<node_handle_t> values;
    for (size_t j = 0; j < stateCount; ++j) {
        if (!emptyInitial[j] || next[j] == nullptr ||
            !nodeIsKind(next[j], runtime::GCNodeKind::Oper)) {
            continue;
        }
        const oper_idx_ptr_t op = nodeGraphOf(next[j])->operTarget(next[j]);
        if (op == nullptr ||
            (op->uri() != ":struct/append_arr" && op->uri() != ":struct/append_arr_owned")) {
            continue;
        }
        const auto normIns = normInputsOf(next[j]);
        if (normIns.size() != 1) {
            continue;
        }
        node_handle_t port = nodeDraftOf(next[j])->ref(normIns.front());
        if (port == nullptr || portBase + j >= body->ports.size() ||
            body->ports[portBase + j] != port) {
            continue;
        }
        if (normUsersOf(port).size() + withUsersOf(port).size() != 1) {
            continue;
        }
        const auto withIns = withInputsOf(next[j]);
        if (withIns.size() != 1) {
            continue;
        }
        Type *element = nodeTypeOf(nodeDraftOf(next[j])->ref(withIns.front()));
        if (element == nullptr) {
            continue;
        }
        collected.push_back(element);
        values.push_back(nodeDraftOf(next[j])->ref(withIns.front()));
    }
    if (collected.empty()) {
        return {};
    }
    // The body's next state values, in state order, then one appended element per collection.
    // They are the values the body computed this round; reading them back through an ACCS of
    // the loop's own value would both lose the append (nothing would use it) and hand the
    // driver the state it started with.
    if (stateCount > 1) {
        for (size_t j = 0; j < stateCount; ++j) {
            if (next[j] == nullptr) {
                return {}; // a constant component the body does not give us as a value
            }
        }
    }
    std::vector<node_handle_t> components(next.begin(), next.end());
    components.insert(components.end(), values.begin(), values.end());
    std::vector<Type *> resultTypes = stateTypes;
    for (Type *element : collected) {
        resultTypes.push_back(element);
    }
    Type *resultType = TupleType::create(resultTypes);
    std::vector<size_t> slots(resultTypes.size());
    std::iota(slots.begin(), slots.end(), size_t{0});
    // The tuple is a node of the body.
    const uint32_t outerRegion = draft.insertRegion();
    draft.setInsertRegion(body->region);
    node_handle_t result = currGraph_->addFillNode(
        resultType,
        runtime::makeFillPayload(runtime::GCFillKind::Tuple, slots));
    const slot_t empty = camel::core::rtdata::toSlot(
        ::Tuple::create(resultTypes.size(), camel::core::mm::autoSpace()));
    node_handle_t source = draft.ref(draft.materializeStaticValue(empty, resultType));
    const std::array<runtime::gc_node_ref_t, 1> sourceInput{nodeIdOf(source)};
    draft.setNormInputs(nodeIdOf(result), sourceInput);
    std::vector<runtime::gc_node_ref_t> componentIds;
    for (node_handle_t component : components) {
        componentIds.push_back(nodeIdOf(component));
    }
    draft.setWithInputs(nodeIdOf(result), componentIds);
    draft.setInsertRegion(outerRegion);
    body->output = result;
    body->value  = result;
    body->type   = resultType;
    registerNodeOrigin(context_, result, loop->loopBody(), "gir.loop.collect");
    return collected;
}

void Builder::markLoopStateInPlace(
    const arm_ptr_t &body, const GCT::node_ptr_t &loop, std::vector<node_handle_t> &initial,
    const std::vector<bool> &privateInitial) {
    // The decision is per state, and only a state of these four properties is rewritten: the
    // state is an array, its first value is a literal (a Data or FILL node), its next value is
    // `state->append<v>`, and the body uses that state's port nowhere else. Anything else -- a
    // struct or tuple state, a first value that is a parameter or an array element, a body that
    // also reads the state -- keeps the copying `append`; the values are the
    // same either way, and only the allocation differs (10.5's in-place guarantee is about the
    // state's data block, not about a promise on the source).
    // A state that was split out of a struct is a component of the states the loop's parts
    // declare, so the count is the initial values', not the LOOP node's names; its first value
    // is a projection of the struct (a value the loop does not own) and is copied once.
    const size_t stateCount = initial.size();
    if (stateCount == 0) {
        return;
    }
    node_handle_t returned = loopStateValueOf(body->value);
    if (returned == nullptr) {
        return;
    }
    // The next value of each state, in state order, for each arm the body can end in: a body that
    // ends in an `if` or `match` has one result per arm, behind the JOIN that forwards the one
    // that ran, and only one arm runs, so a state each arm uses once is held once. One state
    // hands the append call back directly. Two or more return a tuple, a FILL whose With inputs
    // are the computed components and whose payload says which component each one fills; a
    // component written as a constant there (`(xs->append<7>, 0)`) has no value node and no
    // rewrite of its own.
    const std::vector<node_handle_t> arms        = loopArmResultsOf(returned);
    const std::vector<node_handle_t> stateValues = loopStateValues(body->value, arms);
    if (stateCount == 1) {
        markLoopStateInPlaceOne(
            body,
            loop,
            initial,
            0,
            arms,
            !privateInitial.empty() && privateInitial[0],
            stateValues);
        return;
    }
    std::vector<std::vector<node_handle_t>> nexts(stateCount);
    for (node_handle_t arm : arms) {
        if (arm == nullptr || !nodeIsKind(arm, runtime::GCNodeKind::Fill)) {
            return;
        }
        const Type *returnType = valueTypeOf(nodeTypeOf(arm));
        if (returnType == nullptr || returnType->code() != TypeCode::Tuple) {
            return;
        }
        const auto withIns = withInputsOf(arm);
        const auto payload = nodeDraftOf(arm)->payloadOf(nodeIdOf(arm));
        if (payload.size() < sizeof(runtime::GCFillBody)) {
            return;
        }
        const auto *fill = reinterpret_cast<const runtime::GCFillBody *>(payload.data());
        if (fill->fillKind != runtime::GCFillKind::Tuple) {
            return;
        }
        const auto slots = fill->slots();
        if (slots.size() != withIns.size()) {
            return;
        }
        std::vector<node_handle_t> next(stateCount, nullptr);
        for (size_t i = 0; i < slots.size(); ++i) {
            if (static_cast<size_t>(slots[i]) >= stateCount) {
                return;
            }
            next[slots[i]] = nodeDraftOf(arm)->ref(withIns[i]);
        }
        for (size_t j = 0; j < stateCount; ++j) {
            nexts[j].push_back(next[j]);
        }
    }
    for (size_t j = 0; j < stateCount; ++j) {
        markLoopStateInPlaceOne(
            body,
            loop,
            initial,
            j,
            nexts[j],
            !privateInitial.empty() && j < privateInitial.size() && privateInitial[j],
            stateValues);
    }
}

void Builder::markLoopStateInPlaceOne(
    const arm_ptr_t &body, const GCT::node_ptr_t &loop, std::vector<node_handle_t> &initial,
    size_t index, const std::vector<node_handle_t> &nexts, bool initialIsPrivate,
    const std::vector<node_handle_t> &stateValues) {
    if (nexts.empty()) {
        return;
    }
    // The first value has to be a literal: a name, a call or a projection can name an array
    // that something else also reads, and the loop is about to make its first value private.
    // A component split out of a struct state is such a projection, and it is copied
    // below because the struct it comes from is the program's, not the loop's.
    if (!initialIsPrivate && !nodeIsKind(initial[index], runtime::GCNodeKind::Data) &&
        !nodeIsKind(initial[index], runtime::GCNodeKind::Fill)) {
        return;
    }
    const Type *stateType = valueTypeOf(nodeTypeOf(initial[index]));
    if (stateType == nullptr || stateType->code() != TypeCode::Array) {
        return;
    }
    const size_t portAt = index + (camel::core::loopHasIterable(loop->loopKind()) ? 1 : 0);
    if (portAt >= body->ports.size()) {
        return;
    }
    node_handle_t port = body->ports[portAt];
    if (port == nullptr) {
        return;
    }
    // Each next value is the call `state->append<v>` on this state's parameter, or the parameter
    // itself (an arm that leaves the state as it was). The body uses the parameter nowhere else:
    // once the round returns, the value it was bound to is gone (the driver binds the round's own
    // state), so nothing can see it as it was. The arms are exclusive, so one use in each is one
    // holder.
    const oper_group_ptr_t group =
        context_->importModule("")->nativeOperatorGroup(camel::core::oper::AppendOwned);
    if (group == nullptr) {
        return;
    }
    const std::vector<StaticArg> statics(1);
    std::vector<std::pair<node_handle_t, oper_idx_ptr_t>> appends;
    for (node_handle_t next : nexts) {
        if (next == nullptr) {
            return;
        }
        if (next == port) {
            continue;
        }
        if (!nodeIsKind(next, runtime::GCNodeKind::Oper)) {
            return;
        }
        const oper_idx_ptr_t op = nodeGraphOf(next)->operTarget(next);
        if (op == nullptr || op->uri() != ":struct/append_arr") {
            return;
        }
        const auto normIns = normInputsOf(next);
        if (normIns.size() != 1 || nodeDraftOf(next)->ref(normIns.front()) != port) {
            return;
        }
        // The appended value is the call's only With input; the owned form takes the same.
        const auto withIns = withInputsOf(next);
        if (withIns.size() != 1) {
            return;
        }
        Type *valueType = nodeTypeOf(nodeDraftOf(next)->ref(withIns.front()));
        if (valueType == nullptr) {
            return;
        }
        const auto owned = group->resolve({valueType}, {nodeTypeOf(port)}, statics);
        if (!owned.has_value()) {
            return;
        }
        appends.emplace_back(next, *owned);
    }
    if (appends.empty() || normUsersOf(port).size() + withUsersOf(port).size() != nexts.size()) {
        return;
    }
    // Nothing else in the round may hold what the state appends to. The runtime decides the write
    // in place from the block's reference count (`block_->unique() && camelOwned()`), and that
    // count is necessary but not sufficient: a value stored into another container's slot is the
    // same array object, not a second name, so the count stays one while two places can observe
    // the write. The graph knows the users and the runtime cannot, so the decision belongs here,
    // the one place that produces `:struct/append_arr_owned`
    // (docs/design/arrays-memory/README.cn.md 4.3.1: one rule, one place, and the loop's collecting
    // slot among the conflicts).
    //
    // The append hands back the state's next value, so it may reach the state's own value and
    // nothing else. What it may reach is what assembles that value: a tuple of states, the struct
    // a split state came from, a GATE that forwards it -- the readers a walk towards the state's
    // value passes through, which is the direction `loopStateValues` names. It may also be read by
    // a computation that cannot keep it (`len(xs)`, a comparison), the answer
    // `Builder::markNonRetainingReads` uses as its condition 3. Any other reader keeps it:
    // `seen = seen->append<acc>` puts it in the loop's collecting slot, `f(acc)` hands it to a
    // call, `{ v: acc }` puts it in a field. Reading the value from the state's value *downwards*
    // instead would count the other states of the same round -- `seen`'s own append is part of the
    // tuple -- so this walks upwards from the appended value and stops at the state. The state
    // then keeps the copying `append`, and only the allocation differs (design principles 10.5
    // and 11.3).
    {
        std::vector<node_handle_t> examined;
        const auto heldByAnotherName = [&](node_handle_t value) {
            std::vector<node_handle_t> work{value};
            while (!work.empty()) {
                node_handle_t at = work.back();
                work.pop_back();
                if (at == nullptr) {
                    continue;
                }
                for (const auto &users : {normUsersOf(at), withUsersOf(at)}) {
                    for (runtime::gc_node_ref_t userId : users) {
                        node_handle_t user = nodeDraftOf(at)->ref(userId);
                        if (user == nullptr ||
                            std::ranges::find(stateValues, user) != stateValues.end() ||
                            std::ranges::find(examined, user) != examined.end()) {
                            continue; // the round's own state, or a reader already answered for
                        }
                        examined.push_back(user);
                        if (nodeIsKind(user, runtime::GCNodeKind::Gate) ||
                            nodeIsKind(user, runtime::GCNodeKind::Join) ||
                            nodeIsKind(user, runtime::GCNodeKind::Fill) ||
                            nodeIsKind(user, runtime::GCNodeKind::Accs)) {
                            work.push_back(user); // assembles the value: ask its own readers
                            continue;
                        }
                        if (!nodeKeepsItsInput(user)) {
                            continue;
                        }
                        return true;
                    }
                }
            }
            return false;
        };
        for (node_handle_t next : nexts) {
            if (next == nullptr || next == port) {
                continue; // an arm that leaves the state as it was; its uses are the appends
            }
            if (heldByAnotherName(next)) {
                return;
            }
        }
    }
    // The loop starts from a private copy of its first value. A literal is a constant of the
    // graph: the same array object on every evaluation of the loop's site, so appending to it
    // in place would edit the program's own literal. One copy per loop entry buys every round.
    const oper_group_ptr_t copyGroup =
        context_->importModule("")->nativeOperatorGroup(camel::core::oper::CopyArray);
    if (copyGroup == nullptr) {
        return;
    }
    const auto copyOp = copyGroup->resolve({}, {nodeTypeOf(initial[index])}, statics);
    if (!copyOp.has_value()) {
        return;
    }
    node_handle_t copy = currGraph_->addOperNode(*copyOp, 0);
    stampPermissions(copy);
    const size_t initialAt = index < loop->loopStateCount() ? index : 0;
    registerNodeOrigin(context_, copy, loop->loopInitial(initialAt), "gir.loop.copy");
    linkNodes(LinkType::Norm, initial[index], copy);
    initial[index] = copy;
    for (const auto &[next, owned] : appends) {
        nodeGraphOf(next)->retargetOperNode(next, owned);
    }
}

node_handle_t
Builder::exitUsesVariableParameterWrites(const compile_graph_ptr_t &graph, node_handle_t anchor) {
    const FunctionType *funcType = graph != nullptr ? graph->funcType() : nullptr;
    if (funcType == nullptr || anchor == nullptr) {
        return anchor;
    }
    // A `var` parameter is a cell the caller made: what the body leaves in it is what the caller
    // reads afterwards, so the write outlives the call and is a result of the function beside the
    // returned value (design principles 3.1). The write the body leaves is the one `recordWrite`
    // recorded last on the port -- the same node a read of the parameter takes as its version
    // (Builder::valueVersionOf) -- and a parameter the body never wrote has none.
    node_vec_t writers;
    const auto collect = [&](const auto &ports, const bool with) {
        const size_t count = with ? funcType->withTypesCount() : funcType->normTypesCount();
        for (size_t i = 0; i < ports.size() && i < count; ++i) {
            if (!(with ? funcType->withIsVarAt(i) : funcType->normIsVarAt(i))) {
                continue;
            }
            const node_handle_t port = graph->view().ref(ports[i]);
            if (port == nullptr) {
                continue;
            }
            if (const auto writer = modifierOf(port);
                writer.has_value() && sameGraph(*writer, graph)) {
                writers.push_back(*writer);
            }
        }
    };
    collect(graph->view().withPorts(), true);
    collect(graph->view().normPorts(), false);
    if (writers.empty()) {
        return anchor;
    }
    // What the exit already uses, reached back the way the run time lays the graph out: data ports,
    // sync edges and the control edges it waits on. Inside a `sync` block the chain carries the
    // write to the exit already, so nothing is added there and the two bodies build one shape --
    // the same `:op/assn_l` node, used by the same exit, whether the write is written in a block or
    // not.
    const auto *draft = nodeDraftOf(anchor);
    std::set<draft_node_ref_t> reached{nodeIdOf(anchor)};
    std::vector<draft_node_ref_t> work{nodeIdOf(anchor)};
    while (!work.empty()) {
        const draft_node_ref_t id = work.back();
        work.pop_back();
        for (const auto inputs :
             {draft->normInputsOf(id), draft->withInputsOf(id), draft->ctrlInputsOf(id)}) {
            for (const draft_node_ref_t input : inputs) {
                if (reached.insert(input).second) {
                    work.push_back(input);
                }
            }
        }
        for (const draft_node_ref_t input : draft->syncInputsOf(id)) {
            if (reached.insert(input).second) {
                work.push_back(input);
            }
        }
    }
    node_handle_t gate = nullptr;
    for (node_handle_t writer : writers) {
        if (reached.contains(nodeIdOf(writer))) {
            continue;
        }
        if (gate == nullptr) {
            if (nodeIsKind(anchor, runtime::GCNodeKind::Gate)) {
                // The exit's own GATE, made for the chain tail: the wait goes on it.
                gate = anchor;
            } else {
                gate = graph->addGateNode(nodeTypeOf(anchor));
                linkNodes(LinkType::Norm, anchor, gate);
            }
        }
        if (linkCheek(writer, gate) && !runtime::hasControlDependency(
                                           *nodeTransactionOf(gate),
                                           nodeIdOf(writer),
                                           nodeIdOf(gate))) {
            // The control edge is what makes the write run where the value it wrote has no reader
            // of its own in the body (docs/design/effects/edges.cn.md 1; design principles 3.5,
            // F3): the exit waits on it, and the wait is why it runs at all.
            writeControlLink(writer, gate);
        }
    }
    return gate != nullptr ? gate : anchor;
}

void Builder::setBodyResult(const compile_graph_ptr_t &graph, node_handle_t result) {
    if (graph->view().exitNode() != runtime::kInvalidNodeRef) {
        return;
    }
    if (result == nullptr) {
        result               = graph->addStaticDataNode(Data::null());
        node_handle_t anchor = exitUsesVariableParameterWrites(graph, result);
        setGraphReturnFromResult(graph, nodeIdOf(anchor), nodeIdOf(result), Type::Void());
        return;
    }
    // A sync block that ends in a value hands it back behind a GATE (see visitExecNode): the
    // GATE is the output anchor, the value it forwards is what the graph returns.
    node_handle_t value = result;
    if (nodeIsKind(result, runtime::GCNodeKind::Gate) && !normInputsOf(result).empty()) {
        value = nodeDraftOf(result)->ref(normInputsOf(result).front());
    }
    checkDeclaredReturn(graph, nodeTypeOf(value), 0);
    setGraphReturnFromResult(
        graph,
        nodeIdOf(exitUsesVariableParameterWrites(graph, result)),
        nodeIdOf(value),
        nodeTypeOf(value));
}

} // namespace camel::compile::gir