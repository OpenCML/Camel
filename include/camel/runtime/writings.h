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

#include "camel/runtime/draft.h"
#include "camel/runtime/graph.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace camel::runtime {

/// Told which nodes change class when two classes join (the nodes of the smaller class).
class WritingObserver {
  public:
    virtual ~WritingObserver()                                                    = default;
    virtual void joined(uint8_t context, std::span<const gc_node_ref_t> moved) = 0;
};

class WritingStore {
  public:
    static constexpr size_t kMaxLayers = 4;

    WritingStore() = default;
    /// The classes of a log.
    explicit WritingStore(std::span<const WritingUnion> log);

    /// Asserts that `a` and `b` are the same value in every context that has `perms`. Returns
    /// true when that changed the classes of the context `perms`.
    bool merge(
        gc_node_ref_t a, gc_node_ref_t b, uint8_t perms = 0, uint16_t rule = 0, uint8_t flags = 0,
        WritingObserver *observer = nullptr);

    /// The smallest node of the class of `node` in the context `context`.
    gc_node_ref_t find(uint8_t context, gc_node_ref_t node);
    /// A node of the class that stays the same until the class joins another: for keys that
    /// name a class (find gives the smallest node, which changes less often but costs a lookup).
    gc_node_ref_t canon(uint8_t context, gc_node_ref_t node);
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
        // Union by size; `smallest` is read at the root of a class.
        std::vector<gc_node_ref_t> parent;
        std::vector<gc_node_ref_t> size;
        std::vector<gc_node_ref_t> smallest;
        // root -> members; a node alone in its class has no entry. The smaller list is appended
        // to the larger when classes join, so a list is sorted when it is asked for.
        struct Members {
            std::vector<gc_node_ref_t> nodes;
            bool sorted = true;
        };
        std::unordered_map<gc_node_ref_t, Members> members;
    };
    Layer &layer(uint8_t context);
    static gc_node_ref_t findIn(Layer &layer, gc_node_ref_t node);
    static void ensure(Layer &layer, gc_node_ref_t node);
    static bool unionIn(Layer &layer, gc_node_ref_t a, gc_node_ref_t b, WritingObserver *observer);

    std::vector<WritingUnion> log_;
    // Ascending by context.
    std::vector<Layer> layers_;
};


/// The writings of the values of a draft: the classes (WritingStore) and the congruence closure
/// over them. Nodes that compute the same thing from the same classes are one value, so
/// merging two values merges whatever is built from them; nothing is rewritten, no input of a
/// node ever changes, and the graph stays acyclic node by node.
///
/// Only a node that is a pure function of its inputs takes part: a constant, a conversion, an
/// access, a pure operator, with no control input, not written in place and not reading a
/// variable. Every other node is alone in its class. Two nodes are congruent only in the same
/// region and with the same numeric permissions (design principles 7.5, op-semantics 6.4).
class DraftWritings : private WritingObserver {
  public:
    explicit DraftWritings(GraphDraft &draft);
    ~DraftWritings();
    DraftWritings(const DraftWritings &)            = delete;
    DraftWritings &operator=(const DraftWritings &) = delete;

    /// Writes the equivalences back to the draft (GraphDraft::setWritings), which keeps them
    /// until it is encoded.
    void save() { draft_.setWritings(std::vector<WritingUnion>(store_.log().begin(), store_.log().end())); }
    GraphDraft &draft() { return draft_; }
    WritingStore &store() { return store_; }

    /// Indexes every node of the draft and merges the congruent ones.
    void build();
    /// Asserts that `a` and `b` are the same value under `perms`, then closes over congruence.
    /// Returns true when that changed the classes.
    bool assertEqual(gc_node_ref_t a, gc_node_ref_t b, uint8_t perms = 0, uint16_t rule = 0);
    /// The node that computes this, in the class it belongs to: an existing one, or a new one
    /// with the inputs `norm` and `with` (any members of their classes). A node that does not
    /// take part in congruence is always new.
    gc_node_ref_t
    lookupOrAdd(const DraftNodeInit &init, std::span<const gc_node_ref_t> norm,
                std::span<const gc_node_ref_t> with = {});
    /// Closes the classes over congruence: nodes whose inputs changed class are looked up again.
    void rebuild();

    /// The smallest node of the value `node` is a writing of, in the context of its own
    /// permissions.
    gc_node_ref_t find(gc_node_ref_t node);
    /// The writings of the value `node` is one of, ascending (empty: the only one).
    std::span<const gc_node_ref_t> writingsOf(gc_node_ref_t node);
    /// How many merges congruence closure made.
    size_t congruentMerges() const { return congruentMerges_; }

  private:
    struct Index;
    bool takesPart(gc_node_ref_t node);
    uint8_t contextOf(gc_node_ref_t node) const;
    void enqueue(gc_node_ref_t node);
    void joined(uint8_t context, std::span<const gc_node_ref_t> moved) override;
    void index(gc_node_ref_t node);

    GraphDraft &draft_;
    WritingStore store_;
    std::unique_ptr<Index> index_;
    std::vector<gc_node_ref_t> pending_;
    std::vector<bool> queued_;
    size_t congruentMerges_ = 0;
};

} // namespace camel::runtime
