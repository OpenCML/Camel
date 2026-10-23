// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Fusion group registry and the view a module's group kernel reads (see
 * camel/execute/fusion_group.h). The core keeps the attribute keys opaque: it stores what a
 * module registered and asks the module's kernel; it never interprets a key or a value.
 */

#include "camel/execute/fusion_group.h"

#include "camel/core/rtdata/base.h"
#include "camel/profile/profile.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <shared_mutex>

namespace camel::execute {

namespace attr = camel::core::attr;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GCOperBody;

namespace {

struct Registration {
    attr::AttrKey key = attr::kNoKey;
    std::string name;
    FusionGroupKernel kernel = nullptr;
};

struct Registry {
    std::shared_mutex mutex;
    std::vector<Registration> entries;
};

Registry &registry() {
    static Registry instance;
    return instance;
}

bool among(std::span<const gc_node_ref_t> members, gc_node_ref_t node) {
    return std::find(members.begin(), members.end(), node) != members.end();
}

} // namespace

slot_t FusionGroupView::valueOf(gc_node_ref_t node) const {
    if (node >= plan_->dataIndexByRef.size()) {
        return NullSlot;
    }
    const camel::runtime::gc_data_idx_t index = plan_->dataIndexByRef[node];
    return index == 0 ? NullSlot : frame_->get<slot_t>(index);
}

void FusionGroupView::setValueOf(gc_node_ref_t node, slot_t value) const {
    if (node >= plan_->dataIndexByRef.size()) {
        return;
    }
    const camel::runtime::gc_data_idx_t index = plan_->dataIndexByRef[node];
    if (index != 0) {
        frame_->set(index, value);
    }
}

camel::core::OperId FusionGroupView::operOf(gc_node_ref_t node) const {
    const auto *record = graph_->node(node);
    if (record == nullptr || record->kind != camel::runtime::GCNodeKind::Oper) {
        return camel::core::kNoOper;
    }
    return graph_->nodeBodyAs<GCOperBody>(node)->oper;
}

bool FusionGroupView::isPrivateTo(gc_node_ref_t node, std::span<const gc_node_ref_t> members) const {
    if (node == graph_->outputNodeRef() || node == graph_->returnNodeRef() ||
        node == graph_->exitNodeRef()) {
        return false;
    }
    for (gc_node_ref_t user : graph_->normOutputsOf(node)) {
        if (!among(members, user)) {
            return false;
        }
    }
    return graph_->withOutputsOf(node).empty() && graph_->ctrlOutputsOf(node).empty();
}

void registerFusionGroup(attr::AttrKey key, std::string_view name, FusionGroupKernel kernel) {
    if (key == attr::kNoKey || name.empty() || kernel == nullptr) {
        return;
    }
    Registry &r = registry();
    std::unique_lock lock(r.mutex);
    for (Registration &entry : r.entries) {
        if (entry.key == key && entry.name == name) {
            entry.kernel = kernel;
            return;
        }
    }
    r.entries.push_back(Registration{key, std::string(name), kernel});
}

FusionGroupAction runFusionGroup(
    const camel::runtime::GCGraph &graph, camel::core::context::Frame &frame,
    const camel::runtime::ExecPlan &plan, gc_node_ref_t node,
    std::span<const gc_node_ref_t> deferred, std::vector<gc_node_ref_t> &members,
    FusionGroupRef &group) {
    const attr::AttrSetId attrs = graph.nodeAttrs(node);
    if (attrs == attr::kNoAttrs) {
        return FusionGroupAction::None;
    }
    Registration match;
    {
        Registry &r = registry();
        std::shared_lock lock(r.mutex);
        for (const Registration &entry : r.entries) {
            const attr::AttrValueId value = attr::valueOf(attrs, entry.key);
            if (value == attr::kNoValue) {
                continue;
            }
            if (attr::value(value).text != entry.name) {
                continue;
            }
            match = Registration{entry.key, entry.name, entry.kernel};
            break;
        }
    }
    if (match.kernel == nullptr) {
        return FusionGroupAction::None;
    }
    // The key and the value stay available to the kernel without another lookup: the value is
    // the one the node carries under the key.
    const attr::AttrValueId value = attr::valueOf(attrs, match.key);
    const FusionGroupView view(graph, frame, plan, match.key, value);
    members.clear();
    group = FusionGroupRef{match.key, match.name, camel::runtime::kInvalidNodeRef};

    // The group's time is measured here, around the kernel call, and recorded as the group's
    // own row: it is not inside any operator call, so no operator row can carry it and no member
    // row can be added for it (profile.h). Off unless the session is recording.
    const bool recording = camel::profile::enabledInline();
    const auto started   = recording ? std::chrono::steady_clock::now()
                                     : std::chrono::steady_clock::time_point{};
    const FusionGroupAction action = match.kernel(view, node, deferred, members, group);

    if (action == FusionGroupAction::Ran && recording) {
        const uint64_t ns = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                      std::chrono::steady_clock::now() - started)
                                                      .count());
        // The operators the group computed, in execution order: the members the kernel reported
        // and the tail it ran them at. A thread-local scratch keeps the recording allocation-free
        // after the first group of a program.
        static thread_local std::vector<std::string_view> operators;
        operators.clear();
        operators.reserve(members.size() + 1);
        const auto push = [&](gc_node_ref_t member) {
            if (const auto *body = graph.nodeBodyAs<camel::runtime::GCOperBody>(member);
                body != nullptr) {
                operators.push_back(body->uri());
            }
        };
        for (const gc_node_ref_t member : members) {
            push(member);
        }
        push(node);
        camel::profile::fusionGroup(
            attr::keyName(group.key), group.name, operators, ns);
    }
    return action;
}

} // namespace camel::execute