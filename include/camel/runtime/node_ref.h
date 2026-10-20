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
 * Author: Zhenjie Wei
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * A node of a graph under construction, named by the draft that holds it and its NodeId. Unlike
 * a pointer to the node's storage it stays valid when the draft moves the node's storage, and
 * it is the key the compile-time tables use.
 */

#pragma once

#include "camel/runtime/graph.h"

#include <compare>
#include <cstddef>
#include <functional>

namespace camel::runtime {

class GraphDraft;

struct DraftNodeRef {
    GraphDraft *draft = nullptr;
    gc_node_ref_t id  = kInvalidNodeRef;

    constexpr DraftNodeRef() = default;
    constexpr DraftNodeRef(std::nullptr_t) {}
    constexpr DraftNodeRef(GraphDraft *owner, gc_node_ref_t ref) : draft(owner), id(ref) {}

    explicit constexpr operator bool() const { return draft != nullptr; }
    constexpr bool operator==(const DraftNodeRef &) const = default;
    constexpr bool operator==(std::nullptr_t) const { return draft == nullptr; }
    std::strong_ordering operator<=>(const DraftNodeRef &other) const {
        if (draft != other.draft) {
            return std::less<const GraphDraft *>{}(draft, other.draft) ? std::strong_ordering::less
                                                                      : std::strong_ordering::greater;
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
