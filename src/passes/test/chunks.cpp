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
 * Adds a node to the largest graph of the program and commits: the new version has to share every
 * node chunk but the last with the old one. Prints "CHUNKS shared=<n> of <m> ..." for the test
 * plan. (A change of a source position is not a change of content: the commit would keep the
 * old version.)
 */
#include "camel/passes/test/chunks.h"

#include "camel/runtime/draft_session.h"

const camel::runtime::Program *
ChunksTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    GCGraph *largest = nullptr;
    for (GCGraph *graph : program->graphs()) {
        if (largest == nullptr || graph->nodeCount() > largest->nodeCount()) {
            largest = graph;
        }
    }
    if (largest == nullptr || largest->nodeCount() == 0) {
        return program;
    }
    // A node nothing reads, after the others.
    RuntimeGraphDraftSession session(context_, program);
    GraphDraft &draft          = session.edit(largest);
    const gc_node_ref_t edited = draft.addSyncNode();
    const Program *next = session.commit();

    const GCGraph *after = next->graph(largest->id());
    const auto *before   = largest->nodePayload();
    const auto *now      = after ? after->nodePayload() : nullptr;
    size_t shared        = 0;
    for (gc_cnt_t c = 0; before && now && c < before->chunkCount && c < now->chunkCount; ++c) {
        shared += before->chunks[c] == now->chunks[c];
    }
    os << "CHUNKS shared=" << shared << " of " << (before ? before->chunkCount : 0)
       << " nodes=" << largest->nodeCount() << " edited_chunk=" << (edited >> kNodeChunkShift)
       << "\n";
    return next;
}
