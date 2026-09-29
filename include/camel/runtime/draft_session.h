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
 * Updated: May. 02, 2026
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
#include "camel/runtime/draft.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
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

    std::vector<GCGraph *> collectReachableRuntimeGraphs() const;
    GCGraph *commit();

  private:
    struct DraftEntry {
        std::unique_ptr<GraphDraft> draft;
        RuntimeDraftIdentity identity;
    };

    GraphDraft &ensureDraft(GCGraph *graph);
    std::vector<GCGraph *> collectCommitClosure() const;

    camel::core::context::context_ptr_t context_;
    GCGraph *runtimeRoot_ = nullptr;
    std::unordered_map<GCGraph *, std::unique_ptr<DraftEntry>> drafts_;
    std::unordered_map<RuntimeSpecializationKey, GCGraph *, RuntimeSpecializationKeyHasher>
        specializationCache_;
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
