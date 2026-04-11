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
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Runtime draft inline helpers.
 *
 * This layer turns the low-level clone primitive into a reusable inline splice
 * operation on GraphDraft. Passes should not manually clone a callee, bind
 * ports, redirect users, and erase the old FUNC node in ad-hoc order; that
 * sequence is now centralized here.
 */

#pragma once

#include "camel/runtime/draft.h"

namespace camel::runtime {

struct DraftInlineResult {
    gc_node_ref_t callNode  = kInvalidNodeRef;
    gc_node_ref_t valueExit = kInvalidNodeRef;
    gc_node_ref_t ctrlEntry = kInvalidNodeRef;
    gc_node_ref_t ctrlExit  = kInvalidNodeRef;

    explicit operator bool() const { return valueExit != kInvalidNodeRef; }
};

DraftInlineResult inlineCallableInDraft(GraphDraft &draft, gc_node_ref_t funcNodeId);

} // namespace camel::runtime
