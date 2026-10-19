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
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Node attributes: what a library attaches to the nodes of a graph, as graph content
 * (design principles 2.17).
 *
 * An attribute is a key and a value. A key is named by a library ("tiles.kernel",
 * "train.recompute"); the name is its identity outside the process, so a cache records names,
 * never the ids below. A value is a set of up to 64 flags whose meaning the key's library
 * defines; a key with one meaning uses the value 1. The core knows no key: it stores them,
 * keeps them through every rewrite that copies nodes, and shows them.
 *
 * The attributes of a node are one interned set (AttrSetId, 0 for none): a node costs one
 * 32-bit id, and a graph where no node has any stores nothing (GCGraph::nodeAttrs). Sets are
 * combined key by key: granting a set to code that already has one is the union of the values
 * of each key (unite); a node that stands for two nodes keeps what both have (intersect).
 *
 * The program grants attributes with a decorator whose operator is declared to grant them
 * (OperatorTraits::grantsAttrs): to the nodes written in the function or block it decorates,
 * the arms, loop bodies and lambdas written there included, and not to the functions they
 * call (lexical, as design principles 2.13 says of numeric permissions).
 */

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace camel::core::attr {

/// An interned attribute key; 0 is no key.
using AttrKey = uint16_t;
/// An interned set of attributes; 0 is the empty set.
using AttrSetId = uint32_t;

inline constexpr AttrKey kNoKey      = 0;
inline constexpr AttrSetId kNoAttrs  = 0;

struct Attr {
    AttrKey key    = kNoKey;
    uint64_t value = 0; // a set of flags; 0 is the same as no attribute of this key
    bool operator==(const Attr &) const = default;
};

/// The key of `name`, interned on first use. A name is a non-empty run of letters, digits,
/// '_', '.', ':' and '-' (so that the text form below reads back); std::invalid_argument
/// otherwise.
AttrKey key(std::string_view name);
/// The key of `name` when it was interned before.
std::optional<AttrKey> findKey(std::string_view name);
/// The name of `key`; empty for kNoKey or an unknown key.
std::string_view keyName(AttrKey key);

/// The set of `attrs`: values of the same key are united, zero values dropped.
AttrSetId intern(std::span<const Attr> attrs);
/// The set with one attribute.
AttrSetId single(AttrKey key, uint64_t value = 1);
/// The attributes of `set`, ordered by key name (the same order in every process).
std::vector<Attr> entries(AttrSetId set);
/// The value of `key` in `set`, 0 when it has none.
uint64_t valueOf(AttrSetId set, AttrKey key);
/// Both sets, the values of a key in both united.
AttrSetId unite(AttrSetId a, AttrSetId b);
/// What both sets have: the keys of both, their values intersected.
AttrSetId intersect(AttrSetId a, AttrSetId b);

/// "name" for the value 1, "name=0x2a" otherwise, separated by ", ", ordered by name; empty
/// for the empty set. Process-independent: caches and dumps use it.
std::string text(AttrSetId set);
/// The set of a text() form; nullopt when it is not one.
std::optional<AttrSetId> parse(std::string_view text);

} // namespace camel::core::attr
