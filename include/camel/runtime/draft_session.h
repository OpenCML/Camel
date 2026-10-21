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

/// The key a session remembers a specialization under within itself: the callee graph version
/// itself and the bindings. The program keeps its own key (specializations.h): an id and a
/// content revision, so an entry outlives the version it was made for.
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

/// A commit lost a race: the program the session started from is no longer the program of the
/// context, and a graph the session read or wrote has another version in the program that is.
/// The caller may open a new session on the current program and do the work again.
class GraphCommitConflict : public std::runtime_error {
  public:
    GraphCommitConflict(std::vector<GraphId> graphs, const std::string &message)
        : std::runtime_error(message), graphs_(std::move(graphs)) {}
    /// The graphs that differ.
    const std::vector<GraphId> &graphs() const { return graphs_; }

  private:
    std::vector<GraphId> graphs_;
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

    /// The state of the graphs the commit encodes (graph.h, GraphState); the validator applies
    /// the rules of that state. A session that builds a shape no back end runs (the ONNX
    /// export's split of branched calls) says NonExecutable. Executable by default.
    void setGraphState(GraphState state) { graphState_ = state; }

    GraphDraft &edit(GCGraph *graph);
    GraphDraft &rootDraft();

    /// The specialization made for `key`, if this session or the program it works on already has
    /// it: the session's own copies first, then the program's table (specializations.h), whose
    /// entry is used only while the program still holds the graph, the callee is still the
    /// version the entry was made for, and everything the specialization calls is in the program.
    GCGraph *findSpecialization(const RuntimeSpecializationKey &key) const;
    /// Remembers the graph the session made for `key`, in the session and (when the key binds no
    /// GC-managed value) in the program, so a later session over the program finds it.
    void rememberSpecialization(RuntimeSpecializationKey key, GCGraph *graph);
    /// The name numbers of new specializations, kept by the program so that a program derived
    /// from another continues where it left off instead of naming a second graph `walk$spec0`.
    uint64_t nextSpecializationNonce();
    uint64_t nextLiftNonce();

    /// Traces the static values the open drafts hold. The drafts keep them as raw slots, outside
    /// the object graph, so a collection at a safepoint would otherwise move one and leave the
    /// draft (and the graph a commit encodes) pointing at where it was. `ownerType` is the type
    /// of the value that owns the session (a transaction), for the tracing report.
    void traceStaticSlots(
        const camel::core::rtdata::Object::RefRelocator &relocate,
        const camel::core::type::Type *ownerType);

    /// Type-only specializations (see kTypeSpecializationTag) of the function `root` that the
    /// program holds: those reachable when first asked, plus those noted since.
    size_t typeSpecializationCount(GraphId root);
    void noteTypeSpecialization(GraphId root);

    std::vector<GCGraph *> collectReachableRuntimeGraphs() const;
    /// Encodes the edited closure into a new program and returns it. A session over the
    /// program's root installs it as the program of the context; with `detached` (a private
    /// rewrite, such as the working copy of autodiff) the program is left as it is and
    /// the new program is kept alive as a detached one (Context::trackDetachedRuntimeProgram).
    const Program *commit(bool detached = false);
    /// The graphs the session read or wrote (decoded or edited) whose version in the context's
    /// program is not the one it started from; empty while the context's program is still the
    /// session's. A commit of a stale session with no conflict does not fail: it merges its
    /// graphs into the context's newest program (design 4.3.1) and replaces it. A commit that
    /// does conflict makes a program of its own from the one it started from, which is not the
    /// context's (the callers that run a function value of the old program do this routinely). A
    /// caller that means to replace the context's program and must not fork asks first
    /// (optimistic concurrency, design 4.3.1).
    std::vector<GraphId> conflicts() const;
    /// conflicts() against `current`, which the caller names: the compare-and-swap of a commit
    /// that lost the race to another one re-checks against the program that is there now.
    std::vector<GraphId> conflictsWithProgram(const camel::runtime::Program *current) const;
    /// commit(), but throws GraphCommitConflict when conflicts() is not empty.
    const Program *commitChecked();

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
    /// Whether a specialization with these bindings may outlive the session: it binds only
    /// values (no GC-managed object), so the same key means the same specialization in a later
    /// session over the program (specializations.h).
    bool specializationLivesInProgram(const RuntimeSpecializationKey &key) const;
    ProgramSpecializationKey programKeyOf(const RuntimeSpecializationKey &key) const;

    camel::core::context::context_ptr_t context_;
    const Program *program_ = nullptr;
    /// Whether a commit may replace the context's program: true when the session was opened over
    /// that program (it may have been overtaken since: the commit merges), or when the program it
    /// works on is the context's now (someone installed it). A session over a function value's
    /// program (a detached program: the ONNX export, a tile region, a gradient) hands its commit
    /// back to its owner and never publishes it, whatever the context's program is.
    bool mayPublish_ = false;
    mutable std::unique_ptr<Program> work_;
    GCGraph *runtimeRoot_ = nullptr;
    GraphState graphState_ = GraphState::Executable;
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
    /// The members, in the order they were reserved.
    const std::vector<GraphId> &ids() const { return order_; }
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
