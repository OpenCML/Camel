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
 * The type table (see intern.h).
 */

#include "camel/core/type/intern.h"

#include "camel/utils/assert.h"

#include <algorithm>

namespace camel::core::type {

TypeTable &TypeTable::instance() {
    // Never destroyed: type objects outlive every static, and constructors of types may run
    // from the destructors of other statics.
    static TypeTable *table = new TypeTable();
    return *table;
}

TypeTable::TypeTable() {
    // Index 0 is kNoTypeId.
    byId_.append([](Type *&entry) { entry = nullptr; });
}

size_t TypeTable::KeyHash::operator()(const KeyView &key) const {
    // FNV-1a over the words; the components are addresses, so the low bits carry little.
    uint64_t hash = 1469598103934665603ull ^ static_cast<uint64_t>(key.shape);
    for (uintptr_t word : key.words) {
        hash ^= static_cast<uint64_t>(word);
        hash *= 1099511628211ull;
    }
    return static_cast<size_t>(hash ^ (hash >> 32));
}

bool TypeTable::KeyEqual::operator()(const KeyView &a, const KeyView &b) const {
    return a.shape == b.shape && a.words.size() == b.words.size() &&
           std::equal(a.words.begin(), a.words.end(), b.words.begin());
}

void TypeTable::assignId(Type *type) {
    const TypeId id = byId_.append([&](Type *&entry) { entry = type; });
    type->typeId_   = id;
}

Type *TypeTable::intern(
    Shape shape, std::span<const uintptr_t> words, const std::function<Type *()> &make) {
    std::lock_guard lock(mutex_);
    if (auto it = byKey_.find(KeyView{shape, words}); it != byKey_.end()) {
        ++hits_;
        return it->second;
    }
    Type *type = make();
    ASSERT(type != nullptr, "A type constructor returned null to the type table.");
    assignId(type);
    byKey_.emplace(Key{shape, std::vector<uintptr_t>(words.begin(), words.end())}, type);
    return type;
}

void TypeTable::adopt(Type *type) {
    std::lock_guard lock(mutex_);
    if (type->typeId_ == kNoTypeId) {
        assignId(type);
    }
}

size_t TypeTable::hits() const {
    std::lock_guard lock(mutex_);
    return hits_;
}

} // namespace camel::core::type
