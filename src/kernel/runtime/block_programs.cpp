// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/runtime/block_programs.h"

#include <optional>
#include <span>

namespace camel::runtime {

const BlockProgram *BlockPrograms::add(std::string uri, Program *program, GCGraph *root) {
    // The entry's key is the operator's identity in the table, so a node and this store name the
    // block the same way. It is interned rather than looked up: a caller may register a program
    // before the operator's record is written, and the record is the caller's to write (a block's
    // is the effects of its body).
    const camel::core::OperId oper = camel::core::OperatorTable::instance().intern(uri);
    const auto known               = index_.find(oper);
    if (known != index_.end()) {
        return &entries_[known->second];
    }
    BlockProgram entry;
    entry.oper    = oper;
    entry.uri     = std::move(uri);
    entry.program = program;
    entry.root    = root;
    index_.emplace(oper, entries_.size());
    entries_.push_back(std::move(entry));
    return &entries_.back();
}

const BlockProgram *BlockPrograms::byOper(camel::core::OperId oper) const {
    if (oper == camel::core::kNoOper) {
        return nullptr;
    }
    const auto known = index_.find(oper);
    return known == index_.end() ? nullptr : &entries_[known->second];
}

const BlockProgram *BlockPrograms::byUri(std::string_view uri) const {
    const std::optional<camel::core::OperId> oper =
        camel::core::OperatorTable::instance().lookup(uri);
    return oper ? byOper(*oper) : nullptr;
}

} // namespace camel::runtime