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
 * A function value of the heap keeps its Program: the pass makes one of the largest graph, has
 * the program replaced by a commit, collects the graph space between passes, and reads through
 * the function value. Prints "KEEPFN alive=<0|1> graph=<name>" for the test plan.
 */
#include "camel/passes/test/keepfn.h"

#include "camel/core/context/context.h"
#include "camel/core/mm.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft_session.h"

const camel::runtime::Program *
KeepFnTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    GCGraph *largest = nullptr;
    for (GCGraph *graph : program->graphs()) {
        if (largest == nullptr || graph->nodeCount() > largest->nodeCount()) {
            largest = graph;
        }
    }
    if (largest == nullptr) {
        return program;
    }
    // A program the pipeline made (not the one the context was given), then the function value
    // of its graph, then the program that replaces it: nothing but the function value holds the
    // first when the graph space is collected.
    RuntimeGraphDraftSession first(context_, program);
    first.edit(largest).addSyncNode();
    const Program *middle = first.commit();
    context_->settleRuntimeGraphs(middle);
    GCGraph *edited = middle->graph(largest->id());

    auto &heap = camel::core::mm::autoSpace();
    auto *emptyClosure =
        camel::core::type::TupleType::create(std::vector<camel::core::type::Type *>{});
    auto *function = ::Function::create(middle, edited->id(), emptyClosure, heap);
    camel::core::mm::RootHandle held(heap, function, edited->funcType(), "test::keepfn");

    RuntimeGraphDraftSession second(context_, middle);
    second.edit(edited).addSyncNode();
    const Program *next = second.commit();
    context_->settleRuntimeGraphs(next);
    context_->collectRuntimeGraphs();

    const auto *kept = held.getAs<::Function>();
    const GCGraph *graph = kept->graph();
    const bool alive = camel::core::mm::graphSpace().isAllocated(
                           const_cast<Program *>(kept->program())) &&
                       graph != nullptr && kept->program()->contains(graph);
    os << "KEEPFN alive=" << (alive ? 1 : 0)
       << " graph=" << (graph != nullptr ? graph->name() : "?") << "\n";
    return next;
}
