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


/*
 * Values with several writings (docs/design/graph-store 3.1, 3.4): the equivalences asserted
 * between the nodes of one graph, and the classes they make.
 *
 * A node already is a value; several writings are several nodes declared equivalent. What is
 * stored of a graph is the log of the equivalences (WritingUnion), nothing else; the classes
 * are derived from it, by WritingStore, whenever a draft needs them.
 *
 * An equivalence holds under the permissions it names (`perms`, 3.4): a rule that reassociates a
 * sum needs the numeric permission of that, an exact one needs none. The classes are therefore
 * kept per context, a set of permissions: the layer of a context S joins the nodes of every
 * equivalence whose permissions are within S. A node is looked at in the context of its own
 * permissions. A graph has few contexts (the default one, a block that grants more), at most
 * kMaxLayers.
 *
 * Order is part of the result: the root of a class is its smallest node, the members are listed
 * ascending, and nothing here depends on the order of a hash table.
 */

#pragma once

#include "camel/runtime/graph.h"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace camel::runtime {

/// kWritingDerived: congruence closure found the equivalence, it need not be stored.
inline constexpr uint8_t kWritingDerived = 1 << 0;

/// One equivalence between two nodes of a graph, as the log of the graph keeps it.
struct WritingUnion {
    gc_node_ref_t a = kInvalidNodeRef;
    gc_node_ref_t b = kInvalidNodeRef;
    /// The numeric permissions the equivalence needs (0: it holds bit for bit).
    uint8_t perms = 0;
    uint8_t flags = 0;
    /// The rule that asserted it (0: none, congruence).
    uint16_t rule = 0;
};

class WritingStore {
  public:
    static constexpr size_t kMaxLayers = 4;

    WritingStore() = default;
    /// The classes of a log.
    explicit WritingStore(std::span<const WritingUnion> log);

    /// Asserts that `a` and `b` are the same value in every context that has `perms`. Returns
    /// true when that changed the classes of the context `perms`.
    bool
    merge(gc_node_ref_t a, gc_node_ref_t b, uint8_t perms = 0, uint16_t rule = 0, uint8_t flags = 0);

    /// The smallest node of the class of `node` in the context `context`.
    gc_node_ref_t find(uint8_t context, gc_node_ref_t node);
    /// The nodes of the class ascending; empty for a node alone in its class.
    std::span<const gc_node_ref_t> members(uint8_t context, gc_node_ref_t node);
    bool same(uint8_t context, gc_node_ref_t a, gc_node_ref_t b) {
        return find(context, a) == find(context, b);
    }
    /// True when no equivalence was asserted.
    bool empty() const { return log_.empty(); }
    std::span<const WritingUnion> log() const { return log_; }
    /// The contexts that have a layer, ascending.
    std::vector<uint8_t> contexts() const;
    /// The roots of the classes of more than one node in the context, ascending.
    std::vector<gc_node_ref_t> classRoots(uint8_t context);

  private:
    struct Layer {
        uint8_t context = 0;
        std::vector<gc_node_ref_t> parent;
        // root -> members; a node alone in its class has no entry. The smaller list is appended
        // to the larger when classes join, so the lists are sorted when they are asked for.
        struct Members {
            std::vector<gc_node_ref_t> nodes;
            bool sorted = true;
        };
        std::unordered_map<gc_node_ref_t, Members> members;
    };
    Layer &layer(uint8_t context);
    static gc_node_ref_t findIn(Layer &layer, gc_node_ref_t node);
    static bool unionIn(Layer &layer, gc_node_ref_t a, gc_node_ref_t b);

    std::vector<WritingUnion> log_;
    // Ascending by context.
    std::vector<Layer> layers_;
};

} // namespace camel::runtime
