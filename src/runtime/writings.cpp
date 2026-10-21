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

#include <algorithm>
#include <stdexcept>

namespace camel::runtime {

WritingStore::WritingStore(std::span<const WritingUnion> log) {
    for (const WritingUnion &entry : log) {
        merge(entry.a, entry.b, entry.perms, entry.rule, entry.flags);
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

bool WritingStore::unionIn(Layer &layer, gc_node_ref_t a, gc_node_ref_t b) {
    a = findIn(layer, a);
    b = findIn(layer, b);
    if (a == b) {
        return false;
    }
    const gc_node_ref_t high = std::max(a, b);
    const gc_node_ref_t low  = std::min(a, b);
    if (high >= layer.parent.size()) {
        const size_t old = layer.parent.size();
        layer.parent.resize(static_cast<size_t>(high) + 1);
        for (size_t i = old; i < layer.parent.size(); ++i) {
            layer.parent[i] = static_cast<gc_node_ref_t>(i);
        }
    }
    layer.parent[high] = low;

    auto take = [&](gc_node_ref_t root) {
        Layer::Members taken;
        auto it = layer.members.find(root);
        if (it == layer.members.end()) {
            taken.nodes.push_back(root);
        } else {
            taken = std::move(it->second);
            layer.members.erase(it);
        }
        return taken;
    };
    Layer::Members joined = take(low);
    Layer::Members other  = take(high);
    if (other.nodes.size() > joined.nodes.size()) {
        std::swap(joined, other);
    }
    joined.nodes.insert(joined.nodes.end(), other.nodes.begin(), other.nodes.end());
    joined.sorted = false;
    layer.members.emplace(low, std::move(joined));
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
            unionIn(*it, entry.a, entry.b);
        }
    }
    return *it;
}

bool WritingStore::merge(
    gc_node_ref_t a, gc_node_ref_t b, uint8_t perms, uint16_t rule, uint8_t flags) {
    // The context of the equivalence itself decides whether it is new.
    Layer &own     = layer(perms);
    const bool isNew = findIn(own, a) != findIn(own, b);
    log_.push_back({a, b, perms, flags, rule});
    for (Layer &other : layers_) {
        if ((perms & ~other.context) == 0) {
            unionIn(other, a, b);
        }
    }
    return isNew;
}

gc_node_ref_t WritingStore::find(uint8_t context, gc_node_ref_t node) {
    return findIn(layer(context), node);
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
        roots.push_back(entry.first);
    }
    std::sort(roots.begin(), roots.end());
    return roots;
}

} // namespace camel::runtime
