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
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The persistence hooks of graph facts (docs/design/op-semantics 10.6). There is no module cache
 * (`.cmo` with graphs, graph-model step 9) yet; these are the hooks it will call, and they are
 * tested by a round trip.
 *
 *   (i)   GraphContent  is part of the graph: a cache stores it with the graph block (the
 *                       permission column). Its kinds still encode and decode their values, so
 *                       that a loaded value can be compared with the graph it is loaded with.
 *   (ii)  Derived       a fact whose kind encodes it is written as a PortableFact: the kind's key
 *                       and version, the anchors by the graph's name in the program and the node,
 * the portable content hash of every anchored graph, the encoded value. Nothing process-local
 * enters it (no GraphId, revision, OperId, TypeId, pointer); the operators a graph uses enter its
 * hash by URI, semantics key and version, and traits. Loading checks the kind and version, finds
 * each graph by its name, compares its hash, decodes the value and stores it stamped with the
 * current revisions; any mismatch drops the fact. (iii) Target        never persisted by the core:
 * a back end keeps its own compile cache, keyed as design 10.6 says (content hash, OperatorKeys,
 * permissions, hardware, driver, compiler and module versions, options). registerKind refuses
 *                       encode/decode for these kinds.
 *
 * Note that `.cmo` holds the graphs the front end built (graph-model 4.6): what a cache can hold
 * are facts about those.
 */

#pragma once

#include "camel/runtime/facts.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace camel::runtime::fact {

/**
 * A hash of the content of `graph` and of every graph it refers to (at any depth), the same in
 * every process for the same program: the function type, ports, numeric permissions, and every
 * node's kind, flags, type (mangled), slot, inputs and payload, where a referred graph counts by
 * its name and its own content, an operator by its URI, semantics key with version and
 * traits, a static constant by its type and value (a scalar by its bits, an object by its
 * printed form). Source positions do not enter it.
 */
uint64_t portableGraphHash(const Program &program, const GCGraph *graph);

/// One fact in the form a cache would hold it.
struct PortableFact {
    std::string kind;
    uint16_t version = 0;
    /// (name of the graph in the program, node; kInvalidNodeRef for the graph as a whole) per
    /// anchor. The name is the graph's own, with `~<ordinal>` for the second graph of a name.
    std::vector<std::pair<std::string, gc_node_ref_t>> anchors;
    /// portableGraphHash of each distinct anchored graph, in the order of the anchors.
    std::vector<uint64_t> graphHashes;
    /// The kind's encoding of the value.
    std::string value;

    bool operator==(const PortableFact &) const = default;
};

/// One line of text (no newline) and back; nullopt for a line that is not one.
std::string toText(const PortableFact &fact);
std::optional<PortableFact> parsePortableFact(std::string_view line);

/// The facts about the graphs reachable from `root` a cache could keep: every valid stored fact
/// of a Derived kind that encodes, and every GraphContent value of a kind that encodes.
std::vector<PortableFact> exportFacts(FactStore &store, const Program &program);

/// What loading one PortableFact did.
enum class ImportResult : uint8_t {
    Stored,        // a Derived fact, checked and stored
    MatchesGraph,  // a GraphContent value, equal to the graph's
    UnknownKind,   // no kind of that key, or it does not decode
    OtherVersion,  // the kind's version differs
    UnknownGraph,  // an anchor's graph is not in the program (or a node is out of range)
    GraphChanged,  // an anchored graph's content hash differs
    BadValue,      // the kind does not accept the encoded value
    DiffersFromGraph, // a GraphContent value that is not the graph's
};

std::string_view importResultName(ImportResult result);

/// Loads `fact` into `store` against the program of `root` (see the file comment).
ImportResult importFact(FactStore &store, const Program &program, const PortableFact &fact);

} // namespace camel::runtime::fact
