// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Serialize: the on-disk form of a construction tree.
 *
 * The module cache stores the tree a module's declaration was built from, so that the next
 * process can declare that module without reading and parsing its file again. The tree is the
 * input of everything the declaration produces (the graphs of its functions, their signatures,
 * its top-level values, its types), which is why it is what gets stored.
 *
 * Layout, in this order, little-endian throughout:
 *
 *   header      the magic below, the version, and the count of each table that follows
 *   type table  one entry per type the tree mentions, keyed by the canonical identity of
 *               design principles 5.1 (a type's spelling is not its identity: two spellings
 *               of one type and two types of one spelling both exist)
 *   operator table
 *               one entry per operator a node names, keyed by what the operator registry
 *               resolves (`OperId`), never by the name a node prints
 *   node table  every node once, in one pre-order traversal; a node's record carries the
 *               number of its parent, its kind, its name and the number of its children
 *   index tables
 *               one per per-node side table (see tree.h): tags, per-field tags, operator
 *               references, the `using namespace` in force, the namespaces of an operator
 *               overload, the exact value of a numeric literal, the elements of a composite
 *               literal, the lambda parameters without a type, the parameters destructured as
 *               tuples, a declaration's `when`, a match case's guard, the type parameters at a
 *               declaration's head, the parameters' defaults, the names of a call's arguments,
 *               and the value type of a native declaration. Every entry is keyed by a node's
 *               number, because in memory fourteen of those tables are keyed by `const Node*`
 *               and one by `data_ptr_t`, and neither means anything in another process.
 *
 * Two rules that must not be simplified away by a later change:
 *
 *  - An operator reference is keyed by what the registry resolves, not by the spelling a node
 *    prints. Two different operators that share a spelling is exactly the case that goes wrong
 *    when a file is keyed by name, and it goes wrong silently: the tree reads back, the graph
 *    differs.
 *  - A number, a tag or a type identity the file does not hold is a corrupt file. The reader
 *    refuses it and the caller compiles the module from its source. There is deliberately no
 *    "skip that entry and go on" status below, and adding one would turn the cache into a
 *    quiet source of a half-read tree -- the failure mode that is worse than no cache at all.
 *
 * What a reader must be judged by, once write() and read() exist: the graph the module builds
 * from the read-back tree has the same `std::gir` sha256 as the one it builds from the tree it
 * parses, on the same programs; the printed dump (`Tree::print`) is the same, as a cross-check
 * and not as the judge, because none of the tables above appears in that printing; a second
 * round trip of the same file gives the same bytes; and a file whose version was raised by one
 * is refused, after which the module compiles from source with the same graph as before.
 *
 * Not implemented yet. This header fixes the format before the code that follows it: the writer
 * and the reader land as their own commit, with the node table, the type table, the operator
 * table and five of the index tables, and a count of the ones still missing.
 */

#pragma once

#include <array>
#include <cstdint>

namespace camel::compile::gct::serialize {

/// The first eight bytes of every file: `CAMELGCT`.
inline constexpr std::array<char, 8> kMagic = {'C', 'A', 'M', 'E', 'L', 'G', 'C', 'T'};

/// The layout this build writes and the highest it reads. A file with another version is
/// refused, not read as best it can be (`ReadStatus::UnsupportedVersion`).
inline constexpr std::uint32_t kVersion = 1;

/// What a read can answer. `Ok` is the only status a caller may go on with; the others name
/// which part of the file was wrong, so the diagnostic can say more than "the cache is stale".
/// There is no status for "an entry was skipped": an entry the file cannot place is one of the
/// three unknown-entry statuses, and the caller recompiles.
enum class ReadStatus : std::uint8_t {
    Ok,
    BadMagic,             ///< not a file this code wrote
    UnsupportedVersion,   ///< a version this build does not read
    Truncated,            ///< the file ends inside a table, or a count runs past the end
    UnknownTypeIdentity,  ///< a type number no type-table entry answers
    UnknownOperator,      ///< an operator number no operator-table entry answers
    UnknownNodeNumber,    ///< a node number the node table does not hold
    UnknownTag,           ///< a tag byte this build does not know
    TrailingBytes,        ///< the file holds more than the counts say, which is a file that
                          ///< something else appended to
};

} // namespace camel::compile::gct::serialize