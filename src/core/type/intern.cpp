// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

const std::vector<ExtentForm> *TypeTable::internExtents(std::span<const ExtentForm> extents) {
    if (extents.empty()) {
        // No dimensions is not a shape of one unknown dimension; the caller maps both to the
        // one word keeping them apart.
        return nullptr;
    }
    // The text of a normalized form is canonical (terms sorted by name, zero terms dropped),
    // so two spellings of one shape produce one entry.
    std::string key;
    for (size_t i = 0; i < extents.size(); ++i) {
        if (i != 0) {
            key += ',';
        }
        key += extents[i].toString();
    }
    std::lock_guard lock(mutex_);
    auto [it, inserted] = extents_.try_emplace(std::move(key));
    if (inserted) {
        it->second.assign(extents.begin(), extents.end());
    }
    return &it->second;
}

const std::string *TypeTable::internText(std::string_view text) {
    if (text.empty()) {
        return nullptr;
    }
    std::lock_guard lock(mutex_);
    auto [it, inserted] = texts_.try_emplace(std::string(text));
    return &it->first;
}

} // namespace camel::core::type
