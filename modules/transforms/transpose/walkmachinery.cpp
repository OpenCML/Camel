// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The pieces of the walk that are reached from more than one place and are not the walk itself: the
 * one line a refusal is written on, the one place a parameter's shape is recorded, the four questions
 * about a **type and its zero** (the cotangent space of a type, the cotangent space of a value, the
 * zero a type states, and the zero of a value), **the sum of two cotangents in a space** with the
 * aggregate it is assembled into, and **the cotangent bookkeeping** -- which place the answer is
 * about, which node is a constant, which value can hold a cotangent, where a zero goes, one position's
 * cotangent, the join of a value read member by member, and one branch arm's answer.
 *
 * They moved out of `buildTransposeMember` so the node families can reach them without a second copy
 * each. What stays behind is the walk (`transpose/walk.cpp`); what these are handed is `WalkContext`
 * (`transpose/walk.h`), which is why it carries the draft, the forward table, the fact that some
 * cotangent could not be summed and the tables the bookkeeping reads.
 */

#include "transpose/walk.h"

#include "rule_expand.h"
#include "transpose/cotangent.h"
#include "transpose/rules.h"
#include "transpose/signature.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/tangent_space.h"
#include "camel/core/type/base.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/text.h"
#include "camel/core/type/tree.h"
#include "camel/utils/type.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace camel::transforms {

using camel::core::type::ArrayType;
using camel::core::type::FunctionType;
using camel::core::type::Type;

void refuseMember(WalkContext &ctx, TransposeRefusalCause cause, std::optional<size_t> param) {
    ctx.state.refusal       = TransposeRefusal{cause, param};
    ctx.state.refusal.graph = ctx.function->name();
    ctx.os << "TRANSPOSE function=" << ctx.function->name() << " skip="
           << transposeRefusalWord(cause);
}

void noteUnstatedParameterOf(WalkContext &ctx, size_t param) {
    Type *const paramType  = ctx.function->funcType()->normTypeAt(param);
    const std::string type = paramType != nullptr ? paramType->toString() : std::string();
    std::string name;
    const FunctionType *const signatureType = ctx.function->funcType();
    if (signatureType->hasArgNames()) {
        // One-based place in the written signature, past the `with` types: the same arithmetic
        // the refusal record uses, so the name is the parameter's and not its neighbour's.
        const size_t named = signatureType->withTypesCount() + param;
        if (named < signatureType->argNamesCount()) {
            name = std::string(signatureType->argNameAt(named));
        }
    }
    if (!ctx.state.shapeWanted) {
        ctx.state.shapeWanted = true;
        ctx.state.shapeParam  = param;
        ctx.state.shapeType   = type;
        ctx.state.shapeName   = name;
    }
    ctx.state.refusal.paramName = name;
    ctx.state.refusal.paramType = type;
}

// The cotangent space of a type and of a value, and the two zeros. All four are asked by name from
// more than one place in the walk, and all four answer a question about a type rather than about a
// node, which is why they are here and not a family's.

Type *cotangentSpaceOfType(WalkContext &, const Type *type) {
    if (type == nullptr) {
        return nullptr;
    }
    const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(type);
    return space.hasSpace() ? space.space : nullptr;
}

Type *cotangentTypeOfValue(WalkContext &ctx, gc_node_ref_t ref) {
    const camel::runtime::GCNode *node = ctx.function->node(ref);
    return cotangentSpaceOfType(ctx, node != nullptr ? node->dataType() : nullptr);
}

gc_node_ref_t zeroOfCotangentType(WalkContext &ctx, const Type *primal, Type *tangentType) {
    if (primal == nullptr || primal->code() != camel::core::type::TypeCode::Array ||
        tangentType == nullptr || tangentType != primal) {
        return rt::kInvalidNodeRef;
    }
    const auto *const arrayType = tt::as_ptr<ArrayType>(const_cast<Type *>(primal));
    if (arrayType->elemType() == nullptr ||
        !camel::core::type::isFloatingScalar(arrayType->elemType()->code())) {
        return rt::kInvalidNodeRef;
    }
    const std::span<const camel::core::type::ExtentForm> stated = arrayType->extents();
    if (stated.empty()) {
        return rt::kInvalidNodeRef;
    }
    std::vector<int64_t> extents;
    extents.reserve(stated.size());
    for (const camel::core::type::ExtentForm &form : stated) {
        const std::optional<int64_t> size = form.evaluate({});
        if (!size.has_value()) {
            // An extent the type does not state: the zero is the forward argument's, and the
            // witness arrives as one of the cut's residuals. This must not answer an empty array
            // instead -- an unwitnessed zero is wrong at every length but 0 -- so a missing
            // witness is a loud refusal, never a quiet wrong width.
            return rt::kInvalidNodeRef;
        }
        extents.push_back(*size);
    }
    ::Array *const zeros = ::Array::create(
        camel::core::mm::autoSpace(),
        arrayType,
        std::span<const int64_t>(extents),
        /*zeroInit=*/true);
    if (zeros == nullptr) {
        return rt::kInvalidNodeRef;
    }
    return ctx.draft->materializeStaticValue(
        camel::core::rtdata::toSlot<::Array *>(zeros), const_cast<Type *>(primal),
        rt::kGCNodeFlagConstant);
}

gc_node_ref_t zeroOfValue(WalkContext &ctx, gc_node_ref_t valueRef) {
    const camel::runtime::GCNode *node = ctx.function->node(valueRef);
    const Type *type                   = node != nullptr ? node->dataType() : nullptr;
    const camel::core::TangentSpaceAnswer space = type == nullptr
                                                      ? camel::core::TangentSpaceAnswer{}
                                                      : camel::core::tangentSpaceOf(type);
    const gc_node_ref_t forwarded =
        ctx.forwardOf != nullptr && valueRef < ctx.forwardOf->size() ? (*ctx.forwardOf)[valueRef]
                                                                    : rt::kInvalidNodeRef;
    const gc_node_ref_t built =
        zeroOfSpaceOf(*ctx.draft, space.hasSpace() ? space.space : nullptr, forwarded);
    if (built != rt::kInvalidNodeRef) {
        return built;
    }
    // A shape the walk has no value for, on a type that states none: the zero the type itself
    // states (see `zeroOfCotangentType`).
    return space.hasSpace() ? zeroOfCotangentType(ctx, type, space.space) : rt::kInvalidNodeRef;
}

// The sum of two cotangents, and the two node builders it is written with. Everything the sum needs
// that is not a type is in the context, so it reads the same whether the walk or a family asks.
namespace {

/// One operator node of the new graph, with its operands.
gc_node_ref_t primitiveOf(
    WalkContext &ctx, Type *type, const char *uri, std::vector<gc_node_ref_t> inputs) {
    const gc_node_ref_t made = ctx.draft->addOperNode(type, nullptr, uri);
    ctx.draft->setNormInputs(made, std::span<const gc_node_ref_t>(inputs));
    return made;
}

/// One member of an aggregate cotangent, as a node of the new graph: the place `place` of a value
/// whose type is `aggregateType`. A struct's place is named (an `Accs` of `GCAccsKind::StructKey`), a
/// tuple's is numbered (`TupleIndex`) -- the two forms the builder writes for `q.0` and `qq.a`, so a
/// member read here is read exactly as a program reads it.
gc_node_ref_t memberOf(WalkContext &ctx, gc_node_ref_t value, Type *aggregateType, size_t place) {
    Type *const memberType = camel::core::type::tree::childType(aggregateType, place);
    if (value == rt::kInvalidNodeRef || memberType == nullptr) {
        return rt::kInvalidNodeRef;
    }
    gc_node_ref_t member = rt::kInvalidNodeRef;
    if (aggregateType->code() == camel::core::type::TypeCode::Struct) {
        const auto *structure = tt::as_ptr<camel::core::type::StructType>(aggregateType);
        if (structure == nullptr || place >= structure->size()) {
            return rt::kInvalidNodeRef;
        }
        member = ctx.draft->addAccsNode(memberType, structure->fieldName(place));
    } else {
        member = ctx.draft->addAccsNode(memberType, static_cast<uint32_t>(place));
    }
    ctx.draft->setNormInputs(member, std::span<const gc_node_ref_t>(&value, 1));
    return member;
}

} // namespace

gc_node_ref_t aggregateOfMembers(
    WalkContext &ctx, Type *aggregateType, const std::vector<gc_node_ref_t> &parts) {
    if (aggregateType == nullptr) {
        return rt::kInvalidNodeRef;
    }
    const bool structure = aggregateType->code() == camel::core::type::TypeCode::Struct;
    camel::core::rtdata::Object *empty =
        structure ? static_cast<camel::core::rtdata::Object *>(
                        ::Struct::create(parts.size(), camel::core::mm::autoSpace()))
                  : static_cast<camel::core::rtdata::Object *>(
                        ::Tuple::create(parts.size(), camel::core::mm::autoSpace()));
    if (empty == nullptr) {
        return rt::kInvalidNodeRef;
    }
    const gc_node_ref_t source = ctx.draft->materializeStaticValue(
        camel::core::rtdata::toSlot<camel::core::rtdata::Object *>(empty), aggregateType);
    std::vector<size_t> slots;
    for (size_t i = 0; i < parts.size(); ++i) {
        slots.push_back(i);
    }
    const gc_node_ref_t filled = ctx.draft->addFillNode(
        aggregateType,
        rt::makeFillPayload(
            structure ? rt::GCFillKind::Struct : rt::GCFillKind::Tuple, slots));
    ctx.draft->setNormInputs(filled, std::span<const gc_node_ref_t>(&source, 1));
    ctx.draft->setWithInputs(filled, std::span<const gc_node_ref_t>(parts));
    return filled;
}

gc_node_ref_t sumOfCotangents(
    WalkContext &ctx, Type *space, gc_node_ref_t into, gc_node_ref_t node) {
    if (into == rt::kInvalidNodeRef) {
        return node;
    }
    const std::string addUri = addUriOfSpace(space);
    if (space != nullptr && !addUri.empty() &&
        space->code() != camel::core::type::TypeCode::Tuple &&
        space->code() != camel::core::type::TypeCode::Struct) {
        return into == node ? primitiveOf(ctx, space, addUri.c_str(), {node, node})
                            : primitiveOf(ctx, space, addUri.c_str(), {into, node});
    }
    if (space != nullptr && (space->code() == camel::core::type::TypeCode::Tuple ||
                             space->code() == camel::core::type::TypeCode::Struct)) {
        const auto count = camel::core::type::tree::arity(space);
        if (!count) {
            // Not a type with members: the aggregate branch of this sum cannot run, and a count read
            // out of another type's bytes would have made it run on nothing.
            return rt::kInvalidNodeRef;
        }
        std::vector<gc_node_ref_t> parts;
        parts.reserve(*count);
        for (size_t i = 0; i < *count; ++i) {
            Type *const memberType = camel::core::type::tree::childType(space, i);
            const camel::core::TangentSpaceAnswer memberSpace = memberType != nullptr
                                                                   ? camel::core::tangentSpaceOf(
                                                                         memberType)
                                                                   : camel::core::TangentSpaceAnswer{};
            if (!memberSpace.hasSpace() || memberSpace.space == nullptr) {
                // No tangent space of its own: the place is the unit, and the unit's sum is the one
                // value there is. Handing either side on says exactly that.
                const gc_node_ref_t kept = memberOf(ctx, into, space, i);
                parts.push_back(kept != rt::kInvalidNodeRef ? kept : memberOf(ctx, node, space, i));
                continue;
            }
            const gc_node_ref_t left  = memberOf(ctx, into, space, i);
            const gc_node_ref_t right = into == node ? left : memberOf(ctx, node, space, i);
            if (left == rt::kInvalidNodeRef || right == rt::kInvalidNodeRef) {
                parts.clear();
                break;
            }
            const gc_node_ref_t summed = sumOfCotangents(ctx, memberSpace.space, left, right);
            if (summed == rt::kInvalidNodeRef) {
                // The member's own refusal has already been recorded with the member's name.
                parts.clear();
                break;
            }
            parts.push_back(summed);
        }
        if (!parts.empty() && parts.size() == count) {
            const gc_node_ref_t assembled = aggregateOfMembers(ctx, space, parts);
            if (assembled != rt::kInvalidNodeRef) {
                return assembled;
            }
        }
        if (ctx.cotangentArithmeticMissing) {
            return rt::kInvalidNodeRef;
        }
    }
    if (addUri.empty() || space == nullptr) {
        ctx.cotangentArithmeticMissing = true;
        ctx.os << "TRANSPOSE function=" << ctx.function->name() << " cotangent-no-arithmetic"
               << '\n';
        return rt::kInvalidNodeRef;
    }
    return into == node ? primitiveOf(ctx, space, addUri.c_str(), {node, node})
                        : primitiveOf(ctx, space, addUri.c_str(), {into, node});
}

// The cotangent bookkeeping: what the walk keeps about the member it is building, asked one
// question at a time. Each of these was a lambda in `buildTransposeMember` read from more than one
// node family -- the operand family, the aggregate family, the call family -- so they left the walk
// together with the tables they read. What each one decides is on its declaration in
// `transpose/walk.h`; what is here is the reading itself.

bool participatesInTheAnswer(const WalkContext &ctx, size_t at) {
    if (ctx.participates != nullptr && !ctx.participates->empty()) {
        return at < ctx.participates->size() && (*ctx.participates)[at];
    }
    return ctx.places != nullptr && ctx.places->answers(at);
}

bool isConstantNode(const WalkContext &ctx, gc_node_ref_t ref) {
    const camel::runtime::GCNode *node = ctx.function->node(ref);
    return node != nullptr && node->kind == rt::GCNodeKind::Data;
}

gc_node_ref_t constantValueOf(WalkContext &ctx, gc_node_ref_t ref) {
    std::vector<gc_node_ref_t> &constantOf = *ctx.book.constantOf;
    if (constantOf[ref] != rt::kInvalidNodeRef) {
        return constantOf[ref];
    }
    const camel::runtime::GCNode *node  = ctx.function->node(ref);
    const std::span<const slot_t> slots = ctx.function->staticSlots();
    const std::ptrdiff_t index          = -static_cast<std::ptrdiff_t>(node->dataIndex);
    if (node->dataIndex < 0 && index >= 0 && static_cast<size_t>(index) < slots.size()) {
        constantOf[ref] = ctx.draft->materializeStaticValue(
            slots[static_cast<size_t>(index)], node->dataType(), rt::kGCNodeFlagConstant);
    }
    return constantOf[ref];
}

std::pair<gc_node_ref_t, Type *> tupleOfValues(
    WalkContext &ctx, const std::vector<gc_node_ref_t> &parts,
    const std::vector<Type *> &partTypes) {
    Type *const tupleType      = camel::core::type::TupleType::create(partTypes);
    ::Tuple *emptyTuple        = ::Tuple::create(parts.size(), camel::core::mm::autoSpace());
    const gc_node_ref_t source = ctx.draft->materializeStaticValue(
        camel::core::rtdata::toSlot<camel::core::rtdata::Object *>(emptyTuple), tupleType);
    std::vector<size_t> slots;
    for (size_t i = 0; i < parts.size(); ++i) {
        slots.push_back(i);
    }
    const gc_node_ref_t tuple = ctx.draft->addFillNode(
        tupleType, rt::makeFillPayload(rt::GCFillKind::Tuple, slots));
    ctx.draft->setNormInputs(tuple, std::span<const gc_node_ref_t>(&source, 1));
    ctx.draft->setWithInputs(tuple, std::span<const gc_node_ref_t>(parts));
    return std::pair<gc_node_ref_t, Type *>{tuple, tupleType};
}

size_t aggregateSlotCount(const WalkContext &ctx, gc_node_ref_t ref) {
    const camel::runtime::GCNode *node = ctx.function->node(ref);
    if (node == nullptr) {
        return 0;
    }
    if (node->kind == rt::GCNodeKind::Fill) {
        return ctx.function->withInputsOf(ref).size();
    }
    const Type *type = node->dataType();
    return type != nullptr && (type->code() == camel::core::type::TypeCode::Tuple ||
                               type->code() == camel::core::type::TypeCode::Struct)
               ? camel::core::type::tree::arity(type).value()
               : 0;
}

bool holdsACotangent(const WalkContext &ctx, gc_node_ref_t ref) {
    const camel::runtime::GCNode *node = ctx.function->node(ref);
    if (node == nullptr) {
        return true;
    }
    const Type *type = node->dataType();
    return type == nullptr || camel::core::tangentSpaceOf(type).hasSpace();
}

void noteZeroContribution(WalkContext &ctx, gc_node_ref_t target, gc_node_ref_t from) {
    std::vector<gc_node_ref_t> &zeroedValues = ctx.book.zeroedValues;
    if (std::find(zeroedValues.begin(), zeroedValues.end(), target) != zeroedValues.end()) {
        return;
    }
    zeroedValues.push_back(target);
    const camel::runtime::GCNode *node = ctx.function->node(target);
    const Type *type                   = node != nullptr ? node->dataType() : nullptr;
    ctx.os << "TRANSPOSE function=" << ctx.function->name() << " contribution=0 node=" << target
           << " type=" << (type != nullptr ? type->toString() : std::string("?"))
           << " space=none from=" << from << '\n';
}

bool wantsCotangentAt(WalkContext &ctx, gc_node_ref_t target) {
    if (target >= ctx.book.nodeCount) {
        return true;
    }
    // A closure member is a **known value** -- a capture a caller closed over, a residual the cut
    // carried -- and not a linear input of the transposition: it is a constant of this
    // differentiation, like a `with` port, and no cotangent has a slot to land in. Asked before the
    // sets below, because a member is neither a parameter nor an answer for one.
    if (ctx.book.closurePortOf->find(target) != ctx.book.closurePortOf->end()) {
        return false;
    }
    if (!ctx.partial) {
        return true;
    }
    const camel::runtime::GCNode *node = ctx.function->node(target);
    if (node == nullptr || node->kind != rt::GCNodeKind::Port) {
        return true;
    }
    // A `with` port is a value the call supplies and the answer says nothing about: the
    // transposition answers one cotangent per ordinary parameter (`Transpose<F>`), and a weight is
    // not one.
    if (ctx.book.withPortOf->find(target) != ctx.book.withPortOf->end()) {
        return false;
    }
    const auto ports = ctx.function->normPorts();
    for (size_t i = 0; i < ports.size(); ++i) {
        if (ports[i] == target) {
            return participatesInTheAnswer(ctx, i);
        }
    }
    return true;
}

void addCotangentTo(WalkContext &ctx, gc_node_ref_t ref, gc_node_ref_t node) {
    std::vector<gc_node_ref_t> &cotangent = *ctx.book.cotangent;
    std::vector<bool> &hasCotangent       = *ctx.book.hasCotangent;
    // A cotangent no parameter asks for is not collected: with a participating set, the parameters
    // outside it are values the transposed graph reads, not answers it gives, and the answer tuple
    // has no slot for them. Dropped before the arithmetic is looked at -- the coefficient of such a
    // delivery would be the value of a participating parameter, which is exactly what does not
    // exist here.
    if (!wantsCotangentAt(ctx, ref)) {
        return;
    }
    // A value with no tangent space holds no cotangent: the type says so, and the reading names the
    // value rather than leaving the zero unexplained. This is the same question `jvp` asks of a
    // parameter, asked of a value in the middle of a graph.
    if (!holdsACotangent(ctx, ref)) {
        noteZeroContribution(ctx, ref, ref);
        return;
    }
    if (!hasCotangent[ref]) {
        cotangent[ref]    = node;
        hasCotangent[ref] = true;
        return;
    }
    Type *addType            = nullptr;
    const std::string addUri = addUriFor(ctx.function, ref, addType);
    // A space with one operator sums with it; an aggregate sums member by member (`sumOfCotangents`,
    // design principles 9.9), so `addUri` being empty is not the answer for a tuple or a struct --
    // only for a space that has neither an operator nor members.
    const bool aggregate =
        addType != nullptr && (addType->code() == camel::core::type::TypeCode::Tuple ||
                               addType->code() == camel::core::type::TypeCode::Struct);
    if (addType == nullptr || (addUri.empty() && !aggregate)) {
        // Two cotangents land on one value and the value's space registers no addition: there is no
        // sum to write, so this member answers no cotangent. Recorded here and refused by name at
        // the end of the walk (`NoCotangentArithmetic`); a `float` sum here would be an addition the
        // space never declared.
        ctx.cotangentArithmeticMissing = true;
        ctx.os << "TRANSPOSE function=" << ctx.function->name()
               << " cotangent-no-arithmetic node=" << ref << '\n';
        return;
    }
    const gc_node_ref_t summed = sumOfCotangents(ctx, addType, cotangent[ref], node);
    if (summed == rt::kInvalidNodeRef) {
        return;
    }
    cotangent[ref] = summed;
}

void noteRuleUsed(WalkContext &ctx, const std::string &name) {
    std::vector<std::string> &usedRules = *ctx.book.usedRules;
    if (std::find(usedRules.begin(), usedRules.end(), name) == usedRules.end()) {
        usedRules.push_back(name);
    }
}

gc_node_ref_t ruleCotangentOf(
    WalkContext &ctx, const std::function<gc_node_ref_t(gc_node_ref_t)> &knownValue,
    const std::string &ruleName, std::span<const gc_node_ref_t> inputs, size_t position,
    gc_node_ref_t ct, Type *ctType, std::string &why, bool &noInstance, bool &refusedAtTheLaw,
    std::optional<size_t> &unboundSlot) {
    const auto valueOf = [&](size_t slot) -> gc_node_ref_t {
        if (slot == inputs.size()) {
            // The slot past the operands is the extents: the law asked for them
            // (`sum_from_type_t` names one port more than its operator has) and the walk is the one
            // that can read them, because it has the types.
            return *ctx.book.extentsValue;
        }
        return slot < inputs.size() ? knownValue(inputs[slot]) : rt::kInvalidNodeRef;
    };
    // The operands' own types: the cotangent of a parameter is the parameter's tangent space, so the
    // law is built at the types the operator really has. A node that is not in the graph answers no
    // type, and the expansion falls back to the single-type instance.
    std::vector<Type *> operandTypes;
    // The cotangent's own type first: a law that covers two shapes of one operator writes
    // `(ct: T[..])`, and `T` has to be the type the cotangent really has -- a number here, an array
    // there -- or the right side's operator is resolved for the wrong one. `T` is the law's *element*
    // type: an array cotangent contributes its element type, whether its rank is open or fixed. The
    // law writes `T[..]` around that element, so handing it the whole array type would incorrectly
    // add another array nesting.
    Type *ctElement = ctType;
    if (ctType != nullptr && ctType->code() == camel::core::type::TypeCode::Array) {
        if (auto *array = tt::as_ptr<camel::core::type::ArrayType>(ctType)) {
            if (Type *elementType = array->elemType()) {
                ctElement = elementType;
            }
        }
    }
    operandTypes.reserve(inputs.size() + 2);
    operandTypes.push_back(ctElement);
    for (const gc_node_ref_t input : inputs) {
        const camel::runtime::GCNode *inputNode = ctx.function->node(input);
        operandTypes.push_back(
            inputNode != nullptr ? const_cast<Type *>(inputNode->dataType()) : nullptr);
    }
    if (*ctx.book.extentsValue != rt::kInvalidNodeRef) {
        // The law names one port more than its operator has: the extents, which the walk read off
        // the types (`extent_of`'s rule) and no operand of this node carries.
        operandTypes.push_back(intArrayTypeOf());
    }
    // The tuple the right side is expanded at: the one the law has an instance at, decided by the
    // same question the shape test and the choice ask (`lawInstanceTypes`), so the three cannot
    // disagree about which instance is under discussion.
    //
    // The expansion falls back to a single `float` instance on its own when the tuple it is handed
    // has none, and at an `f32` step that `float` instance is a lie:
    // `transpose_of_autodiff::mul_tangent<f32>` would pass `a f32 as ordinary argument 0 of
    // autodiff::add_cotangent<float>, which takes a float`, and the scheduler would stop on the edge
    // invariants. Handing it the step's own element type is what keeps the right side in the width
    // the cotangent is in. A law with no instance at either tuple is expanded at the ports' own
    // types, and the expansion says what it could not build in its own words.
    const std::vector<Type *> at = lawInstanceTypes(*ctx.state.context, ruleName, operandTypes);
    // A cotangent with no shape is a value an open-rank port takes (design principles 5.4: `T[..]`
    // includes rank 0, a rank-zero array being the element itself), and that is not a statement
    // about the type alone: the slot of a `T[..]` position holds the rank-zero array that *is* the
    // element. Without it the body has no rank to read, and where the body asks `numel(ct)` a number
    // is read as a pointer -- a SIGSEGV inside the numel kernel. The conversion is made wherever a
    // value's type goes from `T` to `T[..]` (`Builder::coerceTo`, whose reason is stated in
    // `ArrayType::castSlotFrom`: the run gets a value with a rank to read), and a call argument is
    // the one entrance that is not such a position (it asks the same question before it enters,
    // `builder_operators.cpp`). A law's right side is entered by this walk and not by a call, so the
    // conversion is made here -- the value is handed over through a cast to the port's own type,
    // which is what the language would have inserted had the law been written as a call.
    gc_node_ref_t handed = ct;
    Type *port           = ctType != nullptr
                               ? cotangentPortOf(*ctx.state.context, ruleName, operandTypes, position)
                               : nullptr;
    if (ctType != nullptr && ctType->code() != camel::core::type::TypeCode::Array) {
        const auto *arrayPort =
            port != nullptr && port->code() == camel::core::type::TypeCode::Array
                ? tt::as_ptr<camel::core::type::ArrayType>(port)
                : nullptr;
        if (arrayPort != nullptr && arrayPort->rankZeroType() != nullptr &&
            port->assignableFrom(ctType)) {
            const std::vector<gc_node_ref_t> source{ct};
            handed = ctx.draft->addCastNode(port);
            ctx.draft->setNormInputs(handed, std::span<const gc_node_ref_t>(source));
        }
    }
    // **The port's declared type against the type of the value really handed.** A law is filed under
    // its operator's head and its operand count and the index reads no operand types, so nothing
    // ties the port the law writes for its cotangent to the shape this step has: `mul_tangent`'s law
    // says `(ct: T[..])` and a `float` step hands it the number it was transposed at. The two agree
    // here when the cast above made them one type -- the rank-zero cotangent read as the open-rank
    // array it is.
    //
    // **A disagreement is a compiler defect, not a language rule.** No program reaches the refusal
    // below. The cotangent a program can hand a transposed function is `Tangent<ResultOf<F>>`, which
    // is the type the declaration states, so a port that disagrees with it is a law whose writing
    // and whose own instancing came apart inside the compiler -- the walk has no reading of a
    // program that produces one. So the check is a **guard**, kept because a guard at the one place
    // that hands a value through a port is worth its three lines, and the sentence it feeds says
    // `internal error: ... a compiler defect and not the program's fault`. What it must never be is
    // a sentence about what the language allows: the language allows no such program.
    //
    // What the comparison covers is a difference of the **same kind**: two arrays whose element
    // types differ (`float[3]` and `f32[3]`), or two numbers of different codes (an `f32` step
    // handed to a `float` law). A difference across kinds is not a disagreement at all: a number
    // handed to an array port is the rank-zero case the cast above answers (design principles 5.4),
    // an array handed to a number port is what the law's own instancing answers from the operands'
    // types, and a `Ref` is a type that is not decided yet, which `Type::assignableFrom` cannot
    // compare at all. A comparison that also covered the cross-kind cases finds `mean_axis`'s law on
    // shapes that answer correct numbers, and under correctness > an explicit refusal > a wrong
    // number > a crash, turning a right number into a refusal is a step *down*, so the comparison
    // stays inside one kind.
    const bool portIsArray = port != nullptr && port->code() == camel::core::type::TypeCode::Array;
    const bool handedIsArray =
        ctType != nullptr && ctType->code() == camel::core::type::TypeCode::Array;
    const bool undecided =
        (port != nullptr && port->code() == camel::core::type::TypeCode::Ref) ||
        (ctType != nullptr && ctType->code() == camel::core::type::TypeCode::Ref);
    if (port != nullptr && ctType != nullptr && handed == ct && !undecided &&
        portIsArray == handedIsArray && !port->assignableFrom(ctType)) {
        refusedAtTheLaw = true;
        // `refuseMember` writes the record whole (`TransposeRefusal{cause, param}`), so the two
        // spellings go in after it and not before: a field written first is a field the record
        // drops, and the sentence below read two empty strings.
        refuseMember(ctx, TransposeRefusalCause::CotangentPortMismatch, position);
        ctx.state.refusal.uri        = ruleName;
        ctx.state.refusal.portType   = port->toString();
        ctx.state.refusal.handedType = ctType->toString();
        ctx.os << " rule=" << ruleName << " position=" << position
               << " port=" << ctx.state.refusal.portType
               << " handed=" << ctx.state.refusal.handedType << '\n';
        return rt::kInvalidNodeRef;
    }
    // Which operand the answer read and this walk could not bind. `why` says the same sentence for
    // every such operand, so this is the fact that tells the two repairs apart: a declaration whose
    // extents are missing (write them) and a value nothing hands over (a port, not a declaration).
    // Recorded on the way out so the refusal sites below can name the class instead of reading `why`
    // back.
    size_t unbound = std::numeric_limits<size_t>::max();
    const gc_node_ref_t expanded = camel::transforms::expandRuleByName(
        *ctx.state.context, ctx.state.group, ctx.state.group.transaction(ctx.member), ruleName,
        valueOf, {handed}, position, at, why, &noInstance, &unbound);
    if (expanded == rt::kInvalidNodeRef && unbound != std::numeric_limits<size_t>::max()) {
        unboundSlot = unbound;
    }
    // **The law's right side, against the operand it answers for.** The question is asked here, on
    // the node the expansion built, because this is the one place that holds both: the type the
    // law's body really answered with (the draft's own record of that node, not a guess from the
    // law's text) and the type of the operand at `position`, which is the type the answer must have
    // -- a cotangent of a value has that value's type. A disagreement is refused with the **law's
    // name**, because the law is the thing to edit, and the two spellings follow it: see
    // `statedShapeDisagreement` for what is compared and why nothing else is.
    //
    // This runs after the body was expanded and not instead of it: a law that answers the wrong
    // shape is exactly the law that expanded without complaint, so there is no earlier moment at
    // which it could be caught. The node it built stays in the draft and is discarded with it
    // (`buildTransposeMember` throws the whole member away on a refusal), so nothing half-built
    // survives.
    if (expanded != rt::kInvalidNodeRef) {
        const camel::runtime::DraftNodeHeader *const answered = ctx.draft->header(expanded);
        const Type *const answerType       = answered != nullptr ? answered->dataType : nullptr;
        const Type *const answeredOperand =
            position + 1 < operandTypes.size() ? operandTypes[position + 1] : nullptr;
        if (std::optional<std::string> disagreement =
                statedShapeDisagreement(answerType, answeredOperand);
            disagreement.has_value()) {
            refusedAtTheLaw = true;
            refuseMember(ctx, TransposeRefusalCause::LawAnswerShape, position);
            // The record carries the law and the two spellings; `refuseMember` wrote the cause
            // whole, so the fields go in after it.
            ctx.state.refusal.rule = ruleName;
            ctx.state.refusal.answerType = camel::core::type::typeToString(answerType);
            ctx.state.refusal.answeredOperandType =
                camel::core::type::typeToString(answeredOperand);
            ctx.os << " rule=" << ruleName << " position=" << position
                   << " answer=" << ctx.state.refusal.answerType
                   << " operand=" << ctx.state.refusal.answeredOperandType
                   << " reason=" << *disagreement << '\n';
            return rt::kInvalidNodeRef;
        }
    }
    return expanded;
}

bool joinMemberReadOf(
    WalkContext &ctx, gc_node_ref_t ref, const std::vector<gc_node_ref_t> &slots) {
    std::vector<gc_node_ref_t> &cotangent = *ctx.book.cotangent;
    std::vector<bool> &hasCotangent       = *ctx.book.hasCotangent;
    const camel::runtime::GCNode *const joinNode = ctx.function->node(ref);
    const Type *const joinPrimal = joinNode != nullptr ? joinNode->dataType() : nullptr;
    const gc_node_ref_t whole    = cotangent[ref];
    if (joinPrimal == nullptr ||
        (joinPrimal->code() != camel::core::type::TypeCode::Tuple &&
         joinPrimal->code() != camel::core::type::TypeCode::Struct)) {
        refuseMember(ctx, TransposeRefusalCause::Component);
        ctx.os << " node=" << ref
               << " type=" << (joinPrimal != nullptr ? joinPrimal->toString() : "?")
               << " why=a-member-read-of-a-value-that-is-not-an-aggregate\n";
        return false;
    }
    // The value the fill is written at: the value's own tangent space, which for a tuple of numbers
    // and for a struct of them is the value's type itself (`tangentSpaceOf`: `self`), and for a
    // struct whose members are `f32` is a struct of `f32` -- not one of `float` (design principles
    // 9.9).
    const camel::core::TangentSpaceAnswer joinSpace = camel::core::tangentSpaceOf(joinPrimal);
    if (!joinSpace.hasSpace() || joinSpace.space == nullptr) {
        refuseMember(ctx, TransposeRefusalCause::Component);
        ctx.os << " node=" << ref << " type=" << joinPrimal->toString()
               << " why=no-tangent-space-for-the-parameter\n";
        return false;
    }
    const auto memberCount = camel::core::type::tree::arity(joinPrimal);
    if (!memberCount) {
        refuseMember(ctx, TransposeRefusalCause::Component);
        ctx.os << " node=" << ref << " type=" << joinPrimal->toString()
               << " why=not-a-type-with-members\n";
        return false;
    }
    if (slots.size() > *memberCount) {
        refuseMember(ctx, TransposeRefusalCause::Component);
        ctx.os << " node=" << ref << " slots=" << slots.size() << " arity=" << *memberCount << '\n';
        return false;
    }
    std::vector<gc_node_ref_t> parts;
    for (size_t i = 0; i < *memberCount; ++i) {
        Type *const memberType = camel::core::type::tree::childType(joinPrimal, i);
        const camel::core::TangentSpaceAnswer memberSpace =
            memberType != nullptr ? camel::core::tangentSpaceOf(memberType)
                                  : camel::core::TangentSpaceAnswer{};
        if (!memberSpace.hasSpace() || memberSpace.space == nullptr) {
            refuseMember(ctx, TransposeRefusalCause::Component);
            ctx.os << " node=" << ref << " member=" << i
                   << " type=" << (memberType != nullptr ? memberType->toString() : "?")
                   << " why=no-tangent-space\n";
            return false;
        }
        gc_node_ref_t part = i < slots.size() ? slots[i] : rt::kInvalidNodeRef;
        if (part == rt::kInvalidNodeRef) {
            // The member nobody read: the zero of its own space, asked the same way the answer
            // assembly asks for an unread parameter's (`zeroOfSpaceOf`, then the zero the type
            // itself states). A space with neither is a zero this walk cannot write, and saying so
            // is the refusal rather than a `float` where the member's own shape belongs.
            part = zeroOfSpaceOf(
                *ctx.draft, memberSpace.space,
                ctx.forwardOf != nullptr && ref < ctx.forwardOf->size() ? (*ctx.forwardOf)[ref]
                                                                        : rt::kInvalidNodeRef);
            if (part == rt::kInvalidNodeRef) {
                part = zeroOfCotangentType(ctx, memberType, memberSpace.space);
            }
            if (part == rt::kInvalidNodeRef) {
                refuseMember(ctx, TransposeRefusalCause::Component);
                ctx.os << " node=" << ref << " member=" << i << " memberType="
                       << (memberType != nullptr ? memberType->toString() : "?")
                       << " space=" << memberSpace.space->toString()
                       << " why=no-zero-for-an-unread-member\n";
                return false;
            }
        }
        parts.push_back(part);
    }
    const gc_node_ref_t joined = aggregateOfMembers(ctx, joinSpace.space, parts);
    if (joined == rt::kInvalidNodeRef) {
        refuseMember(ctx, TransposeRefusalCause::Component);
        ctx.os << " node=" << ref << " type=" << joinPrimal->toString()
               << " why=the-member-join-built-nothing\n";
        return false;
    }
    if (whole != rt::kInvalidNodeRef) {
        // The value was read **both whole and by member**, and the adjoint of the two uses is their
        // sum (`f(q) = q.0 * 2.0 + g(q)`). That sum is the aggregate's addition, which is its
        // members' addition one place at a time -- derived from the type, not registered (design
        // principles 9.9) -- so this is a sum and not a refusal (the aggregate's addition is derived
        // from its type; refusing would judge the registry's completeness and call it the type's
        // ability).
        const gc_node_ref_t summed = sumOfCotangents(ctx, joinSpace.space, whole, joined);
        if (summed == rt::kInvalidNodeRef) {
            refuseMember(ctx, TransposeRefusalCause::Component);
            ctx.os << " node=" << ref << " type=" << joinPrimal->toString()
                   << " whole-and-member slots=" << slots.size()
                   << " why=no-addition-for-a-member-of-the-parameter\n";
            return false;
        }
        cotangent[ref] = summed;
    } else {
        cotangent[ref] = joined;
    }
    hasCotangent[ref] = true;
    return true;
}

gc_node_ref_t armCotangentOf(
    WalkContext &,
    const std::function<
        std::vector<gc_node_ref_t>(uint32_t, const std::vector<ArmSeed> &,
                                   const std::vector<gc_node_ref_t> &)> &runSeeded,
    uint32_t oldRegion, gc_node_ref_t armResult, gc_node_ref_t armCotangent,
    const std::vector<gc_node_ref_t> &stops) {
    const std::vector<gc_node_ref_t> back =
        runSeeded(oldRegion, {ArmSeed{armResult, armCotangent}}, stops);
    return back.empty() ? rt::kInvalidNodeRef : back.front();
}

// What one arm does with the cotangents it meets (see `transpose/walk.h` for the four places a
// cotangent can land). These were the innermost lambdas of the arm's walk, which is why they are the
// last of the walk's bookkeeping to leave it: what they read is the arm's own state (`ArmWalk`).

void addLocalCotangent(WalkContext &ctx, ArmWalk &arm, gc_node_ref_t ref, gc_node_ref_t node) {
    std::vector<gc_node_ref_t> &local = *arm.local;
    std::vector<bool> &hasLocal       = *arm.hasLocal;
    if (!hasLocal[ref]) {
        local[ref]    = node;
        hasLocal[ref] = true;
        return;
    }
    local[ref] = sumOfCotangents(ctx, cotangentTypeOfValue(ctx, ref), local[ref], node);
}

void deliverCotangent(
    WalkContext &ctx, ArmWalk &arm, gc_node_ref_t target, gc_node_ref_t node, gc_node_ref_t from) {
    // A value the round read that cannot hold a cotangent contributes zero: the round's answer does
    // not depend on it in a differentiable way, so there is nothing to carry out of the loop and no
    // accumulator to grow. Dropped here rather than at the escape collection, which would otherwise
    // build a float sum for an integer.
    if (!holdsACotangent(ctx, target)) {
        noteZeroContribution(ctx, target, from);
        return;
    }
    if (ctx.function->nodeRegion(target) == arm.oldRegion) {
        addLocalCotangent(ctx, arm, target, node);
        return;
    }
    // The space every accumulation below fills: a state's cotangent for a stop, the member's for a
    // component read, the escaped value's for an escape. One answer, asked once -- the place being
    // filled is a node this build made, so it cannot be asked.
    Type *const ctSpace = cotangentTypeOfValue(ctx, target);
    const std::vector<gc_node_ref_t> &stops = *arm.stops;
    std::vector<gc_node_ref_t> &portCotangents = *arm.portCotangents;
    for (size_t i = 0; i < stops.size(); ++i) {
        if (stops[i] != target) {
            continue;
        }
        portCotangents[i] = sumOfCotangents(ctx, ctSpace, portCotangents[i], node);
        return;
    }
    // A residual the arm read: the cotangent goes back into the slot it came from, named by the
    // slot's number. Nothing inside the arm stands for it, so dropping it here would be a claim
    // about the value rather than a rule about the cotangent.
    if (const camel::runtime::GCNode *targetNode = ctx.function->node(target);
        targetNode != nullptr && targetNode->kind == rt::GCNodeKind::Accs) {
        const auto readFrom = ctx.function->normInputsOf(target);
        const auto *body    = ctx.function->nodeBodyAs<rt::GCAccsBody>(target);
        const bool onAResidualPort =
            !readFrom.empty() && std::any_of(
                                     stops.begin(), stops.end(), [&](gc_node_ref_t stopRef) {
                                         return stopRef == readFrom.front();
                                     });
        if (body != nullptr && onAResidualPort) {
            auto &slot = (*arm.residualBack)[body->value];
            slot       = sumOfCotangents(ctx, ctSpace, slot, node);
            ctx.os << "TRANSPOSE function=" << ctx.function->name()
                   << " residual-return slot=" << body->value << " region=" << arm.oldRegion
                   << '\n';
            return;
        }
    }
    if (arm.allowEscapes) {
        // The round read this value from outside: its cotangent leaves the loop through the capture
        // channel, accumulated over the rounds.
        auto &slot = (*arm.escapes)[target];
        if (slot.cotangent == rt::kInvalidNodeRef) {
            slot = Escape{node, from};
        } else {
            slot.cotangent = sumOfCotangents(ctx, ctSpace, slot.cotangent, node);
        }
        return;
    }
    // Named, not silent: a cotangent the arm cannot place -- a value from outside the region that is
    // neither a stop nor a residual the arm read -- is the one failure this walk has no rule for, and
    // the next reader needs to know which value it landed on.
    ctx.os << "TRANSPOSE function=" << ctx.function->name() << " arm-unplaced region="
           << arm.oldRegion << " target=" << target << " from=" << from << '\n';
    *arm.armsOk = false;
}

gc_node_ref_t intConstantNode(WalkContext &ctx, int64_t value) {
    return ctx.draft->materializeStaticValue(
        camel::core::rtdata::toSlot<camel::core::rtdata::Int64>(value), Type::Int64(),
        rt::kGCNodeFlagConstant);
}

bool isStateInitialValue(const std::vector<gc_node_ref_t> &stateInitials, gc_node_ref_t what) {
    return std::find(stateInitials.begin(), stateInitials.end(), what) != stateInitials.end();
}

gc_node_ref_t componentReadOf(
    WalkContext &ctx, const std::vector<Type *> &componentTypes, gc_node_ref_t loopNode,
    uint32_t index) {
    Type *const componentType = index < componentTypes.size() ? componentTypes[index] : nullptr;
    if (componentType == nullptr) {
        return rt::kInvalidNodeRef;
    }
    const gc_node_ref_t part = ctx.draft->addAccsNode(componentType, index);
    ctx.draft->setNormInputs(part, std::span<const gc_node_ref_t>(&loopNode, 1));
    return part;
}

} // namespace camel::transforms
