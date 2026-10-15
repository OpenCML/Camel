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
    std::string stableId;
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

/// Suffix tag of a specialization that binds no value and only refines parameter types
/// (`loop$tspec3`): the stable id up to the first such tag names the function it derives from.
inline constexpr std::string_view kTypeSpecializationTag = "tspec";

/// The stable id of the function a (possibly type-specialized) graph derives from.
std::string typeSpecializationRoot(std::string_view stableId);

struct RuntimeSpecializationKeyHasher {
    size_t operator()(const RuntimeSpecializationKey &key) const;
};

class RuntimeGraphDraftSession {
  public:
    RuntimeGraphDraftSession(
        const camel::core::context::context_ptr_t &context, GCGraph *runtimeRoot);

    GCGraph *runtimeRoot() const { return runtimeRoot_; }

    bool hasDraft(const GCGraph *graph) const;
    GraphDraft *tryDraft(const GCGraph *graph);
    const GraphDraft *tryDraft(const GCGraph *graph) const;

    GraphDraft &edit(GCGraph *graph);
    GraphDraft &rootDraft();

    GCGraph *findSpecialization(const RuntimeSpecializationKey &key) const;
    void rememberSpecialization(RuntimeSpecializationKey key, GCGraph *graph);

    /// Type-only specializations (see kTypeSpecializationTag) of the function `rootStableId`
    /// that the program holds: those reachable when first asked, plus those noted since.
    size_t typeSpecializationCount(const std::string &rootStableId);
    void noteTypeSpecialization(const std::string &rootStableId);

    std::vector<GCGraph *> collectReachableRuntimeGraphs() const;
    /// Encodes the edited closure and returns its root. A session over the program's root
    /// replaces the program with it; with `detached` (a back end's private rewrite, such as
    /// lowering loops before it runs) the program is left as it is and the new graphs are kept
    /// alive as detached graphs (Context::trackDetachedRuntimeGraphs).
    GCGraph *commit(bool detached = false);

  private:
    struct DraftEntry {
        std::unique_ptr<GraphDraft> draft;
        RuntimeDraftIdentity identity;
    };

    GraphDraft &ensureDraft(GCGraph *graph);
    std::vector<GCGraph *> collectCommitClosure() const;
    /// The graphs of `closure` whose copy gets a new revision (GCGraph::serial): edited, or
    /// referring at any depth to an edited one.
    std::unordered_set<GCGraph *> changedGraphs(const std::vector<GCGraph *> &closure) const;

    camel::core::context::context_ptr_t context_;
    GCGraph *runtimeRoot_ = nullptr;
    std::unordered_map<GCGraph *, std::unique_ptr<DraftEntry>> drafts_;
    /// The graphs handed out for editing (edit, rootDraft) or changed by the commit itself.
    /// The commit drops those it encodes back to the same content (sameContent).
    std::unordered_set<GCGraph *> edited_;
    std::unordered_map<RuntimeSpecializationKey, GCGraph *, RuntimeSpecializationKeyHasher>
        specializationCache_;
    std::unordered_map<std::string, size_t> typeSpecializationCounts_;
    bool typeSpecializationCountsReady_ = false;
};

/// Encodes new graphs that may refer to one another, such as the mutually recursive graphs a
/// transform synthesizes. Each member is reserved first: the returned key stands for the graph
/// wherever a GCGraph pointer goes in the drafts (FUNC callees, static Function values), and
/// encode() replaces every such reference with the encoded graph. A key is never dereferenced
/// and must not escape the drafts of its group.
class GraphDraftGroup {
  public:
    explicit GraphDraftGroup(const camel::core::context::context_ptr_t &context);
    ~GraphDraftGroup();

    GraphDraftGroup(const GraphDraftGroup &)            = delete;
    GraphDraftGroup &operator=(const GraphDraftGroup &) = delete;

    /// Reserves a member with an empty draft and returns its key.
    GCGraph *reserve(std::string stableId, std::string mangledName, std::string name);
    /// The draft of a member.
    GraphDraft &draft(GCGraph *key);
    /// Replaces the draft of a member.
    void define(GCGraph *key, std::unique_ptr<GraphDraft> draft);

    /// Encodes all members.
    void encode();
    /// The graph encoded for `key`.
    GCGraph *encoded(GCGraph *key) const;

  private:
    struct Member {
        RuntimeDraftIdentity identity;
        std::unique_ptr<GraphDraft> draft;
        GCGraph *encoded = nullptr;
        std::byte keyStorage{};
    };

    Member &member(GCGraph *key);
    const Member &member(GCGraph *key) const;

    camel::core::context::context_ptr_t context_;
    std::unordered_map<GCGraph *, std::unique_ptr<Member>> members_;
    std::vector<GCGraph *> order_;
    bool encoded_ = false;
};

} // namespace camel::runtime
