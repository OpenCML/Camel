// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/string.h"
#include "camel/runtime/graph.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace camel::transforms {

/// The three fields of a node record these readings ask, on the two faces that carry a payload: a
/// frozen graph (`GCGraph`) hands out a record whose `kind`, `dataType()` and `dataIndex` are
/// members, and a draft hands out `DraftNode`, which keeps the same three under `header`. Asking
/// them through these keeps every reader below written once, for both.
namespace payload_detail {

template <typename Node> inline runtime::GCNodeKind kindOf(const Node *node) noexcept {
    if constexpr (requires { node->kind; }) {
        return node->kind;
    } else {
        return node->header.kind;
    }
}

template <typename Node>
inline camel::core::type::Type *dataTypeOf(const Node *node) noexcept {
    if constexpr (requires { node->dataType(); }) {
        return node->dataType();
    } else {
        return node->header.dataType;
    }
}

template <typename Node> inline runtime::gc_slot_idx_t dataIndexOf(const Node *node) noexcept {
    if constexpr (requires { node->dataIndex; }) {
        return node->dataIndex;
    } else {
        return node->header.dataIndex;
    }
}

/// Whether node `ref` has a payload: a frozen graph answers it of the record
/// (`GCNode::hasPayload`), a draft's record holds the count.
template <typename Graph>
inline bool hasPayloadOf(const Graph &graph, runtime::gc_node_ref_t ref) noexcept {
    if constexpr (requires { graph.hasPayload(ref); }) {
        return graph.hasPayload(ref);
    } else if constexpr (requires { graph.node(ref)->hasPayload(); }) {
        const auto *node = graph.node(ref);
        return node != nullptr && node->hasPayload();
    } else {
        const auto *node = graph.node(ref);
        return node != nullptr && node->header.payloadBytes > 0;
    }
}

/// The URI of an operator node: out of the body a frozen graph points at (`nodeBodyAs`), out of the
/// URI a face that answers one names, or out of the payload bytes a draft keeps -- the same
/// `GCOperBody`.
template <typename Graph>
inline std::string_view uriOf(const Graph &graph, runtime::gc_node_ref_t ref) noexcept {
    if constexpr (requires { graph.template nodeBodyAs<runtime::GCOperBody>(ref); }) {
        const auto *body = graph.template nodeBodyAs<runtime::GCOperBody>(ref);
        return body == nullptr ? std::string_view() : std::string_view(body->uri());
    } else if constexpr (requires { graph.operUriOf(ref); }) {
        return graph.operUriOf(ref);
    } else {
        const std::span<const std::byte> bytes = graph.payloadOf(ref);
        if (bytes.size() < sizeof(runtime::GCOperBody)) {
            return std::string_view();
        }
        return std::string_view(reinterpret_cast<const runtime::GCOperBody *>(bytes.data())->uri());
    }
}

} // namespace payload_detail

/*
 * The four node kinds that stand for **ordinary operators** -- a conversion (CAST), a copy (COPY),
 * a construction (FILL) and a member read (ACCS) -- read as the operators they stand for: `cast`,
 * `copy`, `fill` and `access` are declared operators, and the front end still builds the same four
 * operations as nodes of those kinds.
 *
 * **Why one place.** A reader that has to know "is this node a construction" asks here, once: the
 * question has one answer -- a construction is an OPER node whose operator says it constructs, or a
 * node of the FILL kind the front end builds -- and a site that asks it in its own words
 * (`node->kind == rt::GCNodeKind::Fill`) answers it for one form only, and is wrong for the other.
 * So the sites that ask it (`jvp.cpp`, `module.cpp`, `rule_expand.cpp`, `split.cpp`,
 * `split_loop_cut.cpp`, `split_internal.h`, `transpose/walk.cpp`, `transpose/walkmachinery.cpp`)
 * ask it here:
 *
 *     if (nodeOperatorOf(graph, ref) == NodeOperator::Fill) { ... }
 *
 * The OPER arm of `nodeOperatorOf(Graph, ref)` answers from the operator record: it reads the
 * node's URI out of `GCOperBody` and names the four by it, so a conversion the front end hands over
 * as an ordinary operator is a conversion here too. What is left for the day the front end stops
 * building the four node kinds is the deletion of the `kind` arm of `nodeOperatorOf(GCNodeKind)`;
 * the eight files do not change again.
 *
 * **Two faces, one question.** The overloads below are templates over the face, because both a
 * frozen graph (`GCGraph`) and the graph a session is editing (`GraphDraft`) answer `nodeCount()`,
 * `node(ref)` and the operator a node names. The payload readers at the bottom are templated over
 * the face as well, and they read the bytes where the face keeps them: a frozen graph's payload is
 * pointed at, a draft's is copied out (`payloadOf`), and neither is reinterpreted in place.
 *
 * `GraphView` (`camel/graph/graph_view.h`) is deliberately not one of these faces: it answers a
 * node's kind, its type and its ports, and it does not hand out a payload at all -- a FILL body, an
 * access's position, the static area -- because under the view the four kinds are operators, so the
 * bytes these two readers decode are not a thing it answers. A caller that reaches one of these
 * readings is holding a frozen graph or a draft, which is what every call site does.
 *
 * **The payload, the same question again.** `NotOne` means "not one of the four", so a caller that
 * only wants a yes/no keeps reading as a predicate. `fillPayloadOf` is the payload side of it, and
 * it answers **both** forms: a FILL node carries the slot list in its body and the values in its
 * with-inputs, and a `fill` call carries the same two things as its own operands -- the template
 * first, then the values in the target type's layout order, which is the slot list a construction
 * needs. So a reader asks for the payload and gets one answer, instead of asking the kind and
 * reading the bytes of a node that no longer has any -- which is what an OPER node would give it,
 * and "an OPER's bytes read as a FILL are some other number", a guard no caller has to write for
 * itself.
 *
 * **A member read is asked of `memberReadOf`, and its shape is the declared one.** The declaration
 * is the authority (`__access_value__`, `builtin/operators/struct.cpp`): one ordinary operand, the
 * value, and the member -- a place or a name -- as the call's `with` argument (`norm.size() == 1`,
 * `with.size() == 1`). An ACCS node keeps the same two things in its body and its one ordinary
 * input, so the reader answers both spellings and says which question was asked
 * (`MemberRead::kind`). Two defects are recorded here because this is where the shape is read: a
 * reader that took the member from the ordinary operands was reading a shape nobody declared, and
 * a reader that asked the kind alone saw an operator-spelled read as *no read at all* -- and a walk
 * that does not see a member read does not project its cotangent, which is a wrong number rather
 * than a refusal (`split_struct_field_read` is the other direction: a keyed read copied as a place
 * indexed a `Struct` as a `Tuple` and the VM stopped at `idx < t->size()`).
 *
 * **What is deliberately not here.** `static_cast<int>(node->kind)` in the refusal readings stays
 * reading the kind: it is a reading of the graph as stored, quoted in a diagnostic, and changing it
 * would change what a program prints.
 */

/// Which of the four operators a node is, when it is one of them.
enum class NodeOperator : uint8_t {
    NotOne = 0,
    Cast,
    Copy,
    Fill,
    Accs,
};

/// The case a copy walk switches a node on: the four families by their operator, every other kind
/// by its kind. `Other` is what a walk's `default:` already covered (a `Dead` place, a draft's
/// `Dref` and the reserved `Bind` never reach a frozen graph, and a walk that meets one has no
/// reading for it).
enum class NodeCase : uint8_t {
    Data,
    Port,
    Brch,
    Join,
    Call,
    Func,
    Oper,
    Gate,
    Loop,
    Cast,
    Copy,
    Fill,
    Accs,
    Other,
};

/// The operator a kind stands for: a conversion, a copy, a construction or a member read, asked of
/// a node the front end still builds as one of those four kinds.
inline NodeOperator nodeOperatorOf(runtime::GCNodeKind kind) noexcept {
    switch (kind) {
    case runtime::GCNodeKind::Cast:
        return NodeOperator::Cast;
    case runtime::GCNodeKind::Copy:
        return NodeOperator::Copy;
    case runtime::GCNodeKind::Fill:
        return NodeOperator::Fill;
    case runtime::GCNodeKind::Accs:
        return NodeOperator::Accs;
    default:
        return NodeOperator::NotOne;
    }
}

/// The same question in the form a copy walk's `switch` needs.
inline NodeCase nodeCaseOf(runtime::GCNodeKind kind) noexcept {
    switch (kind) {
    case runtime::GCNodeKind::Data:
        return NodeCase::Data;
    case runtime::GCNodeKind::Port:
        return NodeCase::Port;
    case runtime::GCNodeKind::Brch:
        return NodeCase::Brch;
    case runtime::GCNodeKind::Join:
        return NodeCase::Join;
    case runtime::GCNodeKind::Call:
        return NodeCase::Call;
    case runtime::GCNodeKind::Func:
        return NodeCase::Func;
    case runtime::GCNodeKind::Oper:
        return NodeCase::Oper;
    case runtime::GCNodeKind::Gate:
        return NodeCase::Gate;
    case runtime::GCNodeKind::Loop:
        return NodeCase::Loop;
    case runtime::GCNodeKind::Cast:
        return NodeCase::Cast;
    case runtime::GCNodeKind::Copy:
        return NodeCase::Copy;
    case runtime::GCNodeKind::Fill:
        return NodeCase::Fill;
    case runtime::GCNodeKind::Accs:
        return NodeCase::Accs;
    default:
        return NodeCase::Other;
    }
}

/// Which of the four operators a URI names, or `NotOne`. This is what a graph says about a node
/// that names an operator instead of wearing a kind: `:op/fill` is a construction, `:op/cast` a
/// conversion, `:op/copy` a copy, and a member read is written two ways because the two reads ask
/// the value different questions (a position, a name) -- `:op/access` and `:op/access_key` are both
/// a member read. Any other URI, including the empty one a node without a record answers, is
/// `NotOne`: an operator is one of the four by what it says it is, not by being an operator.
inline NodeOperator nodeOperatorOf(std::string_view uri) noexcept {
    if (uri == ":op/cast") {
        return NodeOperator::Cast;
    }
    if (uri == ":op/copy") {
        return NodeOperator::Copy;
    }
    if (uri == ":op/fill") {
        return NodeOperator::Fill;
    }
    if (uri == ":op/access" || uri == ":op/access_key") {
        return NodeOperator::Accs;
    }
    return NodeOperator::NotOne;
}

/// Which of the four operators node `ref` of `graph` is. A ref that is not a node is `NotOne`.
///
/// `Graph` is a frozen graph or a draft: both answer `nodeCount()` and a record for `node(ref)`, and
/// the three fields these readings ask of that record are `kind`, `dataType` and `dataIndex` in both
/// (a draft keeps them under `header`, see `payload_detail`).
template <typename Graph>
inline NodeOperator nodeOperatorOf(const Graph &graph, runtime::gc_node_ref_t ref) noexcept {
    if (ref >= graph.nodeCount()) {
        return NodeOperator::NotOne;
    }
    const auto *node = graph.node(ref);
    if (node == nullptr) {
        return NodeOperator::NotOne;
    }
    if (payload_detail::kindOf(node) == runtime::GCNodeKind::Oper) {
        // An operator node is one of the four by the URI its record holds, and every other operator
        // -- the overwhelming majority -- is `NotOne`, which is what makes a reader that only wants
        // a yes/no keep working. The URI is read where the face keeps it (`payload_detail::uriOf`):
        // in place out of `GCOperBody` on a frozen graph, or out of the payload bytes a draft
        // holds -- the same body either way.
        if (!payload_detail::hasPayloadOf(graph, ref)) {
            return NodeOperator::NotOne;
        }
        return nodeOperatorOf(payload_detail::uriOf(graph, ref));
    }
    return nodeOperatorOf(payload_detail::kindOf(node));
}

/// Which case node `ref` is. A ref that is not a node is `Other`, the case a walk's `default:`
/// already answered.
inline NodeCase nodeCaseOf(const runtime::GCGraph &graph, runtime::gc_node_ref_t ref) noexcept {
    switch (nodeOperatorOf(graph, ref)) {
    case NodeOperator::Cast:
        return NodeCase::Cast;
    case NodeOperator::Copy:
        return NodeCase::Copy;
    case NodeOperator::Fill:
        return NodeCase::Fill;
    case NodeOperator::Accs:
        return NodeCase::Accs;
    case NodeOperator::NotOne:
        break;
    }
    if (ref >= graph.nodeCount()) {
        return NodeCase::Other;
    }
    const auto *node = graph.node(ref);
    return node == nullptr ? NodeCase::Other : nodeCaseOf(payload_detail::kindOf(node));
}

/// What a construction says: which kind of aggregate it builds, the values it writes, and the slot
/// each value fills. The two forms state it in different places and this answers both -- a FILL
/// node from its body and its with-inputs, a `fill` call from its ordinary operands (the template
/// first, then the values, which are already in the target type's layout order).
struct FillPayload {
    runtime::GCFillKind fillKind = runtime::GCFillKind::Tuple;
    /// The values the construction writes, in the order the node writes them.
    std::vector<runtime::gc_node_ref_t> values;
    /// The slot the k-th value fills. A call lists them all in order, so this is `0 .. n-1`; a FILL
    /// body may list only some of them and take the rest from the template.
    std::vector<runtime::gc_slot_idx_t> slots;

    /// The slot of the k-th value; a value the table does not reach fills its own place.
    runtime::gc_slot_idx_t slotOf(size_t k) const noexcept {
        return k < slots.size() ? slots[k] : static_cast<runtime::gc_slot_idx_t>(k);
    }
    size_t size() const noexcept { return values.size(); }
};


namespace payload_detail {

/// The node's payload bytes, `payloadOf` on a read face and `nodePayloadBytes` on a frozen graph:
/// the same bytes, one copied out and one pointed at.
template <typename Graph>
std::span<const std::byte> bytesOf(const Graph &graph, runtime::gc_node_ref_t ref) {
    if constexpr (requires { graph.payloadOf(ref); }) {
        return graph.payloadOf(ref);
    } else {
        return graph.nodePayloadBytes(ref);
    }
}

/// The constant a DATA node stands for (`-dataIndex` in the static area, the numbering
/// `GCGraph::staticSlots` and both read faces share), or nullptr when `ref` is no constant. A
/// place and a name are both constants the program wrote, and reading one at a time other than
/// export time is what makes a construction or a member read say what it does.
template <typename Graph>
const ::slot_t *constantOf(const Graph &graph, runtime::gc_node_ref_t ref) {
    const auto *node = graph.node(ref);
    if (node == nullptr || payload_detail::kindOf(node) != runtime::GCNodeKind::Data ||
        payload_detail::dataIndexOf(node) >= 0) {
        return nullptr;
    }
    const auto slots = graph.staticSlots();
    const size_t at  = static_cast<size_t>(-static_cast<int64_t>(payload_detail::dataIndexOf(node)));
    return at < slots.size() ? &slots[at] : nullptr;
}

/// Which construction a value of this type is: the kind a call's operands do not state and a FILL
/// body does.
inline runtime::GCFillKind fillKindOfType(const camel::core::type::Type *type) noexcept {
    if (type == nullptr) {
        return runtime::GCFillKind::Tuple;
    }
    switch (type->code()) {
    case camel::core::type::TypeCode::Struct:
        return runtime::GCFillKind::Struct;
    case camel::core::type::TypeCode::Array:
        return runtime::GCFillKind::Array;
    case camel::core::type::TypeCode::Function:
        return runtime::GCFillKind::FunctionClosure;
    default:
        return runtime::GCFillKind::Tuple;
    }
}

} // namespace payload_detail

/// The construction `ref` is, or nullopt when it is not one. Answers a FILL node and a `:op/fill`
/// call alike: the kind from the body, or from the type the call's value has.
template <typename Graph>
std::optional<FillPayload> fillPayloadOf(const Graph &graph, runtime::gc_node_ref_t ref) {
    const auto *node = graph.node(ref);
    if (node == nullptr) {
        return std::nullopt;
    }
    FillPayload payload;
    if (payload_detail::kindOf(node) == runtime::GCNodeKind::Fill) {
        const std::span<const std::byte> bytes = payload_detail::bytesOf(graph, ref);
        if (bytes.size() < sizeof(runtime::GCFillBody)) {
            return std::nullopt;
        }
        // The span is read out of the graph's own bytes, not out of a copy of the header: a copy
        // would leave `slots()` reading past its end.
        const auto *body = reinterpret_cast<const runtime::GCFillBody *>(bytes.data());
        payload.fillKind = body->fillKind;
        const auto slots = body->slots();
        payload.slots.assign(slots.begin(), slots.end());
        const auto values = graph.withInputsOf(ref);
        payload.values.assign(values.begin(), values.end());
        return payload;
    }
    if (nodeOperatorOf(graph, ref) != NodeOperator::Fill) {
        return std::nullopt;
    }
    const auto norm = graph.normInputsOf(ref);
    if (norm.empty()) {
        return std::nullopt;
    }
    payload.fillKind = payload_detail::fillKindOfType(payload_detail::dataTypeOf(node));
    payload.values.assign(norm.begin() + 1, norm.end());
    payload.slots.resize(payload.values.size());
    for (size_t i = 0; i < payload.slots.size(); ++i) {
        payload.slots[i] = static_cast<runtime::gc_slot_idx_t>(i);
    }
    return payload;
}

/// A member read's answer, in the form a walk asks it: which member of which value. The read is
/// written two ways and both are the same read -- the `ACCS` node (`GCAccsBody`: the kind, the place
/// or the key) and the operator its declaration names (`:op/access` by place, `:op/access_key` by
/// key, each carried as the call's `with` argument; `lib/std/prelude.cml`). A reader that looked at
/// one spelling only saw an operator-spelled read as *no read at all* -- and a walk that does not
/// see a member read does not project its cotangent, which is a wrong number rather than a refusal.
struct MemberRead {
    runtime::GCAccsKind kind = runtime::GCAccsKind::TupleIndex;
    runtime::gc_node_ref_t value = runtime::kInvalidNodeRef; // the aggregate the member is read of
    uint32_t place           = 0;                            // TupleIndex: which member
    std::string key;                                         // StructKey: which member, by name
};

/// The member read `ref` is, whichever spelling wrote it, or nothing when it is not one -- or is not
/// the shape its declaration states. A node like that is reported by name where it runs (the
/// kernel's own reading of its operands), so this answers "not a read" here instead of a second
/// opinion about it.
///
/// A template over the reader for the reason `fillPayloadOf` is one: the faces this directory reads
/// answer `node`, `normInputsOf` and `withInputsOf`, and the payload is asked of `payload_detail`.
template <typename Reader>
inline std::optional<MemberRead> memberReadOf(const Reader &graph, runtime::gc_node_ref_t ref) {
    const auto norm     = graph.normInputsOf(ref);
    const auto *const record = graph.node(ref);
    if (record == nullptr) {
        return std::nullopt;
    }
    // An ACCS node carries the member in its body and the value as its one ordinary input.
    if (payload_detail::kindOf(record) == runtime::GCNodeKind::Accs) {
        const std::span<const std::byte> bytes = payload_detail::bytesOf(graph, ref);
        if (bytes.size() < sizeof(runtime::GCAccsBody) || norm.empty()) {
            return std::nullopt;
        }
        const auto *body = reinterpret_cast<const runtime::GCAccsBody *>(bytes.data());
        MemberRead read{.kind = body->accsKind, .value = norm.front(), .place = body->value};
        read.key = std::string(body->key());
        return read;
    }
    if (payload_detail::kindOf(record) != runtime::GCNodeKind::Oper ||
        !payload_detail::hasPayloadOf(graph, ref)) {
        return std::nullopt;
    }
    // The operator the read is, and the member it names: the declaration says the member is the
    // call's `with` argument and the value its one ordinary operand (`__access_value__`,
    // builtin/operators/struct.cpp), so a call of another shape is not a read this answers.
    const std::string_view uri = payload_detail::uriOf(graph, ref);
    const bool byKey           = uri == ":op/access_key";
    if (!byKey && uri != ":op/access") {
        return std::nullopt;
    }
    const auto member = graph.withInputsOf(ref);
    if (norm.size() != 1 || member.size() != 1) {
        return std::nullopt;
    }
    const ::slot_t *constant = payload_detail::constantOf(graph, member.front());
    if (constant == nullptr) {
        return std::nullopt;
    }
    MemberRead read{
        .kind  = byKey ? runtime::GCAccsKind::StructKey : runtime::GCAccsKind::TupleIndex,
        .value = norm.front()};
    if (byKey) {
        const auto *text = camel::core::rtdata::fromSlot<::String *>(*constant);
        if (text == nullptr) {
            return std::nullopt;
        }
        read.key = text->toString();
    } else {
        const int64_t place = camel::core::rtdata::fromSlot<int64_t>(*constant);
        if (place < 0) {
            return std::nullopt;
        }
        read.place = static_cast<uint32_t>(place);
    }
    return read;
}
} // namespace camel::transforms