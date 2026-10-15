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
 * The symbol table (see symbol.h).
 */

#include "camel/core/symbol.h"

#include <cstring>
#include <mutex>

namespace camel::core {

SymbolTable &SymbolTable::instance() {
    static SymbolTable table;
    return table;
}

SymbolTable::SymbolTable() {
    // Index 0 is the empty string (kEmptySym).
    const std::string_view empty = store({});
    const SymId id               = entries_.append([&](std::string_view &entry) { entry = empty; });
    ids_.emplace(empty, id);
}

std::string_view SymbolTable::store(std::string_view text) {
    const size_t bytes = text.size() + 1;
    char *dest         = nullptr;
    if (bytes > kBlockBytes) {
        // An oversized symbol gets its own block; the current block keeps its free tail.
        blocks_.push_back(std::make_unique<char[]>(bytes));
        dest = blocks_.back().get();
    } else {
        if (bytes > blockRemaining_) {
            blocks_.push_back(std::make_unique<char[]>(kBlockBytes));
            blockCursor_    = blocks_.back().get();
            blockRemaining_ = kBlockBytes;
        }
        dest = blockCursor_;
        blockCursor_ += bytes;
        blockRemaining_ -= bytes;
    }
    if (!text.empty()) {
        std::memcpy(dest, text.data(), text.size());
    }
    dest[text.size()] = '\0';
    return {dest, text.size()};
}

SymId SymbolTable::intern(std::string_view text) {
    {
        std::shared_lock lock(mutex_);
        if (auto it = ids_.find(text); it != ids_.end()) {
            return it->second;
        }
    }
    std::unique_lock lock(mutex_);
    // Another thread may have interned the same text between the two locks.
    if (auto it = ids_.find(text); it != ids_.end()) {
        return it->second;
    }
    const std::string_view stored = store(text);
    const SymId id = entries_.append([&](std::string_view &entry) { entry = stored; });
    ids_.emplace(stored, id);
    return id;
}

std::optional<SymId> SymbolTable::find(std::string_view text) const {
    std::shared_lock lock(mutex_);
    if (auto it = ids_.find(text); it != ids_.end()) {
        return it->second;
    }
    return std::nullopt;
}

} // namespace camel::core
