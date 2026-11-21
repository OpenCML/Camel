// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "ssa_env.h"

#include "camel/utils/assert.h"

namespace camel::compile::gir {

void SsaEnv::ArmWrites::record(decl_key_t decl, node_handle_t value) {
    // A second write of the same declaration in one arm replaces the first: what the arm ends with
    // is what the JOIN carries, and the order is the order it first wrote it in.
    if (const auto [at, fresh] = values_.emplace(decl, value); !fresh) {
        at->second = value;
        return;
    }
    order_.push_back(decl);
}

node_handle_t SsaEnv::ArmWrites::valueOf(decl_key_t decl) const {
    const auto found = values_.find(decl);
    return found == values_.end() ? nullptr : found->second;
}

SsaEnv::decl_key_t SsaEnv::declare(
    const node_scope_ptr_t &region, const std::string &name, const GCT::Node *decl, Type *type) {
    Region &table = regions_[region];
    decl_key_t key{};
    if (const auto bound = table.bindings.find(name); bound != table.bindings.end()) {
        // The region already declares the name. A region declares one name once, so this is the
        // same declaration, taking the value the region has since bound the name to.
        key = bound->second;
    } else {
        key = nextKey_++;
        table.bindings.emplace(name, key);
    }
    Entry &entry = table.entries[key];
    entry.key    = key;
    entry.name   = name;
    entry.type   = type;
    entry.decl   = decl;
    entry.port   = nullptr;
    entry.region = region;
    decls_[key]  = &entry;
    return key;
}

void SsaEnv::declarePort(
    const node_scope_ptr_t &region, const std::string &name, node_handle_t port, Type *type) {
    // A port is the region's own binding of the name, so the declaration of the enclosing region is
    // not the one this region means: the name is this region's, and the head's port is the value the
    // region binds it to. The write the head hands on is recorded against the port, which is why the
    // port is kept on the entry and not only as its value: the value moves with every write of the
    // body.
    const decl_key_t key               = declare(region, name, nullptr, type);
    regions_[region].entries[key].port = port;
}

const SsaEnv::Entry *SsaEnv::entryOf(decl_key_t key) const {
    const auto found = decls_.find(key);
    return found == decls_.end() ? nullptr : found->second;
}

SsaEnv::Entry *SsaEnv::entryOf(decl_key_t key) {
    const auto found = decls_.find(key);
    return found == decls_.end() ? nullptr : found->second;
}

const SsaEnv::Entry *SsaEnv::find(const node_scope_ptr_t &from, const std::string &name) const {
    // The innermost region that declares the name decides: a region that declares the name and
    // holds no SSA value for it (a `let`, a cell, a parameter) answers "no", and the regions
    // outside it are not asked -- the name here is that declaration, not theirs.
    for (node_scope_ptr_t scope = from; scope != nullptr; scope = scope->outer()) {
        if (!scope->has(name, false)) {
            continue;
        }
        const auto table = regions_.find(scope);
        if (table == regions_.end()) {
            return nullptr;
        }
        const auto bound = table->second.bindings.find(name);
        return bound == table->second.bindings.end() ? nullptr : entryOf(bound->second);
    }
    return nullptr;
}

SsaEnv::Entry *SsaEnv::mutableFind(const node_scope_ptr_t &from, const std::string &name) {
    return const_cast<Entry *>(static_cast<const SsaEnv *>(this)->find(from, name));
}

node_handle_t SsaEnv::valueOf(const Entry *entry) const {
    if (entry == nullptr || entry->region == nullptr) {
        return nullptr;
    }
    // The declaration's own region's binding of the name, and nothing outside it: a region that
    // shadows the name holds another declaration, which is not this one's value.
    return entry->region->get(entry->name, false).value_or(nullptr);
}

node_handle_t SsaEnv::portOf(const node_scope_ptr_t &from, const std::string &name) const {
    const Entry *entry = find(from, name);
    return entry == nullptr ? nullptr : entry->port;
}

std::vector<node_handle_t> SsaEnv::portsInScope(const node_scope_ptr_t &from) const {
    std::vector<node_handle_t> ports;
    for (node_scope_ptr_t scope = from; scope != nullptr; scope = scope->outer()) {
        const auto table = regions_.find(scope);
        if (table == regions_.end()) {
            continue;
        }
        for (const auto &entry : table->second.entries) {
            if (entry.second.port != nullptr) {
                ports.push_back(entry.second.port);
            }
        }
    }
    return ports;
}

void SsaEnv::rebind(
    const node_scope_ptr_t &from, const std::string &name, node_handle_t value, ArmWrites *arm) {
    Entry *entry = mutableFind(from, name);
    GUARD(entry != nullptr, "An SSA `var` is written where its declaration is in scope.");
    // The value the declaration had where this branch was entered, taken as the first arm writes
    // it: the write below replaces the binding, so afterwards the join could not tell it from the
    // last arm's.
    if (branchDepth_ > 0) {
        before_.emplace(entry->key, valueOf(entry));
    }
    // The write itself, in the one place the value lives: the region's own binding of the name,
    // which is the binding every read of it takes -- the read of a use site, a capture, a recorder,
    // the scan of a loop's carried state. Nothing else is kept in step with it.
    for (node_scope_ptr_t scope = from; scope != nullptr; scope = scope->outer()) {
        if (!scope->has(name, false)) {
            continue;
        }
        scope->map()[name] = value;
        // A write inside an arm is what that branch's JOIN carries: the arm keeps that it ended the
        // declaration at this value, and the join is where it goes on from.
        if (arm != nullptr) {
            arm->record(entry->key, value);
        }
        return;
    }
    GUARD(false, "An SSA `var` is written where its declaration is in scope.");
}

void SsaEnv::beginBranch() {
    if (branchDepth_ == 0) {
        // The outermost branch starts the gathering: what the value was before it is what its joins
        // hand an arm that wrote nothing, and a branch written inside one of its arms is inside the
        // same gathering (the records its own arms make must not become the outer branch's).
        before_.clear();
    }
    ++branchDepth_;
}

void SsaEnv::endBranch() {
    GUARD(branchDepth_ > 0, "A branch ends where one was begun.");
    --branchDepth_;
}

const SsaEnv::Entry *SsaEnv::liveEntryOf(const node_scope_ptr_t &from, decl_key_t key) const {
    const Entry *entry = entryOf(key);
    if (entry == nullptr) {
        return nullptr;
    }
    // The declaration is still the one these names denote. An arm that declared a name of its own
    // made a declaration of its own, which died with it: the name here is another declaration, and
    // this one has no value to hand the join.
    return find(from, entry->name) == entry ? entry : nullptr;
}

std::optional<SsaEnv::BranchCarry> SsaEnv::branchCarry(
    const std::vector<const ArmWrites *> &arms, const node_scope_ptr_t &from,
    const type_of_t &typeOf) const {
    // One declaration, and one value slot: a branch whose arms write two declarations carries
    // neither (the scan lets a branch write one such name and no more).
    decl_key_t carried = 0;
    bool several       = false;
    for (const ArmWrites *arm : arms) {
        if (arm == nullptr) {
            continue;
        }
        for (const decl_key_t key : arm->order()) {
            if (carried == 0) {
                carried = key;
            } else if (carried != key) {
                several = true;
            }
        }
    }
    if (carried == 0 || several) {
        return std::nullopt;
    }
    // A name declared inside an arm dies with the arm: only a declaration the branch found in scope
    // has a value before the branch to hand to the arm that writes nothing.
    const auto before = before_.find(carried);
    if (before == before_.end()) {
        return std::nullopt;
    }
    const Entry *entry = liveEntryOf(from, carried);
    if (entry == nullptr) {
        return std::nullopt;
    }
    Type *carriedType = typeOf(before->second);
    if (carriedType == nullptr) {
        return std::nullopt;
    }
    BranchCarry carry;
    carry.type = carriedType;
    for (const ArmWrites *arm : arms) {
        const node_handle_t value = arm != nullptr && arm->valueOf(carried) != nullptr
                                        ? arm->valueOf(carried)
                                        : before->second;
        // Every arm has to hand the join a value of the one type, or the join would carry two
        // types: such a shape keeps its cell.
        if (value == nullptr || typeOf(value) != carriedType) {
            return std::nullopt;
        }
        carry.values.push_back(value);
    }
    carry.name = entry->name;
    return carry;
}

void SsaEnv::noteWrite(node_handle_t value, camel::source::origin_id_t origin) {
    writes_[value] = origin;
}

} // namespace camel::compile::gir