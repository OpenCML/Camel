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
 * Created: Oct. 21, 2024
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Shared semantic labels and types for the DOT and JSON GIR exporters.
#pragma once
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"
#include <string>
namespace camel::passes::gir {
struct PortInfo {
    std::string name;
    std::string kind;
    size_t index = 0;
};
const camel::runtime::GCNode *
lookupRecord(camel::runtime::GCGraph *, camel::runtime::gc_node_ref_t);
camel::core::type::Type *
nodeStaticSlotType(camel::runtime::GCGraph *, const camel::runtime::GCNode &);
PortInfo portInfo(camel::runtime::GCGraph *, camel::runtime::gc_node_ref_t);
std::string graphTooltip(camel::runtime::GCGraph *);
std::string recordTooltip(camel::runtime::GCGraph *, const camel::runtime::GCNode &);
std::string recordLabel(
    const camel::runtime::Program &, camel::runtime::GCGraph *, camel::runtime::gc_node_ref_t,
    const camel::runtime::GCNode &);
std::string recordType(const camel::runtime::GCNode &);
} // namespace camel::passes::gir
