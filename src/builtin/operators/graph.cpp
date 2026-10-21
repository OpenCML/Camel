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
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The operators of graph.h.
 */

#include "graph.h"

#include "camel/core/context/context.h"
#include "camel/core/mm.h"
#include "camel/core/mm/gen.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

using namespace camel::core::context;

slot_t __graph_original__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    auto *function = norm.get<::Function *>(0);
    // The program the function's program is a private copy of (Program::original), and the
    // graph of it that this function's graph was copied from.
    const camel::runtime::Program *original = function != nullptr && function->program() != nullptr
                                                  ? function->program()->original()
                                                  : nullptr;
    if (original == nullptr || original->graph(function->graphId()) == nullptr) {
        return norm.slot(0);
    }
    // `function` is a raw pointer held across the allocation below: nothing may move meanwhile.
    camel::core::mm::NoCollectionScope noCollection;
    const auto *closureType = function->tupleType();
    auto *result            = ::Function::create(
        original,
        function->graphId(),
        closureType,
        camel::core::mm::autoSpace());
    if (const ::Tuple *closure = function->tuple()) {
        for (size_t i = 0; i < closure->size(); ++i) {
            result->tuple()->set<slot_t>(i, closure->get<slot_t>(i), closureType);
        }
    }
    return camel::core::rtdata::toSlot<::Function *>(result);
}
