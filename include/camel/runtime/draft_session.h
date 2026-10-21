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
 * Created: Apr. 10, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Runtime graph draft session.
 *
 * The session is the runtime-side rewrite transaction boundary:
 * - decode reachable GCGraph objects into editable GraphDrafts on demand
 * - let passes mutate drafts without touching compile-time GIR
 * - commit by re-encoding the reachable runtime closure and replacing the
 *   manager-owned runtime root atomically
 *
 * The session intentionally operates on runtime graph identity only. It does
 * not delegate structural edits back to compile-time GIR.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/mm/alloc/growable.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/program.h"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camel::runtime {

struct RuntimeDraftIdentity {
    GCGraph *sourceGraph = nullptr;
    std::string mangledName;
    std::string name;
};

enum class RuntimeSpecializationBindingKind : uint8_t {
    Norm,
    With,
    Closure,
    Lift, // closure nodes turned into norm ports (see devirtualizeStaticCallInDraft)
};

struct RuntimeSpecializationBindingKey {
    RuntimeSpecializationBindingKind kind = RuntimeSpecializationBindingKind::Norm;
    size_t index                          = 0;
    slot_t value                          = NullSlot;
    camel::core::type::Type *type         = nullptr;
    uint8_t runtimeFlags                  = 0;

    bool operator==(const RuntimeSpecializationBindingKey &other) const = default;
};

struct RuntimeSpecializationKey {
    GCGraph *baseGraph = nullptr;
    std::vector<RuntimeSpecializationBindingKey> bindings;

    bool operator==(const RuntimeSpecializationKey &other) const = default;
};

/// Thrown when a draft cannot be encoded because the space runtime graphs live in is at its
/// limit. It is the allocator's mm::MemoryExhausted, told with the graph that was being
/// encoded; the type lets the driver of an open-ended rewrite (an export that unrolls a
/// recursion) say what grew, and where.
class GraphSpaceExhausted : public camel::core::mm::MemoryExhausted {
  public:
    GraphSpaceExhausted(
        const camel::core::mm::MemoryExhausted &cause, size_t bytes, const std::string &graph,
        uint64_t origin = 0)
        : camel::core::mm::MemoryExhausted(cause.during("encoding graph '" + graph + "'", origin)),
          bytes_(bytes), graph_(graph) {}

    /// The size of the graph that could not be placed, and its name.
    size_t bytes() const { return bytes_; }
    const std::string &graph() const { return graph_; }

  private:
    size_t bytes_;
    std::string graph_;
};

/// Suffix tag of the name of a specialization that binds no value and only refines parameter
/// types (`loop$tspec3`); GCGraph::typeSpecializationRoot records what it derives from.
inline constexpr std::string_view kTypeSpecializationTag = "tspec";

struct RuntimeSpecializationKeyHasher {
    size_t operator()(const RuntimeSpecializationKey &key) const;
};

class RuntimeGraphDraftSession {
  public:
    /// A session over the graphs reachable from the root of `program`.
    RuntimeGraphDraftSession(
        const camel::core::context::context_ptr_t &context, const Program *program);
    /// A session over the graphs reachable from `runtimeRoot`, a graph of `program`.
    RuntimeGraphDraftSession(
        const camel::core::context::context_ptr_t &context, const Program *program,
        GCGraph *runtimeRoot);

    /// The program the session rewrites; after a commit, the program it produced.
    const Program *program() const { return program_; }
    /// The program the drafts of the session read the graphs they refer to in: `program()`
    /// and what the session added to it (the graphs of function values of other programs it
    /// met, the specializations it made). Not a published program.
    const Program &work() const { return *work_; }
    /// Makes the graphs of `program` that `work()` lacks, and `graph`, visible to the drafts.
    void adoptProgram(const Program &program) const;
    void adoptGraph(GCGraph *graph) const;
    GCGraph *runtimeRoot() const { return runtimeRoot_; }
    /// The context of the program the session rewrites.
    const camel::core::context::context_ptr_t &context() const { return context_; }

    bool hasDraft(const GCGraph *graph) const;
    GraphDraft *tryDraft(const GCGraph *graph);
    const GraphDraft *tryDraft(const GCGraph *graph) const;

    GraphDraft &edit(GCGraph *graph);
    GraphDraft &rootDraft();

    GCGraph *findSpecialization(const RuntimeSpecializationKey &key) const;
    void rememberSpecialization(RuntimeSpecializationKey key, GCGraph *graph);

    /// Type-only specializations (see kTypeSpecializationTag) of the function `root` that the
    /// program holds: those reachable when first asked, plus those noted since.
    size_t typeSpecializationCount(GraphId root);
    void noteTypeSpecialization(GraphId root);

    std::vector<GCGraph *> collectReachableRuntimeGraphs() const;
    /// Encodes the edited closure into a new program and returns it. A session over the
    /// program's root installs it as the program of the context; with `detached` (a back end's
    /// private rewrite, such as lowering loops before it runs) the program is left as it is and
    /// the new program is kept alive as a detached one (Context::trackDetachedRuntimeProgram).
    const Program *commit(bool detached = false);

  private:
    struct DraftEntry {
        std::unique_ptr<GraphDraft> draft;
        RuntimeDraftIdentity identity;
    };

    GraphDraft &ensureDraft(GCGraph *graph);
    std::vector<GCGraph *> collectCommitClosure() const;
    /// The graphs the draft of `graph` refers to (visitDraftGraphs), in visiting order. Kept while
    /// the draft's revision is the same, so that asking for the closure of the program again
    /// does not walk the nodes of every draft again.
    const std::vector<std::pair<GraphId, const Program *>> &draftRefs(GCGraph *graph) const;

    camel::core::context::context_ptr_t context_;
    const Program *program_ = nullptr;
    mutable std::unique_ptr<Program> work_;
    GCGraph *runtimeRoot_ = nullptr;
    std::unordered_map<GCGraph *, std::unique_ptr<DraftEntry>> drafts_;
    /// The graphs handed out for editing (edit, rootDraft) or changed by the commit itself.
    /// The commit drops those it encodes back to the same content (sameContent).
    std::unordered_set<GCGraph *> edited_;
    struct DraftRefs {
        uint64_t revision = 0;
        bool valid        = false;
        std::vector<std::pair<GraphId, const Program *>>
            graphs; // with the program of a function value
    };
    mutable std::unordered_map<GCGraph *, DraftRefs> draftRefs_;
    /// Advanced by every draft of the session when what it refers to may have changed; a cell of
    /// its own, so that the drafts keep pointing at it when the session is moved.
    std::unique_ptr<uint64_t> refsEpoch_ = std::make_unique<uint64_t>(0);
    /// The closure last collected, and the state of the session it was collected in.
    struct ClosureCache {
        bool valid     = false;
        uint64_t epoch = 0;
        uint64_t table = 0;
        size_t drafts  = 0;
        GCGraph *root  = nullptr;
        std::vector<GCGraph *> graphs;
    };
    mutable ClosureCache closureCache_;
    /// 1 at the id of a graph that has a draft: the closure walk asks for every graph.
    mutable std::vector<uint8_t> draftedIds_;
    /// The graphs a frozen graph refers to, by id, kept with the version of the graph they were
    /// read from (a frozen graph never changes): the closure walk then reads these lists and
    /// the table, not the graphs.
    struct FrozenKids {
        const GCGraph *graph = nullptr;
        std::vector<GraphId> ids;
    };
    mutable std::vector<FrozenKids> frozenKids_;
    std::unordered_map<RuntimeSpecializationKey, GCGraph *, RuntimeSpecializationKeyHasher>
        specializationCache_;
    std::unordered_map<GraphId, size_t> typeSpecializationCounts_;
    bool typeSpecializationCountsReady_ = false;
};

/// Encodes new graphs that may refer to one another, such as the mutually recursive graphs a
/// transform synthesizes. Each member is reserved first: the returned GraphId is the graph's,
/// wherever a graph is named in the drafts (FUNC callees, static Function values), and encode()
/// builds the graphs. The members form a program with the graphs of the one the group is made
/// over (program()).
class GraphDraftGroup {
  public:
    GraphDraftGroup(const camel::core::context::context_ptr_t &context, const Program *base);
    ~GraphDraftGroup();

    GraphDraftGroup(const GraphDraftGroup &)            = delete;
    GraphDraftGroup &operator=(const GraphDraftGroup &) = delete;

    /// Reserves a member with an empty draft and returns its id.
    GraphId reserve(std::string mangledName, std::string name);
    /// The draft of a member.
    GraphDraft &draft(GraphId id);
    /// Replaces the draft of a member.
    void define(GraphId id, std::unique_ptr<GraphDraft> draft);

    /// Makes the graphs of `program` that the group's program lacks visible to the members (the
    /// specializations and copies the transformation made on the way, which members call).
    void adopt(const Program &program);
    /// Encodes all members.
    void encode();
    /// The graph encoded for `id`.
    GCGraph *encoded(GraphId id) const;
    /// The program of the group: the graphs of the program it is made over, and the members
    /// once encoded, the first one reserved as its root. A function value made for a member
    /// before encode() can name it in this program; it resolves once the group is encoded.
    const Program *program() const { return program_; }

  private:
    struct Member {
        RuntimeDraftIdentity identity;
        std::unique_ptr<GraphDraft> draft;
        GCGraph *encoded = nullptr;
    };

    Member &member(GraphId id);
    const Member &member(GraphId id) const;

    camel::core::context::context_ptr_t context_;
    std::unordered_map<GraphId, std::unique_ptr<Member>> members_;
    std::vector<GraphId> order_;
    Program *program_ = nullptr;
    bool encoded_     = false;
};

} // namespace camel::runtime
