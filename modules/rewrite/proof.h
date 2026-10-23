// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

/**
 * @file proof.h
 * @brief What `explain` and `explain_all` answer (rewrite design 8.7).
 *
 * `Proof` is the value the rewrite module hands out; its record is the one description of what a
 * run rewrote, so anything that shows a proof reads it instead of running the rewrite again. The
 * definition lives here, not in module.cpp, so the debugger can read the record of the proof it
 * gets from the module without linking the module or keeping a second copy of the format.
 *
 * The class is declared (not defined) with its virtuals: a reader only constructs a view over an
 * existing `ProofObject` and calls the inline accessors below, so no symbol of the module is
 * needed. The layout is part of the module's ABI: `module.cpp` and `proof.cpp` build it.
 */

#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type/base.h"
#include "camel/runtime/program.h"
#include "camel/runtime/writings.h"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace camel::rewritemod {

namespace mm     = camel::core::mm;
namespace rtdata = camel::core::rtdata;
using camel::core::type::Type;

/// The record of one attributed application (rewrite design 8.7): the steps, and for each graph
/// the runtime node id every live draft node took in the program the run produced. A step names
/// its nodes by draft id, the identity that chains one step to the next; the map is what lets a
/// `Node` of the produced program find the rewrite it stands in.
struct RewriteRecord {
    camel::runtime::RewriteLog steps;
    std::unordered_map<camel::runtime::GraphId, std::vector<camel::runtime::gc_node_ref_t>>
        nodeRefsByGraph;
    /// The numeric policy the run applied the rules with: `why_not` matches under the same one,
    /// so a permission the run did not grant is not granted to its answer either.
    bool strict = false;
    /// The program the run started from. A decoded draft keeps each node's runtime id as its draft
    /// id (draft.cpp, GraphDraft::decode), so a node handle into this program names a step of the
    /// record by its own id; a node of the program the run produced is found through the map
    /// above instead. Kept as a traced reference by `Proof`.
    const camel::runtime::Program *input = nullptr;
};

/// `Proof`: what `explain` and `explain_all` answer (rewrite design 8.7). It owns a copy of the
/// run's record, so it stays readable after the `Run` it came from is gone. A proof is either the
/// full derivation (`explain_all`: a header, then one step per rewrite naming the rule, the round
/// and the graph, the nodes that matched, the node that was produced and the nodes that were
/// erased) or one targeted chain (`explain`: the shortest chain between two values, printed as
/// `a = ... = b`, with every equally short chain in the order the rewrites were made).
class ProofObject final : public rtdata::Object {
  public:
    /// One step of a chain and the direction it is read in. The rule was made from its left side
    /// to its right; a chain may read it backwards, because a rewrite is an equality.
    struct Link {
        size_t step  = 0;
        bool reverse = false;
    };
    using ChainPath = std::vector<Link>;
    /// A targeted chain. `same` is set when both values are one node (no rewrite is needed);
    /// `none`, when set, says why no chain connects them. Otherwise `paths` holds every shortest
    /// chain, in schedule order.
    struct Chain {
        std::string from;
        std::string to;
        bool same = false;
        std::string none;
        std::vector<ChainPath> paths;
    };

    static ProofObject *create(
        const RewriteRecord &record, const camel::runtime::RewriteStats &stats,
        mm::IAllocator &allocator);
    static ProofObject *createChain(
        const RewriteRecord &record, const camel::runtime::RewriteStats &stats, Chain chain,
        mm::IAllocator &allocator);

    const RewriteRecord &record() const { return *record_; }
    const camel::runtime::RewriteStats &stats() const { return stats_; }
    /// The program the run started from, or null when it is no longer reachable.
    const camel::runtime::Program *input() const { return input_; }
    /// The targeted chain, when this proof is the answer of `explain`; null for `explain_all`.
    const std::optional<Chain> &chain() const { return chain_; }

    /// Turns a fresh proof into a targeted one. Set once, by the factory, before the proof is
    /// handed out: a proof never changes after that.
    void setChain(Chain chain) { chain_ = std::move(chain); }

    bool equals(const rtdata::Object *other, const Type *type, bool deep = false) const override;
    rtdata::Object *
    clone(mm::IAllocator &allocator, const Type *type, bool deep = false) const override;
    void print(std::ostream &os, const Type *type) const override;
    void onMoved() override {}
    void updateRefs(const rtdata::Object::RefRelocator &relocate, const Type *type) override;
    void finalize() noexcept override;

    ProofObject(RewriteRecord *record, const camel::runtime::RewriteStats &stats)
        : record_(record), stats_(stats), input_(record != nullptr ? record->input : nullptr) {}

  private:
    RewriteRecord *record_;
    camel::runtime::RewriteStats stats_;
    /// Traced: the proof keeps the program the run started from alive as long as a chain may name
    /// one of its nodes. Relocated in updateRefs like any other reference into the graph space.
    const camel::runtime::Program *input_ = nullptr;
    std::optional<Chain> chain_;
};

/// The text a link starts from and the text it reaches, for a step read forwards or backwards.
/// The same helpers serve the module's chain search and the proof's print.
inline std::string linkBefore(const camel::runtime::RewriteStep &step, bool reverse) {
    if (reverse) {
        return step.producedText;
    }
    return step.matchedText.empty() ? std::string("%?") : step.matchedText.front();
}

inline std::string linkAfter(const camel::runtime::RewriteStep &step, bool reverse) {
    return reverse ? linkBefore(step, false) : step.producedText;
}

} // namespace camel::rewritemod