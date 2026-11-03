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
 * Byte order. Every multi-byte field is written little-endian and read little-endian, by
 * converting at the boundary rather than by copying the bytes of a native value: this tree is
 * built on Windows and on Linux, a cache directory can be shared between the two, and a file
 * written by one and read by the other would otherwise be read a word at a time in the wrong
 * order -- which does not fail, it produces a number. The padding of a record is written as zero
 * for the same reason: two builds of one tree must produce the same bytes, and padding is where
 * a compiler is free to write something else.
 *
 * Cache key. A stored tree is only valid for the build that wrote it, so the key carries at least
 * the platform and the word size, and the compiler and the optimization level whenever either can
 * change the tree: a build whose pointers are four bytes wide and one whose pointers are eight
 * will both hash the same source and neither can read the other's file. A matching hash does not
 * save this; only the key can.
 *
 * Where the count comes from. A `test::` pass reads it. Every pass is built with the pipeline's
 * `Context` (`GraphIRPass` holds `context_`), so a test pass walks `context->allUserModules()` and
 * asks each module for its construction tree; it is named on the command line and never in the
 * default pipeline, so a run that does not name it does exactly what it did before, and its
 * `apply` still receives the program the previous pass produced, which it hands back unchanged.
 * That is the shape the counting pass has: no file, no cache, one line on stdout.
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

/// The fixed part of the file. `counts` says how many entries each table holds, so a reader can
/// refuse a file whose tables run past its end before it allocates anything for them.
struct Layout {
    std::array<char, 8> magic = kMagic;
    std::uint32_t version    = kVersion;
    std::uint32_t types      = 0;
    std::uint32_t operators  = 0;
    std::uint32_t nodes      = 0;
    std::uint32_t strings    = 0; ///< bytes of the name pool the records point into
};

/// One node of the tree, in pre-order. `parent` is a node number or `kNoParent` for the root;
/// `name` is an offset into the name pool, `nameLength` its length; `children` is the count the
/// node's record says it has, and the children follow it in pre-order.
struct NodeRecord {
    static constexpr std::uint32_t kNoParent = 0xFFFF'FFFFu;
    std::uint32_t parent     = kNoParent;
    std::uint32_t name       = 0;
    std::uint32_t nameLength = 0;
    std::uint16_t kind       = 0; ///< the `NodeKind` the writer knew when it wrote the file
    std::uint16_t children   = 0;
};

/// One type of the type table. `identity` is the canonical identity of design principles 5.1 as
/// a string, never a spelling of the type: two spellings of one type and two types of one
/// spelling both exist, and a file keyed by spelling answers the wrong entry for one of them.
struct TypeRecord {
    std::uint32_t identity       = 0; ///< offset into the name pool
    std::uint32_t identityLength = 0;
    std::uint16_t flags          = 0; ///< the type's own recorded properties, as the writer sees
                                      ///< them (nullable, fallible and the rest of its shape)
};

/// One entry of the operator table. `oper` is the identifier the operator registry resolves, not
/// the name a node prints: two operators can share a spelling, and a file keyed by spelling
/// answers one of them for the other, silently.
struct OperatorRecord {
    std::uint32_t oper = 0;
};

static_assert(sizeof(NodeRecord) == 16, "the node record is written as sixteen bytes");
static_assert(sizeof(TypeRecord) == 12, "the type record is written as twelve bytes (two of them padding)");
static_assert(sizeof(OperatorRecord) == 4, "the operator record is written as four bytes");

} // namespace camel::compile::gct::serialize