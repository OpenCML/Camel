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
 * The operator semantics of an OPER node of a frozen graph (see operator_instance.h).
 */

#include "camel/runtime/operator_instance.h"

#include "camel/core/operator_table.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/base.h"

#include <vector>

namespace camel::runtime {

namespace core = camel::core;
namespace type = camel::core::type;

namespace {

/// True for a value that is not an object: nothing can change it in place.
bool isPlainScalar(type::Type *t) {
    switch (t->code()) {
    case type::TypeCode::Int32:
    case type::TypeCode::Int64:
    case type::TypeCode::Float32:
    case type::TypeCode::Float64:
    case type::TypeCode::Bool:
        return true;
    default:
        return false;
    }
}

/// The value of `ref` when it is a constant: a static DATA node, also behind GATEs. An object
/// (an int array) counts only when every user of the DATA node is a pure operator.
std::optional<slot_t> staticValueOf(const GCGraph &graph, gc_node_ref_t ref) {
    for (gc_cnt_t hops = 0; hops < graph.nodeCount(); ++hops) {
        const GCNode *n = graph.node(ref);
        if (n->kind != GCNodeKind::Gate || graph.normInputsOf(ref).empty()) {
            break;
        }
        ref = graph.normInputsOf(ref).back();
    }
    const GCNode *n = graph.node(ref);
    if (n->kind != GCNodeKind::Data || n->dataIndex >= 0 || n->dataType() == nullptr) {
        return std::nullopt;
    }
    if (!isPlainScalar(n->dataType())) {
        const auto &table = core::OperatorTable::instance();
        for (gc_node_ref_t user : graph.normOutputsOf(ref)) {
            const GCNode *u = graph.node(user);
            if (u->kind != GCNodeKind::Oper ||
                !table.isPure(graph.nodeBodyAs<GCOperBody>(user)->oper)) {
                return std::nullopt;
            }
        }
        if (!graph.withOutputsOf(ref).empty()) {
            return std::nullopt;
        }
    }
    return graph.staticArea()->get<slot_t>(static_cast<size_t>(-n->dataIndex));
}

} // namespace

std::optional<core::sem::IndexedInstance> instanceOf(const GCGraph &graph, gc_node_ref_t node) {
    if (!graph.containsNodeRef(node) || graph.node(node)->kind != GCNodeKind::Oper) {
        return std::nullopt;
    }
    std::vector<type::Type *> norm, with;
    std::vector<type::StaticArg> statics;
    for (gc_node_ref_t in : graph.normInputsOf(node)) {
        type::Type *t = graph.node(in)->dataType();
        if (t == nullptr) {
            return std::nullopt;
        }
        norm.push_back(t);
        statics.emplace_back(staticValueOf(graph, in));
    }
    for (gc_node_ref_t in : graph.withInputsOf(node)) {
        type::Type *t = graph.node(in)->dataType();
        if (t == nullptr) {
            return std::nullopt;
        }
        with.push_back(t);
    }
    return core::sem::instantiate(
        graph.nodeBodyAs<GCOperBody>(node)->oper,
        core::sem::InstanceContext{.norm = norm, .with = with, .statics = statics});
}

} // namespace camel::runtime
