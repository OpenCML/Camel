// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Node attributes: what a library attaches to the nodes of a graph, as graph content.
 *
 * An attribute is a key and a value. A key is named by a library ("tiles.kernel",
 * "train.recompute"); the name is its identity outside the process, so a cache, a hash, a dump
 * records names, never the ids below. The library registers the key with the kind of its
 * values and its two rules (KeySpec):
 *
 *   combine   granting a value to code that already has one (a decorated block inside a
 *             decorated function): union of flags, the inner value, a chain, ...
 *   merge     one node standing for two (common subexpressions, fusion): the value the
 *             merged node keeps, or a refusal, and the nodes are then not merged.
 *
 * The core knows no key: it stores the values, applies the rules a key declares, keeps the
 * attributes through every rewrite that copies nodes, and shows them. A key nobody registered
 * (a library that is not loaded) combines to the inner value and merges only equal values.
 *
 * A value is one small immutable value, interned (AttrValueId, 0 for none):
 *
 *   Unit     present, nothing more (a mark)
 *   Flags    a set of bit indices, of any width
 *   Int      a 64-bit integer (an enum member, a small id)
 *   Symbol   a string
 *   Graph    a reference to a graph of the program: its GraphId (camel/runtime/graph_id.h),
 *            printed as its decimal digits.
 *   Tuple    a sequence of values (a group: a choice and a group id; a chain: the inlined
 *            function and the call site, nested)
 *
 * The attributes of a node are one interned set (AttrSetId, 0 for none): a node costs one
 * 32-bit id, and a graph where no node has any stores nothing (GCGraph::nodeAttrs).
 *
 * The program grants attributes with a decorator whose operator is declared to grant them
 * (OperatorTable::declareGrant): to the nodes written in the function or block it decorates,
 * the arms, loop bodies and lambdas written there included, and not to the functions they
 * call (lexical, as design principles 7.5 says of numeric permissions).
 *
 * A tag (camel/core/tags.h, design principles 6.2) is the other way: `#tag` before a function,
 * type, field or block writes facts as attributes when the declaration says `on nodes`. A C++
 * module declares both at once with tag::declareNodeTag, which registers the key and the grant
 * of the decorator that is the tag's `@` form.
 */

#pragma once

#include "camel/runtime/graph_id.h"

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace camel::core::attr {

/// An interned attribute key; 0 is no key.
using AttrKey = uint16_t;
/// An interned attribute value; 0 is no value (the attribute is absent).
using AttrValueId = uint32_t;
/// An interned set of attributes; 0 is the empty set.
using AttrSetId = uint32_t;

inline constexpr AttrKey kNoKey       = 0;
inline constexpr AttrValueId kNoValue = 0;
inline constexpr AttrSetId kNoAttrs   = 0;

// ---- Values ----------------------------------------------------------------------------

enum class ValueKind : uint8_t { Unit, Flags, Int, Float, Symbol, Graph, Tuple };

/// A value, decoded.
struct Value {
    ValueKind kind = ValueKind::Unit;
    std::vector<uint64_t> words;    // Flags: bit i of the set is bit i % 64 of word i / 64
    int64_t integer = 0;            // Int
    double real     = 0.0;          // Float, by its bits (NaN payload included: two NaNs differ)
    std::string text;               // Symbol; Graph: the graph's stable id
    std::vector<AttrValueId> items; // Tuple
};

AttrValueId unit();
/// The flags with these bit indices set; no value when there is none.
AttrValueId flags(std::span<const uint32_t> bits);
/// The flags of these words (trailing zero words do not count); no value when all are zero.
AttrValueId flagWords(std::vector<uint64_t> words);
AttrValueId integer(int64_t value);
/// A floating-point value, interned by its bits (two values are the same fact only when their
/// bits agree: 0.0 and -0.0, or two NaNs, are different facts).
AttrValueId real(double value);
AttrValueId symbol(std::string_view text);
/// A reference to the graph `graph` (see Graph above).
AttrValueId graphRef(camel::runtime::GraphId graph);
AttrValueId tuple(std::span<const AttrValueId> items);

/// The value of `id`; a Unit value for kNoValue.
Value value(AttrValueId id);
/// `{1, 70}` (flags), `-3`, `"text"`, `@stable-id`, `(a, b)`; empty for kNoValue.
std::string valueText(AttrValueId id);

// ---- Keys and their rules ----------------------------------------------------------------

/// Granting `inner` to code that has `outer` (either may be kNoValue): the value it then has.
using CombineRule = std::function<AttrValueId(AttrValueId outer, AttrValueId inner)>;
/// The value a node standing for two nodes keeps (either may be kNoValue; kNoValue keeps
/// none), or nullopt: the two nodes must not be merged.
using MergeRule = std::function<std::optional<AttrValueId>(AttrValueId a, AttrValueId b)>;

struct KeySpec {
    ValueKind kind = ValueKind::Unit;
    CombineRule combine; // null: the inner value
    MergeRule merge;     // null: equal values only
};

/// Ready-made rules.
namespace rules {
/// Flags: the union of the sets.
CombineRule unionFlags();
/// Flags: the intersection (what both nodes are allowed); never refuses.
MergeRule intersectFlags();
/// The inner value, where there is one.
CombineRule inner();
/// Equal values (both absent included) merge; any other pair refuses.
MergeRule equalOnly();
/// Tuples: the outer chain followed by the inner one (a value that is not a tuple counts
/// as a chain of one).
CombineRule chain();
} // namespace rules

/// Registers the key `name` with its kind and rules (module load time), or replaces them; the
/// key's id. std::invalid_argument when `name` is not a key name: a non-empty run of letters,
/// digits, '_', '.', ':' and '-'.
AttrKey registerKey(std::string_view name, KeySpec spec);
/// The key of `name`, interned on first use (without a spec, until it is registered).
AttrKey key(std::string_view name);
/// The key of `name` when it was interned before.
std::optional<AttrKey> findKey(std::string_view name);
/// The name of `key`; empty for kNoKey or an unknown key.
std::string_view keyName(AttrKey key);
/// The spec of `key`; nullopt when nobody registered it.
std::optional<KeySpec> specOf(AttrKey key);

// ---- Sets ----------------------------------------------------------------------------------

struct Attr {
    AttrKey key                         = kNoKey;
    AttrValueId value                   = kNoValue;
    bool operator==(const Attr &) const = default;
};

/// The set of `attrs`; two values of one key are combined by the key's rule (the later is
/// the inner one), absent values dropped. std::invalid_argument when a value is not of the
/// kind its key was registered with.
AttrSetId intern(std::span<const Attr> attrs);
/// The set with one attribute.
AttrSetId single(AttrKey key, AttrValueId value);
/// The attributes of `set`, ordered by key name (the same order in every process).
std::vector<Attr> entries(AttrSetId set);
/// The value of `key` in `set`, kNoValue when it has none.
AttrValueId valueOf(AttrSetId set, AttrKey key);
/// `inner` granted to code that has `outer`: each key's combine rule.
AttrSetId unite(AttrSetId outer, AttrSetId inner);
/// The attributes of a node standing for nodes with `a` and `b`, by each key's merge rule;
/// nullopt when a key refuses (the nodes must not be merged).
std::optional<AttrSetId> merge(AttrSetId a, AttrSetId b);

/// "name" for a Unit value, "name=<valueText>" otherwise, separated by ", ", ordered by name;
/// empty for the empty set. Process-independent: caches, hashes and dumps use it.
std::string text(AttrSetId set);

// ---- Fates in the consumers ---------------------------------------------------------------

/**
 * What one consumer does with an attribute a node carries. A node attribute is a choice, not a
 * comment: a consumer that neither acts on it nor records it has dropped it, and a program then
 * means something else there (the same family as a gradient dropped on the floor). No attribute
 * may be in that state, so every (key, value) the tree can write has one row below naming, for
 * each consumer, which of these it is:
 *
 *   Carried     uses the choice, or records it where its format can hold it
 *   Refused     stops, with a message naming the attribute, the node and the reason
 *   NotOnANode  the key is a declaration fact or a numeric permission, never a node attribute
 *
 * The row is the single source the consumers read: the ONNX export refuses a key the table does
 * not hold (or holds with `Refused` for ONNX), Tiles does the same, and `inspect --attrs`
 * checks the table against every attribute key the loaded modules interned. A new key, or a new
 * value of a key with a closed set (`tensor.kernel=...`), therefore fails until classified in
 * all four consumers; that is the class guard.
 */
enum class Fate : uint8_t { Carried, Refused, NotOnANode };

/// One attribute (a key, or a key with one of its values) and what each consumer does with it.
struct FateRow {
    std::string_view key;
    /// The value as `fateValueText` writes it; "*" matches any value; "" is a Unit value.
    std::string_view value;
    /// The pass, decorator or tag that writes it.
    std::string_view writtenBy;
    Fate interpreter;
    Fate dump;
    Fate onnx;
    Fate tiles;
    /// Where a carrying consumer puts it, or why a refusing one stops. One sentence.
    std::string_view note;
};

/// Every attribute the tree can write, ordered by key and value.
std::span<const FateRow> fates();
/// The row of `key` with `value` (exact, else the key's "*" row); nullptr when unclassified.
/// `value` is `fateValueText`'s form: the bare text of a Symbol, "" for a Unit.
const FateRow *fateOf(std::string_view key, std::string_view value);
/// The value as the table writes it: the bare text of a Symbol, "" for Unit, `valueText`
/// otherwise.
std::string fateValueText(AttrValueId id);
/// "carried", "refused", "not-on-a-node"; "unknown" for a value outside the enum.
std::string_view fateName(Fate fate);
/// The names of every attribute key interned so far, in registration order. `inspect --attrs`
/// holds the table against them: a key with no row fails the check.
std::vector<std::string> registeredKeys();

/// Prints the table, one `ATTRIBUTE` line per row, then one `UNCLASSIFIED ATTRIBUTE` line for
/// every interned key with no row and every (key, value) that two rows classify differently;
/// false when there was anything to report that way. `inspect --attrs` prints this and exits 1
/// when it returns false: a new attribute has to be classified in all four consumers.
bool dumpFates(std::ostream &os);

} // namespace camel::core::attr
