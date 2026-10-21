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
