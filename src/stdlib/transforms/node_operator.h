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

/*
 * The four node kinds that become **ordinary operators** -- a conversion (CAST), a copy (COPY), a
 * construction (FILL) and a member read (ACCS) -- read as the operators they are becoming. The
 * refactor plan (docs/design/architecture/refactor-plan.cn.md, stage 2) turns these four from kinds
 * of node into operators declared in `lib/std` and built by the front end as OPER nodes.
 *
 * **Why one place.** A reader that has to know "is this node a construction" used to say
 * `node->kind == rt::GCNodeKind::Fill`, and this directory said it, in its own words, in eight
 * files: `jvp.cpp`, `module.cpp`, `rule_expand.cpp`, `split.cpp`, `split_loop_cut.cpp`,
 * `split_internal.h`, `transpose/walk.cpp` and `transpose/walkmachinery.cpp`. Once the front end
 * builds these four as OPER nodes, every one of those tests is *wrong*: not by a little, but by
 * asking a question the node no longer answers (a construction is then an OPER node whose operator
 * says it constructs). So the question is asked here instead, and the eight files ask it:
 *
 *     if (nodeOperatorOf(graph, ref) == NodeOperator::Fill) { ... }
 *
 * The OPER arm of `nodeOperatorOf(Graph, ref)` answers from the operator record: it reads the
 * node's URI out of `GCOperBody` and names the four by it, so a conversion the front end hands over
 * as an ordinary operator is a conversion here too. What is left for the day the front end stops
 * building the four node kinds is the deletion of the `kind` arm of `nodeOperatorOf(GCNodeKind)`;
 * the eight files do not change again.
 *
 * **Two faces, one question.** `GraphRead` (`compile/gir/graph_read.h`) is the face the lift in
 * `jvp.cpp` reads; a `FrozenGraphRead` and a draft answer it. The overloads below are templates
 * over the face, because both a `GCGraph` and a `GraphRead` answer `nodeCount()`, `node(ref)->kind`
 * and the operator a node names: a frozen graph reads the URI in place out of `GCOperBody`, and a
 * `GraphRead` copies the same body out of its payload and answers that URI. The payload readers at
 * the bottom are templated over the face as well, and they read the bytes where the face keeps
 * them: a frozen graph's payload is pointed at, a draft's is copied out (`payloadOf`), and neither is
 * reinterpreted in place.
 *
 * **The payload, the same question again.** `NotOne` means "not one of the four", so a caller that
 * only wants a yes/no keeps reading as a predicate. `fillPayloadOf` / `accsPayloadOf` are the
 * payload side of it, and they answer **both** forms: a FILL node carries the slot list in its body
 * and the values in its with-inputs, and a `fill` call carries the same two things as its own
 * operands -- the template first, then the values in the target type's layout order, which is the
 * slot list a construction needs. A member read is the same story: an ACCS node keeps the place or
 * the key in its body, and a `:op/access` / `:op/access_key` call keeps it as its first operand, a
 * constant the program wrote. So a reader asks for the payload and gets one answer, instead of
 * asking the kind and reading the bytes of a node that no longer has any -- which is what an OPER
 * node would give it, and "an OPER's bytes read as a FILL are some other number"
 * (`transpose/walk.cpp` used to guard that by hand).
 *
 * These two readings are the last place the kind was still asked, one layer below the case
 * (`nodeCaseOf` above answered the operator). They are also where a graph says which of the two
 * things a member read asks -- a place or a name -- so a copied read that took the wrong one would
 * read another member; `split_struct_field_read` records that defect: a keyed read copied as a
 * place indexed a `Struct` as a `Tuple` and the VM stopped at `idx < t->size()`.
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

/// The operator a kind stands for. **This is the arm stage 2.3 deletes**: after it, a construction is an
/// OPER and there is no kind that means "a cast".
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
/// `Graph` is a frozen graph or a `GraphRead`: both answer `nodeCount()` and the record
/// `node(ref)` hands out, and the record's `kind` is the same field in both (see the file comment).
template <typename Graph>
inline NodeOperator nodeOperatorOf(const Graph &graph, runtime::gc_node_ref_t ref) noexcept {
    if (ref >= graph.nodeCount()) {
        return NodeOperator::NotOne;
    }
    const auto *node = graph.node(ref);
    if (node == nullptr) {
        return NodeOperator::NotOne;
    }
    if (node->kind == runtime::GCNodeKind::Oper) {
        // An operator node is one of the four by the URI its record holds, and every other operator
        // -- the overwhelming majority -- is `NotOne`, which is what makes a reader that only wants
        // a yes/no keep working. A frozen graph reads the URI in place out of `GCOperBody`; a
        // `GraphRead` copies the same body out of its payload and answers the URI as a value, so
        // its answer is asked and used in one expression here.
        if constexpr (requires { graph.template nodeBodyAs<runtime::GCOperBody>(ref); }) {
            if (!node->hasPayload()) {
                return NodeOperator::NotOne;
            }
            const auto *body = graph.template nodeBodyAs<runtime::GCOperBody>(ref);
            return body == nullptr ? NodeOperator::NotOne : nodeOperatorOf(body->uri());
        } else {
            const std::string uri = graph.operUriOf(ref);
            return nodeOperatorOf(uri);
        }
    }
    return nodeOperatorOf(node->kind);
}

/// Which case node `ref` is. A ref that is not a node is `Other`, the case a walk's `default:`
/// already answered.
template <typename Graph>
inline NodeCase nodeCaseOf(const Graph &graph, runtime::gc_node_ref_t ref) noexcept {
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
    return node == nullptr ? NodeCase::Other : nodeCaseOf(node->kind);
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

/// What a member read says: whether it reads by place or by name, and which.
struct AccsPayload {
    runtime::GCAccsKind accsKind = runtime::GCAccsKind::TupleIndex;
    /// The place, when it reads by place; a read by name leaves it 0 and carries `keyName`.
    uint32_t value = 0;
    std::string keyName;

    std::string_view key() const noexcept { return keyName; }
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
    if (node == nullptr || node->kind != runtime::GCNodeKind::Data || node->dataIndex >= 0) {
        return nullptr;
    }
    const auto slots = graph.staticSlots();
    const size_t at  = static_cast<size_t>(-static_cast<int64_t>(node->dataIndex));
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
    if (node->kind == runtime::GCNodeKind::Fill) {
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
    payload.fillKind = payload_detail::fillKindOfType(node->dataType());
    payload.values.assign(norm.begin() + 1, norm.end());
    payload.slots.resize(payload.values.size());
    for (size_t i = 0; i < payload.slots.size(); ++i) {
        payload.slots[i] = static_cast<runtime::gc_slot_idx_t>(i);
    }
    return payload;
}

/// The member read `ref` is, or nullopt when it is not one. Answers an ACCS node and a `:op/access`
/// / `:op/access_key` call alike: the place or the name is the body's, or the call's first operand.
template <typename Graph>
std::optional<AccsPayload> accsPayloadOf(const Graph &graph, runtime::gc_node_ref_t ref) {
    const auto *node = graph.node(ref);
    if (node == nullptr) {
        return std::nullopt;
    }
    AccsPayload payload;
    if (node->kind == runtime::GCNodeKind::Accs) {
        const std::span<const std::byte> bytes = payload_detail::bytesOf(graph, ref);
        if (bytes.size() < sizeof(runtime::GCAccsBody)) {
            return std::nullopt;
        }
        const auto *body = reinterpret_cast<const runtime::GCAccsBody *>(bytes.data());
        payload.accsKind = body->accsKind;
        payload.value    = body->value;
        payload.keyName  = std::string(body->key());
        return payload;
    }
    if (nodeOperatorOf(graph, ref) != NodeOperator::Accs) {
        return std::nullopt;
    }
    const auto norm = graph.normInputsOf(ref);
    if (norm.size() != 2) {
        return std::nullopt;
    }
    const ::slot_t *constant = payload_detail::constantOf(graph, norm.front());
    const auto *position     = graph.node(norm.front());
    const auto *positionType = position == nullptr ? nullptr : position->dataType();
    if (constant == nullptr || positionType == nullptr) {
        return std::nullopt;
    }
    // Which read this is, said by the type of the place it names: a name is text, a place a
    // number. The URI says the same thing and this does not have to read it twice.
    if (positionType->code() == camel::core::type::TypeCode::String) {
        const auto *text = camel::core::rtdata::fromSlot<::String *>(*constant);
        if (text == nullptr) {
            return std::nullopt;
        }
        payload.accsKind = runtime::GCAccsKind::StructKey;
        payload.keyName  = text->toString();
        return payload;
    }
    if (positionType->code() == camel::core::type::TypeCode::Int64) {
        payload.accsKind = runtime::GCAccsKind::TupleIndex;
        payload.value    = static_cast<uint32_t>(camel::core::rtdata::fromSlot<int64_t>(*constant));
        return payload;
    }
    return std::nullopt;
}

} // namespace camel::transforms