// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The content rule of `Ref<T>` (the author's ruling of 2026-11-14): a reference may hold any type
 * but one that holds a reference.
 *
 * A box is reclaimed by counting the handles that point at it -- the last handle to go frees it --
 * so two boxes holding each other's handles would keep each other's count above zero for ever.
 * Neither would be freed, and the program would hold values it can no longer reach. The rule is
 * therefore about the whole of `T`, not its head:
 *
 *   - the type itself (`Ref<Ref<int>>`), a field, an element, a variant's payload, and what a
 *     function takes or returns, walked one layer at a time (design principles 5.5: "what it
 *     holds, the type mentions");
 *   - and the values a closure captures, which a function type does not spell (the captures of a
 *     function value are a fact about its graph, not about its type -- the same reading the state
 *     rule takes, builder_state.cpp). A closure may go into a box when it captured no reference.
 *
 * The second walk follows what each closure took for as many layers as there are: a closure may
 * capture a closure that captured a closure, and the reference can be at the bottom of that chain.
 * One layer the walk cannot read is refused and not passed -- a capture it cannot see is a capture
 * that may hold the reference, and a single box that holds one is never freed (3.5).
 *
 * The report is placed where the reference is made, which is the node whose value is the `Ref<T>`:
 * one place per box, so a program with two such boxes is told about two places.
 */

#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"

#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/stream.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/enum.h"
#include "camel/core/type/ref.h"
#include "camel/utils/type.h"

#include <string>

using namespace std;
using namespace camel::core::type;

namespace camel::compile::gir {

namespace {

/// Whether a value is one of a function type: the only kind whose captures there are to read.
bool isFunctionValue(node_handle_t value) {
    if (value == nullptr) {
        return false;
    }
    const Type *type = valueTypeOf(nodeTypeOf(value));
    return type != nullptr && type->code() == TypeCode::Function;
}

/// How many closures deep the walk follows the values a closure captured. A chain longer than this
/// is not read, and a layer that is not read is refused like one that cannot be (see below).
constexpr int kRefCaptureDepth = 8;

/// The closures a walk went through to reach a value, as one piece of text for a report.
string joinedNames(const vector<string> &names) {
    string joined;
    for (const string &name : names) {
        if (!joined.empty()) {
            joined += "' -> '";
        }
        joined += name;
    }
    return joined;
}

/// The report clause for a layer of the capture walk that could not be read. It is a refusal and
/// not a pass: the rule's premise is that no box holds a reference at any depth (design principles
/// 3.5), so a capture this walk cannot see may be the one that holds one, and letting it through
/// would be exactly the box that is never freed.
string unreadableCaptureClause(const vector<string> &through, const string &why) {
    string clause = "it holds a closure whose captures cannot be read (" + why + ")";
    if (!through.empty()) {
        clause += ", reached through '" + joinedNames(through) + "'";
    }
    return clause + ", so a Ref may be inside it";
}

/// Where a type holds a reference, if it does: the clause a report names the place with.
/// `nullopt` when it holds none. A type this does not read is answered as holding none -- the
/// question is "must this be refused", and the only type it can be wrong about is one whose
/// members this walk does not see, which no type in the core has.
optional<string> refInsideType(Type *type, int depth = 0) {
    if (type == nullptr || depth > 16) {
        return nullopt;
    }
    if (RefType::is(type)) {
        return string("it holds a Ref");
    }
    switch (type->code()) {
    case TypeCode::Tuple: {
        // A tuple, and with it the cell `var T` and the tagged forms `T?` and `T!`: each holds
        // the types of its parts.
        const auto *tuple = static_cast<const TupleType *>(type);
        for (size_t i = 0; i < tuple->size(); ++i) {
            if (refInsideType(tuple->typeAt(i), depth + 1).has_value()) {
                return string("it holds a Ref, in a part of the tuple");
            }
        }
        return nullopt;
    }
    case TypeCode::Struct: {
        const auto *record = static_cast<const StructType *>(type);
        for (size_t i = 0; i < record->size(); ++i) {
            if (refInsideType(record->typeAt(i), depth + 1).has_value()) {
                return "it holds a Ref, in field '" + string(record->fieldName(i)) + "'";
            }
        }
        return nullopt;
    }
    case TypeCode::Array: {
        const auto *array = static_cast<const ArrayType *>(type);
        if (refInsideType(array->elemType(), depth + 1).has_value()) {
            return string("it holds a Ref, in an element");
        }
        return nullopt;
    }
    case TypeCode::Stream: {
        const auto *stream = static_cast<const StreamType *>(type);
        if (refInsideType(stream->elemType(), depth + 1).has_value()) {
            return string("it holds a Ref, in an element");
        }
        return nullopt;
    }
    case TypeCode::Function: {
        // What a function takes and what it returns. What it captured is not in its type; the
        // value the call hands in is asked separately (refInsideACapturedValue).
        const auto *function = static_cast<const FunctionType *>(type);
        for (size_t i = 0; i < function->withTypesCount() + function->normTypesCount(); ++i) {
            Type *parameter = i < function->withTypesCount()
                                  ? function->withTypeAt(i)
                                  : function->normTypeAt(i - function->withTypesCount());
            if (refInsideType(parameter, depth + 1).has_value()) {
                return string("it holds a Ref, in a type the function takes");
            }
        }
        if (function->hasExitType() && refInsideType(function->exitType(), depth + 1).has_value()) {
            return string("it holds a Ref, in the type the function returns");
        }
        return nullopt;
    }
    default:
        break;
    }
    if (const auto *enumType = asEnumType(type); enumType != nullptr) {
        for (size_t tag = 0; tag < enumType->variantCount(); ++tag) {
            const auto &variant = enumType->variant(tag);
            for (Type *field : variant.fieldTypes) {
                if (refInsideType(field, depth + 1).has_value()) {
                    return "it holds a Ref, in the payload of a case of the enum";
                }
            }
        }
        return nullopt;
    }
    if (isOtherType(type->code())) {
        // A module type with type arguments (a `Map`, a `Set`, ...): what it holds is what its
        // arguments are.
        const auto *other = static_cast<const OtherType *>(type);
        for (Type *argument : other->paramsSpan()) {
            if (refInsideType(argument, depth + 1).has_value()) {
                return string("it holds a Ref, in its type argument");
            }
        }
    }
    return nullopt;
}

} // namespace

// A closure may go into a box only when it captured no reference: a function type does not spell
// its captures, so the values the closure took are read from the graph it was built as, one layer
// at a time, and a layer this cannot read is refused rather than passed.
optional<string> Builder::refInsideACapturedValue(
    node_handle_t call, Type *content, int depth) const {
    if (call == nullptr || content == nullptr) {
        return nullopt;
    }
    // Only a function type has captures to follow: what a closure took is a fact about the graph
    // behind its value, and every other shape of `content` was walked by refInsideType.
    if (content->code() != TypeCode::Function) {
        return nullopt;
    }
    const auto &draft = nodeTransactionOf(call)->graph();
    // Every value the call hands in. Only a value of a function type is read: anything else in the
    // call holds no captures, so there is nothing to follow.
    for (draft_node_ref_t input : normInputsOf(call)) {
        const node_handle_t value = draft.ref(input);
        if (!isFunctionValue(value)) {
            continue;
        }
        if (optional<string> where = refInsideClosureCaptures(value, {}, depth)) {
            return where;
        }
    }
    return nullopt;
}

// One layer of that walk: the place a reference is reached at through what the closure `value`
// captured, `through` naming the closures already walked to reach it. `depth` is how many layers
// down this is, and running out of layers is answered like a layer that cannot be read.
optional<string> Builder::refInsideClosureCaptures(
    node_handle_t value, const vector<string> &through, int depth) const {
    if (depth > kRefCaptureDepth) {
        return unreadableCaptureClause(
            through, "the chain of closures is deeper than this walk follows");
    }
    const compile_graph_ptr_t closure = functionValueGraphOf(value);
    if (closure == nullptr) {
        return unreadableCaptureClause(through, "its graph is not one this program holds");
    }
    const auto held = capturedValues_.find(closure.get());
    if (held == capturedValues_.end()) {
        // A capture is recorded when the closure is built, so a graph with no entry took no value:
        // there is nothing inside it to follow, which is a reading and not a failure to read.
        return nullopt;
    }
    for (const CapturedValue &captured : held->second) {
        // The origin is a node of the enclosing graph, kept in a table that outlives it: a handle
        // that no longer names its draft cannot be typed, so it is a layer not read.
        if (!nodeIsCurrent(captured.origin)) {
            return unreadableCaptureClause(through, "a value it captured is no longer built");
        }
        Type *type = valueTypeOf(nodeTypeOf(captured.origin));
        if (type == nullptr) {
            return unreadableCaptureClause(through, "the type of a value it captured is unknown");
        }
        if (refInsideType(type, 0).has_value()) {
            string where =
                "it holds a Ref, in the value '" + captured.name + "' the closure captures";
            if (!through.empty()) {
                where += ", reached through '" + joinedNames(through) + "'";
            }
            return where;
        }
        // A closure that captured a closure: the reference, if there is one, is inside what that
        // one took, one layer further down the same walk (design principles 5.5: the walk takes the
        // layer the type names, and keeps taking them).
        if (type->code() != TypeCode::Function) {
            continue;
        }
        vector<string> deeper = through;
        deeper.push_back(captured.name);
        if (optional<string> inside =
                refInsideClosureCaptures(captured.origin, deeper, depth + 1)) {
            return inside;
        }
    }
    return nullopt;
}

void Builder::checkRefContents() {
    for (const compile_graph_ptr_t &graph : unit_->builtBodies) {
        DraftGraphBuilder *body = graph.get();
        if (body->funcType() == nullptr) {
            continue; // the body did not build (an error was reported)
        }
        const auto &draft = body->transaction().graph();
        for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            if (!draft.alive(id)) {
                continue;
            }
            const node_handle_t node = draft.ref(id);
            if (nodeKindOf(node) != runtime::GCNodeKind::Oper) {
                continue;
            }
            // The node whose value is the reference: `ref(v)` makes one, and a box is made where
            // its type is made.
            const auto *ref = RefType::as(valueTypeOf(nodeTypeOf(node)));
            if (ref == nullptr || ref->contentType() == nullptr) {
                continue;
            }
            Type *content = ref->contentType();
            optional<string> where = refInsideType(content);
            if (!where.has_value()) {
                where = refInsideACapturedValue(node, content);
            }
            if (!where.has_value()) {
                continue;
            }
            DiagnosticBuilder builder = diags_->of(SemanticDiag::RefContentHoldsRef);
            if (const auto origin = nodeDraftOf(node)->nodeOrigin(nodeIdOf(node));
                origin != camel::source::kInvalidOriginId) {
                builder.atOrigin(origin);
            }
            builder.commit(content->toString(), *where);
        }
    }
}

} // namespace camel::compile::gir