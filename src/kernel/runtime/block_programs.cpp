// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/runtime/block_programs.h"

#include <span>

namespace camel::runtime {

namespace attr = camel::core::attr;

namespace {
constexpr std::string_view kKeyName = "block.program";
} // namespace

uint64_t BlockPrograms::add(std::string uri, Program *program, GCGraph *root) {
    const auto known = ordinals_.find(uri);
    if (known != ordinals_.end()) {
        return known->second;
    }
    BlockProgram entry;
    entry.uri     = std::move(uri);
    entry.program = program;
    entry.root    = root;
    entry.ordinal = entries_.size() + 1;
    ordinals_.emplace(entry.uri, entry.ordinal);
    entries_.push_back(std::move(entry));
    return entries_.back().ordinal;
}

const BlockProgram *BlockPrograms::byOrdinal(uint64_t ordinal) const {
    // The ordinals are the positions, one-based; the store is small and an index would have to be
    // kept in step with an erase that does not happen, so the count is the bound.
    if (ordinal == 0 || ordinal > entries_.size()) {
        return nullptr;
    }
    return &entries_[static_cast<size_t>(ordinal - 1)];
}

const BlockProgram *BlockPrograms::byUri(std::string_view uri) const {
    const auto known = ordinals_.find(std::string(uri));
    if (known == ordinals_.end()) {
        return nullptr;
    }
    return byOrdinal(known->second);
}

attr::AttrKey blockProgramKey() {
    static const attr::AttrKey key =
        attr::registerKey(kKeyName, attr::KeySpec{.kind = attr::ValueKind::Int});
    return key;
}

attr::AttrSetId blockProgramAttrs(uint64_t ordinal) {
    return attr::single(blockProgramKey(), attr::integer(static_cast<int64_t>(ordinal)));
}

std::optional<uint64_t> blockProgramOf(attr::AttrSetId attrs) {
    const attr::AttrValueId value = attr::valueOf(attrs, blockProgramKey());
    if (value == attr::kNoValue) {
        return std::nullopt;
    }
    const attr::Value decoded = attr::value(value);
    if (decoded.kind != attr::ValueKind::Int || decoded.integer <= 0) {
        return std::nullopt;
    }
    return static_cast<uint64_t>(decoded.integer);
}

} // namespace camel::runtime