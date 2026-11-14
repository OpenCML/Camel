// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// DraftWritings: what makes two nodes one writing, and the index that answers it.

#include "camel/runtime/writings.h"
#include "writings_internal.h"
#include "camel/runtime/operator_instance.h"
#include "camel/runtime/reachable.h"

#include "camel/core/operator_table.h"
#include "camel/core/tags.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/text.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/draft_writes.h"
#include "camel/runtime/effect_liveness.h"
#include "camel/runtime/graph_txn.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace camel::runtime {

using namespace detail;

// ---- the vocabulary every part of the engine reads

namespace detail {

/// The type a replacement writing gets. A pattern's type is what the rule was written with, and it
/// can be less informed than the node the rule replaces: a law for `f32[..]` replaces a
/// `f32[3]` node. When the pattern accepts the node's own type, the writing keeps it, so
/// a rewritten value has the type of the place it is written to (and a saturation, which merges
/// writings by type, can merge them).
camel::core::type::Type *
writingType(const camel::core::type::Type *patternType, const camel::core::type::Type *nodeType) {
    if (patternType == nullptr) {
        return const_cast<camel::core::type::Type *>(nodeType);
    }
    if (nodeType != nullptr &&
        patternType->assignableFrom(const_cast<camel::core::type::Type *>(nodeType))) {
        return const_cast<camel::core::type::Type *>(nodeType);
    }
    return const_cast<camel::core::type::Type *>(patternType);
}

/// The text of a node while the draft still has it: its id, kind, operator and type, and the
/// value of a static constant (bounded, so a large constant does not turn a proof into a data
/// dump). This is the text an attribution step keeps, because a node a rewrite erases has no
/// later text to read. It is made only when a log was asked for.
std::string nodeText(const GraphTransaction &transaction, gc_node_ref_t id) {
    const auto &draft             = transaction.graph();
    const DraftNodeHeader *header = draft.header(id);
    if (header == nullptr) {
        return "%" + std::to_string(id);
    }
    std::string text = "%" + std::to_string(id) + " ";
    switch (header->kind) {
    case GCNodeKind::Data:
        text += "data";
        break;
    case GCNodeKind::Port:
        text += "port";
        break;
    case GCNodeKind::Cast:
        text += "cast";
        break;
    case GCNodeKind::Copy:
        text += "copy";
        break;
    case GCNodeKind::Fill:
        text += "fill";
        break;
    case GCNodeKind::Accs:
        text += "access";
        break;
    case GCNodeKind::Brch:
        text += "branch";
        break;
    case GCNodeKind::Join:
        text += "join";
        break;
    case GCNodeKind::Call:
        text += "call";
        break;
    case GCNodeKind::Bind:
        text += "bind";
        break;
    case GCNodeKind::Func:
        text += "func";
        break;
    case GCNodeKind::Oper: {
        const auto bytes = draft.payloadOf(id);
        text += bytes.size() >= sizeof(GCOperBody)
                    ? std::string(reinterpret_cast<const GCOperBody *>(bytes.data())->uri())
                    : std::string("oper");
        break;
    }
    case GCNodeKind::Gate:
        text += "gate";
        break;
    case GCNodeKind::Dref:
        text += "dref";
        break;
    case GCNodeKind::Loop:
        text += "loop";
        break;
    case GCNodeKind::Dead:
        text += "dead";
        break;
    }
    if (header->kind == GCNodeKind::Data && header->dataType != nullptr) {
        const auto index = static_cast<long long>(header->dataIndex);
        const auto slots = draft.staticSlots();
        if (index < 0 && static_cast<size_t>(-index) < slots.size()) {
            std::ostringstream value;
            camel::core::rtdata::printSlotCapped(
                value,
                slots[static_cast<size_t>(-index)],
                header->dataType,
                /*arrayLimit=*/4);
            text += " " + value.str();
        }
    }
    if (header->dataType != nullptr) {
        text += " : " + header->dataType->toString();
    }
    return text;
}

/// The bits of a constant that are its value. A slot is 64 bits, but an f32 or an i32 occupies the
/// low 32 and a bool the low 8; the other bytes are no part of the value. They must not decide
/// whether two constants are the same: the memory a slot came from can differ between a Debug and a
/// Release build, and then two equal f32 constants would compare unequal and a law would fire in
/// one build only.
slot_t canonicalConstant(slot_t value, const camel::core::type::Type *type) {
    if (type == nullptr) {
        return value;
    }
    switch (type->code()) {
    case camel::core::type::TypeCode::Int32:
    case camel::core::type::TypeCode::Float32:
        return static_cast<slot_t>(static_cast<std::uint32_t>(value));
    case camel::core::type::TypeCode::Bool:
        return static_cast<slot_t>(static_cast<std::uint8_t>(value));
    default:
        return value;
    }
}

/// Whether two constants are the same: the same type -- the same `Type`, by its canonical id, not
/// by the address of whichever object states it, as the rest of this file compares types -- and the
/// same value bits (see canonicalConstant). One predicate for every place a constant is compared.
bool sameConstantValue(
    slot_t a, const camel::core::type::Type *ta, slot_t b, const camel::core::type::Type *tb) {
    return camel::core::type::sameCanonicalType(ta, tb) &&
           canonicalConstant(a, ta) == canonicalConstant(b, tb);
}

/// The bytes a constant is keyed and compared by: its value's bits, with the bits that are not the
/// value zeroed (see canonicalConstant).
std::string constantPayload(slot_t value, const camel::core::type::Type *type) {
    const slot_t canonical = canonicalConstant(value, type);
    std::string payload(sizeof(canonical), '\0');
    std::memcpy(payload.data(), &canonical, sizeof(canonical));
    return payload;
}

/// The canonical id of `type` as a node key states it (see NodeKey::typeId).
camel::core::type::TypeId canonicalTypeIdOf(const Type *type) {
    return camel::core::type::TypeTable::instance().canonicalId(type);
}

/// Whether `oper`'s declared semantics say that exchanging its two operands gives the same value
/// for a result of type `type` (op-semantics 7.4: `BinaryLaw.commutative`, per element type;
/// `elementOf` names the element type of an aggregate, an empty one reads the type itself). This
/// is the fact the matcher reads to try the other order; an operator that does not declare it is
/// read in the order the program wrote, because only the value of the operator (not a guess about
/// its name or its shape) can justify the exchange.
bool declaredCommutative(camel::core::OperId oper, const camel::core::type::Type *type) {
    if (type == nullptr) {
        return false;
    }
    const camel::core::sem::OperatorSemantics *declared =
        camel::core::OperatorTable::instance().semantics(oper);
    if (declared == nullptr || !declared->binary) {
        return false;
    }
    const camel::core::sem::BinaryAlgebra &algebra = *declared->binary;
    const std::optional<camel::core::type::TypeCode> element =
        algebra.elementOf ? algebra.elementOf(type)
                          : std::optional<camel::core::type::TypeCode>(type->code());
    if (!element) {
        return false;
    }
    for (const camel::core::sem::BinaryLaw &law : algebra.laws) {
        if (law.element == *element) {
            return law.commutative;
        }
    }
    return false;
}

/// Whether `oper`'s declared semantics say that regrouping `(a op b) op c` as `a op (b op c)` gives
/// the same value for a result of type `type` (op-semantics 7b: `BinaryLaw.exactAssociative`, per
/// element type). Only the bit-exact kind is read: a float sum declares none, so regrouping it
/// stays with the `reassociate` permission and the rules that ask for it, and the matcher never
/// does it on its own (rewrite design 6).
bool declaredAssociative(camel::core::OperId oper, const camel::core::type::Type *type) {
    if (type == nullptr) {
        return false;
    }
    const camel::core::sem::OperatorSemantics *declared =
        camel::core::OperatorTable::instance().semantics(oper);
    if (declared == nullptr || !declared->binary) {
        return false;
    }
    const camel::core::sem::BinaryAlgebra &algebra = *declared->binary;
    const std::optional<camel::core::type::TypeCode> element =
        algebra.elementOf ? algebra.elementOf(type)
                          : std::optional<camel::core::type::TypeCode>(type->code());
    if (!element) {
        return false;
    }
    for (const camel::core::sem::BinaryLaw &law : algebra.laws) {
        if (law.element == *element) {
            return law.commutative && law.exactAssociative;
        }
    }
    return false;
}

} // namespace detail

// ---------------------------------------------------------------- DraftWritings

namespace {

using camel::core::type::Type;

/// Whether two nodes state the same type: one canonical id is one structure, whatever object
/// carries it and whatever names its key spells (camel/core/type/text.h, sameCanonicalType).
bool sameType(const Type *a, const Type *b) { return camel::core::type::sameCanonicalType(a, b); }
} // namespace

DraftWritings::DraftWritings(
    GraphDraft &draft, camel::core::sem::NumericPolicy policy, DeletionProtection protection,
    bool ordered)
    : draft_(draft), ownedTransaction_(GraphTransaction::borrow(draft)),
      transaction_(ownedTransaction_.get()), policy_(policy), protection_(protection),
      ordered_(ordered), store_(draft_.writings()), index_(std::make_unique<Index>()) {}

DraftWritings::DraftWritings(
    GraphTransaction &transaction, camel::core::sem::NumericPolicy policy,
    DeletionProtection protection, bool ordered)
    : draft_(transaction.edit()), transaction_(&transaction), policy_(policy),
      protection_(protection), ordered_(ordered), store_(draft_.writings()),
      index_(std::make_unique<Index>()) {}

DraftWritings::~DraftWritings() = default;

camel::core::sem::PermissionMask DraftWritings::contextOf(gc_node_ref_t node) const {
    return camel::core::tag::effectivePermissions(draft_, node, policy_);
}

namespace {

template <typename Written>
bool readsVariable(
    const GraphTransaction &transaction, std::span<const gc_node_ref_t> norm,
    std::span<const gc_node_ref_t> with, Written &&written) {
    const auto &draft = transaction.graph();
    for (auto inputs : {norm, with}) {
        for (gc_node_ref_t in : inputs) {
            const auto *h = draft.header(in);
            if (written(in) ||
                (h != nullptr && h->kind == GCNodeKind::Oper &&
                 camel::core::OperatorTable::instance().touchesClass(
                     reinterpret_cast<const GCOperBody *>(draft.payloadOf(in).data())->oper,
                     camel::core::sem::ResourceClass::Variable))) {
                return true;
            }
        }
    }
    return false;
}

/// Whether a node of this shape computes its value from its data inputs alone, apart from the
/// nodes it reads. A control input is not part of this question: it orders the node, it does not
/// make the value different (design principles 3.3, 3.4), so a pure computation with a
/// control input is a pure computation still.
bool shapeTakesPart(
    const GraphTransaction &transaction, GCNodeKind kind, gc_slot_idx_t dataIndex, const Type *type,
    std::span<const std::byte> payload) {
    const auto &draft = transaction.graph();
    switch (kind) {
    case GCNodeKind::Data: {
        if (dataIndex >= 0) {
            return false;
        }
        const auto index = static_cast<size_t>(-dataIndex);
        if (index >= draft.staticSlots().size()) {
            return false;
        }
        const Type *ty = draft.staticSlotTypes()[index];
        ty             = ty != nullptr ? ty : type;
        return ty != nullptr && !camel::core::type::isGCTraced(ty->code());
    }
    case GCNodeKind::Cast:
    case GCNodeKind::Accs:
        return true;
    case GCNodeKind::Oper:
        return payload.size() >= sizeof(GCOperBody) &&
               camel::core::OperatorTable::instance().mayBeRecomputed(
                   reinterpret_cast<const GCOperBody *>(payload.data())->oper);
    default:
        return false;
    }
}
} // namespace

bool DraftWritings::writtenInPlace(gc_node_ref_t node) {
    auto &written = index_->written;
    if (node >= written.size()) {
        written.resize(draft_.nodeSlotCount(), 0);
    }
    // Asked once per node: walking the users of a value read by thousands of nodes each time
    // would make a saturation quadratic. The writings a saturation adds write nothing.
    if (written[node] == 0) {
        written[node] = isWrittenInPlace(draft_, node) ? 2 : 1;
    }
    return written[node] == 2;
}

uint8_t DraftWritings::shapeOf(gc_node_ref_t node) {
    auto &takes = index_->takes;
    if (node >= takes.size()) {
        takes.resize(draft_.nodeSlotCount(), 0);
    }
    if (takes[node] == 0) {
        const auto *h = draft_.header(node);
        bool yes      = h != nullptr && draft_.alive(node) &&
                        shapeTakesPart(
                            transaction(),
                            h->kind,
                            h->dataIndex,
                            h->dataType,
                            draft_.payloadOf(node)) &&
                        !writtenInPlace(node);
        if (yes && h->kind == GCNodeKind::Oper) {
            yes = !readsVariable(
                transaction(),
                draft_.normInputsOf(node),
                draft_.withInputsOf(node),
                [&](gc_node_ref_t in) { return writtenInPlace(in); });
        }
        // 1: takes part; 2: does not. The control inputs are not asked: they order the node and
        // do not make the value different, so they neither keep it out of a class of
        // writings nor out of a rule's term. A rewrite that replaces a node with control inputs
        // carries them over to the writing that replaces it (`rewrite`), so the order the program
        // wrote survives the replacement.
        takes[node] = yes ? 1 : 2;
    }
    return takes[node];
}

bool DraftWritings::takesPart(gc_node_ref_t node) { return shapeOf(node) == 1; }

namespace {

/// The key of a node of the given parts; `canonOf` names the class of an input.
template <typename CanonOf>
NodeKey makeKey(
    const GraphTransaction &transaction, GCNodeKind kind, camel::core::sem::PermissionMask perms,
    uint32_t region, const Type *type, gc_slot_idx_t dataIndex, std::span<const std::byte> payload,
    std::span<const gc_node_ref_t> norm, std::span<const gc_node_ref_t> with, CanonOf &&canonOf) {
    const auto &draft = transaction.graph();
    NodeKey key;
    key.kind   = static_cast<uint8_t>(kind);
    key.perms  = perms;
    key.region = region;
    // The type the key states is the one the constant's bits are read with; the key itself
    // records its canonical id (NodeKey::typeId).
    const Type *keyType = type;
    switch (kind) {
    case GCNodeKind::Data: {
        const auto index = static_cast<size_t>(-dataIndex);
        const slot_t v   = draft.staticSlots()[index];
        if (const Type *ty = draft.staticSlotTypes()[index]) {
            keyType = ty;
        }
        key.payload = constantPayload(v, keyType);
        break;
    }
    case GCNodeKind::Oper: {
        const auto oper = reinterpret_cast<const GCOperBody *>(payload.data())->oper;
        key.payload.assign(reinterpret_cast<const char *>(&oper), sizeof(oper));
        break;
    }
    default:
        key.payload.assign(reinterpret_cast<const char *>(payload.data()), payload.size());
        break;
    }
    key.typeId = canonicalTypeIdOf(keyType);
    for (gc_node_ref_t in : norm) {
        key.norm.push_back(canonOf(in));
    }
    for (gc_node_ref_t in : with) {
        key.with.push_back(canonOf(in));
    }
    return key;
}

} // namespace

void DraftWritings::enqueue(gc_node_ref_t node) {
    if (node >= queued_.size()) {
        queued_.resize(draft_.nodeSlotCount(), false);
    }
    if (!queued_[node]) {
        queued_[node] = true;
        pending_.push_back(node);
    }
}

void DraftWritings::joined(
    camel::core::sem::PermissionMask context, std::span<const gc_node_ref_t> moved) {
    for (gc_node_ref_t node : moved) {
        for (auto users : {draft_.normUsersOf(node), draft_.withUsersOf(node)}) {
            for (gc_node_ref_t user : users) {
                if (draft_.alive(user) && contextOf(user) == context) {
                    enqueue(user);
                }
            }
        }
    }
}

void DraftWritings::index(gc_node_ref_t node) {
    auto &ix = *index_;
    if (node >= ix.keyOf.size()) {
        ix.keyOf.resize(draft_.nodeSlotCount());
    }
    if (!takesPart(node)) {
        return;
    }
    const camel::core::sem::PermissionMask context = contextOf(node);
    const auto *h                                  = draft_.header(node);
    NodeKey key                                    = makeKey(
        transaction(),
        h->kind,
        h->permissions,
        draft_.nodeRegion(node),
        h->dataType,
        h->dataIndex,
        draft_.payloadOf(node),
        draft_.normInputsOf(node),
        draft_.withInputsOf(node),
        [&](gc_node_ref_t in) { return store_.canon(context, in); });
    // An associative-commutative chain is one value however the program ordered or bracketed it,
    // so its key is not the written inputs but the flattened multiset of its terms' canonical ids,
    // ascending (rewrite design 6, and the canonical form the plan fixes: the same-operator terms
    // flattened and sorted by identity). The key is what the congruence closure reads, so two
    // writings that differ only in the order or the bracketing of such a chain become one class
    // without a rule saying so, which is what the commutation and association rules used to say.
    // Only the declared laws decide, per element type: a float sum is commutative and is read as
    // two terms in either order, while regrouping it stays with `reassociate` and its rules.
    if (h->kind == GCNodeKind::Oper && !key.norm.empty()) {
        const auto oper = reinterpret_cast<const GCOperBody *>(draft_.payloadOf(node).data())->oper;
        const bool associative = declaredAssociative(oper, h->dataType);
        if (associative || declaredCommutative(oper, h->dataType)) {
            std::vector<gc_node_ref_t> terms;
            const std::function<void(gc_node_ref_t)> collect = [&](gc_node_ref_t in) {
                const auto *ih = draft_.header(in);
                if (associative && ih != nullptr && draft_.alive(in) &&
                    ih->kind == GCNodeKind::Oper && ih->permissions == h->permissions &&
                    sameType(ih->dataType, h->dataType) && takesPart(in) &&
                    draft_.withInputsOf(in).empty() && !draft_.normInputsOf(in).empty() &&
                    reinterpret_cast<const GCOperBody *>(draft_.payloadOf(in).data())->oper ==
                        oper) {
                    for (gc_node_ref_t next : draft_.normInputsOf(in)) {
                        collect(next);
                    }
                    return;
                }
                terms.push_back(store_.canon(context, in));
            };
            for (gc_node_ref_t in : draft_.normInputsOf(node)) {
                collect(in);
            }
            std::sort(terms.begin(), terms.end());
            key.norm = std::move(terms);
        }
    }
    if (ix.keyOf[node].has_value()) {
        if (*ix.keyOf[node] == key) {
            return;
        }
        auto old = ix.byKey.find(*ix.keyOf[node]);
        if (old != ix.byKey.end() && old->second == node) {
            ix.byKey.erase(old);
        }
    }
    auto [it, inserted] = ix.byKey.try_emplace(key, node);
    ix.keyOf[node]      = std::move(key);
    if (!inserted && it->second != node && !store_.same(context, it->second, node)) {
        ++congruentMerges_;
        store_.merge(it->second, node, 0, 0, kWritingDerived, this);
    }
}

void DraftWritings::build() {
    for (gc_node_ref_t node = 0; node < draft_.nodeSlotCount(); ++node) {
        if (draft_.alive(node)) {
            index(node);
        }
    }
    rebuild();
}

void DraftWritings::rebuild() {
    while (!pending_.empty()) {
        std::vector<gc_node_ref_t> batch;
        batch.swap(pending_);
        std::sort(batch.begin(), batch.end());
        for (gc_node_ref_t node : batch) {
            queued_[node] = false;
        }
        for (gc_node_ref_t node : batch) {
            index(node);
        }
    }
}

bool DraftWritings::assertEqual(
    gc_node_ref_t a, gc_node_ref_t b, camel::core::sem::PermissionMask perms, uint16_t rule) {
    const bool changed = assertEqualDeferred(a, b, perms, rule);
    rebuild();
    return changed;
}

bool DraftWritings::assertEqualDeferred(
    gc_node_ref_t a, gc_node_ref_t b, camel::core::sem::PermissionMask perms, uint16_t rule) {
    const auto *ha = draft_.header(a);
    const auto *hb = draft_.header(b);
    // Two writings of one value have one type, compared by canonical id: the structural identity
    // of the type, with the names in its key (a dimension name, a parameter name) erased, which
    // is the relation `equals` uses. Comparing the objects would refuse two writings whose types
    // were built in different places, which is what a rewrite does every time it re-encodes.
    if (ha == nullptr || hb == nullptr || !sameType(ha->dataType, hb->dataType) ||
        draft_.nodeRegion(a) != draft_.nodeRegion(b) || ha->permissions != hb->permissions ||
        (perms & ~contextOf(a)) != 0) {
        throw std::logic_error(
            "Two writings of a value have the same type, region and permissions, and the "
            "permissions the equivalence needs.");
    }
    return store_.merge(a, b, perms, rule, 0, this);
}

gc_node_ref_t DraftWritings::lookupOrAdd(
    const DraftNodeInit &init, std::span<const gc_node_ref_t> norm,
    std::span<const gc_node_ref_t> with) {
    auto &ix = *index_;
    std::optional<NodeKey> key;
    if (!greedy_ &&
        shapeTakesPart(transaction(), init.kind, init.dataIndex, init.dataType, init.payload) &&
        !(init.kind == GCNodeKind::Oper &&
          readsVariable(transaction(), norm, with, [&](gc_node_ref_t in) {
              return writtenInPlace(in);
          }))) {
        key = makeKey(
            transaction(),
            init.kind,
            init.permissions,
            init.region,
            init.dataType,
            init.dataIndex,
            init.payload,
            norm,
            with,
            [&](gc_node_ref_t in) {
                return store_.canon(policy_.effective(init.permissions), in);
            });
        if (auto found = ix.byKey.find(*key); found != ix.byKey.end()) {
            return found->second;
        }
    }
    GraphDraft::WritingScope scope(draft_);
    // A node that computes a value into the frame gets a slot of its own.
    DraftNodeInit made = init;
    if (made.dataIndex == 0 && made.dataType != nullptr && made.kind != GCNodeKind::Data &&
        made.kind != GCNodeKind::Port) {
        made.dataIndex = draft_.allocateRuntimeSlot(made.dataType);
    }
    const gc_node_ref_t node = draft_.addNode(made);
    draft_.setNormInputs(node, norm);
    draft_.setWithInputs(node, with);
    if (key.has_value()) {
        if (node >= ix.keyOf.size()) {
            ix.keyOf.resize(draft_.nodeSlotCount());
        }
        if (node >= ix.takes.size()) {
            ix.takes.resize(draft_.nodeSlotCount(), 0);
        }
        ix.takes[node] = 1;
        ix.byKey.emplace(*key, node);
        ix.keyOf[node] = std::move(*key);
    }
    return node;
}

} // namespace camel::runtime
