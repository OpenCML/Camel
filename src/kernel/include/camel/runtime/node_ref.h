// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * A node of a graph under construction, named by the draft that holds it and its NodeId. Unlike
 * a pointer to the node's storage it stays valid when the draft moves the node's storage, and
 * it is the key the compile-time tables use.
 *
 * The draft is named by its identity, not by the address it happens to occupy. The epoch is the
 * number the draft was given when it was made (the draft's own `epoch()`), and it is what makes
 * the name stable: an address is an identity only until the draft is destroyed and the allocator
 * hands the memory to the next one, at which point a handle left over the dead draft would name
 * its successor and be read as if it were its own graph. A handle whose epoch is not the live
 * draft's names no draft at all, so that stale handle is reported instead of being read as
 * another graph.
 */

#pragma once

#include "camel/runtime/graph.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace camel::runtime {

class GraphDraft;

struct DraftNodeRef {
    GraphDraft *draft = nullptr;
    gc_node_ref_t id  = kInvalidNodeRef;
    /// The epoch of the draft this handle names, 0 for no draft: the pair (draft, epoch) names one
    /// draft for as long as the process runs, because no two drafts are ever given the same epoch.
    uint64_t epoch = 0;

    constexpr DraftNodeRef() = default;
    constexpr DraftNodeRef(std::nullptr_t) {}
    constexpr DraftNodeRef(GraphDraft *owner, gc_node_ref_t ref, uint64_t ownerEpoch)
        : draft(owner), id(ref), epoch(ownerEpoch) {}

    explicit constexpr operator bool() const { return draft != nullptr; }
    constexpr bool operator==(const DraftNodeRef &) const = default;
    constexpr bool operator==(std::nullptr_t) const { return draft == nullptr; }
    std::strong_ordering operator<=>(const DraftNodeRef &other) const {
        if (draft != other.draft) {
            return std::less<const GraphDraft *>{}(draft, other.draft) ? std::strong_ordering::less
                                                                      : std::strong_ordering::greater;
        }
        if (epoch != other.epoch) {
            return epoch <=> other.epoch;
        }
        return id <=> other.id;
    }
};

} // namespace camel::runtime

template <> struct std::hash<camel::runtime::DraftNodeRef> {
    size_t operator()(const camel::runtime::DraftNodeRef &ref) const noexcept {
        return std::hash<const void *>{}(ref.draft) * 31u + ref.id;
    }
};
