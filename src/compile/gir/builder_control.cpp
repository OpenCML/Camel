/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Control flow: branches, the arms of a match, loops, and the exit each of them gives.
 */

#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"

#include "camel/compile/gir/static_function.h"
#include "camel/core/numeric_convert.h"
#include "camel/core/operator_names.h"
#include "camel/core/rtdata/string.h"
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

/// The exact value of a literal primitive datum: an integer or a real. A literal element
/// (field) of a composite is converted from it, so that an integer is one rounding from the
/// value written rather than the same double rounded again.
std::optional<literal::Value> exactValueOfData(const data_ptr_t &data) {
    if (const auto integer = std::dynamic_pointer_cast<LongData>(data); integer != nullptr) {
        return literal::Value::fromInt64(integer->data());
    }
    if (const auto integer = std::dynamic_pointer_cast<IntData>(data); integer != nullptr) {
        return literal::Value::fromInt64(integer->data());
    }
    if (const auto real = std::dynamic_pointer_cast<DoubleData>(data); real != nullptr) {
        return literal::Value::fromDouble(real->data());
    }
    if (const auto real = std::dynamic_pointer_cast<FloatData>(data); real != nullptr) {
        return literal::Value::fromDouble(static_cast<double>(real->data()));
    }
    return std::nullopt;
}

bool isNumericType(Type *type) {
    return type != nullptr && type->isPrimitive() &&
           (type->code() == TypeCode::Int32 || type->code() == TypeCode::Int64 ||
            type->code() == TypeCode::Float32 || type->code() == TypeCode::Float64);
}

/// Whether the element `data` of a composite literal can be given `type`: the question
/// compositeDataInType / literalElementInType answer while building the converted value, asked
/// here without building it and without reporting. A value of a kind that enters the type is a
/// fit even when the type cannot hold it (`3000000000` in an `i32`): that is a value the
/// conversion refuses, with the value as written, once the callee is known.
bool elementEntersIn(
    const data_ptr_t &data, Type *type, const GCT::node_ptr_t &origin,
    const literal::Value *written);

/// Whether the composite literal `data` can be given `type`: the walk of compositeDataInType,
/// unchanged, for the positions a literal may enter (design principles 5.1). The two must agree;
/// a literal this accepts is converted by compositeDataInType, which reports what does not fit.
bool compositeEntersIn(const data_ptr_t &data, Type *type, const GCT::node_ptr_t &origin) {
    if (data == nullptr || type == nullptr || asTagged(type) != nullptr) {
        return false;
    }
    if (type->assignableFrom(data->type())) {
        return true;
    }
    if (type->code() == TypeCode::Array && data->type()->code() == TypeCode::Array) {
        Type *elemType   = tt::as_ptr<ArrayType>(type)->elemType();
        const auto array = std::dynamic_pointer_cast<ArrayData>(data);
        if (elemType == nullptr || array == nullptr) {
            return false;
        }
        if (array->raw().empty()) {
            // `[]`: the elements it does not have are the only thing its type leaves open.
            return true;
        }
        const auto *written =
            origin != nullptr ? GCT::Tree::of(origin)->literalElementsOf(data) : nullptr;
        for (size_t index = 0; index < array->raw().size(); ++index) {
            const literal::Value *exact = nullptr;
            if (written != nullptr && index < written->size() && (*written)[index].has_value()) {
                exact = &*(*written)[index];
            }
            if (!elementEntersIn(array->raw()[index], elemType, origin, exact)) {
                return false;
            }
        }
        return true;
    }
    if (type->code() == TypeCode::Struct && data->type()->code() == TypeCode::Struct) {
        const auto *structType = tt::as_ptr<StructType>(type);
        const auto object      = std::dynamic_pointer_cast<StructData>(data);
        if (structType == nullptr || object == nullptr ||
            object->raw().size() != structType->size()) {
            return false;
        }
        const auto *written =
            origin != nullptr ? GCT::Tree::of(origin)->literalElementsOf(data) : nullptr;
        size_t index = 0;
        for (const auto &[name, value] : object->raw()) {
            const std::optional<size_t> fieldIndex = structType->findField(name);
            if (!fieldIndex.has_value()) {
                return false;
            }
            const literal::Value *exact = nullptr;
            if (written != nullptr && index < written->size() && (*written)[index].has_value()) {
                exact = &*(*written)[index];
            }
            ++index;
            if (!elementEntersIn(value, structType->typeAt(*fieldIndex), origin, exact)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool elementEntersIn(
    const data_ptr_t &data, Type *type, const GCT::node_ptr_t &origin,
    const literal::Value *written) {
    if (data == nullptr || type == nullptr || asTagged(type) != nullptr) {
        return false;
    }
    if (type->assignableFrom(data->type())) {
        return true;
    }
    if (isComposite(data->type()->code())) {
        return compositeEntersIn(data, type, origin);
    }
    const std::optional<literal::Value> fallback =
        written == nullptr ? exactValueOfData(data) : std::nullopt;
    const literal::Value *value = written != nullptr ? written : fallback ? &*fallback : nullptr;
    return value != nullptr && isNumericType(type) && isNumericType(data->type()) &&
           literal::kindEnters(*value, type);
}

} // namespace

node_handle_t Builder::coerceTo(node_handle_t node, Type *target, const GCT::node_ptr_t &origin) {
    Type *source = valueTypeOf(nodeTypeOf(node));
    if (target->assignableFrom(source)) {
        return node;
    }
    // A literal takes the type of its context (design principles 5.1): `let x: float = 1`,
    // `f(2)` with `f(x: f32)`, `return 2` under `f32`. A value that does not fit the target
    // type is reported here — never cut to size. What already has a type is not converted:
    // `let x: float = n` with `n: int` is still an error.
    if (const std::optional<literal::Value> value = literalValueOfExpr(origin);
        value.has_value() && target->isPrimitive() && literal::kindEnters(*value, target)) {
        return literalInType(*value, target, origin, true);
    }
    // An array or struct literal takes the element (field) type its position gives it (design
    // principles 5.1): `f([1, 2])` where `f` takes a `float[]`, `return { a: 1 }` under
    // `{ a: float }`. Its elements are positions with an expected type as well, and a literal
    // element that the type cannot hold is reported here.
    if (node_handle_t typed = compositeLiteralInType(node, target, origin); typed != nullptr) {
        return typed;
    }
    if (!entersImplicitly(target, source)) {
        return nullptr;
    }
    node = valueOf(node); // what a variable holds, not the variable
    if (TupleType *tagged = asTagged(target)) {
        // `null` is the null slot of every `T?`; any other value enters the payload first.
        if (tagged->isNullable() && source->code() == TypeCode::Void) {
            return castNodeTo(node, tagged, origin);
        }
        node_handle_t inner = coerceTo(node, tagged->payloadType(), origin);
        return inner != nullptr ? castNodeTo(inner, tagged, origin) : nullptr;
    }
    // The empty literal `[]`: the same literal with the element type its position gives it.
    ASSERT(target->code() == TypeCode::Array, "Only an empty array literal is left here.");
    node_handle_t typed = currGraph_->addStaticDataNode(
        tt::as_shared<Data>(ArrayData::create(tt::as_ptr<ArrayType>(target)->elemType())));
    registerNodeOrigin(context_, typed, origin, "gir.data");
    return typed;
}

node_handle_t Builder::compositeLiteralInType(
    node_handle_t node, Type *target, const GCT::node_ptr_t &origin) {
    if (origin == nullptr || target == nullptr || asTagged(target) != nullptr ||
        origin->type() != GCT::NodeKind::DATA) {
        return nullptr;
    }
    // Only the constant the literal was lowered to: a value computed by the program is not a
    // literal, and takes no type from its position. A constant lives in a static slot, whose
    // index is negative (a runtime slot index is not).
    if (!nodeIsKind(node, runtime::GCNodeKind::Data) || node->header.dataIndex >= 0) {
        return nullptr;
    }
    const data_ptr_t &data = origin->data();
    if (data == nullptr || !isComposite(data->type()->code())) {
        return nullptr;
    }
    if (!tt::as_shared<CompositeData>(data)->resolved()) {
        // An element is computed (`{ a: x, b: 2.0 }`): the literal is not one value, and the
        // path of the computed elements (a FILL node) decides.
        return nullptr;
    }
    const data_ptr_t typed = compositeDataInType(data, target, origin);
    if (typed == nullptr) {
        return nullptr;
    }
    node_handle_t result = currGraph_->addStaticDataNode(typed);
    if (varied_ && currGraph_->outerGraph().get() != nullptr) {
        node_handle_t copyNode = currGraph_->addCopyNode(typed->type());
        linkNodes(LinkType::Norm, result, copyNode);
        result = copyNode;
    }
    registerNodeOrigin(context_, result, origin, "gir.data.literal");
    return result;
}

bool Builder::compositeLiteralEntersIn(
    node_handle_t node, Type *target, const GCT::node_ptr_t &origin) {
    if (origin == nullptr || target == nullptr || asTagged(target) != nullptr ||
        origin->type() != GCT::NodeKind::DATA) {
        return false;
    }
    // Only the constant the literal was lowered to, as in compositeLiteralInType: a value the
    // program computes is not a literal, and a runtime slot's value is not one either.
    if (!nodeIsKind(node, runtime::GCNodeKind::Data) || node->header.dataIndex >= 0) {
        return false;
    }
    const data_ptr_t &data = origin->data();
    if (data == nullptr || !isComposite(data->type()->code()) ||
        !tt::as_shared<CompositeData>(data)->resolved()) {
        return false;
    }
    return compositeEntersIn(data, target, origin);
}

data_ptr_t Builder::compositeDataInType(
    const data_ptr_t &data, Type *type, const GCT::node_ptr_t &origin) {
    if (data == nullptr || type == nullptr || asTagged(type) != nullptr) {
        return nullptr;
    }
    if (type->assignableFrom(data->type())) {
        return data;
    }
    if (type->code() == TypeCode::Array && data->type()->code() == TypeCode::Array) {
        Type *elemType   = tt::as_ptr<ArrayType>(type)->elemType();
        const auto array = std::dynamic_pointer_cast<ArrayData>(data);
        if (elemType == nullptr || array == nullptr) {
            return nullptr;
        }
        // The exact value each element was written as, when the GCT kept them (a composite
        // literal of literals): an element is then converted from the value written, not from
        // the double it was lowered to (design principles 5.1).
        if (array->raw().empty()) {
            // `[]`: the elements it does not have are the only thing its type leaves open, so
            // the empty array takes the element type of the position it stands in (design
            // principles 5.1, 5.2) — `[]` where an `int[]` is expected is the empty `int[]`,
            // whether the position is a declaration, a body's value or a loop state. A literal
            // that has elements keeps their join (H24), so this branch only covers `[]`.
            return ArrayData::create(elemType);
        }
        const auto *written =
            origin != nullptr ? GCT::Tree::of(origin)->literalElementsOf(data) : nullptr;
        ArrayDataFactory factory;
        size_t index = 0;
        for (const auto &element : array->raw()) {
            const literal::Value *exact = nullptr;
            if (written != nullptr && index < written->size() && (*written)[index].has_value()) {
                exact = &*(*written)[index];
            }
            ++index;
            data_ptr_t converted = literalElementInType(element, elemType, origin, exact);
            if (converted == nullptr) {
                return nullptr;
            }
            factory.add(converted);
        }
        return factory.build();
    }
    if (type->code() == TypeCode::Struct && data->type()->code() == TypeCode::Struct) {
        const auto *structType = tt::as_ptr<StructType>(type);
        const auto object      = std::dynamic_pointer_cast<StructData>(data);
        if (structType == nullptr || object == nullptr ||
            object->raw().size() != structType->size()) {
            return nullptr;
        }
        const auto *written =
            origin != nullptr ? GCT::Tree::of(origin)->literalElementsOf(data) : nullptr;
        StructDataFactory factory;
        size_t index = 0;
        for (const auto &[name, value] : object->raw()) {
            const std::optional<size_t> fieldIndex = structType->findField(name);
            if (!fieldIndex.has_value()) {
                return nullptr;
            }
            const literal::Value *exact = nullptr;
            if (written != nullptr && index < written->size() && (*written)[index].has_value()) {
                exact = &*(*written)[index];
            }
            ++index;
            data_ptr_t converted =
                literalElementInType(value, structType->typeAt(*fieldIndex), origin, exact);
            if (converted == nullptr) {
                return nullptr;
            }
            factory.add(name, converted);
        }
        return factory.build();
    }
    return nullptr;
}

data_ptr_t Builder::literalElementInType(
    const data_ptr_t &data, Type *type, const GCT::node_ptr_t &origin,
    const literal::Value *written) {
    if (data == nullptr || type == nullptr || asTagged(type) != nullptr) {
        return nullptr;
    }
    if (type->assignableFrom(data->type())) {
        return data;
    }
    if (isComposite(data->type()->code())) {
        // A nested literal: an array in an array, a struct in an array, and so on.
        return compositeDataInType(data, type, origin);
    }
    const std::optional<literal::Value> fallback =
        written == nullptr ? exactValueOfData(data) : std::nullopt;
    const literal::Value *value = written != nullptr ? written : fallback ? &*fallback : nullptr;
    if (value == nullptr || !isNumericType(type) || !isNumericType(data->type())) {
        // Not a numeric literal (a string, a bool): the type is not one it can be given by
        // taking the position's type, so the value is left as it is.
        return nullptr;
    }
    if (!literal::kindEnters(*value, type)) {
        rejectLiteralData(data, type, origin->origin());
    }
    if (const auto converted = value->toData(type); converted.has_value()) {
        return *converted;
    }
    rejectLiteralData(data, type, origin->origin());
}

void Builder::coerceArmExit(
    const compile_graph_ptr_t &arm, Type *target, const GCT::node_ptr_t &origin) {
    Type *exitType = arm->funcType()->exitType();
    if (exitType != nullptr && target->assignableFrom(exitType)) {
        return;
    }
    // The nodes are added to the arm's own graph; no name is looked up, so the scopes of the
    // position the branch stands at do no harm.
    const compile_graph_ptr_t outer = std::exchange(currGraph_, arm);
    node_handle_t output            = arm->draft().node(arm->draft().exitNode());
    node_handle_t coerced           = coerceTo(output, target, origin);
    ASSERT(coerced != nullptr, "A branch arm is joined only with a type its value enters.");
    if (normInputsOf(coerced).empty() && !ctrlInputsOf(output).empty()) {
        // A literal took the place of the output (`[]`): what the output waited for (the
        // statements of a sync arm) is still waited for.
        node_handle_t gate = currGraph_->addGateNode(target);
        linkNodes(LinkType::Norm, coerced, gate);
        linkNodes(LinkType::Ctrl, output, gate);
        coerced = gate;
    }
    arm->setExitType(target);
    setGraphReturnFromResult(arm, nodeIdOf(coerced), nodeIdOf(coerced), target);
    currGraph_ = outer;
}

Type *Builder::declaredExitTypeHere() const {
    compile_graph_ptr_t graph = currGraph_;
    while (graph != nullptr && openArmGraphs_.contains(graph.get())) {
        graph = graph->outerGraph();
    }
    FunctionType *funcType = graph != nullptr ? graph->funcType() : nullptr;
    // An undeclared return type is set from the first value returned (see
    // checkDeclaredReturn): before that, a type that is present was declared.
    if (funcType == nullptr || !funcType->hasExitType() ||
        graph->draft().exitNode() != runtime::kInvalidNodeRef) {
        return nullptr;
    }
    return funcType->exitType();
}

Type *Builder::ownDeclaredExitType() const {
    FunctionType *funcType = currGraph_ != nullptr ? currGraph_->funcType() : nullptr;
    if (funcType == nullptr || !funcType->hasExitType() ||
        currGraph_->draft().exitNode() != runtime::kInvalidNodeRef) {
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
    if (currGraph_ != nullptr && openArmGraphs_.contains(currGraph_.get())) {
        // Inside a branch arm the value of a `return` is the arm's value, so its position is
        // the one the branch stands at; the function's declared return type has nothing to do
        // with it (design principles 5.1, language reference 7.3).
        return expectedValueType_;
    }
    return declaredExitTypeHere();
}

node_handle_t Builder::adaptExitValue(const GCT::node_ptr_t &gct, node_handle_t value) {
    Type *declared   = declaredExitTypeHere();
    Type *valueType  = valueTypeOf(nodeTypeOf(value));
    const auto where = gct->origin() != 0 ? gct->origin() : gct->at(0)->origin();
    if (gct->exitType() == ExitType::Throw) {
        // `throw e`: the exit `return` is, with the failed form of the declared `T!` as its
        // value (design principles 3.9). OPER __throw__ makes the failed value, the CAST
        // gives it the declared type; both are the same object.
        TupleType *fallible = asFallible(declared);
        if (fallible == nullptr) {
            diags_->of(SemanticDiag::ThrowOutsideFallible)
                .atOrigin(where)
                .commit(
                    declared == nullptr
                        ? "the function around it declares no return type"
                        : "the function around it returns '" + declared->toString() + "'");
            throw BuildAbortException();
        }
        // Design principles 3.9: `throw` takes an error value (made by `error(kind,
        // message)` or bound by `case error e`), nothing else.
        if (!valueType->equals(errorType())) {
            diags_->of(SemanticDiag::ThrowNotAnError).atOrigin(where).commit(valueType->toString());
            throw BuildAbortException();
        }
        node_handle_t thrown = applyTagOperator("__throw__", bindTemporary(value), gct->at(0));
        return fallible->equals(nodeTypeOf(thrown)) ? thrown
                                                    : castNodeTo(thrown, fallible, gct->at(0));
    }
    // `return v` under a declared `T?` / `T!` puts `v` into that form: the counterpart of
    // `throw`, and one of the positions where a value enters by itself (design principles
    // 3.8; coerceTo). A value that already has the declared type, and one that fits neither
    // way (reported by checkDeclaredReturn), is left alone. In a branch arm the position is
    // the arm's (a `return` there is not the function's return, handover H24).
    Type *position = expectedExitTypeHere();
    if (position != nullptr && !position->assignableFrom(valueType)) {
        if (node_handle_t entered = coerceTo(value, position, gct->at(0))) {
            return entered;
        }
    }
    return value;
}

compile_graph_ptr_t
Builder::buildArmGraph(const GCT::node_ptr_t &exec, const std::function<node_handle_t()> &body) {
    graph_ptr_t subGraph = enterScope(FunctionType::create());
    registerGraphOrigin(context_, subGraph, exec, "gir.brch.case.graph");
    bodyStates_[subGraph.get()] = BodyState::Open;
    openArmGraphs_.insert(subGraph.get());
    // A body given by the caller is a body of its own like any other (see visitBodyNode).
    const bool outerSynced        = synced_;
    const node_handle_t outerLast = lastSyncedNode_;
    const auto restore            = [&] {
        synced_         = outerSynced;
        lastSyncedNode_ = outerLast;
        openArmGraphs_.erase(subGraph.get());
        bodyStates_[subGraph.get()] = BodyState::Built;
    };
    try {
        if (body) {
            synced_         = false;
            lastSyncedNode_ = {};
            setBodyResult(subGraph, body());
        } else {
            setBodyResult(subGraph, visitBodyNode(exec));
        }
    } catch (...) {
        restore();
        leaveScope();
        throw;
    }
    restore();
    armGraphs_.insert(subGraph.get());
    unit_->builtBodies.push_back(subGraph);
    leaveScope();
    return subGraph;
}

node_handle_t Builder::visitTaggedBrchNode(const GCT::node_ptr_t &gct) {
    using Kind         = GCT::BranchKind;
    const Kind kind    = gct->branchKind();
    const bool onNull  = kind == Kind::NullThen;
    const char *symbol = onNull ? "??" : kind == Kind::ErrorThen ? "!!" : "?->";

    node_handle_t subject = valueNodeOf(visit(gct->at(0)));
    if (onNull && asTagged(valueTypeOf(nodeTypeOf(subject))) == nullptr &&
        isNarrowedName(gct->at(0))) {
        // `x ?? d` where an `if` around it already narrowed `x` to a `T` (design principles
        // 3.8): allowed, with a warning.
        subject = reopenNarrowedName(gct->at(0), subject, gct, "??");
    }
    Type *subjectType = valueTypeOf(nodeTypeOf(subject));
    TupleType *tagged = asTagged(subjectType);
    if (tagged == nullptr || tagged->isNullable() != onNull) {
        // DESIGN-QUESTION(nullable-error) Q6: `?->` is defined for errors only.
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
    node_handle_t tagNode  = applyTagOperator(onNull ? "__is_null__" : "__is_error__", name, gct);
    const char *valueOp    = onNull ? "__null_value__" : "__error_value__";
    const GCT::node_ptr_t exec = gct->at(1)->at(0);
    std::vector<compile_graph_ptr_t> arms(2);

    if (kind != Kind::ErrorPass) {
        // Arm 0 is the right-hand side, a subgraph of its own: it is entered, and so
        // evaluated, only when the tag selects it. Arm 1 takes the value out.
        arms[0] = buildArmGraph(exec);
        // Design principles 3.8: the right-hand side is a `T`, and the result is a `T`; or
        // it is itself a `T?` (`T!` for `!!`), and the result is that: `a ?? b ?? 9` tries
        // `a`, then `b`, then gives the default. Arm 1 then hands on `a` as it is instead of
        // taking the value out. Either way it is one branch.
        Type *fallback   = arms[0]->funcType()->exitType();
        const bool plain = fallback != nullptr && (payload->unify(fallback) != nullptr ||
                                                   entersImplicitly(payload, fallback));
        const bool chained =
            !plain && fallback != nullptr && entersImplicitly(subjectType, fallback);
        if (plain) {
            if (payload->unify(fallback) == nullptr) {
                coerceArmExit(arms[0], payload, gct);
            }
            arms[1] = buildArmGraph(exec, [&] { return applyTagOperator(valueOp, name, gct); });
        } else if (chained) {
            coerceArmExit(arms[0], subjectType, gct);
            arms[1] = buildArmGraph(exec, [&] {
                const GCT::node_ptr_t dref = tree_->makeDRef(name);
                dref->setOrigin(gct->origin());
                return visitDRefNode(dref);
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

    // `x ?-> f`: arm 1 calls `f` with the value and yields a `U!` (put into that form when
    // `f` returns a plain `U`); arm 0 hands the failed `x` on as that `U!`. Arm 1 is built
    // first because it is what determines `U`.
    Type *resultType = nullptr;
    arms[1]          = buildArmGraph(exec, [&] {
        insertNode(GCT::kPassedValueName, applyTagOperator(valueOp, name, gct));
        node_handle_t result = visitBodyNode(exec);
        ASSERT(result != nullptr, "The call of '?->' produced no node.");
        if (nodeIsKind(result, runtime::GCNodeKind::Gate) && !normInputsOf(result).empty()) {
            result = nodeDraftOf(result)->node(normInputsOf(result).front());
        }
        Type *type = valueTypeOf(nodeTypeOf(result));
        if (asFallible(type) != nullptr) {
            resultType = type;
            return result;
        }
        resultType = TupleType::fallible(type);
        return castNodeTo(result, resultType, gct);
    });
    arms[0]          = buildArmGraph(exec, [&] {
        const GCT::node_ptr_t dref = tree_->makeDRef(name);
        dref->setOrigin(gct->origin());
        node_handle_t failed = visitDRefNode(dref);
        return resultType->equals(subjectType) ? failed : castNodeTo(failed, resultType, gct);
    });
    return buildBranch(gct, tagNode, &arms);
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
    GCT::node_ptr_t taggedCase = nullptr, restCase = nullptr;
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
            if (taggedCase != nullptr) {
                malformed(
                    std::string("it has more than one '") + (isNull ? "case null" : "case error") +
                    "'");
            }
            taggedCase = caseNode;
        } else if (caseType == CaseType::Bind || caseType == CaseType::Else) {
            if (restCase != nullptr) {
                malformed("it has more than one case for the value");
            }
            restCase = caseNode;
        } else {
            malformed("a literal case cannot be compared with a value that is not opened yet");
        }
    }
    if (taggedCase == nullptr) {
        malformed(type->isNullable() ? "'case null' is missing" : "'case error e' is missing");
    }
    if (restCase == nullptr) {
        malformed("the case for the value is missing ('case v' or 'case _')");
    }

    const std::string name = bindTemporary(scrutinee);
    node_handle_t tagNode =
        applyTagOperator(type->isNullable() ? "__is_null__" : "__is_error__", name, gct);
    const GCT::node_ptr_t taggedExec = taggedCase->at(taggedCase->size() - 1);
    const GCT::node_ptr_t restExec   = restCase->at(0);
    std::vector<compile_graph_ptr_t> arms(2);
    arms[0] = buildArmGraph(taggedExec, [&] {
        if (type->isFallible()) {
            insertNode(taggedCase->binding(), applyTagOperator("__error_info__", name, taggedCase));
        }
        return visitBodyNode(taggedExec);
    });
    arms[1] = buildArmGraph(restExec, [&] {
        if (restCase->caseType() == CaseType::Bind) {
            insertNode(
                restCase->binding(),
                applyTagOperator(
                    type->isNullable() ? "__null_value__" : "__error_value__",
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
    node_handle_t isNull =
        applyTagOperator("__is_null__", bindTemporary(valueOf(operands[subject].node)), gct);
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

node_handle_t Builder::reopenNarrowedName(
    const GCT::node_ptr_t &expr, node_handle_t subject, const GCT::node_ptr_t &at,
    const char *construct) {
    // Design principles 3.8: the construct is still allowed on the name; it is applied to the
    // value put back into `T?`, so that it is checked and built exactly as on the name before
    // narrowing (the right side of `??` still has to fit, the cases of the `match` still have
    // to be its fixed patterns), and only its null arm can never be taken.
    Type *type = valueTypeOf(nodeTypeOf(subject));
    diags_->of(SemanticDiag::KnownNotNull)
        .atOrigin(at->origin())
        .commit(expr->name(), type->toString(), construct);
    node_handle_t reopened = coerceTo(subject, TupleType::nullable(type), at);
    ASSERT(reopened != nullptr, "A value always enters the nullable form of its own type.");
    return reopened;
}

node_handle_t Builder::visitConditionBrchNode(const GCT::node_ptr_t &gct) {
    using Kind         = GCT::BranchKind;
    using CaseType     = GCT::CaseType;
    const Kind kind    = gct->branchKind();
    const bool logical = kind == Kind::LogicalAnd || kind == Kind::LogicalOr;
    if (gct->size() != 3 || gct->at(1)->caseType() != CaseType::True ||
        gct->at(2)->caseType() != CaseType::Else) {
        ASSERT(!logical, "'&&' and '||' are lowered with a true arm and an else arm.");
        return nullptr; // a `match`
    }
    const GCT::node_ptr_t &cond = gct->at(0);
    std::vector<NullTest> tests;
    collectNullTests(cond, false, tests);
    if (!logical && tests.empty()) {
        return nullptr; // an `if` that tests no name against null
    }

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
                    "__is_null__",
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

    const auto buildArm = [&](const Arm &arm) {
        const size_t errorsBefore = diags_->count(Severity::Error);
        const size_t diagsBefore  = diags_->count();
        try {
            if (arm.opened.empty()) {
                return buildArmGraph(arm.exec);
            }
            return buildArmGraph(arm.exec, [&] {
                for (const Opened &opened : arm.opened) {
                    // Opened whether or not the arm seems to use the name: a name can be
                    // written where no DREF shows it (the elements of an array literal are
                    // names kept in the constant), and an unused node costs nothing at run
                    // time, as nothing depends on it.
                    node_handle_t value =
                        applyTagOperator("__null_value__", opened.temporary, opened.subject);
                    [[maybe_unused]] const bool bound = insertNode(opened.name, value);
                    ASSERT(bound, "A narrowed name is the first name of its arm.");
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
    std::vector<compile_graph_ptr_t> arms(2);
    arms[thenFirst ? 0 : 1] = buildArm(thenArm);
    arms[thenFirst ? 1 : 0] = buildArm(elseArm);
    if (logical) {
        // The right side is the then arm of `&&` and the else arm of `||`.
        const bool rightIsThen = kind == Kind::LogicalAnd;
        requireBool(
            arms[rightIsThen == thenFirst ? 0 : 1]->funcType()->exitType(),
            "right",
            rightSideOf(gct));
    }
    // An `if` without `else` has no value (design principles 3.7; see buildBranch).
    const bool valueless = !logical && elseArm.exec->size() == 0;
    return buildBranch(gct, condNode, &arms, valueless);
}

node_handle_t Builder::visitBrchNode(const GCT::node_ptr_t &gct) {
    ENTER("BRCH");
    const auto kind = gct->branchKind();
    if (kind == GCT::BranchKind::NullThen || kind == GCT::BranchKind::ErrorThen ||
        kind == GCT::BranchKind::ErrorPass) {
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
    const auto &res = visit(gct->at(0));
    ASSERT(
        res.type() == typeid(node_handle_t),
        "Unexpected result type from Enter the child of BRCH node.");
    node_handle_t condNode = any_cast<node_handle_t>(res);
    node_handle_t node{};
    if (asTagged(valueTypeOf(nodeTypeOf(condNode))) == nullptr && isNarrowedName(gct->at(0))) {
        // `match x { case null => ...  case v => ... }` where an `if` around it already
        // narrowed `x` to a `T` (design principles 3.8): allowed, with a warning. A match
        // without `case null` is an ordinary match on the `T`.
        const bool nullCase = std::any_of(gct->begin() + 1, gct->end(), [](const auto &c) {
            return c->caseType() == GCT::CaseType::Value && isNullLiteral(c->at(0));
        });
        if (nullCase) {
            condNode = reopenNarrowedName(gct->at(0), condNode, gct, "match");
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
                // The two patterns that bind are defined over `T?` / `T!` only. What an
                // identifier pattern means over any other type has not been decided.
                const bool isError = boundCase->caseType() == GCT::CaseType::Error;
                diags_->of(SemanticDiag::ConstructNotSupported)
                    .atOrigin(boundCase->origin())
                    .commit(
                        (isError ? "error " : "") + boundCase->binding(),
                        std::string(isError ? "the pattern 'error e'" : "an identifier pattern") +
                            " is implemented only for a value of type 'T?' or 'T!'; over '" +
                            nodeTypeOf(condNode)->toString() +
                            "' only literal patterns and '_' are");
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
    using CaseType    = GCT::CaseType;
    bool hasValueCase = false, hasTrue = false, hasFalse = false;
    for (size_t i = 1; i < gct->size(); ++i) {
        const auto &caseNode = gct->at(i);
        const auto caseType  = caseNode->caseType();
        if (caseType == CaseType::Else) {
            return;
        }
        if (caseType != CaseType::Value) {
            continue;
        }
        hasValueCase = true;
        if (caseNode->at(0)->type() == GCT::NodeKind::DATA) {
            const auto literal = std::dynamic_pointer_cast<BoolData>(caseNode->at(0)->data());
            if (literal) {
                (literal->data() ? hasTrue : hasFalse) = true;
            }
        }
    }
    if (!hasValueCase) {
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

node_handle_t Builder::buildBranch(
    const GCT::node_ptr_t &gct, node_handle_t condNode,
    const std::vector<compile_graph_ptr_t> *arms, bool valueless) {
    node_handle_t joinNode = currGraph_->addJoinNode(nullptr, nullptr, 0);
    node_handle_t brchNode = currGraph_->addBrchNode(Type::Int64(), joinNode, {}, nullptr);
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

    linkNodes(LinkType::Norm, condNode, brchNode);
    std::vector<runtime::GCBranchArm> branchArms;
    std::vector<node_handle_t> branchFuncs;
    // The variables the arms captured, and whether some arm writes them.
    std::vector<CapturedUse> captured;
    std::vector<DraftGraphBuilder *> armGraphs;
    std::vector<graph_ptr_t> armList;

    // Makes `subGraph` the next arm of the branch.
    const auto addArm = [&](const graph_ptr_t &subGraph) {
        if (dropsArmValues) {
            // The arm of an `if` without `else` gives a value nothing takes.
            rejectDiscardedValue(subGraph.get());
        }
        armGraphs.push_back(subGraph.get());
        armList.push_back(subGraph);
        for (const CapturedUse &use : capturedUsesOf(subGraph)) {
            auto known = std::ranges::find(captured, use.variable, &CapturedUse::variable);
            if (known == captured.end()) {
                captured.push_back(use);
            } else {
                known->written |= use.written;
            }
        }

        currGraph_->addDependencyGraph(subGraph);
        Type *exitType = subGraph->funcType()->exitType();

        // The arm is called by a FUNC node like a function; every value it captures is made
        // ready before the BRCH node runs (addCaptureSite). This keeps the nodes on every
        // branch path between BRCH and JOIN tightly grouped in topological order, with no
        // unrelated external nodes inserted in between, so that the graph scheduler can jump
        // directly without frequent checks.
        auto funcNode = createFuncDataNode(subGraph, false, true, brchNode);
        branchFuncs.push_back(funcNode);
        branchArms.push_back(
            runtime::GCBranchArm{
                .head = nodeIdOf(funcNode),
                .tail = nodeIdOf(funcNode),
            });

        if (absentElse) {
            // Design principles 3.7: an `if` without `else` has no value. Its arm may be of
            // any type and what the arm gives is dropped: the join takes nothing. The arm
            // written is evaluated as any arm is; the one not written is an empty arm.
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
                diags_->of(SemanticDiag::BranchReturnTypeMismatch)
                    .atOrigin(gct->origin())
                    .commit(
                        currGraph_->name() + ": " + currGraph_->funcType()->toString(),
                        joinType->toString(),
                        exitType->toString());
                throw BuildAbortException();
            }
        }

        linkNodes(LinkType::Ctrl, brchNode, funcNode);
        linkNodes(LinkType::With, funcNode, joinNode);
    };

    const size_t armEnd = arms != nullptr ? arms->size() + 1 : gct->size();
    for (size_t i = 1; i < armEnd; i++) {
        graph_ptr_t subGraph;
        if (arms != nullptr) {
            // Arms the caller built (the tagged forms): a boolean condition and two arms.
            subGraph = (*arms)[i - 1];
        } else {
            const auto &caseNode         = gct->at(i);
            GCT::node_ptr_t caseExecNode = nullptr;

            switch (caseNode->caseType()) {
            case GCT::CaseType::True: {
                ASSERT(caseNode->size() == 1, "True case should have only one child.");
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
                ASSERT(i == gct->size() - 1, "Else case should be the last case.");
                ASSERT(caseNode->size() == 1, "Else case should have only one child.");
                caseExecNode = caseNode->at(0);
                absentElse   = ifArmSeen && caseExecNode->size() == 0;
                break;
            case GCT::CaseType::Value: {
                ASSERT(caseNode->size() == 2, "Value case should have two children.");
                any res = visit(caseNode->at(0));
                ASSERT(
                    res.type() == typeid(node_handle_t),
                    "Unexpected result type from visiting the case node.");
                node_handle_t valueNode = any_cast<node_handle_t>(res);
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
                Type *caseType    = valueTypeOf(nodeTypeOf(valueNode));
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
                ASSERT(false, "Unknown case type in BRCH node.");
            }

            subGraph = buildArmGraph(caseExecNode);
        }
        addArm(subGraph);
    }
    if (joinType != nullptr && !absentElse) {
        for (size_t i = 0; i < armList.size(); ++i) {
            Type *exitType = armList[i]->funcType()->exitType();
            if (exitType != nullptr && !joinType->assignableFrom(exitType) &&
                entersImplicitly(joinType, exitType)) {
                coerceArmExit(armList[i], joinType, gct);
                // The node that stands for the arm was typed when the arm was added, with
                // what the arm gave then. A pass that folds the branch to this one arm
                // reads the value through that type (std::opt::fold read a `float?` box as
                // a float).
                // The slot the arm's value is kept in has a type of its own, given when the
                // node was made: a new slot of the joined type (a `float?` box kept in a
                // slot typed `float` is not traced by the collector either).
                setNodeType(branchFuncs[i], joinType);
                nodeDraftOf(branchFuncs[i])
                    ->setNodeDataIndex(
                        nodeIdOf(branchFuncs[i]),
                        currGraph_->addRuntimeSlot(joinType));
            }
        }
    }

    runtime::DraftBrchPayload payload{
        .join       = nodeIdOf(joinNode),
        .armCount   = static_cast<runtime::gc_cnt_t>(branchArms.size()),
        .defaultArm = runtime::kInvalidNodeRef};
    std::vector<std::byte> brchBytes(
        sizeof(runtime::DraftBrchPayload) + sizeof(runtime::GCBranchArm) * branchArms.size());
    std::memcpy(brchBytes.data(), &payload, sizeof(payload));
    if (!branchArms.empty()) {
        std::memcpy(
            brchBytes.data() + sizeof(payload),
            branchArms.data(),
            sizeof(runtime::GCBranchArm) * branchArms.size());
    }
    std::vector<draft_node_ref_t> brchNormInputs(
        normInputsOf(brchNode).begin(),
        normInputsOf(brchNode).end());
    std::vector<draft_node_ref_t> brchWithInputs(
        withInputsOf(brchNode).begin(),
        withInputsOf(brchNode).end());
    std::vector<draft_node_ref_t> brchCtrlInputs(
        ctrlInputsOf(brchNode).begin(),
        ctrlInputsOf(brchNode).end());
    std::vector<draft_node_ref_t> brchNormUsers(
        normUsersOf(brchNode).begin(),
        normUsersOf(brchNode).end());
    std::vector<draft_node_ref_t> brchWithUsers(
        withUsersOf(brchNode).begin(),
        withUsersOf(brchNode).end());
    std::vector<draft_node_ref_t> brchCtrlUsers(
        ctrlUsersOf(brchNode).begin(),
        ctrlUsersOf(brchNode).end());
    const auto *brchHeader   = nodeHeaderOf(brchNode);
    const auto brchDataIndex = brchHeader->dataIndex != 0
                                   ? brchHeader->dataIndex
                                   : currGraph_->addRuntimeSlot(Type::Int64());
    currGraph_->draft().rewriteNode(
        nodeIdOf(brchNode),
        runtime::DraftNodeInit{
            .dataIndex    = brchDataIndex,
            .dataType     = Type::Int64(),
            .runtimeFlags = brchHeader->runtimeFlags,
            .kind         = runtime::GCNodeKind::Brch,
            .payload      = std::span<const std::byte>(brchBytes.data(), brchBytes.size()),
            .normInputs   = brchNormInputs,
            .withInputs   = brchWithInputs,
            .ctrlInputs   = brchCtrlInputs,
            .normUsers    = brchNormUsers,
            .withUsers    = brchWithUsers,
            .ctrlUsers    = brchCtrlUsers});
    runtime::GCJoinBody joinBody{
        .brch     = nodeIdOf(brchNode),
        .armCount = static_cast<runtime::gc_cnt_t>(branchArms.size())};
    std::vector<draft_node_ref_t> joinNormInputs(
        normInputsOf(joinNode).begin(),
        normInputsOf(joinNode).end());
    std::vector<draft_node_ref_t> joinWithInputs(
        withInputsOf(joinNode).begin(),
        withInputsOf(joinNode).end());
    std::vector<draft_node_ref_t> joinCtrlInputs(
        ctrlInputsOf(joinNode).begin(),
        ctrlInputsOf(joinNode).end());
    std::vector<draft_node_ref_t> joinNormUsers(
        normUsersOf(joinNode).begin(),
        normUsersOf(joinNode).end());
    std::vector<draft_node_ref_t> joinWithUsers(
        withUsersOf(joinNode).begin(),
        withUsersOf(joinNode).end());
    std::vector<draft_node_ref_t> joinCtrlUsers(
        ctrlUsersOf(joinNode).begin(),
        ctrlUsersOf(joinNode).end());
    const auto *joinHeader = nodeHeaderOf(joinNode);
    const auto joinDataIndex =
        joinHeader->dataIndex != 0 ? joinHeader->dataIndex : currGraph_->addRuntimeSlot(joinType);
    currGraph_->draft().rewriteNode(
        nodeIdOf(joinNode),
        runtime::DraftNodeInit{
            .dataIndex    = joinDataIndex,
            .dataType     = joinType,
            .runtimeFlags = joinHeader->runtimeFlags,
            .kind         = runtime::GCNodeKind::Join,
            .payload      = std::as_bytes(std::span{&joinBody, 1}),
            .normInputs   = joinNormInputs,
            .withInputs   = joinWithInputs,
            .ctrlInputs   = joinCtrlInputs,
            .normUsers    = joinNormUsers,
            .withUsers    = joinWithUsers,
            .ctrlUsers    = joinCtrlUsers});

    linkNodes(LinkType::Norm, brchNode, joinNode);

    // Entering an arm is like calling it: the branch uses every variable an arm captured and
    // writes those an arm writes. It begins at BRCH and is complete at JOIN.
    std::vector<node_handle_t> capturedWrites;
    for (const CapturedUse &use : captured) {
        orderAfterLastWriter(use.variable, brchNode);
        if (use.written) {
            capturedWrites.push_back(use.origin);
        }
    }
    if (!capturedWrites.empty() && !waited_ && !synced_) {
        unit_->pendingIgnoredWrites.push_back(
            {armGraphs, capturedWrites, gct->origin(), "The branch", "an arm writes"});
    }
    for (const CapturedUse &use : captured) {
        if (use.written) {
            recordWrite(use.variable, brchNode, joinNode, true);
        } else {
            pendingUses_[use.variable].push_back({joinNode, false});
        }
    }

    if (synced_) {
        if (lastSyncedNode_ != nullptr && shouldAddSyncCtrlLink(lastSyncedNode_, brchNode)) {
            linkNodes(LinkType::Ctrl, lastSyncedNode_, brchNode);
        }
        lastSyncedNode_ = joinNode;
        chainedNodes_.insert(brchNode);
        chainedNodes_.insert(joinNode);
        // The arms run between the BRCH and the JOIN, so in the chain too.
        chainedNodes_.insert(branchFuncs.begin(), branchFuncs.end());
    }

    return joinNode;
}

void Builder::buildLoopGraph(const compile_graph_ptr_t &graph, const GCT::node_ptr_t &exec) {
    // The graph is written inside the current one, like an arm: its scope is entered from
    // here, so the names of the enclosing code are visible in it (and captured).
    const bool atRoot = currGraph_ == rootGraph_;
    enterGraphScope(graph, "");
    if (atRoot) {
        rootSubGraphOwners_[graph.get()] = unit_;
    }
    registerGraphOrigin(context_, graph, exec, "gir.loop.graph");
    bodyStates_[graph.get()] = BodyState::Open;
    try {
        for (draft_node_ref_t portId : graph->draft().normPorts()) {
            node_handle_t port = graph->draft().node(portId);
            insertNode(graph->nodePortName(port), port);
            if (recorder_) {
                recordDeclaration(graph->nodePortName(port), port, exec);
            }
        }
        setBodyResult(graph, visitBodyNode(exec));
    } catch (...) {
        bodyStates_[graph.get()] = BodyState::Built;
        failedBodies_.insert(graph.get());
        leaveScope();
        throw;
    }
    bodyStates_[graph.get()] = BodyState::Built;
    unit_->builtBodies.push_back(graph);
    leaveScope();
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

    // The first values and the array: evaluated once, where the loop is (they do not see the
    // names of the head). A loop of one state has the state's type, so a state written as the
    // empty literal takes the type of the position the loop's value goes to (`loop losses = []
    // for ...` where the loop gives a `float[]`; design principles 5.1, 5.2). A loop of several
    // states gives a tuple, and says nothing about one state.
    Type *loopExpected = nullptr;
    if (k == 1 && expectedValueType_ != nullptr) {
        Type *value = valueTypeOf(expectedValueType_);
        if (value != nullptr && value->code() == TypeCode::Array) {
            loopExpected = value;
        }
    }
    std::vector<node_handle_t> initial;
    std::vector<Type *> stateTypes;
    for (size_t j = 0; j < k; ++j) {
        const auto &expr = gct->loopInitial(j);
        node_handle_t node;
        {
            ExpectedValueScope stateScope(expectedValueType_, loopExpected);
            node = valueOf(functionValueOf(valueNodeOf(visit(expr)), expr));
        }
        if (loopExpected != nullptr) {
            // A value that is not the empty literal is converted only when its position
            // accepts it; one that does not fit stays as it is and is reported below.
            if (node_handle_t typed = coerceTo(node, loopExpected, expr); typed != nullptr) {
                node = typed;
            }
        }
        Type *type         = valueTypeOf(nodeTypeOf(node));
        const bool untyped = type == nullptr || type->code() == TypeCode::Void ||
                             (type->code() == TypeCode::Array &&
                              tt::as_ptr<ArrayType>(type)->elemType()->code() == TypeCode::Void);
        if (untyped) {
            diags_->of(SemanticDiag::LoopStateWithoutType)
                .atOrigin(expr->origin())
                .commit(gct->loopStateName(j), type ? type->toString() : std::string("void"));
            throw BuildAbortException();
        }
        initial.push_back(node);
        stateTypes.push_back(type);
    }
    node_handle_t sequence = nullptr;
    Type *elementType      = nullptr;
    if (hasIterable) {
        const auto &expr = gct->loopSequence();
        sequence         = valueOf(functionValueOf(valueNodeOf(visit(expr)), expr));
        Type *type       = valueTypeOf(nodeTypeOf(sequence));
        // A `for` takes the elements of an array. A string, a map and a tensor give theirs
        // through the `__iter__` operator their type declares (design principles 3.10, 11.1,
        // 11.3): the code points of a string as one-character strings, the (key, value) pairs
        // of a map in key order, and the sub-tensors of a tensor along its first dimension.
        // The operator is resolved like any other, by the type of the sequence; the loop then
        // iterates the array it gives, on every back end, with one driver.
        if (type != nullptr && type->code() != TypeCode::Array) {
            const std::vector<StaticArg> statics(1);
            std::optional<oper_idx_ptr_t> elements;
            if (const auto group = module_->getImportedEntity("__iter__");
                group.has_value() && group->isOperGroup()) {
                elements = group->operGroup()->resolve({}, {type}, statics, Modifier::None);
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
            linkNodes(LinkType::Norm, sequence, array);
            sequence = array;
            type     = valueTypeOf(nodeTypeOf(sequence));
            ASSERT(
                type != nullptr && type->code() == TypeCode::Array,
                "The elements of an iterable must be an array.");
        }
        if (type == nullptr) {
            diags_->of(SemanticDiag::LoopSequenceNotIterable)
                .atOrigin(expr->origin())
                .commit(sourceTextOf(expr), std::string("void"));
            throw BuildAbortException();
        }
        elementType = tt::as_ptr<ArrayType>(type)->elemType();
    }
    Type *stateType =
        k == 1 ? stateTypes.front() : static_cast<Type *>(TupleType::create(stateTypes));

    // The two graphs, made before either is built so that every capture is made in both in
    // the same order (captureTwins_). They take the state (the body the element first) as
    // ordinary parameters; their captures become with-ports once they are built.
    const size_t ordinal        = ++loopCounts_[currGraph_.get()];
    const std::string graphBase = std::format("{}.loop{}", currGraph_->name(), ordinal);
    const auto makeLoopGraph    = [&](bool withElement, const char *role) {
        FunctionTypeFactory factory;
        if (withElement) {
            factory.addNormArg(gct->loopElementName(), elementType, false);
        }
        for (size_t j = 0; j < k; ++j) {
            factory.addNormArg(gct->loopStateName(j), stateTypes[j], false);
        }
        return createCompileGraph(
            factory.build(),
            currGraph_,
            std::format("{}.{}", graphBase, role));
    };
    compile_graph_ptr_t condGraph =
        camel::core::loopHasCondition(kind) ? makeLoopGraph(false, "cond") : nullptr;
    compile_graph_ptr_t bodyGraph = makeLoopGraph(hasIterable, "body");
    if (condGraph) {
        captureTwins_[condGraph.get()] = bodyGraph;
        captureTwins_[bodyGraph.get()] = condGraph;
    }

    if (condGraph) {
        buildLoopGraph(condGraph, gct->loopCondition());
        Type *condType = condGraph->funcType()->exitType();
        if (condType == nullptr || condType->code() != TypeCode::Bool) {
            diags_->of(SemanticDiag::LoopConditionNotBool)
                .atOrigin(gct->loopCondition()->origin())
                .commit(condType ? condType->toString() : std::string("void"));
            throw BuildAbortException();
        }
    }
    buildLoopGraph(bodyGraph, gct->loopBody());
    // The body gives the next state: of the state's type, or a value that enters it (an
    // `int` where the state is an `int?`, design principles 3.8).
    Type *bodyType = bodyGraph->funcType()->exitType();
    if (bodyType == nullptr || !stateType->assignableFrom(bodyType)) {
        if (bodyType != nullptr && entersImplicitly(stateType, bodyType)) {
            coerceArmExit(bodyGraph, stateType, gct->loopBody());
        } else {
            diags_->of(SemanticDiag::LoopBodyTypeMismatch)
                .atOrigin(gct->loopBody()->origin())
                .commit(
                    bodyType ? bodyType->toString() : std::string("void"),
                    stateType->toString());
            throw BuildAbortException();
        }
    }

    markLoopStateInPlace(bodyGraph, gct, initial);

    // Called by the LOOP node: the captures are with-ports, in the order both graphs share.
    for (const compile_graph_ptr_t &graph : {condGraph, bodyGraph}) {
        if (!graph) {
            continue;
        }
        const std::vector<draft_node_ref_t> closureNodes(
            graph->draft().closureNodes().begin(),
            graph->draft().closureNodes().end());
        for (draft_node_ref_t closureId : closureNodes) {
            graph->draft().removeNormPort(closureId);
            graph->draft().appendWithPort(closureId);
            graph->draft().removeClosureNode(closureId);
        }
        graph->setParameterized(true);
    }

    node_handle_t loopNode = currGraph_->addLoopNode(
        kind,
        DraftGraphBuilder::LoopTarget{condGraph, bodyGraph},
        stateType);
    registerNodeOrigin(context_, loopNode, gct, "gir.loop");
    stampPermissions(loopNode);
    if (sequence != nullptr) {
        linkNodes(LinkType::Norm, sequence, loopNode);
    }
    for (node_handle_t node : initial) {
        linkNodes(LinkType::Norm, node, loopNode);
    }
    // The captured values are the With inputs, linked by the link step in the graphs' order
    // (the condition graph has the same captures as the body graph).
    addCaptureSite(loopNode, bodyGraph, nullptr);

    // Running the loop is like calling its graphs (and entering an arm): it uses every
    // variable they captured and writes those they write.
    std::vector<CapturedUse> captured;
    for (const compile_graph_ptr_t &graph : {condGraph, bodyGraph}) {
        if (!graph) {
            continue;
        }
        for (const CapturedUse &use : capturedUsesOf(graph)) {
            auto known = std::ranges::find(captured, use.variable, &CapturedUse::variable);
            if (known == captured.end()) {
                captured.push_back(use);
            } else {
                known->written |= use.written;
            }
        }
    }
    std::vector<node_handle_t> capturedWrites;
    for (const CapturedUse &use : captured) {
        orderAfterLastWriter(use.variable, loopNode);
        if (use.written) {
            capturedWrites.push_back(use.origin);
        }
    }
    if (!capturedWrites.empty() && !waited_ && !synced_) {
        std::vector<DraftGraphBuilder *> graphs{bodyGraph.get()};
        if (condGraph) {
            graphs.push_back(condGraph.get());
        }
        unit_->pendingIgnoredWrites.push_back(
            {graphs, capturedWrites, gct->origin(), "The loop", "its body writes"});
    }
    for (const CapturedUse &use : captured) {
        if (use.written) {
            recordWrite(use.variable, loopNode, loopNode, true);
        } else {
            pendingUses_[use.variable].push_back({loopNode, false});
        }
    }

    if (synced_) {
        if (lastSyncedNode_ != nullptr && shouldAddSyncCtrlLink(lastSyncedNode_, loopNode)) {
            linkNodes(LinkType::Ctrl, lastSyncedNode_, loopNode);
        }
        lastSyncedNode_ = loopNode;
        chainedNodes_.insert(loopNode);
    }
    LEAVE("LOOP");
    return loopNode;
}

node_handle_t Builder::visitAnnoNode(const GCT::node_ptr_t &gct) {
    ENTER("ANNO");
    ASSERT(gct->size() == 1, "ANNO node should have exactly one child.");
    node_handle_t node = valueNodeOf(visit(gct->at(0)));
    LEAVE("ANNO");
    return node;
}

node_handle_t Builder::visitExitNode(const GCT::node_ptr_t &gct) {
    ENTER("EXIT");
    ExpectedValueScope expectedScope(expectedValueType_, expectedExitTypeHere());
    // `return f`: the function value of a function named here. The name itself is not a
    // value, and left in place it reached the end of compilation as a DREF node.
    node_handle_t resNode = functionValueOf(valueNodeOf(visit(gct->at(0))), gct);
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

    if (!pendingCtrlInputs.empty() && !nodeIsKind(resNode, runtime::GCNodeKind::Gate)) {
        node_handle_t gatedValue = currGraph_->addGateNode(nodeTypeOf(resNode));
        linkNodes(LinkType::Norm, resNode, gatedValue);
        outputAnchor = gatedValue;
    }
    for (node_handle_t ctrlInput : pendingCtrlInputs) {
        // A return GATE must retain a CTRL input even when its value already depends on it.
        if (ctrlInput != nullptr && linkCheek(ctrlInput, outputAnchor) &&
            !runtime::hasControlDependency(
                *nodeDraftOf(outputAnchor),
                nodeIdOf(ctrlInput),
                nodeIdOf(outputAnchor))) {
            linkNodes(LinkType::Ctrl, ctrlInput, outputAnchor);
        }
    }
    checkDeclaredReturn(currGraph_, nodeTypeOf(resNode), gct->origin(), gct->at(0));
    if (!droppedGraphs_.contains(currGraph_.get())) {
        setGraphReturnFromResult(
            currGraph_,
            nodeIdOf(outputAnchor),
            nodeIdOf(resNode),
            nodeTypeOf(resNode));
    }

    LEAVE("EXIT");
    return resNode;
}

node_handle_t Builder::visitBodyNode(const GCT::node_ptr_t &gct) {
    const bool outerSynced        = std::exchange(synced_, false);
    const node_handle_t outerLast = std::exchange(lastSyncedNode_, {});
    // The last statement of a body is the value the body hands on: a branch there (`if c then
    // … else …` as a function's whole body) stands in the position of that value, and so do
    // the `return`s written in its arms (handover H24: an arm's `return` takes the type of the
    // position the arm is at, never the declared type of the function around it).
    Type *outerTail               = std::exchange(tailExpectedType_, tailPositionType());
    const GCT::Node *outerTailFor = std::exchange(tailExpectedFor_, gct);
    node_handle_t result{};
    try {
        result = visitExecNode(gct);
        if (lastBlockStatement_.first != nullptr) {
            bodyValueStatement_[currGraph_.get()] = lastBlockStatement_;
        }
    } catch (...) {
        synced_         = outerSynced;
        lastSyncedNode_ = outerLast;
        tailExpectedType_ = outerTail;
        tailExpectedFor_  = outerTailFor;
        throw;
    }
    synced_         = outerSynced;
    lastSyncedNode_ = outerLast;
    tailExpectedType_ = outerTail;
    tailExpectedFor_  = outerTailFor;
    return result;
}

void Builder::markLoopStateInPlace(
    const compile_graph_ptr_t &bodyGraph, const GCT::node_ptr_t &loop,
    std::vector<node_handle_t> &initial) {
    const size_t stateCount = loop->loopStateCount();
    if (stateCount == 0 || initial.size() != stateCount) {
        return;
    }
    runtime::GraphDraft &draft = bodyGraph->draft();
    if (draft.returnKind() != runtime::GCReturnKind::Self ||
        draft.returnNode() == runtime::kInvalidNodeRef) {
        return;
    }
    node_handle_t returned = draft.node(draft.returnNode());
    if (returned == nullptr) {
        return;
    }
    // The next value of each state, in state order. One state hands the append call back
    // directly. Two or more return a tuple, a FILL whose With inputs are the computed
    // components and whose payload says which component each one fills; a component written as
    // a constant there (`(xs->append<7>, 0)`) has no value node and no rewrite of its own.
    if (stateCount == 1) {
        markLoopStateInPlaceOne(bodyGraph, loop, initial, 0, returned);
        return;
    }
    if (!nodeIsKind(returned, runtime::GCNodeKind::Fill)) {
        return;
    }
    const Type *returnType = valueTypeOf(nodeTypeOf(returned));
    if (returnType == nullptr || returnType->code() != TypeCode::Tuple) {
        return;
    }
    const auto withIns = withInputsOf(returned);
    const auto payload = nodeDraftOf(returned)->payloadOf(nodeIdOf(returned));
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
        if (slots[i] >= stateCount) {
            return;
        }
        next[slots[i]] = nodeDraftOf(returned)->node(withIns[i]);
    }
    for (size_t j = 0; j < stateCount; ++j) {
        markLoopStateInPlaceOne(bodyGraph, loop, initial, j, next[j]);
    }
}

void Builder::markLoopStateInPlaceOne(
    const compile_graph_ptr_t &bodyGraph, const GCT::node_ptr_t &loop,
    std::vector<node_handle_t> &initial, size_t index, node_handle_t next) {
    if (next == nullptr || !nodeIsKind(next, runtime::GCNodeKind::Oper)) {
        return;
    }
    // The first value has to be a literal: a name, a call or a projection can name an array
    // that something else also reads, and the loop is about to make its first value private.
    if (!nodeIsKind(initial[index], runtime::GCNodeKind::Data) &&
        !nodeIsKind(initial[index], runtime::GCNodeKind::Fill)) {
        return;
    }
    const Type *stateType = valueTypeOf(nodeTypeOf(initial[index]));
    if (stateType == nullptr || stateType->code() != TypeCode::Array) {
        return;
    }
    const oper_idx_ptr_t op = nodeGraphOf(next)->operTarget(next);
    if (op == nullptr || op->uri() != ":struct/append_arr") {
        return;
    }
    // The array the call appends to is this state's parameter, and the body uses that
    // parameter nowhere else: once the round returns, the value it was bound to is gone (the
    // driver binds the round's own state), so nothing can see it as it was.
    const auto normIns = normInputsOf(next);
    if (normIns.size() != 1) {
        return;
    }
    node_handle_t port = nodeDraftOf(next)->node(normIns.front());
    if (port == nullptr || bodyGraph->nodePortName(port) != loop->loopStateName(index)) {
        return;
    }
    if (normUsersOf(port).size() + withUsersOf(port).size() != 1) {
        return;
    }
    // The appended value is the call's only With input; the owned form takes the same.
    const auto withIns = withInputsOf(next);
    if (withIns.size() != 1) {
        return;
    }
    Type *valueType = nodeTypeOf(nodeDraftOf(next)->node(withIns.front()));
    if (valueType == nullptr) {
        return;
    }
    const auto group = module_->getImportedEntity("__append_owned__");
    if (!group.has_value() || !group->isOperGroup()) {
        return;
    }
    const std::vector<StaticArg> statics(1);
    const auto owned =
        group->operGroup()->resolve({valueType}, {nodeTypeOf(port)}, statics, Modifier::None);
    if (!owned.has_value()) {
        return;
    }
    // The loop starts from a private copy of its first value. A literal is a constant of the
    // graph: the same array object on every evaluation of the loop's site, so appending to it
    // in place would edit the program's own literal. One copy per loop entry buys every round.
    const auto copyGroup = module_->getImportedEntity("__copy_arr__");
    if (!copyGroup.has_value() || !copyGroup->isOperGroup()) {
        return;
    }
    const auto copyOp =
        copyGroup->operGroup()->resolve({}, {nodeTypeOf(initial[index])}, statics, Modifier::None);
    if (!copyOp.has_value()) {
        return;
    }
    node_handle_t copy = currGraph_->addOperNode(*copyOp, 0);
    stampPermissions(copy);
    registerNodeOrigin(context_, copy, loop->loopInitial(index), "gir.loop.copy");
    linkNodes(LinkType::Norm, initial[index], copy);
    initial[index] = copy;
    nodeGraphOf(next)->retargetOperNode(next, *owned);
}

void Builder::setBodyResult(const compile_graph_ptr_t &graph, node_handle_t result) {
    if (graph->draft().exitNode() != runtime::kInvalidNodeRef) {
        return;
    }
    if (result == nullptr) {
        result = graph->addStaticDataNode(Data::null());
        setGraphReturnFromResult(graph, nodeIdOf(result), nodeIdOf(result), Type::Void());
        return;
    }
    // A sync block that ends in a value hands it back behind a GATE (see visitExecNode): the
    // GATE is the output anchor, the value it forwards is what the graph returns.
    node_handle_t value = result;
    if (nodeIsKind(result, runtime::GCNodeKind::Gate) && !normInputsOf(result).empty()) {
        value = nodeDraftOf(result)->node(normInputsOf(result).front());
    }
    checkDeclaredReturn(graph, nodeTypeOf(value), 0);
    setGraphReturnFromResult(graph, nodeIdOf(result), nodeIdOf(value), nodeTypeOf(value));
}

} // namespace camel::compile::gir
