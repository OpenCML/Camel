// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only: one node, one type -- and the reading that says a frozen graph never needs a second
 * place to look for it.
 *
 * A node's value has one type, and `GraphView::typeOf` is the one answer. Two readers used to keep
 * a branch of their own for the case "the record carries no type; it is at the node's static slot":
 * `onnx/passes/export.cpp`'s `constantOf` and `tiles/src/region_extract.cpp`'s `typeOf`. The claim
 * that branch rests on -- that such nodes exist -- was never read, so this pass reads it on every
 * node of every graph of a program:
 *
 *   - a node whose value sits in the static area (`staticIndexOf` answers an index) answers a type
 *     through the face, and it is the same pointer the graph's own static area holds at that index;
 *   - a node of the graph answers the record's type, which is what the encoder wrote: it folds the
 *     type a value has wherever it sits into `record.typeId` when it writes the graph
 *     (`effectiveDraftNodeType` then `typeIdOf`, runtime/graph.cpp), so a frozen graph cannot hold a
 *     node whose type is only at its static slot;
 *   - and a draft of that same graph -- the other representation of the same nodes -- answers the
 *     same type node by node, which is where the branch is live: a draft's `typeOf` reads the static
 *     slot when the header carries no type (draft_view.cpp).
 *
 *   [VT] graph I nodes=N static=S static-untyped=0 typed=T
 *        per graph: how many nodes were read, how many sit in the static area, how many of those
 *        answered no type (must be 0), and how many nodes answered a type at all.
 *   [VT] draft graph=I nodes=N differ=0
 *        the draft of graph I against the graph, node by node: how many answered a different type.
 *   [VT] agree=1 graphs=G nodes=N static=S untyped=0 typed=T draft-nodes=N draft-differ=0
 *        every reading above held, with the counts printed so a run that read no node cannot pass.
 *   [VT] node graph=I ref=R record=<type|none> view=<type|none> slot=<index|none>
 *        one node that differed, named by what each side answered.
 */

#include "camel/passes/test/view_types.h"

#include "camel/execute/pass/registration.h"
#include "camel/runtime/draft_session.h"
#include "graph/callee_graph_of.h"
#include "graph/draft_view.h"
#include "graph/gc_graph_view.h"

#include <string>

namespace {

using camel::graph::NodeRef;
using camel::runtime::GCGraph;

std::string nameOf(const camel::core::type::Type *type) {
    return type == nullptr ? std::string("none") : type->toString();
}

} // namespace

const camel::runtime::Program *
ViewTypesTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    if (program == nullptr || program->graphs().empty()) {
        return program;
    }
    const camel::graph::CalleeGraphOf calleeGraphOf =
        [program](camel::runtime::GraphId id) -> const camel::graph::Graph * {
        return static_cast<const camel::graph::Graph *>(program->graph(id));
    };
    const auto graphs = program->graphs();
    size_t nodes      = 0;
    size_t static_    = 0;
    size_t untyped    = 0;
    size_t typed      = 0;
    size_t differed   = 0;
    size_t draftNodes = 0;
    size_t draftDiffer = 0;
    for (size_t i = 0; i < graphs.size(); ++i) {
        const GCGraph *graph = graphs[i];
        if (graph == nullptr) {
            continue;
        }
        const camel::graph::FrozenGraphAdapter view(*graph, calleeGraphOf);
        size_t graphNodes  = 0;
        size_t graphStatic = 0;
        size_t graphNoType = 0;
        size_t graphTyped  = 0;
        const camel::core::type::TupleType *staticTypes = graph->staticDataType();
        for (camel::runtime::gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            const camel::runtime::GCNode *record = graph->node(ref);
            if (record == nullptr) {
                continue;
            }
            ++graphNodes;
            const NodeRef node              = static_cast<NodeRef>(ref);
            const auto answered             = view.typeOf(node);
            const auto index                = view.staticIndexOf(node);
            const bool sameAsRecord         = answered == record->dataType();
            if (answered != nullptr) {
                ++graphTyped;
            }
            if (index) {
                ++graphStatic;
                // The type the graph keeps at that slot, which is the answer the deleted branch
                // used to give; a node with a slot and no type on either side is what the branch
                // was written for, and it must not appear.
                const camel::core::type::Type *atSlot =
                    staticTypes != nullptr && *index < staticTypes->size()
                        ? staticTypes->typeAt(*index)
                        : nullptr;
                if (answered == nullptr && atSlot == nullptr) {
                    ++graphNoType;
                }
            }
            if (!sameAsRecord) {
                ++differed;
                os << "[VT] node graph=" << i << " ref=" << ref
                   << " record=" << nameOf(record->dataType())
                   << " view=" << nameOf(answered)
                   << " slot=" << (index ? std::to_string(*index) : std::string("none")) << "\n";
            }
        }
        nodes += graphNodes;
        static_ += graphStatic;
        untyped += graphNoType;
        typed += graphTyped;
        os << "[VT] graph " << i << " nodes=" << graphNodes << " static=" << graphStatic
           << " static-untyped=" << graphNoType << " typed=" << graphTyped << "\n";
        // The other representation of the same nodes: a draft of the graph answers the same type
        // per node. This is where a draft's static-slot branch is live (draft_view.cpp), so the
        // comparison is not two empty answers.
        camel::runtime::RuntimeGraphDraftSession session(context_, program);
        camel::runtime::GraphTransaction &transaction = session.edit(graphs[i]);
        camel::runtime::GraphDraft &draft             = transaction.edit();
        const camel::graph::DraftGraphAdapter draftView(draft);
        size_t draftGraphNodes = 0;
        size_t draftDiffered   = 0;
        for (camel::runtime::gc_node_ref_t ref = 0; ref < draft.nodeSlotCount(); ++ref) {
            const camel::runtime::DraftNodeHeader *header = draft.header(ref);
            if (header == nullptr) {
                continue;
            }
            ++draftGraphNodes;
            const NodeRef node                    = static_cast<NodeRef>(ref);
            const auto frozenAnswered             = view.typeOf(node);
            const camel::core::type::Type *answer = draftView.typeOf(node);
            if (answer != frozenAnswered) {
                ++draftDiffered;
            }
        }
        draftNodes += draftGraphNodes;
        draftDiffer += draftDiffered;
        os << "[VT] draft graph=" << i << " nodes=" << draftGraphNodes
           << " differ=" << draftDiffered << "\n";
    }
    os << "[VT] agree=" << (differed == 0 && untyped == 0 && draftDiffer == 0 ? 1 : 0)
       << " graphs=" << graphs.size() << " nodes=" << nodes << " static=" << static_
       << " untyped=" << untyped << " typed=" << typed << " draft-nodes=" << draftNodes
       << " draft-differ=" << draftDiffer << "\n";
    return program;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The test-only pass this file implements, under the name its header declares.
const PassKernel viewTypesKernel(kViewTypesTestPass, makePass<ViewTypesTestPass>());

} // namespace