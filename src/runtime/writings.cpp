// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/runtime/writings.h"

#include "camel/core/operator_table.h"
#include "camel/core/tags.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/draft_writes.h"

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

// ---------------------------------------------------------------- engine roots

namespace {

/// Keeps what the engine holds outside the graph relocatable across the safepoints of a rule
/// condition: the draft's static values and the constant slots the compiled patterns copied out
/// of it. The rules' Program keeps the objects alive; this only updates the copies.
class EngineRootScope {
  public:
    EngineRootScope(GraphDraft &draft, std::span<const Rule> rules) : draft_(draft), rules_(rules) {
        camel::core::mm::autoSpace().registerExternalRootTracer(
            this,
            "rewrite.engineRoots",
            [this](const camel::core::mm::GenerationalAllocatorWithGC::RefRelocator &relocate) {
                draft_.relocateStaticValues(relocate);
                traceRuleConstants(rules_, relocate);
            });
#ifndef NDEBUG
        // The Debug-only safepoint draft-root check counts this scope for this draft only: a
        // session that rewrites several graphs still needs its RootScope for the others.
        debugDraftRootOpened(draft_);
#endif
    }
    ~EngineRootScope() {
#ifndef NDEBUG
        debugDraftRootClosed(draft_);
#endif
        camel::core::mm::autoSpace().unregisterExternalRootTracer(this);
    }
    EngineRootScope(const EngineRootScope &)            = delete;
    EngineRootScope &operator=(const EngineRootScope &) = delete;

  private:
    GraphDraft &draft_;
    std::span<const Rule> rules_;
};

/// The type a replacement writing gets. A pattern's type is what the rule was written with, and it
/// can be less informed than the node the rule replaces: a law for `Tensor<f32>` replaces a
/// `Tensor<f32, [3]>` node. When the pattern accepts the node's own type, the writing keeps it, so
/// a rewritten value has the type of the place it is written to (and a saturation, which merges
/// writings by type, can merge them).
camel::core::type::Type *writingType(
    const camel::core::type::Type *patternType, const camel::core::type::Type *nodeType) {
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
std::string nodeText(const GraphDraft &draft, gc_node_ref_t id) {
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
    case GCNodeKind::Sync:
        text += "sync";
        break;
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
                value, slots[static_cast<size_t>(-index)], header->dataType, /*arrayLimit=*/4);
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
/// Release build, and then two equal f32 constants would compare unequal and a law would fire in one
/// build only (H99).
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

/// The bytes a constant is keyed and compared by: its value's bits, with the bits that are not the
/// value zeroed (see canonicalConstant).
std::string constantPayload(slot_t value, const camel::core::type::Type *type) {
    const slot_t canonical = canonicalConstant(value, type);
    std::string payload(sizeof(canonical), '\0');
    std::memcpy(payload.data(), &canonical, sizeof(canonical));
    return payload;
}

} // namespace

// ---------------------------------------------------------------- WritingStore

WritingStore::WritingStore(std::span<const WritingUnion> log) {
    for (const WritingUnion &entry : log) {
        merge(entry.a, entry.b, entry.perms, entry.rule, entry.flags);
    }
}

void WritingStore::ensure(Layer &layer, gc_node_ref_t node) {
    if (node < layer.parent.size()) {
        return;
    }
    const size_t old = layer.parent.size();
    layer.parent.resize(static_cast<size_t>(node) + 1);
    layer.size.resize(layer.parent.size(), 1);
    layer.smallest.resize(layer.parent.size());
    for (size_t i = old; i < layer.parent.size(); ++i) {
        layer.parent[i]   = static_cast<gc_node_ref_t>(i);
        layer.smallest[i] = static_cast<gc_node_ref_t>(i);
    }
}

gc_node_ref_t WritingStore::findIn(Layer &layer, gc_node_ref_t node) {
    if (node >= layer.parent.size()) {
        return node;
    }
    gc_node_ref_t root = node;
    while (layer.parent[root] != root) {
        root = layer.parent[root];
    }
    while (layer.parent[node] != root) {
        const gc_node_ref_t next = layer.parent[node];
        layer.parent[node]       = root;
        node                     = next;
    }
    return root;
}

bool WritingStore::unionIn(
    Layer &layer, gc_node_ref_t a, gc_node_ref_t b, WritingObserver *observer) {
    a = findIn(layer, a);
    b = findIn(layer, b);
    if (a == b) {
        return false;
    }
    ensure(layer, std::max(a, b));
    // The smaller class joins the larger.
    gc_node_ref_t big = a, small = b;
    if (layer.size[big] < layer.size[small] ||
        (layer.size[big] == layer.size[small] && big > small)) {
        std::swap(big, small);
    }
    auto smallMembers = layer.members.find(small);
    std::vector<gc_node_ref_t> moved;
    const bool smallHad = smallMembers != layer.members.end();
    if (!smallHad) {
        moved.push_back(small);
    } else {
        moved = smallMembers->second.nodes;
    }
    if (observer != nullptr) {
        observer->joined(layer.context, moved);
    }
    layer.parent[small] = big;
    layer.size[big] += layer.size[small];
    layer.smallest[big] = std::min(layer.smallest[big], layer.smallest[small]);

    auto bigMembers = layer.members.find(big);
    if (bigMembers == layer.members.end()) {
        bigMembers = layer.members.emplace(big, Layer::Members{{big}, true}).first;
    }
    bigMembers->second.nodes.insert(bigMembers->second.nodes.end(), moved.begin(), moved.end());
    bigMembers->second.sorted = false;
    if (smallHad) {
        layer.members.erase(small);
    }
    return true;
}

WritingStore::Layer &WritingStore::layer(camel::core::sem::PermissionMask context) {
    auto it = std::lower_bound(
        layers_.begin(),
        layers_.end(),
        context,
        [](const Layer &layer, uint8_t value) { return layer.context < value; });
    if (it != layers_.end() && it->context == context) {
        return *it;
    }
    if (layers_.size() >= kMaxLayers) {
        throw std::logic_error(
            "A graph has more than four numeric permission contexts among its writings.");
    }
    it = layers_.insert(it, Layer{.context = context});
    // The equivalences of the log whose permissions the context has.
    for (const WritingUnion &entry : log_) {
        if ((entry.perms & ~context) == 0) {
            unionIn(*it, entry.a, entry.b, nullptr);
        }
    }
    return *it;
}

bool WritingStore::merge(
    gc_node_ref_t a, gc_node_ref_t b, camel::core::sem::PermissionMask perms, uint16_t rule, uint8_t flags,
    WritingObserver *observer) {
    // The context of the equivalence itself decides whether it is new.
    Layer &own       = layer(perms);
    const bool isNew = findIn(own, a) != findIn(own, b);
    log_.push_back({a, b, perms, flags, rule});
    for (Layer &other : layers_) {
        if ((perms & ~other.context) == 0) {
            unionIn(other, a, b, observer);
        }
    }
    return isNew;
}

gc_node_ref_t WritingStore::canon(camel::core::sem::PermissionMask context, gc_node_ref_t node) {
    return findIn(layer(context), node);
}

gc_node_ref_t WritingStore::find(camel::core::sem::PermissionMask context, gc_node_ref_t node) {
    Layer &l        = layer(context);
    const auto root = findIn(l, node);
    return root < l.smallest.size() ? l.smallest[root] : root;
}

std::span<const gc_node_ref_t> WritingStore::members(camel::core::sem::PermissionMask context, gc_node_ref_t node) {
    Layer &l        = layer(context);
    const auto root = findIn(l, node);
    auto it         = l.members.find(root);
    if (it == l.members.end()) {
        return {};
    }
    if (!it->second.sorted) {
        std::sort(it->second.nodes.begin(), it->second.nodes.end());
        it->second.sorted = true;
    }
    return it->second.nodes;
}

std::vector<camel::core::sem::PermissionMask> WritingStore::contexts() const {
    std::vector<camel::core::sem::PermissionMask> result;
    for (const Layer &l : layers_) {
        result.push_back(l.context);
    }
    return result;
}

std::vector<gc_node_ref_t> WritingStore::classRoots(camel::core::sem::PermissionMask context) {
    Layer &l = layer(context);
    std::vector<gc_node_ref_t> roots;
    for (const auto &entry : l.members) {
        roots.push_back(l.smallest[entry.first]);
    }
    std::sort(roots.begin(), roots.end());
    return roots;
}

// ---------------------------------------------------------------- DraftWritings

namespace {

using camel::core::type::Type;

/// What makes two nodes the same value when their inputs are.
struct NodeKey {
    uint8_t kind     = 0;
    camel::core::sem::PermissionMask perms    = 0;
    uint32_t region  = 0;
    const Type *type = nullptr;
    std::string payload;
    std::vector<gc_node_ref_t> norm;
    std::vector<gc_node_ref_t> with;
    bool operator==(const NodeKey &) const = default;
};

struct NodeKeyHash {
    size_t operator()(const NodeKey &key) const {
        size_t h = std::hash<std::string>{}(key.payload);
        auto mix = [&h](size_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); };
        mix(key.kind);
        mix(key.perms);
        mix(key.region);
        mix(reinterpret_cast<uintptr_t>(key.type));
        for (gc_node_ref_t in : key.norm) {
            mix(in);
        }
        mix(0xffffffffu);
        for (gc_node_ref_t in : key.with) {
            mix(in);
        }
        return h;
    }
};

} // namespace

struct DraftWritings::Index {
    std::unordered_map<NodeKey, gc_node_ref_t, NodeKeyHash> byKey;
    std::vector<std::optional<NodeKey>> keyOf;
    // 0 unknown, 1 takes part, 2 does not.
    std::vector<uint8_t> takes;
    // 0 unknown, 1 not written in place, 2 written.
    std::vector<uint8_t> written;
};

DraftWritings::DraftWritings(GraphDraft &draft, camel::core::sem::NumericPolicy policy)
    : draft_(draft), policy_(policy), store_(draft.writings()), index_(std::make_unique<Index>()) {}

DraftWritings::~DraftWritings() = default;

camel::core::sem::PermissionMask DraftWritings::contextOf(gc_node_ref_t node) const {
    return camel::core::tag::effectivePermissions(draft_, node, policy_);
}

namespace {

template <typename Written>
bool readsVariable(
    const GraphDraft &draft, std::span<const gc_node_ref_t> norm,
    std::span<const gc_node_ref_t> with, Written &&written) {
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

/// Whether a node carries the program's order instead of being a pure value: a call runs a
/// graph, a branch, join, sync or loop runs (or orders) what it holds, a gate waits for its
/// control inputs, and an operator that is not pure has an effect (design principles 3.3).
/// This is the rule the generic rewrite pass uses to decide what it may not reorder
/// (`carriesOrdering` in src/passes/opt/generic/rewrite.cpp).
bool carriesOrdering(const GraphDraft &draft, gc_node_ref_t node) {
    const auto *h = draft.header(node);
    if (h == nullptr) {
        return false;
    }
    switch (h->kind) {
    case GCNodeKind::Func:
    case GCNodeKind::Call:
    case GCNodeKind::Join:
    case GCNodeKind::Brch:
    case GCNodeKind::Sync:
    case GCNodeKind::Loop:
        return true;
    case GCNodeKind::Gate:
        return !draft.ctrlInputsOf(node).empty();
    case GCNodeKind::Oper: {
        const std::span<const std::byte> payload = draft.payloadOf(node);
        return payload.size() < sizeof(GCOperBody) ||
               !camel::core::OperatorTable::instance().isPure(
                   reinterpret_cast<const GCOperBody *>(payload.data())->oper);
    }
    default:
        return false;
    }
}

/// Whether a node of this shape computes its value from its data inputs alone, apart from the
/// nodes it reads. A control input is not part of this question: it orders the node, it does not
/// make the value different (design principles 3.3, 3.4; H95), so a pure computation with a
/// control input is a pure computation still.
bool shapeTakesPart(
    const GraphDraft &draft, GCNodeKind kind, gc_slot_idx_t dataIndex, const Type *type,
    std::span<const std::byte> payload) {
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
               camel::core::OperatorTable::instance().isPure(
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
        bool yes =
            h != nullptr && draft_.alive(node) &&
            shapeTakesPart(draft_, h->kind, h->dataIndex, h->dataType, draft_.payloadOf(node)) &&
            !writtenInPlace(node);
        if (yes && h->kind == GCNodeKind::Oper) {
            yes = !readsVariable(
                draft_,
                draft_.normInputsOf(node),
                draft_.withInputsOf(node),
                [&](gc_node_ref_t in) { return writtenInPlace(in); });
        }
        // 1: takes part; 2: does not. The control inputs are not asked: they order the node and
        // do not make the value different (H95), so they neither keep it out of a class of
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
    const GraphDraft &draft, GCNodeKind kind, camel::core::sem::PermissionMask perms, uint32_t region, const Type *type,
    gc_slot_idx_t dataIndex, std::span<const std::byte> payload,
    std::span<const gc_node_ref_t> norm, std::span<const gc_node_ref_t> with, CanonOf &&canonOf) {
    NodeKey key;
    key.kind   = static_cast<uint8_t>(kind);
    key.perms  = perms;
    key.region = region;
    key.type   = type;
    switch (kind) {
    case GCNodeKind::Data: {
        const auto index = static_cast<size_t>(-dataIndex);
        const slot_t v   = draft.staticSlots()[index];
        if (const Type *ty = draft.staticSlotTypes()[index]) {
            key.type = ty;
        }
        key.payload = constantPayload(v, key.type);
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

void DraftWritings::joined(camel::core::sem::PermissionMask context, std::span<const gc_node_ref_t> moved) {
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
    const auto *h         = draft_.header(node);
    NodeKey key           = makeKey(
        draft_,
        h->kind,
        h->permissions,
        draft_.nodeRegion(node),
        h->dataType,
        h->dataIndex,
        draft_.payloadOf(node),
        draft_.normInputsOf(node),
        draft_.withInputsOf(node),
        [&](gc_node_ref_t in) { return store_.canon(context, in); });
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

bool DraftWritings::assertEqual(gc_node_ref_t a, gc_node_ref_t b, camel::core::sem::PermissionMask perms, uint16_t rule) {
    const bool changed = assertEqualDeferred(a, b, perms, rule);
    rebuild();
    return changed;
}

bool DraftWritings::assertEqualDeferred(
    gc_node_ref_t a, gc_node_ref_t b, camel::core::sem::PermissionMask perms, uint16_t rule) {
    const auto *ha = draft_.header(a);
    const auto *hb = draft_.header(b);
    if (ha == nullptr || hb == nullptr || ha->dataType != hb->dataType ||
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
        shapeTakesPart(draft_, init.kind, init.dataIndex, init.dataType, init.payload) &&
        !(init.kind == GCNodeKind::Oper && readsVariable(draft_, norm, with, [&](gc_node_ref_t in) {
              return writtenInPlace(in);
          }))) {
        key = makeKey(
            draft_,
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

namespace {

std::mutex &costModelsMutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, std::shared_ptr<const CostModel>> &costModels() {
    static std::map<std::string, std::shared_ptr<const CostModel>> models;
    return models;
}

class NodeCountModel : public CostModel {
  public:
    double nodeCost(const CostContext &, gc_node_ref_t) const override { return 1.0; }
};

class WeightedModel : public CostModel {
  public:
    double nodeCost(const CostContext &context, gc_node_ref_t node) const override {
        const GraphDraft &draft = context.draft;
        const auto *h           = draft.header(node);
        switch (h->kind) {
        case GCNodeKind::Data:
        case GCNodeKind::Port:
            return 0.1;
        case GCNodeKind::Cast:
            return 0.5;
        case GCNodeKind::Oper: {
            const std::string_view uri =
                reinterpret_cast<const GCOperBody *>(draft.payloadOf(node).data())->uri();
            auto has = [&](std::string_view name) { return uri.find(name) != uri.npos; };
            if (has("div") || has("mod")) {
                return 8.0;
            }
            if (has("mul")) {
                return 3.0;
            }
            if (has("add") || has("sub") || has("neg")) {
                return 1.0;
            }
            return 2.0;
        }
        default:
            return 2.0;
        }
    }
};

void ensureBuiltinCostModels() {
    static const bool once = [] {
        costModels().emplace("nodes", std::make_shared<NodeCountModel>());
        costModels().emplace("weighted", std::make_shared<WeightedModel>());
        return true;
    }();
    (void)once;
}

} // namespace

void registerCostModel(std::string name, std::shared_ptr<const CostModel> model) {
    std::lock_guard lock(costModelsMutex());
    ensureBuiltinCostModels();
    costModels()[std::move(name)] = std::move(model);
}

std::shared_ptr<const CostModel> findCostModel(const std::string &name) {
    std::lock_guard lock(costModelsMutex());
    ensureBuiltinCostModels();
    auto found = costModels().find(name);
    return found == costModels().end() ? nullptr : found->second;
}

WritingChoice
DraftWritings::choiceByCost(const CostModel &model, camel::core::context::Context *context) {
    rebuild();
    const size_t slots = draft_.nodeSlotCount();
    const CostContext costContext{draft_, context};
    constexpr double kInfinite = std::numeric_limits<double>::infinity();

    // The value each node is a writing of, and how many writings the value has.
    WritingChoice choice;
    choice.classOf.assign(slots, kInvalidNodeRef);
    choice.multi.assign(slots, 0);
    choice.chosen.assign(slots, kInvalidNodeRef);
    choice.cost.assign(slots, kInfinite);
    choice.own.assign(slots, 0.0);
    std::vector<double> own(slots, 0.0);
    for (gc_node_ref_t node = 0; node < slots; ++node) {
        if (!draft_.alive(node)) {
            continue;
        }
        const camel::core::sem::PermissionMask perms = contextOf(node);
        choice.classOf[node] = store_.canon(perms, node);
        choice.multi[node]   = store_.members(perms, node).empty() ? 0 : 1;
        own[node]            = model.nodeCost(costContext, node);
        if (!(own[node] > 0.0)) {
            throw std::logic_error("A cost model gives every node a positive cost.");
        }
    }

    // The cheapest way to compute each value: the cost of a writing is its own plus that of the
    // values it reads. Repeated until nothing gets cheaper; the first writing (smallest node)
    // wins a tie.
    std::vector<double> best(slots, kInfinite);
    std::vector<gc_node_ref_t> chosen(slots, kInvalidNodeRef);
    const auto inputsOf = [&](gc_node_ref_t node) {
        std::vector<gc_node_ref_t> all;
        for (auto part :
             {draft_.normInputsOf(node), draft_.withInputsOf(node), draft_.ctrlInputsOf(node)}) {
            all.insert(all.end(), part.begin(), part.end());
        }
        return all;
    };
    bool changed = true;
    for (size_t round = 0; changed && round <= slots; ++round) {
        changed = false;
        for (gc_node_ref_t node = 0; node < slots; ++node) {
            if (!draft_.alive(node)) {
                continue;
            }
            double total = own[node];
            bool known   = true;
            for (gc_node_ref_t input : inputsOf(node)) {
                const double cost = best[choice.classOf[input]];
                if (cost == kInfinite) {
                    known = false;
                    break;
                }
                total += cost;
            }
            if (known && total < best[choice.classOf[node]]) {
                best[choice.classOf[node]]   = total;
                chosen[choice.classOf[node]] = node;
                changed                      = true;
            }
        }
    }
    for (gc_node_ref_t node = 0; node < slots; ++node) {
        if (!draft_.alive(node)) {
            continue;
        }
        choice.chosen[node] = chosen[choice.classOf[node]];
        choice.cost[node]   = best[choice.classOf[node]];
        choice.own[node]    = own[node];
    }
    return choice;
}

ExtractStats
DraftWritings::extract(const CostModel &model, camel::core::context::Context *context) {
    GraphDraft::WritingScope scope(draft_);
    ExtractStats stats;
    const size_t slots = draft_.nodeSlotCount();
    const WritingChoice choice = choiceByCost(model, context);
    const std::vector<gc_node_ref_t> &classOf = choice.classOf;
    const std::vector<uint8_t> &multi         = choice.multi;
    const std::vector<gc_node_ref_t> &chosen  = choice.chosen;
    for (gc_node_ref_t node = 0; node < slots; ++node) {
        if (draft_.alive(node) && multi[node] && classOf[node] == node) {
            ++stats.classes;
        }
    }
    const auto inputsOf = [&](gc_node_ref_t node) {
        std::vector<gc_node_ref_t> all;
        for (auto part :
             {draft_.normInputsOf(node), draft_.withInputsOf(node), draft_.ctrlInputsOf(node)}) {
            all.insert(all.end(), part.begin(), part.end());
        }
        return all;
    };

    // What stays: what the graph names (its ports, entry, exit, result), the nodes extraction
    // does not trim, and the writing chosen for every value that is read.
    //
    // The retention boundary (design principles 3.1; graph store 3.5 and 3.6): extraction does
    // not decide which side effects happen, so it does not trim a node with an effect, a node
    // that carries the program's order, or a value written in place. Everything else is a pure
    // computation — a writing of its own value like any other — and stays only if something that
    // stays reads it. That includes a pure node with control inputs: the control input orders
    // it, it does not make it an effect. A saturation that added thousands of candidate
    // writings therefore does not leave them in the extracted program as dead nodes.
    std::vector<uint8_t> selected(slots, 0);
    std::vector<gc_node_ref_t> work;
    const auto select = [&](gc_node_ref_t node) {
        if (node != kInvalidNodeRef && draft_.alive(node) && !selected[node]) {
            selected[node] = 1;
            work.push_back(node);
        }
    };
    for (gc_node_ref_t node = 0; node < slots; ++node) {
        if (draft_.alive(node) && (writtenInPlace(node) || carriesOrdering(draft_, node))) {
            select(node);
        }
    }
    for (auto list : {draft_.normPorts(), draft_.withPorts(), draft_.closureNodes()}) {
        for (gc_node_ref_t node : list) {
            select(node);
        }
    }
    // The nodes the graph names as its entry, exit and result stand for values: the writing
    // chosen for the value takes their place.
    const auto valueOf = [&](gc_node_ref_t node) {
        return node != kInvalidNodeRef && draft_.alive(node) && multi[node] ? chosen[classOf[node]]
                                                                            : node;
    };
    const gc_node_ref_t entry  = valueOf(draft_.entryNode());
    const gc_node_ref_t exit   = valueOf(draft_.exitNode());
    const gc_node_ref_t output = valueOf(draft_.outputNode());
    const gc_node_ref_t result = valueOf(draft_.returnNode());
    for (gc_node_ref_t node : {entry, exit, output, result}) {
        select(node);
    }
    std::vector<gc_node_ref_t> regionResults;
    for (uint32_t r = 0; r < draft_.regionCount(); ++r) {
        regionResults.push_back(valueOf(draft_.region(r).result));
        select(regionResults.back());
    }
    for (size_t at = 0; at < work.size(); ++at) {
        // The other writings of this value that waited for something (control inputs): the
        // writing that stays waits for it too.
        if (multi[work[at]]) {
            for (gc_node_ref_t member : store_.members(contextOf(work[at]), work[at])) {
                for (gc_node_ref_t wait : draft_.ctrlInputsOf(member)) {
                    const gc_node_ref_t pick = chosen[classOf[wait]];
                    if (pick == kInvalidNodeRef) {
                        throw std::logic_error(
                            "A control input has no writing that does not read itself.");
                    }
                    select(pick);
                }
            }
        }
        for (gc_node_ref_t input : inputsOf(work[at])) {
            const gc_node_ref_t pick = chosen[classOf[input]];
            if (pick == kInvalidNodeRef) {
                throw std::logic_error("A value has no writing that does not read itself.");
            }
            select(pick);
        }
        if (!multi[work[at]] && chosen[classOf[work[at]]] == kInvalidNodeRef) {
            throw std::logic_error(
                "A node reads a value that has no writing that does not read itself.");
        }
    }

    // The nodes that stay read the writings that stay.
    for (gc_node_ref_t node = 0; node < slots; ++node) {
        if (!selected[node]) {
            continue;
        }
        bool rewired    = false;
        const auto pick = [&](std::span<const gc_node_ref_t> inputs) {
            std::vector<gc_node_ref_t> picked(inputs.begin(), inputs.end());
            for (gc_node_ref_t &input : picked) {
                const gc_node_ref_t to = chosen[classOf[input]];
                if (to != input) {
                    input   = to;
                    rewired = true;
                }
            }
            return picked;
        };
        const auto norm = pick(draft_.normInputsOf(node));
        const auto with = pick(draft_.withInputsOf(node));
        auto ctrl       = pick(draft_.ctrlInputsOf(node));
        if (multi[node] && chosen[classOf[node]] == node) {
            for (gc_node_ref_t member : store_.members(contextOf(node), node)) {
                if (member == node) {
                    continue;
                }
                for (gc_node_ref_t wait : draft_.ctrlInputsOf(member)) {
                    const gc_node_ref_t to = chosen[classOf[wait]];
                    if (to != node && std::find(ctrl.begin(), ctrl.end(), to) == ctrl.end()) {
                        ctrl.push_back(to);
                        rewired = true;
                    }
                }
            }
        }
        if (rewired) {
            draft_.setNormInputs(node, norm);
            draft_.setWithInputs(node, with);
            draft_.setCtrlInputs(node, ctrl);
            ++stats.rewired;
        }
    }
    if (entry != draft_.entryNode()) {
        draft_.setEntryNode(entry);
    }
    if (exit != draft_.exitNode()) {
        draft_.setExitNode(exit);
    }
    if (output != draft_.outputNode()) {
        draft_.setOutputNode(output);
    }
    if (result != draft_.returnNode()) {
        draft_.setReturnNode(result, draft_.returnKind());
    }
    for (uint32_t r = 0; r < draft_.regionCount(); ++r) {
        if (regionResults[r] != draft_.region(r).result) {
            draft_.setRegionResult(r, regionResults[r]);
        }
    }
    for (gc_node_ref_t node = 0; node < slots; ++node) {
        if (draft_.alive(node) && !selected[node]) {
            draft_.eraseNode(node);
            ++stats.erased;
        }
    }
    draft_.setWritings({});
    store_ = WritingStore();
    index_ = std::make_unique<Index>();
    pending_.clear();
    queued_.clear();
    return stats;
}

bool hasWritings(const Program &program) {
    for (const GCGraph *graph : program.graphs()) {
        if (!graph->executable()) {
            return true;
        }
    }
    return false;
}

const Program *extractWritings(
    const std::shared_ptr<camel::core::context::Context> &context, const Program *program,
    const CostModel &model) {
    if (program == nullptr || !hasWritings(*program)) {
        return program;
    }
    RuntimeGraphDraftSession session(context, program);
    for (GCGraph *graph : program->graphs()) {
        if (graph->executable()) {
            continue;
        }
        GraphDraft &draft = session.edit(graph);
        DraftWritings writings(draft);
        writings.extract(model, context.get());
    }
    return session.commit();
}

gc_node_ref_t DraftWritings::find(gc_node_ref_t node) { return store_.find(contextOf(node), node); }

std::span<const gc_node_ref_t> DraftWritings::writingsOf(gc_node_ref_t node) {
    return store_.members(contextOf(node), node);
}

GraphWritings graphWritings(
    const Program &program, const GCGraph *graph, const CostModel &model,
    camel::core::context::Context *context) {
    GraphWritings classes;
    // The executable state has one writing per value by definition: a dump of such a graph draws
    // no class, and does not pay for a decode of it.
    if (graph == nullptr || graph->executable()) {
        return classes;
    }
    const std::unique_ptr<GraphDraft> draft = GraphDraft::decode(program, graph);
    if (draft == nullptr) {
        return classes;
    }
    // The same policy the driver's extraction uses (`extractWritings`), so the classes and the
    // choice the dump shows are the ones the pipeline would act on.
    DraftWritings writings(*draft);
    writings.build();
    const WritingChoice choice = writings.choiceByCost(model, context);
    for (gc_node_ref_t node = 0; node < draft->nodeSlotCount(); ++node) {
        if (!draft->alive(node)) {
            continue;
        }
        classes.nodes[node] = GraphWritings::Node{
            .root = choice.classOf[node], .own = choice.own[node], .value = choice.cost[node]};
        // One entry per value, at its root (the smallest writing); the members are ascending.
        if (!choice.multi[node] || choice.classOf[node] != node) {
            continue;
        }
        const auto members = writings.writingsOf(node);
        if (members.size() < 2) {
            continue;
        }
        GraphWritings::Value value;
        value.root     = node;
        value.writings.assign(members.begin(), members.end());
        value.chosen = choice.chosen[node];
        value.cost   = choice.cost[node];
        classes.valueOf[node] = classes.values.size();
        for (gc_node_ref_t member : value.writings) {
            classes.valueOf[member] = classes.values.size();
        }
        classes.values.push_back(std::move(value));
    }
    return classes;
}

// ---------------------------------------------------------------- rules

Pattern patVar(uint32_t var, camel::core::type::Type *type) {
    Pattern p;
    p.kind = Pattern::Kind::Var;
    p.var  = var;
    p.type = type;
    return p;
}

Pattern patConst(uint32_t var, camel::core::type::Type *type) {
    Pattern p   = patVar(var, type);
    p.constOnly = true;
    return p;
}

Pattern patLiteral(slot_t value, camel::core::type::Type *type) {
    Pattern p;
    p.kind  = Pattern::Kind::Constant;
    p.value = canonicalConstant(value, type);
    p.type  = type;
    return p;
}

Pattern patOp(camel::core::OperId oper, std::vector<Pattern> args, camel::core::type::Type *type) {
    Pattern p;
    p.kind = Pattern::Kind::Op;
    p.oper = oper;
    p.type = type;
    p.args = std::move(args);
    return p;
}

Pattern patCompute(
    camel::core::type::Type *type, std::function<slot_t(std::span<const slot_t>)> compute,
    std::vector<Pattern> args) {
    Pattern p;
    p.kind    = Pattern::Kind::Compute;
    p.type    = type;
    p.compute = std::move(compute);
    p.args    = std::move(args);
    return p;
}

std::optional<std::pair<slot_t, camel::core::type::Type *>> Match::constant(uint32_t var) const {
    return writings.constantOf(vars[var]);
}

namespace {

void tracePatternConstants(
    const Pattern &pattern, const camel::core::rtdata::Object::RefRelocator &relocate) {
    if (pattern.kind == Pattern::Kind::Constant && pattern.type && pattern.type->isGCTraced() &&
        pattern.value != NullSlot) {
        auto *ref = camel::core::rtdata::fromSlot<camel::core::rtdata::Object *>(pattern.value);
        if (ref) {
            ref = relocate(
                ref,
                pattern.type,
                camel::core::rtdata::RefTraceInfo{
                    .owner     = nullptr,
                    .ownerType = nullptr,
                    .slotType  = pattern.type,
                    .ownerKind = "RulePattern",
                    .slotName  = {},
                    .slotIndex = 0,
                });
            pattern.value = camel::core::rtdata::toSlot(ref);
        }
    }
    for (const Pattern &arg : pattern.args) {
        tracePatternConstants(arg, relocate);
    }
}

} // namespace

void traceRuleConstants(
    std::span<const Rule> rules, const camel::core::rtdata::Object::RefRelocator &relocate) {
    for (const Rule &rule : rules) {
        tracePatternConstants(rule.lhs, relocate);
        tracePatternConstants(rule.rhs, relocate);
    }
}

std::optional<std::pair<slot_t, camel::core::type::Type *>>
DraftWritings::constantOf(gc_node_ref_t node) {
    const camel::core::sem::PermissionMask context = contextOf(node);
    const auto members    = store_.members(context, node);
    const auto look       = [&](gc_node_ref_t candidate)
        -> std::optional<std::pair<slot_t, camel::core::type::Type *>> {
        const auto *h = draft_.header(candidate);
        if (h == nullptr || !draft_.alive(candidate) || h->kind != GCNodeKind::Data ||
            h->dataIndex >= 0) {
            return std::nullopt;
        }
        const auto index = static_cast<size_t>(-h->dataIndex);
        if (index >= draft_.staticSlots().size() || !takesPart(candidate)) {
            return std::nullopt;
        }
        camel::core::type::Type *slotType = draft_.staticSlotTypes()[index];
        camel::core::type::Type *ty = slotType != nullptr ? slotType : h->dataType;
        return std::pair{canonicalConstant(draft_.staticSlots()[index], ty), ty};
    };
    if (members.empty()) {
        return look(node);
    }
    for (gc_node_ref_t member : members) {
        if (auto found = look(member)) {
            return found;
        }
    }
    return std::nullopt;
}

gc_node_ref_t DraftWritings::constantNode(
    slot_t value, camel::core::type::Type *type, camel::core::sem::PermissionMask perms, uint32_t region) {
    const slot_t stored = canonicalConstant(value, type);
    NodeKey key;
    key.kind    = static_cast<uint8_t>(GCNodeKind::Data);
    key.perms   = perms;
    key.region  = region;
    key.type    = type;
    key.payload = constantPayload(stored, type);
    auto &ix = *index_;
    if (auto found = ix.byKey.find(key); found != ix.byKey.end()) {
        return found->second;
    }
    GraphDraft::WritingScope scope(draft_);
    const gc_node_ref_t node = draft_.materializeStaticValue(stored, type);
    if (perms != 0) {
        draft_.setNodePermissions(node, perms);
    }
    if (region != 0) {
        draft_.setNodeRegion(node, region);
    }
    index(node);
    return node;
}

/// Whether a pattern term of `patternType` may match a node of `nodeType`: the type the pattern
/// was written for must accept the node's (a `Tensor<f32>` pattern reads a `Tensor<f32, [3]>`,
/// a pattern for `Tensor<f32>` never reads a `Tensor<int>`). The scalar operators have one id per
/// element type, so their id already says this; the tensor operators share one id for every
/// element type, and without the check a tensor law would rewrite another dtype's arithmetic
/// with an f32 rounding argument (rewrite design 2.2).
namespace {
bool acceptsType(
    const camel::core::type::Type *patternType, const camel::core::type::Type *nodeType) {
    return patternType == nullptr || nodeType == nullptr ||
           patternType->assignableFrom(const_cast<camel::core::type::Type *>(nodeType));
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
} // namespace

bool DraftWritings::matchFrom(
    camel::core::sem::PermissionMask context, std::vector<Item> &work, size_t at, std::vector<gc_node_ref_t> &bind,
    std::vector<uint8_t> &bound, std::vector<gc_node_ref_t> *waits, bool commuted,
    const std::function<bool()> &emit) {
    if (at == work.size()) {
        return emit();
    }
    const Item item  = work[at];
    const Pattern &p = *item.pattern;
    switch (p.kind) {
    case Pattern::Kind::Var:
        if (p.constOnly && !constantOf(item.node).has_value()) {
            return false;
        }
        if (const auto *header = draft_.header(item.node);
            header == nullptr || !acceptsType(p.type, header->dataType)) {
            return false;
        }
        if (bound[p.var]) {
            bool same = store_.same(context, bind[p.var], item.node);
            if (!same && p.constOnly) {
                // A repeated `const c` is a repeated constant value, not a repeated node: source
                // `x * 3.0 + x * 3.0` has two DATA nodes of the same value.
                const auto a = constantOf(bind[p.var]);
                const auto b = constantOf(item.node);
                same = a && b && a->second == b->second &&
                       canonicalConstant(a->first, a->second) ==
                           canonicalConstant(b->first, b->second);
            }
            return same && matchFrom(context, work, at + 1, bind, bound, waits, commuted, emit);
        }
        bound[p.var] = 1;
        bind[p.var]  = item.node;
        {
            const bool matched = matchFrom(context, work, at + 1, bind, bound, waits, commuted, emit);
            bound[p.var]       = 0;
            return matched;
        }
    case Pattern::Kind::Constant: {
        const auto found = constantOf(item.node);
        if (!found || found->second != p.type ||
            canonicalConstant(found->first, p.type) != canonicalConstant(p.value, p.type)) {
            return false;
        }
        return matchFrom(context, work, at + 1, bind, bound, waits, commuted, emit);
    }
    case Pattern::Kind::Op: {
        // The root of a rule is the node itself; below it, any writing of the value read.
        std::vector<gc_node_ref_t> candidates;
        if (at == 0) {
            candidates.push_back(item.node);
        } else {
            const auto members = store_.members(context, item.node);
            candidates.assign(members.begin(), members.end());
            if (candidates.empty()) {
                candidates.push_back(item.node);
            }
        }
        // One pairing of the pattern's operands with a candidate's data inputs. `swapped` reads
        // the two inputs in the other order; it is legal only for a binary term whose operator's
        // declared semantics say the exchange gives the same value for this element type.
        const auto matchCandidate = [&](gc_node_ref_t candidate, bool swapped) -> bool {
            const auto *h = draft_.header(candidate);
            if (h == nullptr || !draft_.alive(candidate) || h->kind != GCNodeKind::Oper ||
                h->permissions != matchPerms_ || !acceptsType(p.type, h->dataType) ||
                !takesPart(candidate) ||
                reinterpret_cast<const GCOperBody *>(draft_.payloadOf(candidate).data())->oper !=
                    p.oper) {
                return false;
            }
            const auto inputs = draft_.normInputsOf(candidate);
            if (inputs.size() != p.args.size() || !draft_.withInputsOf(candidate).empty()) {
                return false;
            }
            if (swapped && (inputs.size() != 2 || !declaredCommutative(p.oper, h->dataType))) {
                return false;
            }
            // What this term waits for: the rewrite carries it onto the writing that replaces the
            // root, so the law does not move the computation across a control edge's ordering.
            const size_t savedWaits = waits != nullptr ? waits->size() : 0;
            if (waits != nullptr) {
                for (gc_node_ref_t wait : draft_.ctrlInputsOf(candidate)) {
                    if (std::find(waits->begin(), waits->end(), wait) == waits->end()) {
                        waits->push_back(wait);
                    }
                }
            }
            const size_t saved = work.size();
            for (size_t i = 0; i < inputs.size(); ++i) {
                const size_t input = swapped ? (i == 0 ? 1 : 0) : i;
                work.push_back({&p.args[i], inputs[input]});
            }
            const bool matched = matchFrom(context, work, at + 1, bind, bound, waits, commuted, emit);
            work.resize(saved);
            if (waits != nullptr) {
                waits->resize(savedWaits);
            }
            return matched;
        };
        bool accepted = false;
        for (gc_node_ref_t candidate : candidates) {
            accepted = matchCandidate(candidate, false) || accepted;
        }
        if (!accepted && commuted) {
            // Nothing matched in the order the law wrote. A law is about values, not about the
            // order the program wrote them in: if the operator's semantics say exchanging its
            // operands gives the same value, the other order denotes the same left side, so the
            // right side (built below from the law's own text) is the same value too. The
            // natural order was tried first, so a law that writes the operands in a particular
            // order keeps that order whenever the program matches it, and the replacement keeps
            // the written shape. Operators that do not declare commutativity are never commuted,
            // and a two-way rule's reverse already reads the other order (`commuted` false).
            for (gc_node_ref_t candidate : candidates) {
                accepted = matchCandidate(candidate, true) || accepted;
            }
        }
        return accepted;
    }
    case Pattern::Kind::Compute:
        return false; // right side only
    }
    return false;
}

gc_node_ref_t
DraftWritings::build(const Pattern &pattern, const Match &match, const DraftNodeInit &base) {
    switch (pattern.kind) {
    case Pattern::Kind::Var:
        return match.vars[pattern.var];
    case Pattern::Kind::Constant:
        return constantNode(
            pattern.value, writingType(pattern.type, base.dataType), base.permissions, base.region);
    case Pattern::Kind::Compute: {
        std::vector<slot_t> values;
        for (const Pattern &arg : pattern.args) {
            const gc_node_ref_t node = build(arg, match, base);
            const auto value         = constantOf(node);
            if (!value) {
                throw std::logic_error(
                    "A computed term of a rule reads a value that is not a constant.");
            }
            values.push_back(value->first);
        }
        return constantNode(
            pattern.compute(values), writingType(pattern.type, base.dataType), base.permissions,
            base.region);
    }
    case Pattern::Kind::Op: {
        std::vector<gc_node_ref_t> inputs;
        for (const Pattern &arg : pattern.args) {
            inputs.push_back(build(arg, match, base));
        }
        GCOperBody body;
        body.oper = pattern.oper;
        std::vector<std::byte> bytes(sizeof(GCOperBody));
        std::memcpy(bytes.data(), &body, sizeof(body));
        DraftNodeInit init = base;
        init.kind          = GCNodeKind::Oper;
        init.dataIndex     = 0;
        init.dataType      = writingType(pattern.type, base.dataType);
        init.payload       = bytes;
        return lookupOrAdd(init, inputs);
    }
    }
    return kInvalidNodeRef;
}

RewriteStats DraftWritings::rewrite(
    std::span<const Rule> rules, size_t maxRounds, RewriteLog *log) {
    for (const Rule &rule : rules) {
        if (rule.twoWay) {
            throw std::invalid_argument(
                "Rule '" + rule.name +
                "' holds both ways (<=>): rewriting applies a rule one way, saturation uses both.");
        }
    }
    if (!draft_.writings().empty()) {
        throw std::logic_error("A rewrite needs a draft with one writing per value.");
    }
    GraphDraft::WritingScope scope(draft_);
    EngineRootScope roots(draft_, rules);
    greedy_ = true;
    RewriteStats stats;
    auto &ix = *index_;
    // The constants there are, so that a constant a rule needs is found, not made again.
    for (gc_node_ref_t node = 0; node < draft_.nodeSlotCount(); ++node) {
        if (draft_.alive(node) && draft_.header(node)->kind == GCNodeKind::Data &&
            takesPart(node)) {
            const auto *h = draft_.header(node);
            NodeKey key;
            key.kind             = static_cast<uint8_t>(GCNodeKind::Data);
            key.perms            = h->permissions;
            key.region           = draft_.nodeRegion(node);
            key.type             = h->dataType;
            const auto slotIndex = static_cast<size_t>(-h->dataIndex);
            const slot_t value   = draft_.staticSlots()[slotIndex];
            if (const Type *ty = draft_.staticSlotTypes()[slotIndex]) {
                key.type = ty;
            }
            key.payload = constantPayload(value, key.type);
            ix.byKey.try_emplace(key, node);
            if (node >= ix.keyOf.size()) {
                ix.keyOf.resize(draft_.nodeSlotCount());
            }
            ix.keyOf[node] = key;
        }
    }

    size_t varCount                                = 0;
    std::function<void(const Pattern &)> countVars = [&](const Pattern &p) {
        if (p.kind == Pattern::Kind::Var) {
            varCount = std::max<size_t>(varCount, p.var + 1);
        }
        for (const Pattern &arg : p.args) {
            countVars(arg);
        }
    };
    for (const Rule &rule : rules) {
        countVars(rule.lhs);
        countVars(rule.rhs);
    }

    // Nodes the graph keeps by name; they are never erased.
    const auto kept = [&](gc_node_ref_t node) {
        if (node == draft_.entryNode() || node == draft_.exitNode() ||
            node == draft_.outputNode() || node == draft_.returnNode()) {
            return true;
        }
        for (auto list : {draft_.normPorts(), draft_.withPorts(), draft_.closureNodes()}) {
            if (std::find(list.begin(), list.end(), node) != list.end()) {
                return true;
            }
        }
        for (uint32_t r = 0; r < draft_.regionCount(); ++r) {
            if (draft_.region(r).result == node) {
                return true;
            }
        }
        return false;
    };
    const auto unused = [&](gc_node_ref_t node) {
        return draft_.normUsersOf(node).empty() && draft_.withUsersOf(node).empty() &&
               draft_.ctrlUsersOf(node).empty();
    };
    // Erases a node nothing reads, then what only it was reading (pure nodes only). `step`, when
    // given, notes each node and its text before the draft forgets both.
    const auto eraseUnused = [&](gc_node_ref_t first, RewriteStep *step) {
        std::vector<gc_node_ref_t> work{first};
        while (!work.empty()) {
            const gc_node_ref_t node = work.back();
            work.pop_back();
            if (!draft_.alive(node) || kept(node) || !unused(node) || shapeOf(node) == 2) {
                continue;
            }
            std::vector<gc_node_ref_t> inputs;
            for (auto part :
                 {draft_.normInputsOf(node),
                  draft_.withInputsOf(node),
                  draft_.ctrlInputsOf(node)}) {
                inputs.insert(inputs.end(), part.begin(), part.end());
            }
            if (node < ix.keyOf.size() && ix.keyOf[node].has_value()) {
                auto found = ix.byKey.find(*ix.keyOf[node]);
                if (found != ix.byKey.end() && found->second == node) {
                    ix.byKey.erase(found);
                }
                ix.keyOf[node].reset();
            }
            if (step != nullptr) {
                step->erased.push_back(node);
                step->erasedText.push_back(nodeText(draft_, node));
            }
            draft_.eraseNode(node);
            ++stats.erased;
            work.insert(work.end(), inputs.begin(), inputs.end());
        }
    };

    for (; stats.rounds < maxRounds;) {
        ++stats.rounds;
        size_t appliedInRound = 0;
        const size_t slots    = draft_.nodeSlotCount();
        for (gc_node_ref_t node = 0; node < slots; ++node) {
            if (!draft_.alive(node) || !takesPart(node)) {
                continue;
            }
            ++stats.visited;
            const camel::core::sem::PermissionMask context = contextOf(node);
            matchPerms_           = draft_.header(node)->permissions;
            for (const Rule &rule : rules) {
                if ((rule.needs & ~context) != 0 || rule.lhs.kind != Pattern::Kind::Op) {
                    continue;
                }
                // The first match of the rule whose condition holds.
                std::vector<Item> work{{&rule.lhs, node}};
                std::vector<gc_node_ref_t> bind(varCount, kInvalidNodeRef);
                std::vector<uint8_t> bound(varCount, 0);
                std::vector<gc_node_ref_t> matchedWaits(
                    draft_.ctrlInputsOf(node).begin(), draft_.ctrlInputsOf(node).end());
                std::optional<std::vector<gc_node_ref_t>> chosen;
                std::optional<std::vector<gc_node_ref_t>> chosenWaits;
                matchFrom(context, work, 0, bind, bound, &matchedWaits, true, [&] {
                    if (!chosen && (!rule.when || rule.when(Match{*this, node, bind}))) {
                        chosen      = bind;
                        chosenWaits = matchedWaits;
                        return true;
                    }
                    return false;
                });
                if (!chosen) {
                    continue;
                }
                const auto *h = draft_.header(node);
                DraftNodeInit base;
                base.dataType       = h->dataType;
                base.permissions    = h->permissions;
                base.region         = draft_.nodeRegion(node);
                base.origin         = draft_.nodeOrigin(node);
                base.attrs          = draft_.nodeAttrs(node);
                const size_t before = draft_.nodeSlotCount();
                gc_node_ref_t made  = kInvalidNodeRef;
                try {
                    made = build(rule.rhs, Match{*this, node, *chosen}, base);
                } catch (const RuleSkipped &) {
                    continue;
                }
                if (made == node) {
                    continue;
                }
                // Everything the match read that ordered it: the root's own control inputs and
                // those of the operator terms the law stops reading. A law over data inputs is
                // sound whatever the node is ordered after, but the engine must not move the
                // computation across an effect, so the writing that replaces it waits for all of
                // them (design principles 3.3, 3.4; H95).
                const std::vector<gc_node_ref_t> waits = chosenWaits.value_or(std::vector<gc_node_ref_t>{});
                // A node that already existed cannot be made to wait without risking a cycle.
                if (!waits.empty() && made < before) {
                    continue;
                }
                // The attribution of this rewrite (rewrite design 8.7). The step is made only
                // when a log was asked for, so a plain rewrite allocates nothing for it; the
                // text of the nodes is read now, because the erased ones lose it below.
                std::optional<RewriteStep> step;
                RewriteStep *recorded = nullptr;
                if (log != nullptr) {
                    step.emplace();
                    step->rule   = rule.name;
                    step->ruleId = rule.id;
                    step->round  = stats.rounds;
                    step->matched.push_back(node);
                    step->matchedText.push_back(nodeText(draft_, node));
                    for (gc_node_ref_t bound : *chosen) {
                        if (bound == kInvalidNodeRef ||
                            std::find(step->matched.begin(), step->matched.end(), bound) !=
                                step->matched.end()) {
                            continue;
                        }
                        step->matched.push_back(bound);
                        step->matchedText.push_back(nodeText(draft_, bound));
                    }
                    step->produced     = made;
                    step->producedText = nodeText(draft_, made);
                    recorded           = &*step;
                }
                draft_.replaceAllValueUses(node, made);
                draft_.replaceAllCtrlUses(node, made);
                for (gc_node_ref_t wait : waits) {
                    if (wait != made) {
                        draft_.appendInput(DraftEdgeKind::Ctrl, made, wait);
                    }
                }
                if (draft_.entryNode() == node)
                    draft_.setEntryNode(made);
                if (draft_.exitNode() == node)
                    draft_.setExitNode(made);
                if (draft_.outputNode() == node)
                    draft_.setOutputNode(made);
                if (draft_.returnNode() == node)
                    draft_.setReturnNode(made, draft_.returnKind());
                for (uint32_t r = 0; r < draft_.regionCount(); ++r) {
                    if (draft_.region(r).result == node) {
                        draft_.setRegionResult(r, made);
                    }
                }
                ++appliedInRound;
                ++stats.applied;
                eraseUnused(node, recorded);
                if (log != nullptr) {
                    log->push_back(std::move(*step));
                }
                break;
            }
        }
        if (appliedInRound == 0) {
            stats.fixpoint = true;
            break;
        }
    }
    greedy_ = false;
    return stats;
}

std::string DraftWritings::textOf(gc_node_ref_t node) { return nodeText(draft_, node); }

namespace {
std::string_view operatorText(camel::core::OperId oper) {
    return camel::core::OperatorTable::instance().uri(oper);
}
} // namespace

RuleMiss DraftWritings::whyNot(const Rule &rule, gc_node_ref_t node) {
    RuleMiss miss;
    const DraftNodeHeader *rootHeader = draft_.header(node);
    if (rootHeader == nullptr || !draft_.alive(node)) {
        miss.kind   = RuleMiss::Kind::Match;
        miss.detail = "the value is no longer in the program";
        return miss;
    }
    if (!takesPart(node)) {
        miss.kind   = RuleMiss::Kind::NotPart;
        miss.detail = std::format(
            "{} is not a value a rewrite may read: control, branch, region and anchor nodes order "
            "the computation, they do not compute a value",
            nodeText(draft_, node));
        return miss;
    }
    const camel::core::sem::PermissionMask context = contextOf(node);
    if ((rule.needs & ~context) != 0) {
        miss.kind   = RuleMiss::Kind::Needs;
        miss.detail = std::format(
            "the rule needs the permission {}, which the value's context does not grant (it grants "
            "{}), so the law is not allowed here",
            camel::core::sem::permissionsText(rule.needs & ~context),
            camel::core::sem::permissionsText(context));
        return miss;
    }
    if (rule.lhs.kind != Pattern::Kind::Op) {
        miss.kind   = RuleMiss::Kind::Match;
        miss.detail = "the rule's left side is not an operator pattern, so it matches no node";
        return miss;
    }
    // The engine matches the operands of a node in the root's permission class (rewrite design
    // 3): a rewrite may not cross a permission boundary, so neither may this answer.
    matchPerms_ = rootHeader->permissions;

    size_t varCount                                = 0;
    std::function<void(const Pattern &)> countVars = [&](const Pattern &p) {
        if (p.kind == Pattern::Kind::Var) {
            varCount = std::max(varCount, static_cast<size_t>(p.var) + 1);
        }
        for (const Pattern &arg : p.args) {
            countVars(arg);
        }
    };
    countVars(rule.lhs);
    // What the match found so far, kept together so a failed operand order can be undone without
    // disturbing the order that succeeded.
    struct Binds {
        std::vector<gc_node_ref_t> node;
        std::vector<uint8_t> bound;
    };
    Binds binds{std::vector<gc_node_ref_t>(varCount, kInvalidNodeRef), std::vector<uint8_t>(varCount, 0)};

    std::function<std::optional<std::string>(const Pattern &, gc_node_ref_t, const std::string &, Binds &)>
        walk;
    walk = [&](const Pattern &p, gc_node_ref_t at, const std::string &where,
               Binds &state) -> std::optional<std::string> {
        switch (p.kind) {
        case Pattern::Kind::Compute:
            return std::format("{} is a computed constant, which only a right side may write", where);
        case Pattern::Kind::Var: {
            if (p.constOnly && !constantOf(at)) {
                return std::format(
                    "{} is {} where the pattern writes a constant", where, nodeText(draft_, at));
            }
            const DraftNodeHeader *header = draft_.header(at);
            if (header == nullptr || !acceptsType(p.type, header->dataType)) {
                return std::format(
                    "{} is {} where the pattern expects a value of type {}", where,
                    nodeText(draft_, at),
                    p.type != nullptr ? p.type->toString() : std::string("any"));
            }
            if (state.bound[p.var]) {
                if (state.node[p.var] != at) {
                    return std::format(
                        "{} is {}, but the pattern's parameter is already {} (one parameter is one "
                        "value)",
                        where, nodeText(draft_, at), nodeText(draft_, state.node[p.var]));
                }
                return std::nullopt;
            }
            state.bound[p.var] = 1;
            state.node[p.var]  = at;
            return std::nullopt;
        }
        case Pattern::Kind::Constant: {
            const auto found = constantOf(at);
            if (!found || found->second != p.type ||
                canonicalConstant(found->first, p.type) != canonicalConstant(p.value, p.type)) {
                return std::format(
                    "{} is {} where the pattern writes a constant", where, nodeText(draft_, at));
            }
            return std::nullopt;
        }
        case Pattern::Kind::Op: {
            const DraftNodeHeader *header = draft_.header(at);
            if (header == nullptr || header->kind != GCNodeKind::Oper) {
                return std::format(
                    "{} is {} where the pattern writes the operator {}", where,
                    nodeText(draft_, at), operatorText(p.oper));
            }
            if (header->permissions != matchPerms_) {
                return std::format(
                    "{} is {}, in a different permission class than the value the rule matched; a "
                    "rewrite may not cross one",
                    where, nodeText(draft_, at));
            }
            if (!acceptsType(p.type, header->dataType)) {
                return std::format(
                    "{} is {} where the pattern expects a value of type {}", where,
                    nodeText(draft_, at),
                    p.type != nullptr ? p.type->toString() : std::string("any"));
            }
            // The operator is the pattern's own: a different one accepts no operand (the engine
            // compares the operator indexes, not their shapes).
            if (reinterpret_cast<const GCOperBody *>(draft_.payloadOf(at).data())->oper != p.oper) {
                return std::format(
                    "{} is {} where the pattern writes the operator {}", where, nodeText(draft_, at),
                    operatorText(p.oper));
            }
            if (!takesPart(at)) {
                return std::format(
                    "{} is {}, which no rewrite may read", where, nodeText(draft_, at));
            }
            const auto inputs = draft_.normInputsOf(at);
            if (inputs.size() != p.args.size() || !draft_.withInputsOf(at).empty()) {
                return std::format(
                    "{} is {}: it has {} value input(s){} where the pattern writes {} operand(s)",
                    where, nodeText(draft_, at), inputs.size(),
                    draft_.withInputsOf(at).empty() ? "" : " and with-inputs", p.args.size());
            }
            const auto attempt =
                [&](bool swapped, Binds &trial) -> std::optional<std::string> {
                for (size_t i = 0; i < p.args.size(); ++i) {
                    const size_t input   = swapped ? (i == 0 ? 1 : 0) : i;
                    const std::string sub = std::format(
                        "{}{}{} of {}", where.empty() ? "" : where + ", ", "operand ", i + 1,
                        operatorText(p.oper));
                    if (auto failure = walk(p.args[i], inputs[input], sub, trial)) {
                        return failure;
                    }
                }
                return std::nullopt;
            };
            Binds natural = binds;
            auto failure  = attempt(false, natural);
            if (!failure) {
                binds = std::move(natural);
                return std::nullopt;
            }
            // The engine reads the written order first and the exchanged order only when that
            // failed and the operator's semantics say the exchange keeps the value (rewrite
            // design 2.2). The answer tries the same two orders, so a rule that does fire
            // commuted is not reported as a mismatch.
            if (inputs.size() == 2 && declaredCommutative(p.oper, header->dataType)) {
                Binds exchanged = binds;
                if (!attempt(true, exchanged)) {
                    binds = std::move(exchanged);
                    return std::nullopt;
                }
            }
            return failure;
        }
        }
        return std::nullopt;
    };

    if (auto failure = walk(rule.lhs, node, "the value", binds)) {
        miss.kind   = RuleMiss::Kind::Match;
        miss.detail = *failure;
        return miss;
    }
    if (rule.when) {
        try {
            const Match match{*this, node, binds.node};
            if (!rule.when(match)) {
                std::string values;
                for (gc_node_ref_t var : binds.node) {
                    if (var == kInvalidNodeRef) {
                        continue;
                    }
                    if (!values.empty()) {
                        values += ", ";
                    }
                    values += nodeText(draft_, var);
                }
                miss.kind   = RuleMiss::Kind::Condition;
                miss.detail = std::format(
                    "the left side matches ({}), but the condition is false",
                    values.empty() ? std::string("no parameter") : values);
                return miss;
            }
        } catch (const std::exception &e) {
            miss.kind   = RuleMiss::Kind::Condition;
            miss.detail = std::format(
                "the left side matches, but the condition could not be evaluated: {}", e.what());
            return miss;
        }
    }
    miss.kind   = RuleMiss::Kind::Blocked;
    miss.detail =
        "the left side matches and the condition holds, but the engine made no step here: it "
        "built the same node, or the rewrite would have moved the computation across a control "
        "edge";
    return miss;
}

SaturateStats DraftWritings::saturate(std::span<const Rule> rules, const Budget &budget) {
    SaturateStats stats;
    GraphDraft::WritingScope scope(draft_);
    EngineRootScope roots(draft_, rules);
    rebuild();
    struct Found {
        size_t rule;
        gc_node_ref_t root;
        std::vector<gc_node_ref_t> vars;
    };
    size_t varCount                                = 0;
    std::function<void(const Pattern &)> countVars = [&](const Pattern &p) {
        if (p.kind == Pattern::Kind::Var) {
            varCount = std::max<size_t>(varCount, p.var + 1);
        }
        for (const Pattern &arg : p.args) {
            countVars(arg);
        }
    };
    for (const Rule &rule : rules) {
        countVars(rule.lhs);
        countVars(rule.rhs);
    }
    for (; stats.rounds < budget.rounds; ++stats.rounds) {
        // All the matches of the round are found before any is applied.
        std::vector<Found> found;
        const auto findBegan = std::chrono::steady_clock::now();
        const size_t slots   = draft_.nodeSlotCount();
        // The nodes a rule may be rooted at, by operator, ascending.
        std::unordered_map<camel::core::OperId, std::vector<gc_node_ref_t>> byOper;
        for (gc_node_ref_t node = 0; node < slots; ++node) {
            if (draft_.alive(node) && draft_.header(node)->kind == GCNodeKind::Oper &&
                takesPart(node)) {
                byOper[reinterpret_cast<const GCOperBody *>(draft_.payloadOf(node).data())->oper]
                    .push_back(node);
            }
        }
        for (size_t r = 0; r < rules.size(); ++r) {
            const Rule &rule = rules[r];
            if (rule.lhs.kind != Pattern::Kind::Op) {
                continue;
            }
            const auto candidates = byOper.find(rule.lhs.oper);
            if (candidates == byOper.end()) {
                continue;
            }
            for (gc_node_ref_t node : candidates->second) {
                const camel::core::sem::PermissionMask context = contextOf(node);
                if ((rule.needs & ~context) != 0) {
                    continue;
                }
                matchPerms_ = draft_.header(node)->permissions;
                std::vector<Item> work{{&rule.lhs, node}};
                std::vector<gc_node_ref_t> bind(varCount, kInvalidNodeRef);
                std::vector<uint8_t> bound(varCount, 0);
                matchFrom(context, work, 0, bind, bound, nullptr, !rule.twoWay, [&] {
                    if (!rule.when || rule.when(Match{*this, node, bind})) {
                        found.push_back(Found{r, node, bind});
                        return true;
                    }
                    return false;
                });
            }
        }
        stats.matches += found.size();
        const auto applyBegan = std::chrono::steady_clock::now();
        stats.findSeconds += std::chrono::duration<double>(applyBegan - findBegan).count();
        bool progress = false;
        for (const Found &match : found) {
            const Rule &rule = rules[match.rule];
            if (stats.newWritings >= budget.newWritings || stats.unions >= budget.unions) {
                return stats;
            }
            const auto *h = draft_.header(match.root);
            DraftNodeInit base;
            base.dataType       = h->dataType;
            base.permissions    = h->permissions;
            base.region         = draft_.nodeRegion(match.root);
            base.origin         = draft_.nodeOrigin(match.root);
            base.attrs          = draft_.nodeAttrs(match.root);
            const size_t before = draft_.nodeSlotCount();
            gc_node_ref_t made  = kInvalidNodeRef;
            try {
                made = build(rule.rhs, Match{*this, match.root, match.vars}, base);
            } catch (const RuleSkipped &) {
                continue;
            }
            stats.newWritings += draft_.nodeSlotCount() - before;
            if (draft_.nodeSlotCount() != before) {
                progress = true;
            }
            if (made != match.root && !store_.same(contextOf(match.root), made, match.root)) {
                if (assertEqualDeferred(match.root, made, rule.needs, rule.id)) {
                    ++stats.unions;
                    progress = true;
                }
            }
        }
        rebuild();
        stats.applySeconds +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() - applyBegan).count();
        if (!progress) {
            stats.saturated = true;
            ++stats.rounds;
            break;
        }
    }
    return stats;
}

} // namespace camel::runtime

