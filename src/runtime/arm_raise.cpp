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
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Arm regions as graphs of their own, for the code that differentiates a branch arm by arm.
 *
 * The arm of a branch is a region of the graph that has the branch. This outlines each arm into
 * a graph and puts a FUNC node in its place (head and tail of the arm, ordered after the BRCH,
 * read by the JOIN): the form the front end builds, with the values the arm reads from outside
 * as its with-ports. The result is a private program (detached), not the one of the context.
 */
#include "camel/runtime/arm_raise.h"

#include "camel/runtime/draft_clone.h"
#include "camel/runtime/draft_session.h"

#include <algorithm>
#include <unordered_map>

namespace camel::runtime {

using camel::core::type::FunctionType;
using camel::core::type::Type;

namespace {

/// The number of regions between `region` and the body.
uint32_t depthOf(const GraphDraft &draft, uint32_t region) {
    uint32_t depth = 0;
    while (region != 0) {
        region = draft.region(region).parent;
        ++depth;
    }
    return depth;
}

void appendUnique(std::vector<gc_node_ref_t> &list, gc_node_ref_t ref) {
    if (std::ranges::find(list, ref) == list.end()) {
        list.push_back(ref);
    }
}

/// Outlines the arm of the Arm region `region` of `draft` into a member of `group`. False, with
/// `draft` unchanged, when the arm cannot be outlined (a node of it is read from outside).
bool raiseArm(
    GraphDraft &draft, GraphDraftGroup &group, const std::string &mangledName,
    const std::string &name, uint32_t region) {
    const GCRegion arm       = draft.region(region);
    const gc_node_ref_t brch = arm.owner;
    const size_t slots       = draft.nodeSlotCount();
    const gc_node_ref_t join =
        reinterpret_cast<const DraftBrchPayload *>(draft.payloadOf(brch).data())->join;

    std::vector<uint8_t> member(slots, 0);
    std::vector<gc_node_ref_t> members;
    for (gc_node_ref_t id = 0; id < slots; ++id) {
        if (draft.alive(id) && draft.nodeRegion(id) != 0 &&
            draft.regionWithin(draft.nodeRegion(id), region)) {
            member[id] = 1;
            members.push_back(id);
        }
    }
    // What the JOIN takes from this arm.
    gc_node_ref_t result = arm.result;
    const std::vector<gc_node_ref_t> joinWith(
        draft.withInputsOf(join).begin(),
        draft.withInputsOf(join).end());
    const bool joinHasInput = arm.index < joinWith.size();
    if (joinHasInput) {
        result = joinWith[arm.index];
    }

    const auto isConstant = [&](gc_node_ref_t id) {
        return draft.header(id)->kind == GCNodeKind::Data;
    };
    std::vector<gc_node_ref_t> externals;
    std::vector<gc_node_ref_t> ctrlPreds;
    const auto external = [&](gc_node_ref_t input) {
        if (input != kInvalidNodeRef && !member[input] && !isConstant(input)) {
            appendUnique(externals, input);
        }
    };
    for (gc_node_ref_t id : members) {
        for (auto inputs : {draft.normInputsOf(id), draft.withInputsOf(id)}) {
            for (gc_node_ref_t input : inputs) {
                external(input);
            }
        }
        for (gc_node_ref_t pred : draft.ctrlInputsOf(id)) {
            if (!member[pred] && pred != brch) {
                appendUnique(ctrlPreds, pred);
            }
        }
        // Nothing but the JOIN reads a node of the arm from outside, and the JOIN only its
        // result.
        for (auto users : {draft.normUsersOf(id), draft.withUsersOf(id)}) {
            for (gc_node_ref_t user : users) {
                if (!member[user] && !(user == join && id == result)) {
                    return false;
                }
            }
        }
    }
    if (result != kInvalidNodeRef && !member[result]) {
        external(result);
    }
    for (gc_node_ref_t input : externals) {
        if (draft.header(input)->dataType == nullptr) {
            return false;
        }
    }
    Type *exitType = result == kInvalidNodeRef ? Type::Void() : draft.header(result)->dataType;
    if (exitType == nullptr) {
        return false;
    }

    // The graph of the arm: the nodes of the draft, those of the arm kept, the values it reads
    // from outside turned into with-ports.
    const GraphId armId = group.reserve(mangledName, name);
    group.define(armId, std::make_unique<GraphDraft>());
    GraphDraft &body             = group.draft(armId);
    DraftGraphCloneResult cloned = cloneDraftGraphIntoDraft(body, draft, 0);
    const auto &toBody           = cloned.sourceToCloned;
    body.clearRegions();
    std::unordered_map<gc_node_ref_t, gc_node_ref_t> portOf; // by the node in `body`
    for (gc_node_ref_t input : externals) {
        Type *type               = draft.header(input)->dataType;
        const gc_node_ref_t port = body.addPortNode(type, body.allocateRuntimeSlot(type));
        body.appendWithPort(port);
        portOf.emplace(toBody[input], port);
    }
    const auto remap = [&](std::span<const gc_node_ref_t> inputs) {
        std::vector<gc_node_ref_t> mapped(inputs.begin(), inputs.end());
        bool changed = false;
        for (gc_node_ref_t &input : mapped) {
            if (const auto it = portOf.find(input); it != portOf.end()) {
                input   = it->second;
                changed = true;
            }
        }
        return std::pair{std::move(mapped), changed};
    };
    for (gc_node_ref_t id : members) {
        const gc_node_ref_t inBody = toBody[id];
        if (auto [norm, changed] = remap(body.normInputsOf(inBody)); changed) {
            body.setNormInputs(inBody, norm);
        }
        if (auto [with, changed] = remap(body.withInputsOf(inBody)); changed) {
            body.setWithInputs(inBody, with);
        }
    }
    gc_node_ref_t bodyResult = kInvalidNodeRef;
    if (result != kInvalidNodeRef) {
        const gc_node_ref_t inBody = toBody[result];
        bodyResult                 = portOf.contains(inBody) ? portOf.at(inBody) : inBody;
    }
    // What the arm does not hold goes; the constants it reads stay, those nothing reads go too.
    for (gc_node_ref_t id = 0; id < slots; ++id) {
        const gc_node_ref_t inBody = toBody[id];
        if (inBody != kInvalidNodeRef && !member[id] && !isConstant(id) &&
            !portOf.contains(inBody)) {
            body.eraseNode(inBody);
        }
    }
    for (gc_node_ref_t id = 0; id < slots; ++id) {
        const gc_node_ref_t inBody = toBody[id];
        if (inBody != kInvalidNodeRef && isConstant(id) && body.alive(inBody) &&
            inBody != bodyResult && body.normUsersOf(inBody).empty() &&
            body.withUsersOf(inBody).empty() && body.ctrlUsersOf(inBody).empty()) {
            body.eraseNode(inBody);
        }
    }
    for (const auto &[original, port] : portOf) {
        if (body.alive(original)) {
            body.eraseNode(original);
        }
    }
    if (bodyResult != kInvalidNodeRef) {
        body.setOutputNode(bodyResult);
        body.setExitNode(bodyResult);
        body.setReturnNode(bodyResult, GCReturnKind::Self);
    }
    // A gate that waited on what the arm does not hold waits on nothing.
    body.dissolveUnorderedGates();
    body.setFuncType(FunctionType::create()->withExitType(exitType));

    // The call that takes the place of the arm.
    const gc_node_ref_t call = draft.addFuncNode(armId, exitType, 0, 0);
    draft.addDependency(armId);
    draft.addSubGraph(armId);
    draft.setNodeOrigin(call, draft.nodeOrigin(members.empty() ? brch : members.front()));
    draft.setWithInputs(call, externals);
    std::vector<gc_node_ref_t> ctrl{brch};
    ctrl.insert(ctrl.end(), ctrlPreds.begin(), ctrlPreds.end());
    draft.setCtrlInputs(call, ctrl);
    if (joinHasInput) {
        std::vector<gc_node_ref_t> joined = joinWith;
        joined[arm.index]                 = call;
        draft.setWithInputs(join, joined);
    }
    draft.setBranchArm(brch, arm.index, GCBranchArm{.head = call, .tail = call});
    // What waited on the code of the arm waits on the call.
    for (gc_node_ref_t id : members) {
        const std::vector<gc_node_ref_t> users(
            draft.ctrlUsersOf(id).begin(),
            draft.ctrlUsersOf(id).end());
        for (gc_node_ref_t user : users) {
            if (member[user] || user == call) {
                continue;
            }
            std::vector<gc_node_ref_t> inputs(
                draft.ctrlInputsOf(user).begin(),
                draft.ctrlInputsOf(user).end());
            std::replace(inputs.begin(), inputs.end(), id, call);
            std::vector<gc_node_ref_t> unique;
            for (gc_node_ref_t input : inputs) {
                appendUnique(unique, input);
            }
            draft.setCtrlInputs(user, unique);
        }
    }
    for (gc_node_ref_t id : members) {
        if (draft.alive(id)) {
            draft.eraseNode(id);
        }
    }
    draft.setNodeRegion(call, arm.parent);
    draft.retireRegion(region);
    return true;
}

} // namespace

const Program *raiseBranchArms(
    const camel::core::context::context_ptr_t &context, const Program *program, GCGraph *root) {
    if (program == nullptr || root == nullptr) {
        return program;
    }
    RuntimeGraphDraftSession session(context, program, root);
    std::vector<GCGraph *> withArms;
    for (GCGraph *graph : session.collectReachableRuntimeGraphs()) {
        for (uint32_t r = 1; r < graph->regionCount(); ++r) {
            if (graph->region(r)->kind == GCRegionKind::Arm) {
                withArms.push_back(graph);
                break;
            }
        }
    }
    if (withArms.empty()) {
        return program;
    }
    GraphDraftGroup group(context, program);
    for (GCGraph *graph : withArms) {
        GraphDraft &draft = session.edit(graph);
        // The arms inside arms first: the arm that holds them then holds their calls.
        std::vector<uint32_t> arms;
        for (uint32_t r = 1; r < draft.regionCount(); ++r) {
            if (draft.region(r).kind == GCRegionKind::Arm) {
                arms.push_back(r);
            }
        }
        std::ranges::stable_sort(arms, [&](uint32_t a, uint32_t b) {
            return depthOf(draft, a) > depthOf(draft, b);
        });
        size_t count = 0;
        for (uint32_t r : arms) {
            const std::string suffix = ".arm" + std::to_string(count);
            if (raiseArm(draft, group, graph->mangledName() + suffix, graph->name() + suffix, r)) {
                ++count;
            }
        }
    }
    group.encode();
    for (GraphId id : group.ids()) {
        session.adoptGraph(group.encoded(id));
    }
    return session.commit(/*detached=*/true);
}

} // namespace camel::runtime
