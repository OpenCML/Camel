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
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The operator table (see operator_table.h).
 */

#include "camel/core/operator_table.h"

#include "camel/utils/assert.h"

#include <mutex>

namespace camel::core {

OperatorTable &OperatorTable::instance() {
    static OperatorTable table;
    return table;
}

OperatorTable::OperatorTable() {
    // Index 0 is kNoOper: the empty URI, never declared.
    const OperId id = entries_.append([](Entry &entry) { entry.uri = kEmptySym; });
    ids_.emplace(kEmptySym, id);
    // The operators with compile-time ids come next, in the order of their ids.
    for (OperId known = 1; known < oper::kWellKnownCount; ++known) {
        const OperId interned = internLocked(kWellKnownOperatorUris[known]);
        ASSERT(interned == known, "The well-known operators must be interned first, in order.");
        (void)interned;
    }
}

uint8_t OperatorTable::encode(OperatorTraits traits) {
    return static_cast<uint8_t>(
        kDeclared | (traits.pure ? kPure : 0) | (traits.elementwise ? kElementwise : 0) |
        (traits.stateOnly ? kStateOnly : 0) | (traits.mayFault ? 0 : kNoFault) |
        (traits.callsGraphs ? kCallsGraphs : 0));
}

OperatorTraits OperatorTable::decode(uint8_t bits) {
    return OperatorTraits{
        .pure        = (bits & kPure) != 0,
        .elementwise = (bits & kElementwise) != 0,
        .stateOnly   = (bits & kStateOnly) != 0,
        .mayFault    = (bits & kNoFault) == 0,
        .callsGraphs = (bits & kCallsGraphs) != 0,
    };
}

OperId OperatorTable::internLocked(std::string_view uri) {
    const SymId symbol = internSymbol(uri);
    if (auto it = ids_.find(symbol); it != ids_.end()) {
        return it->second;
    }
    uint8_t family = 0;
    if (uri.starts_with(':')) {
        family |= kFamilyBuiltin;
    }
    if (uri.starts_with(":mark/")) {
        family |= kFamilyMarked;
    }
    if (uri.starts_with(":op/assn")) {
        family |= kFamilyAssignment;
    }
    const OperId id = entries_.append([&](Entry &entry) {
        entry.uri    = symbol;
        entry.family = family;
    });
    ids_.emplace(symbol, id);
    return id;
}

OperId OperatorTable::intern(std::string_view uri) {
    if (const auto known = lookup(uri)) {
        return *known;
    }
    std::unique_lock lock(mutex_);
    return internLocked(uri);
}

std::optional<OperId> OperatorTable::lookup(std::string_view uri) const {
    // A URI that was never interned as a symbol cannot have an entry.
    const auto symbol = SymbolTable::instance().find(uri);
    if (!symbol) {
        return std::nullopt;
    }
    std::shared_lock lock(mutex_);
    if (auto it = ids_.find(*symbol); it != ids_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void OperatorTable::set(std::string_view uri, OperatorTraits traits) {
    const OperId id = intern(uri);
    entries_[id].traits.store(encode(traits), std::memory_order_relaxed);
}

void OperatorTable::declare(
    std::string_view protocol, std::initializer_list<std::pair<std::string_view, bool>> operators) {
    for (const auto &[name, pure] : operators) {
        set(std::string(protocol) + ":" + std::string(name), {.pure = pure});
    }
}

std::optional<OperatorTraits> OperatorTable::find(OperId id) const {
    const uint8_t bits = traitBits(id);
    if ((bits & kDeclared) == 0) {
        return std::nullopt;
    }
    return decode(bits);
}

std::optional<OperatorTraits> OperatorTable::find(std::string_view uri) const {
    const auto id = lookup(uri);
    return id ? find(*id) : std::nullopt;
}

bool OperatorTable::isPure(OperId id) const { return (traitBits(id) & kPure) != 0; }

bool OperatorTable::isPure(std::string_view uri) const {
    const auto id = lookup(uri);
    return id && isPure(*id);
}

bool OperatorTable::hasExternalEffect(OperId id) const {
    return (traitBits(id) & (kPure | kStateOnly)) == 0;
}

bool OperatorTable::hasExternalEffect(std::string_view uri) const {
    const auto id = lookup(uri);
    return !id || hasExternalEffect(*id);
}

void OperatorTable::setResolver(std::string_view uri, type::resolver_ptr_t resolver) {
    std::unique_lock lock(mutex_);
    entries_[internLocked(uri)].resolver = std::move(resolver);
}

type::resolver_ptr_t OperatorTable::resolver(OperId id) const {
    std::shared_lock lock(mutex_);
    return entries_[id].resolver;
}

type::resolver_ptr_t OperatorTable::resolver(std::string_view uri) const {
    const auto id = lookup(uri);
    return id ? resolver(*id) : nullptr;
}

void OperatorTable::setTypeFolder(std::string_view uri, TypeFolder folder) {
    std::unique_lock lock(mutex_);
    entries_[internLocked(uri)].folder = std::make_unique<TypeFolder>(std::move(folder));
}

const TypeFolder *OperatorTable::typeFolder(OperId id) const {
    std::shared_lock lock(mutex_);
    return entries_[id].folder.get();
}

const TypeFolder *OperatorTable::typeFolder(std::string_view uri) const {
    const auto id = lookup(uri);
    return id ? typeFolder(*id) : nullptr;
}

} // namespace camel::core
