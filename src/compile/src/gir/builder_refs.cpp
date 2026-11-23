// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The content rule of `Ref<T>`: a reference may hold any type
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

/// The phrase every walk over a content starts at: the value the box is made of.
const string kTheContent = "the content";

/// The phrase a report names the closure with: the box's own value, or the field, part or element
/// of the container the value holds it in.
string closureAt(const string &where) {
    return where == kTheContent ? string("the closure") : "the closure in " + where;
}

/// The report clause for a layer of the walk that could not be read. It is a refusal and not a
/// pass: the rule's premise is that no box holds a reference at any depth (design principles 3.5),
/// so a capture this walk cannot see may be the one that holds one, and letting it through would be
/// exactly the box that is never freed.
string
unreadableCaptureClause(const string &where, const vector<string> &through, const string &why) {
    string clause = "it holds " + closureAt(where) + " whose captures cannot be read (" + why + ")";
    if (!through.empty()) {
        clause += ", reached through '" + joinedNames(through) + "'";
    }
    return clause + ", so a Ref may be inside it";
}

/// Whether a type mentions a function anywhere inside it. A value whose type does is a value the
/// walk has to open, because what a function captured is not in its type; a container whose type
/// mentions none holds no closure and needs no opening. A type this does not read is answered as
/// mentioning none, with the reading and the depth `refInsideType` takes.
bool mentionsFunction(const Type *type, int depth = 0) {
    if (type == nullptr || depth > 16) {
        return false;
    }
    if (type->code() == TypeCode::Function) {
        return true;
    }
    switch (type->code()) {
    case TypeCode::Tuple: {
        const auto *tuple = static_cast<const TupleType *>(type);
        for (size_t i = 0; i < tuple->size(); ++i) {
            if (mentionsFunction(tuple->typeAt(i), depth + 1)) {
                return true;
            }
        }
        return false;
    }
    case TypeCode::Struct: {
        const auto *record = static_cast<const StructType *>(type);
        for (size_t i = 0; i < record->size(); ++i) {
            if (mentionsFunction(record->typeAt(i), depth + 1)) {
                return true;
            }
        }
        return false;
    }
    case TypeCode::Array: {
        const auto *array = static_cast<const ArrayType *>(type);
        return mentionsFunction(array->elemType(), depth + 1);
    }
    case TypeCode::Stream: {
        const auto *stream = static_cast<const StreamType *>(type);
        return mentionsFunction(stream->elemType(), depth + 1);
    }
    default:
        break;
    }
    if (const auto *enumType = asEnumType(type); enumType != nullptr) {
        for (size_t tag = 0; tag < enumType->variantCount(); ++tag) {
            for (Type *field : enumType->variant(tag).fieldTypes) {
                if (mentionsFunction(field, depth + 1)) {
                    return true;
                }
            }
        }
        return false;
    }
    if (isOtherType(type->code())) {
        const auto *other = static_cast<const OtherType *>(type);
        for (Type *argument : other->paramsSpan()) {
            if (mentionsFunction(argument, depth + 1)) {
                return true;
            }
        }
    }
    return false;
}

/// One member of a composite value, as the fill that built it names it.
struct HeldMember {
    node_handle_t value;
    Type *type;
    size_t slot;
    string name;  // the field's name; empty for a part or an element
    string where; // "field 'cb'", "part 1", "element 0"
};

/// The members of a composite value the graph spells out. A record, a tuple and an array are one
/// `FILL` node: its payload names the slots the fill fills and its `with` inputs are one value per
/// slot, so a field, a part and an element are all read here the same way. `nullopt` when the value
/// is not such a fill -- a call's result, a parameter -- and what it holds is then not in this
/// graph.
optional<vector<HeldMember>> membersOf(node_handle_t value) {
    value = throughCopy(value);
    Type *type = value != nullptr ? valueTypeOf(nodeTypeOf(value)) : nullptr;
    if (type == nullptr) {
        return nullopt;
    }
    const auto fillKind = constructionKindOf(value, type);
    if (!fillKind.has_value()) {
        return nullopt;
    }
    vector<HeldMember> members;
    for (const auto &[slot, member] : constructionValuesOf(value)) {
        switch (*fillKind) {
        case runtime::GCFillKind::Struct: {
            const auto *record =
                type->code() == TypeCode::Struct ? static_cast<const StructType *>(type) : nullptr;
            if (record == nullptr || slot >= record->size()) {
                return nullopt;
            }
            const string name = string(record->fieldName(slot));
            members.push_back({member, record->typeAt(slot), slot, name, "field '" + name + "'"});
            break;
        }
        case runtime::GCFillKind::Tuple: {
            const auto *tuple =
                type->code() == TypeCode::Tuple ? static_cast<const TupleType *>(type) : nullptr;
            if (tuple == nullptr || slot >= tuple->size()) {
                return nullopt;
            }
            members.push_back(
                {member, tuple->typeAt(slot), slot, "", "part " + std::to_string(slot)});
            break;
        }
        case runtime::GCFillKind::Array: {
            const auto *array =
                type->code() == TypeCode::Array ? static_cast<const ArrayType *>(type) : nullptr;
            if (array == nullptr) {
                return nullopt;
            }
            members.push_back(
                {member, array->elemType(), slot, "", "element " + std::to_string(slot)});
            break;
        }
        default:
            // A closure's own fill: its `with` inputs are its captures, which the walk reads from
            // the graph behind it instead.
            return nullopt;
        }
    }
    return members;
}

/// The member a projection reads. `q.cb` is how the graph takes a field of a value, not a value of
/// its own: a fill may hold the projection rather than what it projects (a literal written as one
/// type and entered into another is copied field by field, `builder_expr.cpp`), so the walk goes on
/// at the member it names. `nullptr` when the base is not a value this graph spells out.
///
/// The member is asked of `accessedPartOf`, which answers both spellings a read has (the operator
/// with the member as its first operand, the ACCS kind with it in the payload); the value it reads
/// is the last ordinary operand either way, which is the port this walk starts from.
node_handle_t memberBehindProjection(node_handle_t projection) {
    const auto inputs = normInputsOf(projection);
    if (inputs.empty()) {
        return nullptr;
    }
    const node_handle_t base = nodeDraftOf(projection)->ref(inputs.back());
    const auto members       = membersOf(base);
    if (!members.has_value()) {
        return nullptr;
    }
    Type *const baseType = valueTypeOf(nodeTypeOf(base));
    if (baseType == nullptr) {
        return nullptr;
    }
    const std::optional<size_t> slot = accessedPartOf(projection, baseType);
    if (!slot.has_value()) {
        return nullptr;
    }
    for (const HeldMember &member : members.value()) {
        if (member.slot == *slot) {
            return member.value;
        }
    }
    return nullptr;
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
// its captures, so the values the closure took are read from the graph it was built as. A record, a
// tuple or an array can hold that closure -- in a field, a part, an element, however many layers
// down -- and the type walk above says only that a function is in there, not what it captured. So
// the walk follows the content into the value the call hands in and opens what it finds: a
// container's members, and a closure's captures.
//
// It is a work list with the values it has opened kept, not a recursion with a depth bound: what
// the walk asks of a value does not depend on how it got there, so a value opened once is not
// opened again, the graphs are finite, and the walk ends without a limit this code would have to
// guess. A layer it cannot open, and cannot show to hold no function in the first place, is
// refused rather than passed (3.5): a capture it cannot see may be the one holding a reference.
optional<string> Builder::refInsideACapturedValue(node_handle_t call, Type *content) const {
    if (call == nullptr || content == nullptr) {
        return nullopt;
    }
    // One value the walk is at: the value, its type, the phrase naming what holds it, and the
    // closures it passed through by capture to reach it.
    struct Held {
        node_handle_t value;
        Type *type;
        string where;
        vector<string> through;
    };
    const auto &draft = nodeTransactionOf(call)->graph();
    vector<Held> work;
    vector<Held> rest;
    for (draft_node_ref_t input : normInputsOf(call)) {
        const node_handle_t value = draft.ref(input);
        Type *type                = value == nullptr ? nullptr : valueTypeOf(nodeTypeOf(value));
        if (type == nullptr) {
            continue;
        }
        Held held{value, type, kTheContent, {}};
        // The value the box is made of, when the call states it. Every other argument is kept as a
        // fallback so that a call whose content type is written differently cannot slip past the
        // walk unread.
        (content != nullptr && type->equals(content) ? work : rest).push_back(held);
    }
    if (work.empty()) {
        work = rest;
    }

    unordered_set<node_handle_t> opened;
    while (!work.empty()) {
        const Held item = work.back();
        work.pop_back();
        if (item.value == nullptr || !opened.insert(item.value).second) {
            continue;
        }
        // A projection names a member of a value rather than a value of its own: the walk goes on
        // at the member it reads, and the projection's type says what that member is held as.
        if (nodeIsKind(item.value, runtime::GCNodeKind::Accs)) {
            const node_handle_t member = memberBehindProjection(item.value);
            if (member == nullptr) {
                return unreadableCaptureClause(
                    item.where,
                    item.through,
                    "a value it holds is projected from a value this program does not spell out");
            }
            work.push_back({member, item.type, item.where, item.through});
            continue;
        }
        if (item.type->code() == TypeCode::Function) {
            const compile_graph_ptr_t closure = functionValueGraphOf(item.value);
            if (closure == nullptr) {
                return unreadableCaptureClause(
                    item.where,
                    item.through,
                    "its graph is not one this program holds");
            }
            const auto held = capturedValues_.find(closure.get());
            if (held == capturedValues_.end()) {
                // A capture is recorded when the closure is built, so a graph with no entry took no
                // value: nothing inside it to follow, which is a reading and not a failure to read.
                continue;
            }
            for (const CapturedValue &captured : held->second) {
                // The origin is a node of the enclosing graph, kept in a table that outlives it: a
                // handle that no longer names its draft cannot be typed.
                if (!nodeIsCurrent(captured.origin)) {
                    return unreadableCaptureClause(
                        item.where,
                        item.through,
                        "a value it captured is no longer built");
                }
                Type *type = valueTypeOf(nodeTypeOf(captured.origin));
                if (type == nullptr) {
                    return unreadableCaptureClause(
                        item.where,
                        item.through,
                        "the type of a value it captured is unknown");
                }
                if (refInsideType(type, 0).has_value()) {
                    string where = "it holds a Ref, in the value '" + captured.name + "' " +
                                   closureAt(item.where) + " captures";
                    if (!item.through.empty()) {
                        where += ", reached through '" + joinedNames(item.through) + "'";
                    }
                    return where;
                }
                // What it captured is a value of its own: a closure's captures are read from its
                // graph, and a container's members from the fill. A container sits where the
                // capture put it, not where the closure that took it sits, and the chain of names
                // says which value it is.
                vector<string> through = item.through;
                through.push_back(captured.name);
                const bool container = type->code() != TypeCode::Function;
                work.push_back(
                    {captured.origin,
                     type,
                     container ? string("a value the closure captures") : item.where,
                     through});
            }
            continue;
        }
        // A container: only one whose type mentions a function has a closure to reach, and only a
        // fill's members can be read. Anything else was decided by refInsideType over the type.
        if (!mentionsFunction(item.type)) {
            continue;
        }
        const optional<vector<HeldMember>> members = membersOf(item.value);
        if (!members.has_value()) {
            return unreadableCaptureClause(
                item.where,
                item.through,
                "the container it is held in is not a value this program spells out");
        }
        for (const HeldMember &member : members.value()) {
            work.push_back(
                {member.value, member.type, member.where + " of " + item.where, item.through});
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
            Type *content          = ref->contentType();
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