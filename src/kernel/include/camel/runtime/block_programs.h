// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The programs the bodies of blocks compiled to.
 *
 * A block is an operator whose record states what it means and nothing about how it runs: the
 * implementation is resolved per context by the executor manager (`camel/core/operator_table.h`).
 * This store is the other half of that arrangement -- where the programs live.
 *
 * One identity, not two: a block's node names its operator by the operator table's id, the executor
 * hands that id to the kernel through the call's views (`ArgsView::oper`,
 * `setCallOperator`), and the id answers the program. Nothing about which program to run is stored
 * in the node's attributes, and there is no second numbering of the store: the URI a block is
 * registered under is the spelling of that same operator, so `byUri` is `byOper` reached through
 * the table, and the two can never disagree.
 *
 * A program registered here is pinned by the context and lives as long as the context does: the
 * module that compiled it is gone by the time the block's node runs, and so is its name.
 *
 * Written while a program is being built, read while it runs -- the two do not overlap, so the
 * store carries no lock; a scheduler that ran a block while another was being registered would
 * be reading and writing at once, and nothing does.
 */
#pragma once

#include "camel/core/operator_table.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace camel::runtime {

class Program;
class GCGraph;

/// One block: the URI its operator is named by, the program of its body and the graph of that
/// program the call runs.
struct BlockProgram {
    /// The operator the block is: the table's id for `uri`, which is what a call of the block hands
    /// its kernel and what this entry is keyed by (`camel/core/operator_table.h`).
    camel::core::OperId oper = camel::core::kNoOper;
    std::string uri;
    Program *program = nullptr;
    GCGraph *root    = nullptr;
};

class BlockPrograms {
  public:
    /// Registers `program`, whose root is `root`, under `uri`, and answers the entry -- nullptr
    /// when `uri` names no operator at all. A URI registered before keeps the entry it has: one
    /// block, one program.
    const BlockProgram *add(std::string uri, Program *program, GCGraph *root);
    /// The block whose operator is `oper`, or nullptr.
    const BlockProgram *byOper(camel::core::OperId oper) const;
    /// The block whose operator `uri` names, or nullptr: the same entry `byOper` answers, reached
    /// by the spelling a caller that holds only a URI has.
    const BlockProgram *byUri(std::string_view uri) const;
    /// The entries, in the order they were registered.
    std::span<const BlockProgram> entries() const { return entries_; }
    size_t size() const { return entries_.size(); }

  private:
    std::vector<BlockProgram> entries_;
    std::unordered_map<camel::core::OperId, size_t> index_;
};

} // namespace camel::runtime