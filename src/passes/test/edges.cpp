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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Commits a graph that breaks an edge invariant (a cycle of control inputs; a COPY whose type is
 * not its input's) and prints whether the commit accepted it. The third pass commits a valid
 * edit as the control.
 */
#include "camel/passes/test/edges.h"

#include "camel/core/rtdata/base.h"
#include "camel/runtime/draft_session.h"

#include <exception>

namespace {

using namespace camel::runtime;

enum class Edit { Cycle, Type, Ok };

const Program *commitEdit(
    const camel::core::context::context_ptr_t &context, const Program *program, std::ostream &os,
    Edit edit) {
    if (program == nullptr || program->root() == nullptr) {
        return program;
    }
    RuntimeGraphDraftSession session(context, program);
    GraphDraft &draft = session.edit(program->root());
    switch (edit) {
    case Edit::Cycle: {
        const gc_node_ref_t a = draft.addSyncNode();
        const gc_node_ref_t b = draft.addSyncNode();
        draft.setCtrlInputs(a, std::span<const gc_node_ref_t>(&b, 1));
        draft.setCtrlInputs(b, std::span<const gc_node_ref_t>(&a, 1));
        break;
    }
    case Edit::Type: {
        const gc_node_ref_t value = draft.materializeStaticValue(
            camel::core::rtdata::toSlot<float>(1.0f),
            camel::core::type::Type::Float32());
        const gc_node_ref_t copy = draft.addCopyNode(camel::core::type::Type::Int32());
        draft.setNormInputs(copy, std::span<const gc_node_ref_t>(&value, 1));
        break;
    }
    case Edit::Ok:
        draft.addSyncNode();
        break;
    }
    try {
        const Program *next = session.commit();
        os << "EDGES accepted\n";
        return next;
    } catch (const std::exception &error) {
        os << "EDGES refused: " << error.what() << "\n";
        return program;
    }
}

} // namespace

const camel::runtime::Program *
EdgesCycleTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    return commitEdit(context_, program, os, Edit::Cycle);
}

const camel::runtime::Program *
EdgesTypeTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    return commitEdit(context_, program, os, Edit::Type);
}

const camel::runtime::Program *
EdgesOkTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    return commitEdit(context_, program, os, Edit::Ok);
}

const camel::runtime::Program *
CommitMergeTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    if (program == nullptr || program->root() == nullptr) {
        return program;
    }
    // A graph of the program the sessions reach, other than the root: the first session writes
    // the root, the second another graph, so the two edits do not meet.
    GCGraph *other = nullptr;
    {
        RuntimeGraphDraftSession probe(context_, program);
        for (GCGraph *graph : probe.collectReachableRuntimeGraphs()) {
            if (graph != program->root()) {
                other = graph;
                break;
            }
        }
    }
    if (other == nullptr) {
        os << "MERGE skip=1\n";
        return program;
    }
    const GraphId rootId      = program->root()->id();
    const GCGraph *rootBefore = program->graph(rootId);
    const GCGraph *otherBefore = program->graph(other->id());
    RuntimeGraphDraftSession first(context_, program);
    RuntimeGraphDraftSession second(context_, program);
    first.edit(program->root()).addSyncNode();
    second.edit(other).addSyncNode();
    const Program *one = first.commit();
    os << "MERGE stale=" << (context_->currentRuntimeProgram() == one ? 1 : 0) << "\n";
    // The second session started from the program the first replaced; its graph (and nothing it
    // wrote) changed in the program of the context, so the commit merges instead of forking.
    const Program *two = second.commit();
    const bool installed = context_->currentRuntimeProgram() == two;
    // The root the first commit made has one more node than the program it started from; the
    // merge keeps that version (or a version of the same content) and adds the second's graph.
    const GCGraph *mergedRoot  = two->graph(rootId);
    const GCGraph *mergedOther = two->graph(other->id());
    const bool keptFirst       = mergedRoot != nullptr && one->graph(rootId) != nullptr &&
                           mergedRoot->nodeCount() == one->graph(rootId)->nodeCount() &&
                           mergedRoot->nodeCount() > rootBefore->nodeCount();
    const bool keptSecond = mergedOther != nullptr && mergedOther != otherBefore;
    os << "MERGE installed=" << (installed ? 1 : 0) << " first_kept=" << (keptFirst ? 1 : 0)
       << " second_kept=" << (keptSecond ? 1 : 0)
       << " root_edited=" << (mergedRoot != rootBefore ? 1 : 0) << "\n";
    return two;
}

const camel::runtime::Program *
CommitConflictTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    if (program == nullptr || program->root() == nullptr) {
        return program;
    }
    GCGraph *other = nullptr;
    for (GCGraph *graph : program->graphs()) {
        if (graph != program->root()) {
            other = graph;
            break;
        }
    }
    RuntimeGraphDraftSession first(context_, program);
    RuntimeGraphDraftSession second(context_, program);
    RuntimeGraphDraftSession third(context_, program);
    first.edit(program->root()).addSyncNode();
    second.edit(program->root()).addSyncNode();
    if (other != nullptr) {
        third.edit(other);
    }
    os << "CONFLICTS before=" << second.conflicts().size() << "\n";
    const Program *next = first.commit();
    os << "CONFLICTS same_graph=" << second.conflicts().size()
       << " other_graph=" << (other != nullptr ? third.conflicts().size() : 0) << "\n";
    try {
        second.commitChecked();
        os << "CHECKED accepted\n";
    } catch (const GraphCommitConflict &conflict) {
        os << "CHECKED refused: " << conflict.graphs().size() << " graph(s)\n";
    }
    return next;
}
