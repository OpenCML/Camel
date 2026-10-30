// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The read operators of the graph module. They read the frozen graphs of a Program directly and
 * are pure: a Program never changes.
 */

#include "read.h"

#include "handle.h"
#include "nodekind.h"
#include "regionkind.h"

#include "camel/core/error/runtime.h"
#include "camel/core/node_attrs.h"
#include "camel/core/operator_table.h"
#include "camel/core/tags.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/rtdata/variant.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_writes.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/graph_txn.h"
#include "camel/runtime/program.h"
#include "camel/runtime/reachable.h"
#include "camel/utils/assert.h"

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>

using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using camel::core::OperatorTable;
using camel::runtime::DraftNodeHeader;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GCGraph;
using camel::runtime::GCNode;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::GCRegion;
using camel::runtime::GraphDraft;
using camel::runtime::GraphId;
using camel::runtime::kInvalidNodeRef;
using camel::runtime::Program;

namespace camel::graphmod {

/// Whether `constant` reads values of this type: the scalars and the string, whose slot is the
/// value (or a reference the graph keeps alive).
bool constantType(const Type *type) {
    switch (type->code()) {
    case TypeCode::Int32:
    case TypeCode::Int64:
    case TypeCode::Float32:
    case TypeCode::Float64:
    case TypeCode::Bool:
    case TypeCode::String:
        return true;
    default:
        return false;
    }
}

namespace {

namespace mm  = camel::core::mm;
namespace err = camel::core::error;

// ---- the arguments ----

/// The graph a Graph or Node handle names; a fault when the Program holds no such graph.
const GCGraph &graphOf(const Handle *handle, const char *site) {
    const GCGraph *graph =
        handle->program() != nullptr ? handle->program()->graph(handle->graph()) : nullptr;
    if (graph == nullptr) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            site,
            "the graph is not in its program");
    }
    return *graph;
}

/// The node of a frozen graph; a fault when the graph has no such node or the node is dead.
const GCNode &nodeOfFrozen(const Handle *handle, const char *site) {
    const GCGraph &graph = graphOf(handle, site);
    const GCNode *node =
        graph.containsNodeRef(handle->index()) ? graph.node(handle->index()) : nullptr;
    if (node == nullptr || node->kind == GCNodeKind::Dead) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            site,
            "the node is not in its graph");
    }
    return *node;
}

/// Reads that belong to the frozen graphs only (a region, an operator, a transaction) go through
/// this name, which is the frozen reader: there is nothing of them in a draft.
const GCNode &nodeOf(const Handle *handle, const char *site) {
    return nodeOfFrozen(handle, site);
}

/// A node of a frozen graph or of the draft being rewritten (a rule condition reads the draft).
/// Everything the read operators ask about a node is here; the rest of the file picks the
/// source.
struct NodeView {
    const GCNode *frozen               = nullptr;
    const DraftNodeHeader *draftHeader = nullptr;
    const GraphDraft *draft            = nullptr;
    gc_node_ref_t ref                  = kInvalidNodeRef;

    bool isDraft() const { return draft != nullptr; }
    GCNodeKind kind() const { return isDraft() ? draftHeader->kind : frozen->kind; }
    Type *dataType() const { return isDraft() ? draftHeader->dataType : frozen->dataType(); }
    int64_t dataIndex() const { return isDraft() ? draftHeader->dataIndex : frozen->dataIndex; }
    bool hasPayload() const {
        return isDraft() ? !draft->payloadOf(ref).empty() : frozen->hasPayload();
    }
};

NodeView viewOf(const Handle *handle, const char *site) {
    NodeView view;
    // A node is in the draft exactly when its handle names the transaction that owns the draft:
    // there is no other way to be in a draft, and the handle keeps no draft pointer of its own.
    const auto *transaction = handle->transaction();
    const auto *draft = transaction != nullptr ? &transaction->graph() : nullptr;
    if (draft != nullptr) {
        view.draft       = draft;
        view.ref         = handle->index();
        view.draftHeader = draft->header(view.ref);
        if (view.draftHeader == nullptr || !draft->alive(view.ref) ||
            view.draftHeader->kind == GCNodeKind::Dead) {
            err::throwRuntimeFault(
                err::RuntimeDiag::InvalidArgument,
                site,
                "the node is not in its graph");
        }
        return view;
    }
    view.frozen = &nodeOfFrozen(handle, site);
    return view;
}

Handle *graphHandle(const Program *program, GraphId graph) {
    return Handle::create(HandleKind::Graph, program, graph, 0, nullptr, mm::autoSpace());
}

Handle *nodeHandle(const Program *program, GraphId graph, uint32_t node) {
    return Handle::create(HandleKind::Node, program, graph, node, nullptr, mm::autoSpace());
}

/// The handle of a node read from `from`: the same draft when `from` names one, otherwise the
/// same program and graph. A draft is named by the transaction that owns it, so the handle made
/// here names that transaction and nothing else.
Handle *nodeHandle(const Handle *from, gc_node_ref_t node) {
    if (const auto *transaction = from->transaction()) {
        return Handle::createInTransaction(*transaction, node, mm::autoSpace());
    }
    return Handle::create(
        HandleKind::Node,
        from->program(),
        from->graph(),
        node,
        nullptr,
        mm::autoSpace());
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

slot_t stringSlot(std::string_view text) {
    return toSlot(String::from(std::string(text), mm::autoSpace()));
}

/// The result type `T?` for a handle type or a scalar.
TupleType *nullableOf(Type *payload) { return TupleType::nullable(payload); }

// ---- a function ----

/// The graphs of a function: its own and every graph it calls, directly or not, once each in the
/// order of their ids. The program they live in is the function's, and does not appear.
slot_t graphsOfKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *function   = norm.get<::Function *>(0);
    const Program *program = function->program();
    GCGraph *root          = function->graph();
    if (program == nullptr || root == nullptr) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "graph.graphs_of",
            "the function has no graph");
    }
    std::vector<GCGraph *> graphs = camel::runtime::collectReachableGraphs(*program, root);
    std::sort(graphs.begin(), graphs.end(), [](const GCGraph *a, const GCGraph *b) {
        return toValue(a->id()) < toValue(b->id());
    });
    std::vector<Handle *> items;
    for (const GCGraph *graph : graphs) {
        items.push_back(graphHandle(program, graph->id()));
    }
    return handleArray(HandleKind::Graph, items);
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
    const auto *handle  = norm.get<Handle *>(0);
    const NodeView view = viewOf(handle, site);
    std::vector<gc_node_ref_t> refs;
    const auto take = [&](std::span<const gc_node_ref_t> run) {
        refs.insert(refs.end(), run.begin(), run.end());
    };
    if (view.isDraft()) {
        switch (group) {
        case 0:
            take(view.draft->normInputsOf(view.ref));
            break;
        case 1:
            take(view.draft->withInputsOf(view.ref));
            break;
        case 2:
            take(view.draft->syncInputsOf(view.ref));
            break;
        default:
            take(view.draft->orderInputsOf(view.ref));
            break;
        }
    } else {
        const GCGraph &graph = graphOf(handle, site);
        switch (group) {
        case 0:
            take(graph.normInputsOf(handle->index()));
            break;
        case 1:
            take(graph.withInputsOf(handle->index()));
            break;
        case 2:
            for (const auto &edge : graph.syncPredsOf(handle->index())) {
                refs.push_back(edge.pred);
            }
            break;
        default:
            for (const auto &edge : graph.orderPredsOf(handle->index())) {
                refs.push_back(edge.pred);
            }
            break;
        }
    }
    std::vector<Handle *> items;
    for (auto ref : refs) {
        items.push_back(nodeHandle(handle, ref));
    }
    return handleArray(HandleKind::Node, items);
}

slot_t inputsKernel(ArgsView &, ArgsView &norm, Context &) {
    return inputsOf(norm, "graph.inputs", 0);
}
slot_t withInputsKernel(ArgsView &, ArgsView &norm, Context &) {
    return inputsOf(norm, "graph.with_inputs", 1);
}
/// The statements written before the node in the same `sync` block, whose "used" reaches it.
slot_t syncInputsKernel(ArgsView &, ArgsView &norm, Context &) {
    return inputsOf(norm, "graph.sync_inputs", 2);
}
/// The nodes that must run before it when both run, along an edge that carries no "used".
slot_t orderInputsKernel(ArgsView &, ArgsView &norm, Context &) {
    return inputsOf(norm, "graph.order_inputs", 3);
}

/// The nodes that read node `n` as an input of any of the three groups, once each, in NodeId
/// order.
/// The nodes that read the value of node `n` (as an ordinary or a with input), once each, in
/// NodeId order.
slot_t valueUsersKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle  = norm.get<Handle *>(0);
    const NodeView view = viewOf(handle, "graph.value_users");
    std::vector<gc_node_ref_t> users;
    if (view.isDraft()) {
        for (auto refs : {view.draft->normUsersOf(view.ref), view.draft->withUsersOf(view.ref)}) {
            users.insert(users.end(), refs.begin(), refs.end());
        }
    } else {
        const GCGraph &graph = graphOf(handle, "graph.value_users");
        for (auto refs :
             {graph.normOutputsOf(handle->index()), graph.withOutputsOf(handle->index())}) {
            users.insert(users.end(), refs.begin(), refs.end());
        }
    }
    std::sort(users.begin(), users.end());
    users.erase(std::unique(users.begin(), users.end()), users.end());
    std::vector<Handle *> items;
    for (auto ref : users) {
        items.push_back(nodeHandle(handle, ref));
    }
    return handleArray(HandleKind::Node, items);
}

/// Whether the node is a constant: a DATA node whose value is a static slot of the graph.
slot_t isConstantKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle  = norm.get<Handle *>(0);
    const NodeView node = viewOf(handle, "graph.is_constant");
    return toSlot<Bool>(node.kind() == GCNodeKind::Data && node.dataIndex() < 0);
}

slot_t usersKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle  = norm.get<Handle *>(0);
    const NodeView view = viewOf(handle, "graph.users");
    std::vector<gc_node_ref_t> users;
    if (view.isDraft()) {
        for (auto refs :
             {view.draft->normUsersOf(view.ref),
              view.draft->withUsersOf(view.ref),
              view.draft->ctrlUsersOf(view.ref)}) {
            users.insert(users.end(), refs.begin(), refs.end());
        }
    } else {
        const GCGraph &graph = graphOf(handle, "graph.users");
        for (auto refs :
             {graph.normOutputsOf(handle->index()),
              graph.withOutputsOf(handle->index()),
              graph.ctrlOutputsOf(handle->index())}) {
            users.insert(users.end(), refs.begin(), refs.end());
        }
    }
    std::sort(users.begin(), users.end());
    users.erase(std::unique(users.begin(), users.end()), users.end());
    std::vector<Handle *> items;
    for (auto ref : users) {
        items.push_back(nodeHandle(handle, ref));
    }
    return handleArray(HandleKind::Node, items);
}

slot_t kindKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle  = norm.get<Handle *>(0);
    const NodeView node = viewOf(handle, "graph.kind");
    const auto tag      = nodeKindTag(node.kind());
    if (!tag) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "graph.kind",
            "the node has no kind a program shows");
    }
    return toSlot(Variant::create(*tag, 0, mm::autoSpace()));
}

/// The operator of an OPER node, null for any other node.
slot_t opKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle  = norm.get<Handle *>(0);
    const NodeView node = viewOf(handle, "graph.op");
    if (node.kind() != GCNodeKind::Oper || !node.hasPayload()) {
        return NullSlot;
    }
    camel::core::OperId oper = camel::core::kNoOper;
    if (node.isDraft()) {
        const auto bytes = node.draft->payloadOf(node.ref);
        if (bytes.size() < sizeof(GCOperBody)) {
            return NullSlot;
        }
        oper = reinterpret_cast<const GCOperBody *>(bytes.data())->oper;
    } else {
        oper = graphOf(handle, "graph.op").nodeBodyAs<GCOperBody>(handle->index())->oper;
    }
    Handle *op =
        Handle::create(HandleKind::Op, nullptr, GraphId::Invalid, oper, nullptr, mm::autoSpace());
    return makeSome(toSlot(op), nullableOf(HandleType::get(HandleKind::Op)));
}

slot_t opNameKernel(ArgsView &, ArgsView &norm, Context &) {
    return stringSlot(OperatorTable::instance().uri(norm.get<Handle *>(0)->index()));
}

slot_t isPureKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle  = norm.get<Handle *>(0);
    const NodeView node = viewOf(handle, "graph.is_pure");
    bool pure           = false;
    if (node.kind() == GCNodeKind::Oper && node.hasPayload()) {
        camel::core::OperId oper = camel::core::kNoOper;
        if (node.isDraft()) {
            const auto bytes = node.draft->payloadOf(node.ref);
            if (bytes.size() >= sizeof(GCOperBody)) {
                oper = reinterpret_cast<const GCOperBody *>(bytes.data())->oper;
            }
        } else {
            oper = graphOf(handle, "graph.is_pure").nodeBodyAs<GCOperBody>(handle->index())->oper;
        }
        pure = OperatorTable::instance().isPure(oper);
    }
    return toSlot<Bool>(pure);
}

slot_t typeOfKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle = norm.get<Handle *>(0);
    Type *type         = viewOf(handle, "graph.type_of").dataType();
    if (type == nullptr) {
        return NullSlot;
    }
    Handle *ref =
        Handle::create(HandleKind::TypeRef, nullptr, GraphId::Invalid, 0, type, mm::autoSpace());
    return makeSome(toSlot(ref), nullableOf(HandleType::get(HandleKind::TypeRef)));
}

slot_t typeNameKernel(ArgsView &, ArgsView &norm, Context &) {
    return stringSlot(norm.get<Handle *>(0)->typeRef()->toString());
}

/// The graph a CALL or FUNC node names, null for any other node. A node of the draft being
/// rewritten names no graph: a draft is not a published version, so a condition cannot follow a
/// call (and a rule's pattern cannot match one).
slot_t calleeKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle = norm.get<Handle *>(0);
    if (handle->transaction() != nullptr) {
        return NullSlot;
    }
    const GCNode &node   = nodeOfFrozen(handle, "graph.callee");
    const GCGraph &graph = graphOf(handle, "graph.callee");
    if (node.kind != GCNodeKind::Call && node.kind != GCNodeKind::Func) {
        return NullSlot;
    }
    const GraphId callee = graph.calleeIdOf(handle->index());
    if (!isValid(callee) || handle->program()->graph(callee) == nullptr) {
        return NullSlot;
    }
    return makeSome(
        toSlot(graphHandle(handle->program(), callee)),
        nullableOf(HandleType::get(HandleKind::Graph)));
}

slot_t tagsKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle   = norm.get<Handle *>(0);
    const NodeView node  = viewOf(handle, "graph.tags");
    // The one query for the facts of a node (camel/core/tags.h): the attributes of a node are its
    // tags.
    const auto facts = node.isDraft()
                           ? camel::core::tag::factsOf(*node.draft, node.ref)
                           : camel::core::tag::factsOf(graphOf(handle, "graph.tags"), handle->index());
    return stringSlot(camel::core::attr::text(facts.tags));
}

// ---- the rules a declaration writes ----

namespace attr = camel::core::attr;

/// Whether a fact's value names a function: a graph reference, or a tuple with one in it. Only a
/// tag whose parameter is a function makes such a value, so this is what tells a rule tag from
/// every other fact (`#strict`, a numeric permission, `#impl(kernel)`).
bool namesAFunction(attr::AttrValueId id) {
    const attr::Value value = attr::value(id);
    if (value.kind == attr::ValueKind::Graph) {
        return true;
    }
    if (value.kind != attr::ValueKind::Tuple) {
        return false;
    }
    for (const attr::AttrValueId item : value.items) {
        if (namesAFunction(item)) {
            return true;
        }
    }
    return false;
}

/// The facts of the declaration the function of this graph is whose value names a function
/// (`#jvp(rule)`, `#transpose(rule)`, `#vjp(rule)`), ordered by the tag's qualified name as
/// attr::entries orders them. The declaration's facts are cold metadata of the graph
/// (GCGraph::declarationFacts): a declaration fact is on no node (`attr::fateOf`), so a pass reads
/// it through the function. A graph that is no declaration, and a declaration that writes none,
/// have none.
std::vector<attr::Attr> ruleFactsOf(const GCGraph &graph) {
    std::vector<attr::Attr> facts;
    for (const attr::Attr &entry : attr::entries(graph.declarationFacts())) {
        if (namesAFunction(entry.value)) {
            facts.push_back(entry);
        }
    }
    return facts;
}

/// The tag a fact belongs to: its attribute key is the tag's qualified name, and that is how the
/// declaration is found back (the name a program writes, `jvp`, is `Decl::ident`).
const camel::core::tag::Decl *tagOf(const attr::Attr &fact) {
    const auto id = camel::core::tag::find(std::string(attr::keyName(fact.key)));
    return id.has_value() ? camel::core::tag::get(*id) : nullptr;
}

/// Whether the fact is the one a program writes as `name`: the tag's name alone (`jvp`) or its
/// qualified name (`autodiff::jvp`).
bool namesTag(const attr::Attr &fact, const camel::core::tag::Decl *decl, std::string_view name) {
    if (name == attr::keyName(fact.key)) {
        return true;
    }
    return decl != nullptr && name == decl->ident;
}

/// The graphs a fact's value names, in the order the tag's arguments were written. A value that is
/// not a graph reference names none.
std::vector<GraphId> graphsOf(attr::AttrValueId id) {
    std::vector<GraphId> graphs;
    const attr::Value value = attr::value(id);
    if (value.kind == attr::ValueKind::Graph) {
        // The value is the graph's id as text (attr::graphRef); a text that is not a number names
        // no graph this program has.
        try {
            graphs.push_back(static_cast<GraphId>(std::stoul(value.text)));
        } catch (const std::exception &) {
        }
    } else if (value.kind == attr::ValueKind::Tuple) {
        for (const attr::AttrValueId item : value.items) {
            for (const GraphId graph : graphsOf(item)) {
                graphs.push_back(graph);
            }
        }
    }
    return graphs;
}

/// The fact a Tag handle names: the entry at the handle's index of its graph's declaration facts.
std::optional<attr::Attr> factOf(const Handle *handle, const char *site) {
    const std::vector<attr::Attr> facts = ruleFactsOf(graphOf(handle, site));
    if (handle->index() >= facts.size()) {
        return std::nullopt;
    }
    return facts[handle->index()];
}

slot_t declarationTagsKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle   = norm.get<Handle *>(0);
    const auto facts     = ruleFactsOf(graphOf(handle, "graph.declaration_tags"));
    std::vector<Handle *> tags;
    for (size_t i = 0; i < facts.size(); ++i) {
        tags.push_back(Handle::create(
            HandleKind::Tag,
            handle->program(),
            handle->graph(),
            static_cast<uint32_t>(i),
            nullptr,
            mm::autoSpace()));
    }
    return handleArray(HandleKind::Tag, tags);
}

slot_t tagNameKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle = norm.get<Handle *>(0);
    const auto fact    = factOf(handle, "graph.tag_name");
    if (!fact.has_value()) {
        return stringSlot(std::string_view{});
    }
    if (const auto decl = tagOf(*fact); decl != nullptr) {
        return stringSlot(decl->ident);
    }
    return stringSlot(attr::keyName(fact->key));
}

slot_t tagGraphsKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle = norm.get<Handle *>(0);
    const auto fact    = factOf(handle, "graph.tag_graphs");
    std::vector<Handle *> graphs;
    if (fact.has_value() && handle->program() != nullptr) {
        for (const GraphId id : graphsOf(fact->value)) {
            if (handle->program()->graph(id) != nullptr) {
                graphs.push_back(graphHandle(handle->program(), id));
            }
        }
    }
    return handleArray(HandleKind::Graph, graphs);
}

/// The rule the declaration of `g` writes for the transformation `name`: `#jvp(rule)` is the rule
/// of forward mode, `#transpose(rule)` the rule of a linear function, `#vjp(rule)` the rule that
/// gives the pullback directly. Null when the declaration writes none, when it writes that tag
/// more than once, and when the tag's argument is not a function.
slot_t ruleOfKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle   = norm.get<Handle *>(0);
    const auto *name     = norm.get<String *>(1);
    const GCGraph &graph = graphOf(handle, "graph.rule_of");
    std::optional<GraphId> found;
    for (const attr::Attr &fact : ruleFactsOf(graph)) {
        if (!namesTag(fact, tagOf(fact), name->view())) {
            continue;
        }
        const std::vector<GraphId> graphs = graphsOf(fact.value);
        if (found.has_value() || graphs.empty()) {
            return NullSlot;
        }
        found = graphs.front();
    }
    if (!found.has_value() || handle->program() == nullptr ||
        handle->program()->graph(*found) == nullptr) {
        return NullSlot;
    }
    return makeSome(
        toSlot(graphHandle(handle->program(), *found)),
        nullableOf(HandleType::get(HandleKind::Graph)));
}

/// Whether the node anchors its graph: its exit, output or return node. A rewrite keeps such a
/// node; a transaction does not replace or erase it.
slot_t isAnchorKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle  = norm.get<Handle *>(0);
    const NodeView node = viewOf(handle, "graph.is_anchor");
    const auto ref      = handle->index();
    if (node.isDraft()) {
        const GraphDraft &draft = *node.draft;
        return toSlot<Bool>(
            ref == draft.exitNode() || ref == draft.outputNode() || ref == draft.returnNode());
    }
    const GCGraph &graph = graphOf(handle, "graph.is_anchor");
    bool anchor =
        ref == graph.exitNodeRef() || ref == graph.outputNodeRef() || ref == graph.returnNodeRef();
    return toSlot<Bool>(anchor);
}

/// Whether the value of the node may be written in place: it is passed to a `var` parameter or
/// to an operator that changes its operand, directly or through gates. A rewrite must not treat
/// such a node as one value. The answer for a graph is computed once and kept while the graph
/// version is the same.
slot_t isWrittenSlotKernel(ArgsView &, ArgsView &norm, Context &) {
    static std::mutex mutex;
    static std::
        map<std::pair<const GCGraph *, uint64_t>, std::shared_ptr<camel::runtime::WrittenSlots>>
            cache;
    const auto *handle  = norm.get<Handle *>(0);
    const NodeView node = viewOf(handle, "graph.is_written_slot");
    if (node.isDraft()) {
        // The draft changes in place, so there is no version to cache an answer under.
        return toSlot<Bool>(camel::runtime::WrittenSlots(*node.draft)(node.ref));
    }
    const GCGraph &graph = graphOf(handle, "graph.is_written_slot");
    std::shared_ptr<camel::runtime::WrittenSlots> written;
    {
        std::lock_guard lock(mutex);
        const auto key = std::make_pair(&graph, graph.revision());
        auto found     = cache.find(key);
        if (found == cache.end()) {
            if (cache.size() > 64) {
                cache.clear();
            }
            const auto draft = camel::runtime::GraphDraft::decode(*handle->program(), &graph);
            found =
                cache.emplace(key, std::make_shared<camel::runtime::WrittenSlots>(*draft)).first;
        }
        written = found->second;
    }
    return toSlot<Bool>((*written)(handle->index()));
}

slot_t originKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle    = norm.get<Handle *>(0);
    const NodeView node   = viewOf(handle, "graph.origin");
    const uint64_t origin = node.isDraft()
                                ? node.draft->nodeOrigin(node.ref)
                                : graphOf(handle, "graph.origin").nodeOrigin(handle->index());
    return toSlot<Int64>(static_cast<int64_t>(origin));
}

/// The value of a node that holds a constant of the type `T` the call names (`constant::<int>(n)`),
/// null for any other node. `T` is the payload of the result type `T?` the call was built with.
slot_t constantKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle  = norm.get<Handle *>(0);
    const NodeView node = viewOf(handle, "graph.constant");
    const TupleType *answer = asNullable(norm.resultType());
    ASSERT(answer != nullptr, "constant answers a T?");
    Type *wanted        = answer->payloadType();
    const Type *type    = node.dataType();
    const auto index    = static_cast<int64_t>(-node.dataIndex());
    if (node.kind() != GCNodeKind::Data || type == nullptr || type->code() != wanted->code() ||
        node.dataIndex() >= 0 ||
        !(node.isDraft()
              ? node.draft->normInputsOf(node.ref).empty()
              : graphOf(handle, "graph.constant").normInputsOf(handle->index()).empty()) ||
        !(node.isDraft()
              ? node.draft->withInputsOf(node.ref).empty()
              : graphOf(handle, "graph.constant").withInputsOf(handle->index()).empty()) ||
        !(node.isDraft()
              ? node.draft->ctrlInputsOf(node.ref).empty()
              : graphOf(handle, "graph.constant").ctrlInputsOf(handle->index()).empty())) {
        return NullSlot;
    }
    if (node.isDraft()) {
        const auto slots = node.draft->staticSlots();
        if (static_cast<size_t>(index) >= slots.size()) {
            return NullSlot;
        }
        return makeSome(slots[index], nullableOf(wanted));
    }
    const GCGraph &graph = graphOf(handle, "graph.constant");
    if (static_cast<size_t>(index) >= graph.staticSlots().size()) {
        return NullSlot;
    }
    return makeSome(graph.staticSlots()[index], nullableOf(wanted));
}

// ---- regions ----

/// The region a node is in (the innermost). Every node is in one: region 0 is the body, also in
/// a graph whose region table is empty.
slot_t regionOfKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle = norm.get<Handle *>(0);
    nodeOf(handle, "graph.region_of");
    const GCGraph &graph = graphOf(handle, "graph.region_of");
    Handle *region      = Handle::create(
        HandleKind::Region,
        handle->program(),
        handle->graph(),
        graph.nodeRegion(handle->index()),
        nullptr,
        mm::autoSpace());
    return toSlot(region);
}

/// The body of a graph whose region table is empty; region 0 is always the body.
const GCRegion &bodyRegion() {
    static const GCRegion body;
    return body;
}

const GCRegion &regionOf(const Handle *handle, const char *site) {
    const GCGraph &graph = graphOf(handle, site);
    if (handle->index() == 0) {
        return graph.region(0) != nullptr ? *graph.region(0) : bodyRegion();
    }
    const GCRegion *region = graph.region(handle->index());
    if (region == nullptr) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            site,
            "the graph has no such region");
    }
    return *region;
}

slot_t regionKindKernel(ArgsView &, ArgsView &norm, Context &) {
    const GCRegion &region = regionOf(norm.get<Handle *>(0), "graph.region_kind");
    const auto tag         = regionKindTag(region.kind);
    if (!tag) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "graph.region_kind",
            "the region has no kind a program shows");
    }
    return toSlot(Variant::create(*tag, 0, mm::autoSpace()));
}

/// The region this one is in; null for the body, whose region id is 0.
slot_t regionParentKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle = norm.get<Handle *>(0);
    if (handle->index() == 0) {
        return NullSlot;
    }
    const GCRegion &region = regionOf(handle, "graph.region_parent");
    const GCGraph &graph   = graphOf(handle, "graph.region_parent");
    if (graph.region(region.parent) == nullptr) {
        return NullSlot;
    }
    Handle *parent = Handle::create(
        HandleKind::Region,
        handle->program(),
        handle->graph(),
        region.parent,
        nullptr,
        mm::autoSpace());
    return makeSome(toSlot(parent), nullableOf(HandleType::get(HandleKind::Region)));
}

slot_t showKernel(ArgsView &, ArgsView &norm, Context &) {
    const auto *handle  = norm.get<Handle *>(0);
    const NodeView node = viewOf(handle, "graph.show");
    std::string text    = "%" + std::to_string(handle->index()) + " ";
    const auto tag      = nodeKindTag(node.kind());
    text += tag ? nodeKindName(*tag) : "?";
    if (node.kind() == GCNodeKind::Oper && node.hasPayload()) {
        if (node.isDraft()) {
            const auto bytes = node.draft->payloadOf(node.ref);
            if (bytes.size() >= sizeof(GCOperBody)) {
                text +=
                    " " + std::string(reinterpret_cast<const GCOperBody *>(bytes.data())->uri());
            }
        } else {
            text +=
                " " +
                std::string(
                    graphOf(handle, "graph.show").nodeBodyAs<GCOperBody>(handle->index())->uri());
        }
    }
    if (const Type *type = node.dataType()) {
        text += " : " + type->toString();
    }
    return stringSlot(text);
}

// ---- the tables ----

// Every read but `constant`: its signature is in stdlib/graph.cml, so the table carries the
// kernel of each name and nothing else.
struct ReadOp {
    const char *name;
    operator_t kernel;
};

const std::vector<ReadOp> &readOps() {
    static const std::vector<ReadOp> ops = {
        {"graphs_of", graphsOfKernel},
        {"name", nameKernel},
        {"nodes", nodesKernel},
        {"inputs", inputsKernel},
        {"with_inputs", withInputsKernel},
        {"sync_inputs", syncInputsKernel},
        {"order_inputs", orderInputsKernel},
        {"users", usersKernel},
        {"value_users", valueUsersKernel},
        {"is_constant", isConstantKernel},
        {"kind", kindKernel},
        {"op", opKernel},
        {"op_name", opNameKernel},
        {"is_pure", isPureKernel},
        {"type_of", typeOfKernel},
        {"type_name", typeNameKernel},
        {"callee", calleeKernel},
        {"tags", tagsKernel},
        {"declaration_tags", declarationTagsKernel},
        {"tag_name", tagNameKernel},
        {"tag_graphs", tagGraphsKernel},
        {"rule_of", ruleOfKernel},
        {"is_anchor", isAnchorKernel},
        {"is_written_slot", isWrittenSlotKernel},
        {"origin", originKernel},
        {"show", showKernel},
        {"region_of", regionOfKernel},
        {"region_kind", regionKindKernel},
        {"region_parent", regionParentKernel},
    };
    return ops;
}

} // namespace

const std::unordered_map<std::string, operator_t> &readKernels() {
    static const std::unordered_map<std::string, operator_t> kernels = [] {
        std::unordered_map<std::string, operator_t> out;
        for (const ReadOp &op : readOps()) {
            out.emplace(op.name, op.kernel);
        }
        out.emplace("constant", constantKernel);
        return out;
    }();
    return kernels;
}

} // namespace camel::graphmod
