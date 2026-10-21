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
 * Author: Zhenjie Wei
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The specializations a program has made (program.h).
 *
 * A rewrite specializes a call: it copies the callee graph with some of its formals bound to
 * constants (or refined types) and points the call at the copy. The copy costs a graph, so the
 * rewrite remembers which copy it made for what it bound and reuses it instead of making another.
 *
 * The table lives with the program, not with a session: the ONNX export and std::opt simplify a
 * program in rounds, each round a session of its own over the program the round before produced,
 * and a specialization made in the first round is still the right answer in the second. It is a
 * value member of Program, copied with it, so a program a rewrite derives from another carries
 * the table along and a session that is handed a program it did not make writes to its own copy.
 *
 * An entry names the callee by GraphId and content revision (graph_id.h, GCGraph::revision), not
 * by address: a graph that was edited since gets a new revision and its entries no longer match,
 * which is what should happen. It names the specialization by GraphId too: a program that no
 * longer holds that graph resolves the id to nothing, and the session makes the specialization
 * again. The commit does what it can to keep that from happening: it binds every graph the table
 * names (and what those graphs call) into the program it produces, so the specializations of a
 * program are part of it even where nothing reachable calls them. That is the program keeping
 * what it made, not a cache holding a graph the program dropped.
 *
 * Only bindings that are values in the language's sense are remembered: numbers, booleans and
 * refined types. A specialization that binds a GC-managed object (a captured tensor, say) is
 * about that object instance, so it stays with the session that met it; keeping it here would
 * both tie the program to an object and compare addresses that a collection may move.
 */

#pragma once

#include "camel/core/slot.h"
#include "camel/runtime/graph_id.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace camel::core::type {
class Type;
} // namespace camel::core::type

namespace camel::runtime {

/// Which formal of the callee a binding removes (`Norm`/`With`), or that it binds the closure
/// values of a function (`Closure`), or that it turns the closure nodes into norm ports (`Lift`,
/// lambda lifting; see devirtualizeStaticCallInDraft).
enum class RuntimeSpecializationBindingKind : uint8_t {
    Norm,
    With,
    Closure,
    Lift,
};

struct RuntimeSpecializationBindingKey {
    RuntimeSpecializationBindingKind kind = RuntimeSpecializationBindingKind::Norm;
    size_t index                          = 0;
    /// The bound value; NullSlot for a binding that only refines the formal's type.
    slot_t value                 = NullSlot;
    camel::core::type::Type *type = nullptr;
    uint8_t runtimeFlags         = 0;

    bool operator==(const RuntimeSpecializationBindingKey &other) const = default;
};

/// What an entry was made for: the callee graph by identity and content, and the bindings in the
/// order makeRuntimeSpecializationKey puts them (sorted).
struct ProgramSpecializationKey {
    GraphId baseGraph     = kInvalidGraphId;
    uint64_t baseRevision = 0;
    std::vector<RuntimeSpecializationBindingKey> bindings;

    bool operator==(const ProgramSpecializationKey &other) const = default;
};

struct ProgramSpecializationKeyHasher {
    size_t operator()(const ProgramSpecializationKey &key) const {
        size_t seed = static_cast<size_t>(toValue(key.baseGraph));
        seed ^= static_cast<size_t>(key.baseRevision) + 0x9e3779b97f4a7c15ull + (seed << 6) +
                (seed >> 2);
        for (const RuntimeSpecializationBindingKey &binding : key.bindings) {
            const size_t h =
                std::hash<const void *>{}(static_cast<const void *>(binding.type));
            seed ^= h + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
            seed ^= std::hash<size_t>{}(binding.index) + 0x9e3779b97f4a7c15ull + (seed << 6) +
                    (seed >> 2);
            seed ^= std::hash<slot_t>{}(binding.value) + 0x9e3779b97f4a7c15ull + (seed << 6) +
                    (seed >> 2);
            seed ^= std::hash<uint8_t>{}(
                        static_cast<uint8_t>(binding.kind)) +
                    0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
            seed ^= std::hash<uint8_t>{}(binding.runtimeFlags) + 0x9e3779b97f4a7c15ull +
                    (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

/// The specializations of a program: what was bound -> the graph the rewrite made for it. Copied
/// with the program (program.h); the names of new specializations (`walk$spec3`) continue from a
/// per-program counter, so a program derived from another does not name a graph what the program
/// it came from already named.
class RuntimeSpecializationTable {
  public:
    bool find(const ProgramSpecializationKey &key, GraphId &graph) const {
        const auto it = entries_.find(key);
        if (it == entries_.end()) {
            return false;
        }
        graph = it->second;
        return true;
    }

    void remember(ProgramSpecializationKey key, GraphId graph) {
        entries_.insert_or_assign(std::move(key), graph);
    }

    /// Copies the entries `other` has that this table does not (a commit that merged into another
    /// program's lineage: both tables hold specializations of the graphs the merge was made over).
    void adoptMissingFrom(const RuntimeSpecializationTable &other) {
        for (const auto &[key, graph] : other.entries_) {
            entries_.insert({key, graph});
        }
    }

    bool empty() const { return entries_.empty(); }
    size_t size() const { return entries_.size(); }
    /// The entries, for a commit that binds the graphs they name into the program (program.h).
    const std::unordered_map<ProgramSpecializationKey, GraphId, ProgramSpecializationKeyHasher> &
    entries() const {
        return entries_;
    }

    uint64_t nextSpecializationNonce() { return specializationNonce_++; }
    uint64_t nextLiftNonce() { return liftNonce_++; }

  private:
    std::unordered_map<ProgramSpecializationKey, GraphId, ProgramSpecializationKeyHasher> entries_;
    uint64_t specializationNonce_ = 0;
    uint64_t liftNonce_           = 0;
};

} // namespace camel::runtime