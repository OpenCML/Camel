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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The read operators of the graph module. They read the frozen graphs of a Program directly and
 * are pure: a Program never changes.
 */

#include "read.h"

#include "handle.h"
#include "nodekind.h"

#include "camel/core/error/runtime.h"
#include "camel/core/node_attrs.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/rtdata/variant.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include <optional>
#include <span>

using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using camel::core::OperatorTable;
using camel::runtime::GCGraph;
using camel::runtime::GCNode;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::GraphId;
using camel::runtime::Program;

namespace camel::graphmod {

namespace {

namespace mm = camel::core::mm;
namespace err = camel::core::error;

// ---- the arguments ----

const Program *programArg(ArgsView &norm, size_t index) {
    return norm.get<const Program *>(index);
}

/// The graph a Graph or Node handle names; a fault when the Program holds no such graph.
const GCGraph &graphOf(const Handle *handle, const char *site) {
    const GCGraph *graph =
        handle->program() != nullptr ? handle->program()->graph(handle->graph()) : nullptr;
    if (graph == nullptr) {
        err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, site, "the graph is not in its program");
    }
    return *graph;
}

/// The node a Node handle names; a fault when the graph has no such node or the node is dead.
const GCNode &nodeOf(const Handle *handle, const char *site) {
    const GCGraph &graph = graphOf(handle, site);
    const GCNode *node   = graph.containsNodeRef(handle->index()) ? graph.node(handle->index()) : nullptr;
    if (node == nullptr || node->kind == GCNodeKind::Dead) {
        err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, site, "the node is not in its graph");
    }
    return *node;
}

Handle *graphHandle(const Program *program, GraphId graph) {
    return Handle::create(HandleKind::Graph, program, graph, 0, nullptr, mm::autoSpace());
}

Handle *nodeHandle(const Program *program, GraphId graph, uint32_t node) {
    return Handle::create(HandleKind::Node, program, graph, node, nullptr, mm::autoSpace());
}

/// An array of the handles in `items`, of the array type of `kind`.
slot_t handleArray(HandleKind kind, const std::vector<Handle *> &items) {
    ArrayType *arrayType = ArrayType::create(HandleType::get(kind));
    Array *array         = Array::create(mm::autoSpace(), items.size());
    for (size_t i = 0; i < items.size(); ++i) {
        array->set<Object *>(i, items[i], arrayType);
    }
    return toSlot(array);
}

slot_t stringSlot(std::string_view text) { return toSlot(String::from(std::string(text), mm::autoSpace())); }

/// The result type `T?` for a handle type or a scalar.
TupleType *nullableOf(Type *payload) { return TupleType::nullable(payload); }

// ---- the program ----

slot_t graphsKernel(ArgsView &, ArgsView &norm, Context &) {
    const Program *program = programArg(norm, 0);
    std::vector<Handle *> items;
    for (const GCGraph *graph : program->graphs()) {
        items.push_back(graphHandle(program, graph->id()));
    }
    return handleArray(HandleKind::Graph, items);
}

slot_t rootKernel(ArgsView &, ArgsView &norm, Context &) {
    const Program *program = programArg(norm, 0);
    const GCGraph *root    = program->root();
    if (root == nullptr) {
        err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, "graph.root", "the program has no root");
    }
    return toSlot(graphHandle(program, root->id()));
}

// ---- a graph ----

slot_t nameKernel(ArgsView &, ArgsView &norm, Context &) {
    return stringSlot(graphOf(norm.get<Handle *>(0), "graph.name").name());
}

slot_t nodesKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle   = norm.get<Handle *>(0);
    const GCGraph &graph = graphOf(handle, "graph.nodes");
    std::vector<Handle *> items;
    for (const GCNode *node : graph.nodes()) {
        if (node->kind == GCNodeKind::Dead) {
            continue;
        }
        items.push_back(nodeHandle(handle->program(), handle->graph(), graph.nodeRef(node)));
    }
    return handleArray(HandleKind::Node, items);
}

// ---- a node ----

slot_t inputsOf(ArgsView &norm, const char *site, int group) {
    const auto *handle   = norm.get<Handle *>(0);
    const GCGraph &graph = graphOf(handle, site);
    nodeOf(handle, site);
    const std::span<const camel::runtime::gc_node_ref_t> refs =
        group == 0   ? graph.normInputsOf(handle->index())
        : group == 1 ? graph.withInputsOf(handle->index())
                     : graph.ctrlInputsOf(handle->index());
    std::vector<Handle *> items;
    for (auto ref : refs) {
        items.push_back(nodeHandle(handle->program(), handle->graph(), ref));
    }
    return handleArray(HandleKind::Node, items);
}

slot_t inputsKernel(ArgsView &, ArgsView &norm, Context &) { return inputsOf(norm, "graph.inputs", 0); }
slot_t withInputsKernel(ArgsView &, ArgsView &norm, Context &) {
    return inputsOf(norm, "graph.with_inputs", 1);
}
slot_t ctrlInputsKernel(ArgsView &, ArgsView &norm, Context &) {
    return inputsOf(norm, "graph.ctrl_inputs", 2);
}

/// The nodes that read node `n` as an input of any of the three groups, once each, in NodeId
/// order.
slot_t usersKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle   = norm.get<Handle *>(0);
    const GCGraph &graph = graphOf(handle, "graph.users");
    nodeOf(handle, "graph.users");
    std::vector<camel::runtime::gc_node_ref_t> users;
    for (auto refs :
         {graph.normOutputsOf(handle->index()),
          graph.withOutputsOf(handle->index()),
          graph.ctrlOutputsOf(handle->index())}) {
        users.insert(users.end(), refs.begin(), refs.end());
    }
    std::sort(users.begin(), users.end());
    users.erase(std::unique(users.begin(), users.end()), users.end());
    std::vector<Handle *> items;
    for (auto ref : users) {
        items.push_back(nodeHandle(handle->program(), handle->graph(), ref));
    }
    return handleArray(HandleKind::Node, items);
}

slot_t kindKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle = norm.get<Handle *>(0);
    const GCNode &node = nodeOf(handle, "graph.kind");
    const auto tag     = nodeKindTag(node.kind);
    if (!tag) {
        err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, "graph.kind", "the node has no kind a program shows");
    }
    return toSlot(Variant::create(*tag, 0, mm::autoSpace()));
}

/// The operator of an OPER node, null for any other node.
slot_t opKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle   = norm.get<Handle *>(0);
    const GCNode &node   = nodeOf(handle, "graph.op");
    const GCGraph &graph = graphOf(handle, "graph.op");
    if (node.kind != GCNodeKind::Oper || !node.hasPayload()) {
        return NullSlot;
    }
    const auto *body = graph.nodeBodyAs<GCOperBody>(handle->index());
    Handle *op = Handle::create(HandleKind::Op, nullptr, GraphId::Invalid, body->oper, nullptr, mm::autoSpace());
    return makeSome(toSlot(op), nullableOf(HandleType::get(HandleKind::Op)));
}

slot_t opNameKernel(ArgsView &, ArgsView &norm, Context &) {
    return stringSlot(OperatorTable::instance().uri(norm.get<Handle *>(0)->index()));
}

slot_t isPureKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle   = norm.get<Handle *>(0);
    const GCNode &node   = nodeOf(handle, "graph.is_pure");
    const GCGraph &graph = graphOf(handle, "graph.is_pure");
    const bool pure      = node.kind == GCNodeKind::Oper && node.hasPayload() &&
                      OperatorTable::instance().isPure(graph.nodeBodyAs<GCOperBody>(handle->index())->oper);
    return toSlot<Bool>(pure);
}

slot_t typeOfKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle = norm.get<Handle *>(0);
    Type *type         = nodeOf(handle, "graph.type_of").dataType();
    if (type == nullptr) {
        return NullSlot;
    }
    Handle *ref = Handle::create(HandleKind::TypeRef, nullptr, GraphId::Invalid, 0, type, mm::autoSpace());
    return makeSome(toSlot(ref), nullableOf(HandleType::get(HandleKind::TypeRef)));
}

slot_t typeNameKernel(ArgsView &, ArgsView &norm, Context &) {
    return stringSlot(norm.get<Handle *>(0)->typeRef()->toString());
}

/// The graph a CALL or FUNC node names, null for any other node.
slot_t calleeKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle   = norm.get<Handle *>(0);
    const GCNode &node   = nodeOf(handle, "graph.callee");
    const GCGraph &graph = graphOf(handle, "graph.callee");
    if (node.kind != GCNodeKind::Call && node.kind != GCNodeKind::Func) {
        return NullSlot;
    }
    const GraphId callee = graph.calleeIdOf(handle->index());
    if (!isValid(callee) || handle->program()->graph(callee) == nullptr) {
        return NullSlot;
    }
    return makeSome(toSlot(graphHandle(handle->program(), callee)), nullableOf(HandleType::get(HandleKind::Graph)));
}

slot_t tagsKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle   = norm.get<Handle *>(0);
    nodeOf(handle, "graph.tags");
    const GCGraph &graph = graphOf(handle, "graph.tags");
    return stringSlot(camel::core::attr::text(graph.nodeAttrs(handle->index())));
}

slot_t showKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle   = norm.get<Handle *>(0);
    const GCNode &node   = nodeOf(handle, "graph.show");
    const GCGraph &graph = graphOf(handle, "graph.show");
    std::string text     = "%" + std::to_string(handle->index()) + " ";
    const auto tag       = nodeKindTag(node.kind);
    text += tag ? nodeKindName(*tag) : "?";
    if (node.kind == GCNodeKind::Oper && node.hasPayload()) {
        text += " " + std::string(graph.nodeBodyAs<GCOperBody>(handle->index())->uri());
    }
    if (const Type *type = node.dataType()) {
        text += " : " + type->toString();
    }
    return stringSlot(text);
}


// ---- the tables ----

struct ReadOp {
    const char *name;
    operator_t kernel;
    Type *param;
    Type *result;
};

const std::vector<ReadOp> &readOps() {
    static const std::vector<ReadOp> ops = [] {
        const auto handle = [](HandleKind kind) -> Type * { return HandleType::get(kind); };
        const auto array  = [&](HandleKind kind) -> Type * {
            return ArrayType::create(HandleType::get(kind));
        };
        const auto maybe = [&](HandleKind kind) -> Type * {
            return TupleType::nullable(HandleType::get(kind));
        };
        Type *program = handle(HandleKind::Program);
        Type *graph   = handle(HandleKind::Graph);
        Type *node    = handle(HandleKind::Node);
        return std::vector<ReadOp>{
            {"graphs", graphsKernel, program, array(HandleKind::Graph)},
            {"root", rootKernel, program, graph},
            {"name", nameKernel, graph, Type::String()},
            {"nodes", nodesKernel, graph, array(HandleKind::Node)},
            {"inputs", inputsKernel, node, array(HandleKind::Node)},
            {"with_inputs", withInputsKernel, node, array(HandleKind::Node)},
            {"ctrl_inputs", ctrlInputsKernel, node, array(HandleKind::Node)},
            {"users", usersKernel, node, array(HandleKind::Node)},
            {"kind", kindKernel, node, nodeKindType()},
            {"op", opKernel, node, maybe(HandleKind::Op)},
            {"op_name", opNameKernel, handle(HandleKind::Op), Type::String()},
            {"is_pure", isPureKernel, node, Type::Bool()},
            {"type_of", typeOfKernel, node, maybe(HandleKind::TypeRef)},
            {"type_name", typeNameKernel, handle(HandleKind::TypeRef), Type::String()},
            {"callee", calleeKernel, node, maybe(HandleKind::Graph)},
            {"tags", tagsKernel, node, Type::String()},
            {"show", showKernel, node, Type::String()},
        };
    }();
    return ops;
}

} // namespace

const std::vector<oper_group_ptr_t> &readGroups() {
    static const std::vector<oper_group_ptr_t> groups = [] {
        std::vector<oper_group_ptr_t> out;
        for (const ReadOp &op : readOps()) {
            out.push_back(OperatorGroup::create(
                op.name,
                {{std::string("graph:") + op.name,
                  StaticFuncTypeResolver::create({}, {{op.param, false}}, op.result)}}));
        }
        return out;
    }();
    return groups;
}

const std::unordered_map<std::string, operator_t> &readKernels() {
    static const std::unordered_map<std::string, operator_t> kernels = [] {
        std::unordered_map<std::string, operator_t> out;
        for (const ReadOp &op : readOps()) {
            out.emplace(op.name, op.kernel);
        }
        return out;
    }();
    return kernels;
}

} // namespace camel::graphmod
