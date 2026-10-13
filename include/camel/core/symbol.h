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
 * Created: Oct. 13, 2026
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The symbol table: interned strings, referred to by a 32-bit SymId.
 *
 * Part of the graph data model (docs/design/graph-model, sections 3.2, 3.8 and 4.3). Names that
 * graphs carry (operator URIs, struct field names of ACCS nodes, and later port and graph
 * names) are stored once here, and graphs hold the index instead of the characters. Two
 * symbols are the same text exactly when their ids are equal.
 *
 * Ownership and lifetime: the table owns the characters, in blocks it never frees or moves. A
 * SymId and the string_view it resolves to stay valid until the process exits, so a frozen
 * graph, a draft and a pass may all keep either without any bookkeeping.
 *
 * Thread-safety: intern() and find() are safe to call from any thread (a reader/writer lock
 * guards the text -> id map). view() takes no lock: the parallel scheduler and the collector
 * read graphs concurrently, and resolving an id that a graph already holds must not contend.
 * See camel/utils/append_only_table.h for the exact guarantee.
 *
 * The characters live in the table's own blocks rather than in the permanent space: operators
 * are registered while modules load, before the command line has configured the space limits.
 */

#pragma once

#include "camel/utils/append_only_table.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace camel::core {

/// Index of an interned string in the SymbolTable.
using SymId = uint32_t;

/// The empty string. It is always present, so a zero-initialized SymId field means "no name".
inline constexpr SymId kEmptySym = 0;

class SymbolTable {
  public:
    static SymbolTable &instance();

    /// The id of `text`, adding it to the table when it is new.
    SymId intern(std::string_view text);

    /// The id of `text` when it has been interned, nullopt otherwise. Adds nothing.
    std::optional<SymId> find(std::string_view text) const;

    /// The characters of `id`; the view is NUL-terminated and never invalidated. Lock-free.
    std::string_view view(SymId id) const { return entries_[id]; }

    /// Number of distinct symbols, the empty string included.
    uint32_t size() const { return entries_.size(); }

  private:
    SymbolTable();

    /// Copies `text` into the table's blocks (with a terminating NUL).
    std::string_view store(std::string_view text);

    // Characters are bump-allocated from blocks of this size; a longer symbol gets a block of
    // its own. Blocks are owned by the table and released only with it.
    static constexpr size_t kBlockBytes = 64 * 1024;

    mutable std::shared_mutex mutex_;
    // Keys are views into blocks_, so they stay valid as the map rehashes.
    std::unordered_map<std::string_view, SymId> ids_;
    camel::utils::AppendOnlyTable<std::string_view> entries_;
    std::vector<std::unique_ptr<char[]>> blocks_;
    char *blockCursor_     = nullptr;
    size_t blockRemaining_ = 0;
};

/// Shorthands for the process-wide table.
inline SymId internSymbol(std::string_view text) { return SymbolTable::instance().intern(text); }
inline std::string_view symbolText(SymId id) { return SymbolTable::instance().view(id); }

} // namespace camel::core
