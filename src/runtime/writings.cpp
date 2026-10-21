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
 * Author: Camel Contributors
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */


#include "camel/runtime/writings.h"

#include "camel/core/operator_table.h"
#include "camel/runtime/draft_writes.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace camel::runtime {

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
    if (layer.size[big] < layer.size[small] || (layer.size[big] == layer.size[small] && big > small)) {
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

WritingStore::Layer &WritingStore::layer(uint8_t context) {
    auto it = std::lower_bound(
        layers_.begin(), layers_.end(), context, [](const Layer &layer, uint8_t value) {
            return layer.context < value;
        });
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
    gc_node_ref_t a, gc_node_ref_t b, uint8_t perms, uint16_t rule, uint8_t flags,
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

gc_node_ref_t WritingStore::canon(uint8_t context, gc_node_ref_t node) {
    return findIn(layer(context), node);
}

gc_node_ref_t WritingStore::find(uint8_t context, gc_node_ref_t node) {
    Layer &l        = layer(context);
    const auto root = findIn(l, node);
    return root < l.smallest.size() ? l.smallest[root] : root;
}

std::span<const gc_node_ref_t> WritingStore::members(uint8_t context, gc_node_ref_t node) {
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

std::vector<uint8_t> WritingStore::contexts() const {
    std::vector<uint8_t> result;
    for (const Layer &l : layers_) {
        result.push_back(l.context);
    }
    return result;
}

std::vector<gc_node_ref_t> WritingStore::classRoots(uint8_t context) {
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
    uint8_t kind  = 0;
    uint8_t perms = 0;
    uint32_t region = 0;
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
};

DraftWritings::DraftWritings(GraphDraft &draft)
    : draft_(draft), store_(draft.writings()), index_(std::make_unique<Index>()) {}

DraftWritings::~DraftWritings() = default;

uint8_t DraftWritings::contextOf(gc_node_ref_t node) const { return draft_.nodePermissions(node); }

namespace {

bool readsVariable(const GraphDraft &draft, std::span<const gc_node_ref_t> norm,
                   std::span<const gc_node_ref_t> with) {
    for (auto inputs : {norm, with}) {
        for (gc_node_ref_t in : inputs) {
            const auto *h = draft.header(in);
            if (isWrittenInPlace(draft, in) ||
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

/// Whether a node of this shape takes part in congruence, apart from the nodes it reads.
bool shapeTakesPart(
    const GraphDraft &draft, GCNodeKind kind, gc_slot_idx_t dataIndex, const Type *type,
    std::span<const std::byte> payload, size_t ctrlInputs) {
    if (ctrlInputs != 0) {
        return false;
    }
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

bool DraftWritings::takesPart(gc_node_ref_t node) {
    auto &takes = index_->takes;
    if (node >= takes.size()) {
        takes.resize(draft_.nodeSlotCount(), 0);
    }
    if (takes[node] == 0) {
        const auto *h = draft_.header(node);
        bool yes      = h != nullptr && draft_.alive(node) &&
                   shapeTakesPart(
                       draft_,
                       h->kind,
                       h->dataIndex,
                       h->dataType,
                       draft_.payloadOf(node),
                       draft_.ctrlInputsOf(node).size()) &&
                   !isWrittenInPlace(draft_, node);
        if (yes && h->kind == GCNodeKind::Oper) {
            yes = !readsVariable(draft_, draft_.normInputsOf(node), draft_.withInputsOf(node));
        }
        takes[node] = yes ? 1 : 2;
    }
    return takes[node] == 1;
}

namespace {

/// The key of a node of the given parts; `canonOf` names the class of an input.
template <typename CanonOf>
NodeKey makeKey(
    const GraphDraft &draft, GCNodeKind kind, uint8_t perms, uint32_t region, const Type *type,
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
        key.payload.assign(reinterpret_cast<const char *>(&v), sizeof(v));
        if (const Type *ty = draft.staticSlotTypes()[index]) {
            key.type = ty;
        }
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

void DraftWritings::joined(uint8_t context, std::span<const gc_node_ref_t> moved) {
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
    const uint8_t context = contextOf(node);
    const auto *h         = draft_.header(node);
    NodeKey key           = makeKey(
        draft_,
        h->kind,
        context,
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
        store_.merge(it->second, node, context, 0, kWritingDerived, this);
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

bool DraftWritings::assertEqual(gc_node_ref_t a, gc_node_ref_t b, uint8_t perms, uint16_t rule) {
    const auto *ha = draft_.header(a);
    const auto *hb = draft_.header(b);
    if (ha == nullptr || hb == nullptr || ha->dataType != hb->dataType ||
        draft_.nodeRegion(a) != draft_.nodeRegion(b)) {
        throw std::logic_error(
            "Two writings of a value have the same type and are in the same region.");
    }
    const bool changed = store_.merge(a, b, perms, rule, 0, this);
    rebuild();
    return changed;
}

gc_node_ref_t DraftWritings::lookupOrAdd(
    const DraftNodeInit &init, std::span<const gc_node_ref_t> norm,
    std::span<const gc_node_ref_t> with) {
    auto &ix = *index_;
    std::optional<NodeKey> key;
    if (shapeTakesPart(draft_, init.kind, init.dataIndex, init.dataType, init.payload, 0) &&
        !(init.kind == GCNodeKind::Oper && readsVariable(draft_, norm, with))) {
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
            [&](gc_node_ref_t in) { return store_.canon(init.permissions, in); });
        if (auto found = ix.byKey.find(*key); found != ix.byKey.end()) {
            return found->second;
        }
    }
    GraphDraft::WritingScope scope(draft_);
    const gc_node_ref_t node = draft_.addNode(init);
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

gc_node_ref_t DraftWritings::find(gc_node_ref_t node) {
    return store_.find(contextOf(node), node);
}

std::span<const gc_node_ref_t> DraftWritings::writingsOf(gc_node_ref_t node) {
    return store_.members(contextOf(node), node);
}

} // namespace camel::runtime
