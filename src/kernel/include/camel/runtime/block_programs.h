// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The programs the bodies of blocks compiled to.
 *
 * A block is an operator whose record states what it means and nothing about how it runs: the
 * implementation is resolved per context by the executor manager (`camel/core/operator_table.h`).
 * This store is the other half of that arrangement -- where the programs live. The block's URI
 * answers its ordinal, the ordinal answers the program and the graph to run, and the operator's
 * node carries the ordinal in its attributes, which is the channel through which what a pass
 * recorded about a node ("which implementation runs it") reaches the operator that runs it
 * (camel/core/operator.h, NodeAttrs).
 *
 * A program registered here is pinned by the context and lives as long as the context does: the
 * module that compiled it is gone by the time the block's node runs, and so is its name.
 *
 * Written while a program is being built, read while it runs -- the two do not overlap, so the
 * store carries no lock; a scheduler that ran a block while another was being registered would
 * be reading and writing at once, and nothing does.
 */
#pragma once

#include "camel/core/node_attrs.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace camel::runtime {

class Program;
class GCGraph;

/// One block: the URI of its operator, the program of its body and the graph of that program the
/// call runs.
struct BlockProgram {
    std::string uri;
    Program *program = nullptr;
    GCGraph *root    = nullptr;
    /// What the block's node carries in its attributes: it is not a graph id, which is a number
    /// inside one program, and not the URI, which an attribute could carry only as a Symbol.
    uint64_t ordinal = 0;
};

class BlockPrograms {
  public:
    /// Registers `program`, whose root is `root`, under `uri`, and answers its ordinal. A URI
    /// registered before keeps the entry it has: one block, one program.
    uint64_t add(std::string uri, Program *program, GCGraph *root);
    const BlockProgram *byOrdinal(uint64_t ordinal) const;
    const BlockProgram *byUri(std::string_view uri) const;
    /// The entries, in the order they were registered.
    std::span<const BlockProgram> entries() const { return entries_; }
    size_t size() const { return entries_.size(); }

  private:
    std::vector<BlockProgram> entries_;
    std::unordered_map<std::string, uint64_t> ordinals_;
};

/// The attribute key a block's node carries its ordinal in: `block.program`, an Int.
camel::core::attr::AttrKey blockProgramKey();
/// The attribute set with the block's ordinal in it.
camel::core::attr::AttrSetId blockProgramAttrs(uint64_t ordinal);
/// The ordinal of the block `attrs` names, or nothing when the set names none.
std::optional<uint64_t> blockProgramOf(camel::core::attr::AttrSetId attrs);

} // namespace camel::runtime