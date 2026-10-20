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
 * Created: Aug. 17, 2024
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "builder.h"
#include "builder_support.h"
#include "builder_helpers.h"

#include "camel/compile/gir/static_function.h"
#include "camel/core/numeric_convert.h"
#include "camel/core/operator_names.h"
#include "camel/core/rtdata/string.h"
#include "camel/execute/entry_args.h"
#include "camel/runtime/control.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"
#include "core/module/entity_internal.h"

#include "camel/core/operator_traits.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/other.h"

#include <atomic>
#include <cctype>
#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {

void Builder::ensureRootGraph() {
    if (rootGraph_) {
        return;
    }
    rootGraph_ = createCompileGraph(FunctionType::create(), nullptr, "__root__");
    if (auto sourceContext = context_ ? context_->sourceContext() : nullptr) {
        rootGraph_->setExtra<camel::source::SourceContext, kSourceContextExtraIndex>(
            sourceContext.get());
    }
}

void Builder::buildEntryCall() {
    compile_graph_ptr_t entryGraph = nullptr;
    if (auto decoratedMain = decoratedGraphAt("main")) {
        entryGraph = *decoratedMain;
    } else if (auto gv = graphsAt("main"); gv.has_value() && !gv.value()->empty()) {
        entryGraph = gv.value()->front();
    }
    if (entryGraph) {
        // The entry graph stands for the entry function where a diagnostic is about the
        // program as a whole (a translation pass that finds nothing to translate): it
        // shares the origin of `main`, also after passes have inlined `main` into it.
        if (auto sourceContext = context_ ? context_->sourceContext() : nullptr) {
            const auto mainOrigin = sourceContext->debugMap().graphOrigin(entryGraph->stableId());
            if (mainOrigin != camel::source::kInvalidOriginId) {
                sourceContext->debugMap().registerGraphOrigin(rootGraph_->stableId(), mainOrigin);
                // Also under a fixed key: the entry graph is rewritten (and renamed) by
                // passes, the question "where is main" outlives that.
                sourceContext->debugMap().registerGraphOrigin(
                    camel::execute::kEntryFunctionOriginKey,
                    mainOrigin);
            }
        }
        // `main` is a function like any other: its parameters are the inputs of the
        // program. The entry graph takes the same parameters (`with` ones first) and
        // passes them on, so it never calls `main` with fewer arguments than it takes.
        // Who supplies them is not decided here: a run binds values given from outside
        // (see camel/execute/entry_args.h), a translation takes them as the inputs of
        // what it produces.
        FunctionType *entryType = entryGraph->funcType();
        std::vector<node_handle_t> entryWithArgs, entryNormArgs;
        if (entryType->withTypesCount() + entryType->normTypesCount() > 0) {
            FunctionTypeFactory rootTypeFactory;
            const auto nameAt = [&](size_t argIndex, const char *prefix, size_t i) {
                return argIndex < entryType->argNamesCount()
                           ? std::string(entryType->argNameAt(argIndex))
                           : std::format("{}{}", prefix, i);
            };
            for (size_t i = 0; i < entryType->withTypesCount(); ++i) {
                const std::string name = nameAt(i, "__w", i);
                const bool isVar       = entryType->withIsVarAt(i);
                Type *type             = entryType->withTypeAt(i);
                rootTypeFactory.addWithArg(name, type, isVar);
                entryWithArgs.push_back(
                    rootGraph_
                        ->addPortNode(isVar ? TupleType::cell(type) : type, name, true, isVar));
            }
            for (size_t i = 0; i < entryType->normTypesCount(); ++i) {
                const std::string name = nameAt(entryType->withTypesCount() + i, "__n", i);
                const bool isVar       = entryType->normIsVarAt(i);
                Type *type             = entryType->normTypeAt(i);
                rootTypeFactory.addNormArg(name, type, isVar);
                entryNormArgs.push_back(
                    rootGraph_
                        ->addPortNode(isVar ? TupleType::cell(type) : type, name, false, isVar));
            }
            rootGraph_->setFuncType(rootTypeFactory.build());
        }
        auto entryFuncValue = createFuncDataNode(entryGraph, true, false);
        auto entryCall      = rootGraph_->addCallNode(entryGraph->funcType()->exitType());
        linkNodes(LinkType::With, entryFuncValue, entryCall);
        for (node_handle_t arg : entryWithArgs) {
            linkNodes(LinkType::With, arg, entryCall);
        }
        for (node_handle_t arg : entryNormArgs) {
            linkNodes(LinkType::Norm, arg, entryCall);
        }
        setGraphReturnFromResult(
            rootGraph_,
            nodeIdOf(entryCall),
            nodeIdOf(entryCall),
            entryGraph->funcType()->exitType());
    } else if (context_->requireEntry()) {
        diags_->of(SemanticDiag::EntryModuleMissingMain).commit(module_->name());
        throw BuildAbortException();
    } else {
        // A library module (no `main`) has no entry call. The root graph still needs a
        // well-formed exit: the graph dumps (std::gir, std::jgir, `inspect -g`) and every
        // reachability walk end at it, and an invalid exit is not a graph one can encode.
        // Nothing executes this root, so the exit is an empty (void) return.
        auto empty = rootGraph_->addStaticDataNode(Data::null());
        setGraphReturnFromResult(rootGraph_, nodeIdOf(empty), nodeIdOf(empty), Type::Void());
    }
}

compile_graph_ptr_t Builder::enterScope(FunctionType *funcType, const std::string &name) {
    std::string scopeName = name;
    if (name.empty()) {
        const bool atRoot = currGraph_ == rootGraph_;
        currGraph_        = createCompileGraph(funcType, currGraph_);
        if (atRoot) {
            rootSubGraphOwners_[currGraph_.get()] = unit_;
        }
    } else {
        // The functions of one name declared in one block are overloads, one graph each. A
        // graph is found again (the forward declaration of a module-level function, then its
        // definition) by the function type object the declaration carries, never by the name
        // alone: by name the second overload was built into the graph of the first.
        compile_graph_ptr_t found;
        size_t index = 0;
        if (auto graphs = graphScope_->get(name, false); graphs.has_value()) {
            for (; index < graphs.value()->size(); ++index) {
                if (graphs.value()->at(index)->funcType() == funcType) {
                    found = graphs.value()->at(index);
                    break;
                }
            }
        }
        if (!found) {
            found = createCompileGraph(funcType, currGraph_, name);
            insertGraph(name, found);
            if (currGraph_ == rootGraph_) {
                rootSubGraphOwners_[found.get()] = unit_;
            }
        }
        currGraph_ = found;
        // Each overload has its own scope: they may name their parameters alike.
        if (index > 0) {
            scopeName = std::format("{}#{}", name, index);
        }
    }
    enterGraphScope(currGraph_, scopeName);
    return currGraph_;
}

void Builder::rejectRedeclaredFunction(
    const std::string &name, FunctionType *funcType, const GCT::node_ptr_t &gct) {
    if (name.empty()) {
        return;
    }
    if (redeclaredFuncTypes_.contains(funcType)) {
        // Reported at the forward declaration of this function.
        throw BuildAbortException();
    }
    auto graphs = graphScope_->get(name, false);
    if (!graphs.has_value()) {
        return;
    }
    for (const auto &graph : *graphs.value()) {
        if (graph->funcType() == funcType) {
            return; // this very function, declared ahead
        }
    }
    // Overloads are told apart by their parameter types alone: a call has nothing else to
    // choose by.
    const auto sameParameters = [](FunctionType *a, FunctionType *b) {
        if (a->withTypesCount() != b->withTypesCount() ||
            a->normTypesCount() != b->normTypesCount()) {
            return false;
        }
        for (size_t i = 0; i < a->withTypesCount(); ++i) {
            if (!a->withTypeAt(i)->equals(b->withTypeAt(i))) {
                return false;
            }
        }
        for (size_t i = 0; i < a->normTypesCount(); ++i) {
            if (!a->normTypeAt(i)->equals(b->normTypeAt(i))) {
                return false;
            }
        }
        return true;
    };
    for (const auto &graph : *graphs.value()) {
        if (sameParameters(graph->funcType(), funcType)) {
            redeclaredFuncTypes_.insert(funcType);
            diags_->of(SemanticDiag::Redeclaration).atOrigin(gct->origin()).commit(name);
            throw BuildAbortException();
        }
    }
}

void Builder::enterGraphScope(const compile_graph_ptr_t &graph, const std::string &name) {
    currGraph_ = graph;
    if (auto sourceContext = context_ ? context_->sourceContext() : nullptr) {
        currGraph_->setExtra<camel::source::SourceContext, kSourceContextExtraIndex>(
            sourceContext.get());
    }
    nodeScope_      = nodeScope_->enter(name);
    graphScope_     = graphScope_->enter(name);
    decoratedScope_ = decoratedScope_->enter(name);
    templateScope_  = templateScope_->enter(name);
}

void Builder::leaveScope() {
    nodeScope_      = nodeScope_->leave();
    graphScope_     = graphScope_->leave();
    decoratedScope_ = decoratedScope_->leave();
    templateScope_  = templateScope_->leave();
    currGraph_      = currGraph_ ? currGraph_->outerGraph() : nullptr;
}

bool Builder::insertNode(const std::string &name, node_handle_t node) {
    if (nodeScope_->has(name, false)) {
        return false;
    }
    nodeScope_->insert(name, node);
    return true;
}

bool Builder::insertGraph(const std::string &name, const compile_graph_ptr_t &graph) {
    if (auto graphs = graphScope_->get(name, false); graphs.has_value()) {
        graphs.value()->push_back(graph);
        return true;
    }
    graphScope_->insert(name, std::make_shared<graph_set_t>(1, graph));
    return true;
}

bool Builder::insertDecoratedGraph(const std::string &name, const compile_graph_ptr_t &graph) {
    if (!decoratedScope_) {
        return false;
    }
    decoratedScope_->insert(name, graph);
    return true;
}

node_handle_t Builder::resolveNodeByRef(const std::string &name) {
    auto optSrcNode = nodeAt(name);
    if (!optSrcNode.has_value()) {
        EXEC_WHEN_DEBUG({
            nodeScope_->dump(
                std::cerr,
                [](std::ostream &os, const std::string &key, const node_handle_t &value) {
                    os << "[" << key << "] ";
                    if (value == nullptr) {
                        os << "<invalid>";
                        return;
                    }
                    auto *graph = nodeGraphOf(value);
                    os << graph->name() << "#" << nodeIdOf(value);
                },
                0);
        });
        if (isPoisonedName(name)) {
            throw BuildAbortException();
        }
        auto sourceContext = context_ ? context_->sourceContext() : nullptr;
        auto origin        = (sourceContext && currGraph_)
                                 ? sourceContext->debugMap().graphOrigin(currGraph_->stableId())
                                 : camel::source::kInvalidOriginId;
        diags_->of(SemanticDiag::UnresolvedReference).atOrigin(origin).commit(name);
        throw BuildAbortException();
    }
    node_handle_t node = optSrcNode.value();

    // A value of an enclosing graph (a module's top level included) is captured.
    return captureInto(currGraph_, node, name);
}

bool Builder::isPoisonedName(const std::string &name) const {
    if (poisonedNames_.empty()) {
        return false;
    }
    for (node_scope_ptr_t scope = nodeScope_; scope; scope = scope->outer()) {
        auto it = poisonedNames_.find(scope);
        if (it != poisonedNames_.end() && it->second.contains(name)) {
            return true;
        }
    }
    return false;
}


void Builder::recordDeclaration(
    const std::string &name, node_handle_t node, const GCT::node_ptr_t &decl) {
    const auto origin                    = decl->origin();
    declOrigins_[nodeScope_.get()][name] = origin;
    recorder_->valueDeclared(origin, name, valueTypeOf(nodeTypeOf(node)), isVariable(node));
    recorder_->valueBound(
        origin,
        name,
        currGraph_ ? currGraph_->stableId() : std::string(),
        node->header.origin,
        nodeTypeOf(node),
        isVariable(node));
}

camel::source::origin_id_t Builder::declarationOf(const std::string &name) const {
    // A capture port is entered into the scopes between the reference and the declaration
    // without a record: the first scope that has one is the scope of the declaration.
    for (node_scope_ptr_t scope = nodeScope_; scope; scope = scope->outer()) {
        if (!scope->has(name, false)) {
            continue;
        }
        if (auto it = declOrigins_.find(scope.get()); it != declOrigins_.end()) {
            if (auto found = it->second.find(name); found != it->second.end()) {
                return found->second;
            }
        }
    }
    return camel::source::kInvalidOriginId;
}

void Builder::recordResult(const GCT::node_ptr_t &gct, const std::any &res) {
    if (res.type() != typeid(node_handle_t)) {
        return;
    }
    node_handle_t node = any_cast<node_handle_t>(res);
    if (node == nullptr || nodeIsKind(node, runtime::GCNodeKind::Dref)) {
        return;
    }
    recorder_->expressionTyped(gct->origin(), valueTypeOf(nodeTypeOf(node)));
}

any Builder::visit(const GCT::node_ptr_t &node) {
    if (recorder_ == nullptr) [[likely]] {
        return dispatch(node);
    }
    any res = dispatch(node);
    recordResult(node, res);
    return res;
}

any Builder::dispatch(const GCT::node_ptr_t &node) {
    switch (node->type()) {
    case GCT::NodeKind::DECL:
        return visitDeclNode(node);
    case GCT::NodeKind::FUNC:
        if (node->name().empty() && isGenericHere(node->at(0)->dataType())) {
            return anonymousTemplateOf(node);
        }
        return visitFuncNode(node);
    case GCT::NodeKind::DATA:
        return visitDataNode(node);
    case GCT::NodeKind::TYPE:
        return visitTypeNode(node);
    case GCT::NodeKind::NREF:
        return visitNRefNode(node);
    case GCT::NodeKind::DREF:
        return visitDRefNode(node);
    case GCT::NodeKind::VARI:
        return visitVariNode(node);
    case GCT::NodeKind::WAIT:
        return visitWaitNode(node);
    case GCT::NodeKind::LINK:
        return visitLinkNode(node);
    case GCT::NodeKind::WITH:
        return visitWithNode(node);
    case GCT::NodeKind::TAPP:
        return visitTypeApplication(node);
    case GCT::NodeKind::ACCS:
        return visitAccsNode(node);
    case GCT::NodeKind::BRCH:
        return visitBrchNode(node);
    case GCT::NodeKind::ANNO:
        return visitAnnoNode(node);
    case GCT::NodeKind::EXIT:
        return visitExitNode(node);
    case GCT::NodeKind::EXEC:
        return visitExecNode(node);
    case GCT::NodeKind::EXPT:
        // The export list is read when the module is declared (exportDeclarations).
        return nullptr;
    case GCT::NodeKind::CAST:
        return visitCastNode(node);
    case GCT::NodeKind::LOOP:
        return visitLoopNode(node);
    default:
        ASSERT(false, "Unknown GCT NodeType");
    }
    return nullptr;
}

void_ptr_t Builder::visitDeclNode(const GCT::node_ptr_t &gct) {
    ENTER("DECL");
    GCT::node_ptr_t typeNode = gct->at(0);
    Type *type               = typeNode->dataType();
    if (!gct->isFunc()) {
        unit_->typeDecls[Reference(gct->name())] = type;
        LEAVE("DECL");
        return nullptr;
    }

    if (isGenericHere(type)) {
        // A generic function is a template: its graphs are its instances.
        LEAVE("DECL");
        return nullptr;
    }
    FunctionType *funcType = tt::as_ptr<FunctionType>(type);
    rejectRedeclaredFunction(gct->name(), funcType, gct);
    rejectOperatorConflict(gct->name(), funcType, gct);
    compile_graph_ptr_t graph = enterScope(funcType, gct->name());
    leaveScope();

    LEAVE("DECL");
    return nullptr;
}

compile_graph_ptr_t Builder::visitFuncNode(const GCT::node_ptr_t &gct) {
    ENTER("FUNC");
    ExpectedValueScope functionScope(expectedValueType_, nullptr);
    std::string name         = gct->name();
    GCT::node_ptr_t typeLoad = gct->at(0);
    Type *type               = typeLoad->dataType();
    FunctionType *funcType   = tt::as_ptr<FunctionType>(type);
    if (isGenericHere(type)) {
        // A generic function: registered as a template when its block was entered
        // (registerGenericTemplates), built when a call asks for an instance.
        LEAVE("FUNC");
        return nullptr;
    }
    if (instantiationDepth_ > 0) {
        // Inside an instance every function and lambda of the body is built once per
        // instance, and a graph completes its function type in place: each needs its own,
        // with the type variables replaced.
        funcType = instantiateFunctionType(funcType, typeBindings_);
    }
    rejectRedeclaredFunction(name, funcType, gct);
    rejectOperatorConflict(name, funcType, gct);
    staticGrantsOf(gct); // its decorators are resolved here, where they are written
    compile_graph_ptr_t graph = enterScope(funcType, name);
    buildFuncBody(gct, graph, funcType, name);
    if (name.empty()) {
        lambdaGraphs_[gct] = graph.get();
    }
    leaveScope();

    // The decorators that grant attributes as the program is built were applied to the body
    // (buildFuncBody); the others make the decorated function. A decorated block applies its
    // own where it is called (visitDecoratedBlock).
    const std::vector<GCT::node_ptr_t> annoNodes =
        gct->decoratedBlock() ? std::vector<GCT::node_ptr_t>{} : appliedDecoratorsOf(gct);
    if (!annoNodes.empty()) {
        graph_ptr_t decorated = buildDecoratedGraph(name, graph, annoNodes);
        insertDecoratedGraph(name, decorated);
        decoratedOf_[graph.get()] = decorated;
    }

    LEAVE("FUNC");
    return graph;
}

void Builder::stampPermissions(node_handle_t operNode) const {
    if (operNode != nullptr) {
        operNode->header.permissions = numericPermissions_;
        operNode->header.attrs       = currentAttrs_;
    }
}

void Builder::buildFuncBody(
    const GCT::node_ptr_t &gct, const compile_graph_ptr_t &graph, FunctionType *funcType,
    const std::string &name) {
    // The function's graph has the permissions of its own body (which include those of the
    // code it is written in), not those of the place that asked for it to be built (a call
    // that instantiates a generic function, a block that declares it).
    const GCT::node_ptr_t body = gct->size() > 1 ? gct->at(1) : nullptr;
    NumericPermissionScope permissions(
        numericPermissions_,
        body && body->type() == GCT::NodeKind::EXEC ? body->numericPermissions() : 0);
    // The attributes its decorators grant join those in force where it is written (each key's
    // combine rule, camel/core/node_attrs.h), for everything written in it.
    AttrScope attrs(
        currentAttrs_,
        camel::core::attr::unite(currentAttrs_, staticGrantsOf(gct).first));
    try {
        registerGraphOrigin(context_, graph, gct, "gir.func.graph");
        const auto withPorts = graph->draft().withPorts();
        for (size_t i = 0; i < withPorts.size(); ++i) {
            node_handle_t port = graph->draft().node(withPorts[i]);
            insertNode(graph->nodePortName(port), port);
            if (i < funcType->withTypesCount() && funcType->withIsVarAt(i)) {
                variables_.insert(port);
            }
            if (recorder_) {
                recordDeclaration(graph->nodePortName(port), port, gct);
            }
        }
        const auto normPorts = graph->draft().normPorts();
        for (size_t i = 0; i < normPorts.size(); ++i) {
            node_handle_t port = graph->draft().node(normPorts[i]);
            insertNode(graph->nodePortName(port), port);
            if (i < funcType->normTypesCount() && funcType->normIsVarAt(i)) {
                variables_.insert(port);
            }
            if (recorder_) {
                recordDeclaration(graph->nodePortName(port), port, gct);
            }
        }
        bodyStates_[graph.get()] = BodyState::Open;
        if (!funcType->hasExitType()) {
            inferredExitGraphs_.insert(graph.get()); // the body sets it from now on
        }
        // A decorated block whose value nothing takes is a function whose value is dropped:
        // it returns nothing, and a `return` in it gives no value (visitExitNode). Not a sync
        // block: its statements run in order whatever is done with its value, as they do
        // when the block is written without decorators.
        const bool dropped = droppedBodies_.contains(gct->at(1)) && !gct->at(1)->synced();
        if (dropped) {
            droppedGraphs_.insert(graph.get());
        }
        node_handle_t bodyResult = visitBodyNode(gct->at(1));
        setBodyResult(graph, dropped ? nullptr : bodyResult);
        // A body declared `: void` drops the value of its last statement.
        if (!inferredExitGraphs_.contains(graph.get()) &&
            isVoidGraphExitType(funcType->exitType())) {
            rejectDiscardedValue(graph.get());
        }
        bodyStates_[graph.get()] = BodyState::Built;
        unit_->builtFunctions.push_back({graph.get(), gct->origin(), name});
        unit_->builtBodies.push_back(graph);
        if (recorder_) {
            recorder_->functionBuilt(gct->origin(), name, graph->stableId(), funcType);
        }
    } catch (const camel::core::mm::MemoryExhausted &e) {
        // Say which function was being compiled; the allocator does not know.
        bodyStates_[graph.get()] = BodyState::Built;
        leaveScope();
        throw e.during(
            std::format("compiling function '{}'", name.empty() ? graph->name() : name),
            gct->origin());
    } catch (...) {
        // The caller entered the scope of `graph`; an error leaves it on the way out.
        bodyStates_[graph.get()] = BodyState::Built;
        failedBodies_.insert(graph.get());
        leaveScope();
        throw;
    }
}

std::set<node_handle_t> Builder::capturedWritesOf(DraftGraphBuilder *root) const {
    const auto isOpen = [&](DraftGraphBuilder *graph) {
        auto state = bodyStates_.find(graph);
        return state != bodyStates_.end() && state->second == BodyState::Open;
    };
    const auto summaryOf = [&](DraftGraphBuilder *graph) -> const CaptureSummary * {
        auto summary = captures_.find(graph);
        return summary == captures_.end() ? nullptr : &summary->second;
    };
    // A graph writes only variables it captured, and a write reaches `root` only through
    // graphs that all captured the variable: only the callees that captured one of root's
    // variables matter. (With all modules on one root, the calls reach across the whole
    // program; walking every callee made this quadratic in the length of a call chain.)
    const auto *rootSummary = summaryOf(root);
    if (rootSummary == nullptr || rootSummary->variables.empty()) {
        return {};
    }
    const std::unordered_set<node_handle_t> rootVariables(
        rootSummary->variables.begin(),
        rootSummary->variables.end());
    const auto sharesVariable = [&](const CaptureSummary &summary) {
        return std::ranges::any_of(summary.variables, [&](node_handle_t variable) {
            return rootVariables.contains(variable);
        });
    };
    // The graphs reachable through direct calls and arms. An open body is not looked into:
    // what it will write is not known yet, so all it captured stands for it.
    std::vector<DraftGraphBuilder *> reached{root};
    std::unordered_set<DraftGraphBuilder *> seen{root};
    std::unordered_map<DraftGraphBuilder *, std::set<node_handle_t>> writes;
    for (size_t i = 0; i < reached.size(); ++i) {
        DraftGraphBuilder *graph = reached[i];
        const auto *summary      = summaryOf(graph);
        if (isOpen(graph)) {
            writes[graph].insert(summary->variables.begin(), summary->variables.end());
            continue;
        }
        writes[graph] = summary->written;
        for (DraftGraphBuilder *callee : summary->callees) {
            const auto *calleeSummary = summaryOf(callee);
            if (calleeSummary != nullptr && sharesVariable(*calleeSummary) &&
                seen.insert(callee).second) {
                reached.push_back(callee);
            }
        }
    }
    // Least fixpoint of: a graph writes what its callees write, as far as it captured the
    // variable itself (a callee's write of the graph's own local variable stops there). A
    // recursive call adds nothing to the function's own set, so recursion needs no special
    // case here.
    for (bool changed = true; changed;) {
        changed = false;
        for (DraftGraphBuilder *graph : reached) {
            const auto *summary = summaryOf(graph);
            if (isOpen(graph)) {
                continue;
            }
            auto &own = writes[graph];
            for (DraftGraphBuilder *callee : summary->callees) {
                auto calleeWrites = writes.find(callee);
                if (calleeWrites == writes.end()) {
                    continue; // not reached: it captured none of root's variables
                }
                for (node_handle_t variable : calleeWrites->second) {
                    if (std::ranges::find(summary->variables, variable) !=
                            summary->variables.end() &&
                        own.insert(variable).second) {
                        changed = true;
                    }
                }
            }
        }
    }
    return writes[root];
}

std::vector<Builder::CapturedUse> Builder::capturedUsesOf(const compile_graph_ptr_t &callee) {
    auto &callees = captures_[currGraph_.get()].callees;
    if (std::ranges::find(callees, callee.get()) == callees.end()) {
        callees.push_back(callee.get());
    }
    auto summary = captures_.find(callee.get());
    if (summary == captures_.end() || summary->second.variables.empty()) {
        return {};
    }
    // Capturing into the current graph may add to its summary: copies.
    const std::vector<node_handle_t> variables = summary->second.variables;
    const std::set<node_handle_t> written      = capturedWritesOf(callee.get());
    std::vector<CapturedUse> uses;
    for (node_handle_t origin : variables) {
        // The callee's variable, as the current graph sees it (see captureInto).
        node_handle_t variable = captureInto(currGraph_, origin, captureNameOf(origin, ""));
        uses.push_back({variable, origin, written.contains(origin)});
    }
    return uses;
}

void Builder::reportIgnoredCapturedWrites() {
    for (const PendingIgnoredWrite &pending : unit_->pendingIgnoredWrites) {
        std::set<node_handle_t> written;
        for (DraftGraphBuilder *graph : pending.graphs) {
            written.merge(capturedWritesOf(graph));
        }
        std::vector<std::string> names;
        for (node_handle_t variable : pending.variables) {
            if (written.contains(variable)) {
                names.push_back("'" + captureNameOf(variable, originNames_[variable]) + "'");
            }
        }
        if (names.empty()) {
            continue;
        }
        diags_->of(SemanticDiag::IgnoredSideEffect)
            .atOrigin(pending.origin)
            .commit(
                pending.subject,
                std::format(
                    "{} the captured variable{} {}",
                    pending.verb,
                    names.size() > 1 ? "s" : "",
                    strutil::join(names, ", ", [](const std::string &name) { return name; })));
    }
    unit_->pendingIgnoredWrites.clear();
}

void Builder::reportUndeclaredCapturedWrites() {
    // The prefix `sync` of a function declares that it has side effects (design principles
    // 2.3); a function with a `var` parameter is already checked for it
    // (VarParamInAsyncFunction, in the GCT builder). A function that writes a variable it
    // captured, itself or through the functions it calls, has side effects as well. This is a
    // declaration check only: the modifier changes nothing in how calls are compiled.
    for (const BuiltFunction &function : unit_->builtFunctions) {
        if (function.graph->funcType()->modifiers().sync() ||
            blockGraphs_.contains(function.graph)) {
            continue; // declared so, or a decorated block (no declaration to write it on)
        }
        std::vector<std::string> names;
        for (node_handle_t variable : capturedWritesOf(function.graph)) {
            names.push_back("'" + captureNameOf(variable, originNames_[variable]) + "'");
        }
        if (names.empty()) {
            continue;
        }
        // In the order of the names, not of the nodes.
        std::ranges::sort(names);
        diags_->of(SemanticDiag::CapturedWriteInAsyncFunction)
            .atOrigin(function.origin)
            .commit(
                function.name.empty() ? std::string("The lambda")
                                      : std::format("Function '{}'", function.name),
                names.size() > 1 ? "s" : "",
                strutil::join(names, ", ", [](const std::string &name) { return name; }));
    }
    unit_->builtFunctions.clear();
}

bool Builder::isEffectNode(DraftGraphBuilder *graph, node_handle_t node) const {
    const auto declaresEffects = [](Type *type) {
        if (type == nullptr || type->code() != TypeCode::Function) {
            return false;
        }
        const auto *func = tt::as_ptr<FunctionType>(type);
        if (func->modifiers().sync()) {
            return true;
        }
        for (size_t i = 0; i < func->normTypesCount(); ++i) {
            if (func->normIsVarAt(i)) {
                return true;
            }
        }
        for (size_t i = 0; i < func->withTypesCount(); ++i) {
            if (func->withIsVarAt(i)) {
                return true;
            }
        }
        return false;
    };
    const auto calleeOf = [&](node_handle_t func) { return graph->funcTarget(func); };
    switch (nodeKindOf(node)) {
    case runtime::GCNodeKind::Oper: {
        const auto &op = graph->operTarget(node);
        return op != nullptr && camel::core::OperatorTable::instance().hasExternalEffect(op->uri());
    }
    case runtime::GCNodeKind::Func: {
        const auto callee = calleeOf(node);
        // An arm is entered by its branch, which is what gets reported.
        if (callee == nullptr || armGraphs_.contains(callee.get())) {
            return false;
        }
        return declaresEffects(callee->funcType()) || effectfulGraphs_.contains(callee.get());
    }
    case runtime::GCNodeKind::Call: {
        const auto callee = withInputsOf(node);
        return !callee.empty() && declaresEffects(nodeTypeOf(nodeDraftOf(node)->node(callee[0])));
    }
    case runtime::GCNodeKind::Loop: {
        // Each round calls the condition and the body graph.
        const auto &target = graph->loopTarget(node);
        return (target.cond && effectfulGraphs_.contains(target.cond.get())) ||
               effectfulGraphs_.contains(target.body.get());
    }
    case runtime::GCNodeKind::Join: {
        for (draft_node_ref_t arm : withInputsOf(node)) {
            node_handle_t armNode = nodeDraftOf(node)->node(arm);
            if (nodeIsKind(armNode, runtime::GCNodeKind::Func)) {
                const auto callee = calleeOf(armNode);
                if (callee != nullptr && effectfulGraphs_.contains(callee.get())) {
                    return true;
                }
            }
        }
        return false;
    }
    default:
        return false;
    }
}

void Builder::settleEffectfulGraphs() {
    // Whether a call is an effect depends on its callee's body, which may be written later,
    // in another module, or be the caller itself: the set of effectful graphs is the least
    // fixpoint over the bodies of the whole program (a part of the link step), and only then
    // are the warnings issued, per module, in the order its bodies were built.
    for (size_t known = effectfulGraphs_.size() + 1; known != effectfulGraphs_.size();) {
        known = effectfulGraphs_.size();
        for (const ModuleUnit *unit : unitOrder_) {
            for (const compile_graph_ptr_t &graph : unit->builtBodies) {
                reportDroppedEffects(graph, false);
            }
        }
    }
}

void Builder::reportDroppedEffectsOfBodies() {
    for (const compile_graph_ptr_t &graph : unit_->builtBodies) {
        reportDroppedEffects(graph, true);
    }
    unit_->builtBodies.clear();
}

void Builder::reportDroppedEffects(const compile_graph_ptr_t &graph, bool report) {
    auto &draft = graph->draft();
    // What the exit depends on, through data and control edges.
    std::unordered_set<draft_node_ref_t> needed;
    std::vector<draft_node_ref_t> work;
    for (draft_node_ref_t root : {draft.outputNode(), draft.exitNode(), draft.returnNode()}) {
        if (root != runtime::kInvalidNodeRef && needed.insert(root).second) {
            work.push_back(root);
        }
    }
    if (work.empty()) {
        return; // the body did not build (an error was reported)
    }
    while (!work.empty()) {
        const draft_node_ref_t id = work.back();
        work.pop_back();
        for (auto inputs :
             {draft.normInputsOf(id), draft.withInputsOf(id), draft.ctrlInputsOf(id)}) {
            for (draft_node_ref_t input : inputs) {
                if (needed.insert(input).second) {
                    work.push_back(input);
                }
            }
        }
    }
    std::vector<draft_node_ref_t> dropped;
    for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (!draft.alive(id) || !isEffectNode(graph.get(), draft.node(id))) {
            continue;
        }
        if (needed.contains(id)) {
            effectfulGraphs_.insert(graph.get());
        } else if (
            graph != rootGraph_ || (id < rootNodeOwners_.size() && rootNodeOwners_[id] == unit_)) {
            // On the root graph, the top level of the module being reported only: another
            // module's top-level value that this program does not use is not a mistake.
            dropped.push_back(id);
        }
    }
    if (!report) {
        return;
    }
    // One warning per dropped chain: an effect whose result feeds another dropped effect
    // (`println(log(1))`, the statements of a dropped sync block) is reported through the
    // last one.
    const auto feedsDroppedEffect = [&](draft_node_ref_t start) {
        std::unordered_set<draft_node_ref_t> seen{start};
        std::vector<draft_node_ref_t> stack{start};
        while (!stack.empty()) {
            const draft_node_ref_t id = stack.back();
            stack.pop_back();
            for (auto users :
                 {draft.normUsersOf(id), draft.withUsersOf(id), draft.ctrlUsersOf(id)}) {
                for (draft_node_ref_t user : users) {
                    if (!seen.insert(user).second) {
                        continue;
                    }
                    if (std::ranges::find(dropped, user) != dropped.end()) {
                        return true;
                    }
                    stack.push_back(user);
                }
            }
        }
        return false;
    };
    auto sourceContext = context_ ? context_->sourceContext() : nullptr;
    for (draft_node_ref_t id : dropped) {
        if (feedsDroppedEffect(id)) {
            continue;
        }
        node_handle_t node = draft.node(id);
        std::string subject;
        switch (nodeKindOf(node)) {
        case runtime::GCNodeKind::Oper: {
            const std::string &name = graph->operTarget(node)->name();
            subject = name.starts_with("__assn") ? std::string("The assignment")
                                                 : std::format("The call of '{}'", name);
        } break;
        case runtime::GCNodeKind::Func:
            subject = std::format("The call of '{}'", graph->funcTarget(node)->name());
            break;
        case runtime::GCNodeKind::Join:
            subject = "The branch";
            break;
        case runtime::GCNodeKind::Loop:
            subject = "The loop";
            break;
        default:
            subject = "The call";
            break;
        }
        camel::source::origin_id_t origin = node->header.origin;
        if (origin == camel::source::kInvalidOriginId && sourceContext) {
            origin = sourceContext->debugMap().graphOrigin(graph->stableId());
        }
        diags_->of(SemanticDiag::EffectNotExecuted).atOrigin(origin).commit(subject);
    }
}

void Builder::reportUnusedFallibleValues() {
    // Design principles 3.9: a `T!` that is evaluated and that nothing takes is a warning,
    // because the error it may hold is lost without a trace. On the graph "evaluated and
    // not taken" is a node without a data user that something is ordered after: a statement
    // of a sync block. (Outside a sync block a value nothing takes is not evaluated at all:
    // design principles 3.1, which has its own warning, EffectNotExecuted.)
    auto sourceContext = context_ ? context_->sourceContext() : nullptr;
    const auto warn =
        [&](const compile_graph_ptr_t &graph, node_handle_t node, Type *type, const char *remark) {
            camel::source::origin_id_t origin = node->header.origin;
            if (origin == camel::source::kInvalidOriginId && sourceContext) {
                origin = sourceContext->debugMap().graphOrigin(graph->stableId());
            }
            diags_->of(SemanticDiag::UnusedFallibleValue)
                .atOrigin(origin)
                .commit(type->toString(), remark);
        };
    // The `T!` a node gives, if it gives one. A call was typed when it was built, which for
    // a callee whose return type is taken from its body may be before that body was
    // complete (a recursive callee): the callee is asked.
    const auto fallibleTypeOf = [](const compile_graph_ptr_t &graph, node_handle_t node) {
        Type *type = nodeTypeOf(node);
        if (nodeIsKind(node, runtime::GCNodeKind::Func)) {
            if (const auto callee = graph->funcTarget(node)) {
                type = callee->funcType()->exitType();
            }
        }
        return asFallible(valueTypeOf(type));
    };
    // The entry function, when it declares no return type: what its body ends with is not
    // the result of the program (design principles 9.8), so nothing takes that either.
    DraftGraphBuilder *undeclaredMain = nullptr;
    if (context_ && module_ && context_->mainModule() == module_) {
        if (auto mains = graphsAt("main");
            mains.has_value() && !mains.value()->empty() &&
            inferredExitGraphs_.contains(mains.value()->front().get())) {
            undeclaredMain = mains.value()->front().get();
        }
    }

    for (const compile_graph_ptr_t &graph : unit_->builtBodies) {
        auto &draft = graph->draft();
        if (draft.exitNode() == runtime::kInvalidNodeRef) {
            continue; // the body did not build (an error was reported)
        }
        const auto isExit = [&](draft_node_ref_t id) {
            return id == draft.exitNode() || id == draft.outputNode() || id == draft.returnNode();
        };
        if (graph.get() == undeclaredMain && draft.returnNode() != runtime::kInvalidNodeRef) {
            node_handle_t value = draft.node(draft.returnNode());
            if (TupleType *type = fallibleTypeOf(graph, value);
                type != nullptr && !discardedValues_.contains(value)) {
                warn(
                    graph,
                    value,
                    type,
                    " ('main' declares no return type, so what its body ends with is not the "
                    "result of the program)");
            }
        }
        for (draft_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            if (!draft.alive(id)) {
                continue;
            }
            node_handle_t node = draft.node(id);
            switch (nodeKindOf(node)) {
            case runtime::GCNodeKind::Oper:
            case runtime::GCNodeKind::Func:
            case runtime::GCNodeKind::Call:
            case runtime::GCNodeKind::Join:
                break;
            default:
                continue; // not the result of an evaluation
            }
            const bool ordered = !draft.ctrlUsersOf(id).empty();
            if (nodeIsKind(node, runtime::GCNodeKind::Join) &&
                isVoidGraphExitType(nodeTypeOf(node)) && (ordered || isExit(id))) {
                // An `if` without `else`: the join takes nothing, so what an arm gives is
                // dropped where the arm ends.
                for (draft_node_ref_t armId : draft.withInputsOf(id)) {
                    node_handle_t arm   = draft.node(armId);
                    const auto armGraph = nodeIsKind(arm, runtime::GCNodeKind::Func)
                                              ? graph->funcTarget(arm)
                                              : nullptr;
                    if (armGraph == nullptr ||
                        armGraph->draft().returnNode() == runtime::kInvalidNodeRef) {
                        continue;
                    }
                    node_handle_t value = armGraph->draft().node(armGraph->draft().returnNode());
                    if (TupleType *type = fallibleTypeOf(armGraph, value);
                        type != nullptr && !discardedValues_.contains(value)) {
                        warn(armGraph, value, type, "");
                    }
                }
                continue;
            }
            if (!ordered || isExit(id) || !draft.normUsersOf(id).empty() ||
                !draft.withUsersOf(id).empty() || discardedValues_.contains(node)) {
                continue;
            }
            if (TupleType *type = fallibleTypeOf(graph, node)) {
                warn(graph, node, type, "");
            }
        }
    }
}

void Builder::rejectNameWithoutValue(
    node_handle_t node, const std::string &name, const GCT::node_ptr_t &origin) {
    if (!nodeIsKind(node, runtime::GCNodeKind::Dref)) {
        return;
    }
    rejectUnappliedBinding(node, origin);
    if (genericDrefs_.contains(node)) {
        (void)genericValueFor(node, nullptr, origin); // nothing here fixes its types
    }
    const auto &target = nodeGraphOf(node)->drefTarget(node);
    std::string what   = "an operator with several signatures";
    if (std::holds_alternative<graph_vec_ptr_t>(target)) {
        auto graphs = asCompileGraphVec(target);
        what        = std::format("{} functions of that name", graphs ? graphs->size() : 0);
        if (graphs && !graphs->empty()) {
            what += " [" +
                    strutil::join(
                        *graphs,
                        "; ",
                        [](const graph_ptr_t &g) { return g->funcType()->toString(); }) +
                    "]";
        }
    } else if (std::holds_alternative<oper_group_ptr_t>(target)) {
        // The candidates, so that the reader can write the wrapper the message asks for.
        const auto &ops        = std::get<oper_group_ptr_t>(target);
        const std::string sigs = strutil::join(
            ops->resolvers(),
            "; ",
            [](const std::pair<std::string, resolver_ptr_t> &p) { return p.second->signature(); });
        if (ops->resolvers().size() == 1) {
            const bool fixed = ops->resolvers().front().second->fixedSignature() != nullptr;
            what = fixed ? std::format("an operator that changes its argument in place [{}]", sigs)
                         : std::format(
                               "an operator whose signature depends on its arguments [{}]",
                               sigs);
        } else {
            what =
                std::format("an operator with {} signatures [{}]", ops->resolvers().size(), sigs);
        }
    }
    diags_->of(SemanticDiag::FunctionNameAsValue).atOrigin(origin->origin()).commit(name, what);
    throw BuildAbortException();
}

void Builder::reportImportNameConflicts() {
    // Two imports by name that bring functions of the same name and the same parameter types
    // (design principles 10.2): the call used to take the function of the import written
    // first, silently. Functions of different parameter types form a set of overloads, as
    // before; whether that stays is undecided.
    if (!module_) {
        return;
    }
    const auto sameSignature = [](FunctionType *a, FunctionType *b) {
        if (!a || !b || a->withTypesCount() != b->withTypesCount() ||
            a->normTypesCount() != b->normTypesCount()) {
            return false;
        }
        for (size_t i = 0; i < a->withTypesCount(); ++i) {
            if (!a->withTypeAt(i)->equals(b->withTypeAt(i))) {
                return false;
            }
        }
        for (size_t i = 0; i < a->normTypesCount(); ++i) {
            if (!a->normTypeAt(i)->equals(b->normTypeAt(i))) {
                return false;
            }
        }
        return true;
    };
    const auto signatureOf = [](FunctionType *type) {
        std::string text;
        if (type->withTypesCount() > 0) {
            text += "<";
            for (size_t i = 0; i < type->withTypesCount(); ++i) {
                text += (i ? ", " : "") + type->withTypeAt(i)->toString();
            }
            text += "> ";
        }
        text += "(";
        for (size_t i = 0; i < type->normTypesCount(); ++i) {
            text += (i ? ", " : "") + type->normTypeAt(i)->toString();
        }
        return text + ")";
    };
    bool found = false;
    for (const auto &[ref, imports] : module_->namesImportedMoreThanOnce()) {
        for (size_t later = 1; later < imports.size(); ++later) {
            bool reported = false;
            for (size_t earlier = 0; earlier < later && !reported; ++earlier) {
                const auto first = imports[earlier].mod->getExportedEntity(imports[earlier].remote);
                const auto second = imports[later].mod->getExportedEntity(imports[later].remote);
                if (!first || !second) {
                    continue;
                }
                using camel::core::module::detail::EntityAccess;
                // What the two imports bring that cannot stand side by side: two functions of
                // the same parameter types, or a value and anything else of that name (a value
                // is not overloaded).
                std::optional<std::string> clash;
                // A value: exported by a module written in Camel (a node of the root graph) or
                // a constant of a C++ module (design principles 10.1).
                const auto isValue = [](const camel::core::module::entity &e) {
                    return EntityAccess::isNode(e) || e.isValue();
                };
                if (isValue(*first) || isValue(*second)) {
                    const bool same =
                        (EntityAccess::isNode(*first) && EntityAccess::isNode(*second) &&
                         EntityAccess::node(*first) == EntityAccess::node(*second)) ||
                        (first->isValue() && second->isValue() &&
                         first->value() == second->value());
                    if (!same) {
                        clash = isValue(*first) && isValue(*second)
                                    ? std::string("a value in both")
                                    : std::string("a value in one of them");
                    }
                } else if (EntityAccess::isGraphSet(*first) && EntityAccess::isGraphSet(*second)) {
                    const auto firstSet  = asCompileGraphSet(EntityAccess::graphSet(*first));
                    const auto secondSet = asCompileGraphSet(EntityAccess::graphSet(*second));
                    for (const auto &f : *firstSet) {
                        for (const auto &g : *secondSet) {
                            if (!clash && f != g && sameSignature(f->funcType(), g->funcType())) {
                                clash = "the same signature " + signatureOf(f->funcType());
                            }
                        }
                    }
                }
                if (clash) {
                    auto diag         = diags_->of(SemanticDiag::ImportNameConflict);
                    const auto &here  = imports[later];
                    const auto &there = imports[earlier];
                    if (here.origin != camel::source::kInvalidOriginId) {
                        diag.atOrigin(here.origin);
                    } else {
                        diag.at(TokenRange{here.tokenStart, here.tokenEnd});
                    }
                    if (there.origin != camel::source::kInvalidOriginId) {
                        diag.relatedOrigin(there.origin, "also imported here");
                    } else {
                        diag.related(
                            TokenRange{there.tokenStart, there.tokenEnd},
                            "also imported here");
                    }
                    diag.commit(
                        ref.toString(),
                        imports[earlier].mod->name(),
                        imports[later].mod->name(),
                        *clash);
                    reported = true;
                    found    = true;
                }
            }
        }
    }
    if (found) {
        throw BuildAbortException();
    }
}

void Builder::markNonRetainingReads() {
    // A read `r` of a variable becomes `:var/peek`, which leaves the variable's sole-reference
    // mark set (Tuple::holdsUnsharedArray), when the array it reads can be referred to by
    // nothing but the variable once the users of `r` have run, and they all run before the
    // variable can be written again. Then the next `acc = acc->append<v>` may still change the
    // array in place: nobody can see the difference. The conditions, every one required:
    //
    //  1. `r` was made in a sync block, so the sync chain orders it (syncedReads_).
    //  2. `r` is not the value of its graph (returned, the exit): that value leaves.
    //  3. Every user of `r` is an operator node in the sync chain, declared pure (no state, so
    //     the result is all it leaves behind), whose result type holds no reference to an
    //     array: a number, a bool, a string or nothing (`len`, `contains`, `==`, `<`, `[]`
    //     of an array of numbers, `format`). A call, a branch, a closure, a literal holding
    //     the value, a `let` used later by such a node: any of them keeps `r` a `:var/get`.
    //  4. No node made between `r` and its last user may write the variable: no operator
    //     that is neither pure nor state-only (assignments, `:op/assn_append`, effects), no
    //     call and no branch (their bodies may write it through a capture). The chain runs
    //     the nodes in the order they were made, so the users of `r` run before any later
    //     writer.
    //  5. The graph has no writer outside the chain (made in a nested block that is not a
    //     sync block), which could run between `r` and its users.
    //
    // Anything not covered keeps the read a `:var/get`, which clears the mark: at worst the
    // next append copies, as it would without this. The decision is made here, once the
    // graph is complete; the passes treat both reads alike (oper::isVariableRead).
    auto &table              = camel::core::OperatorTable::instance();
    const auto referenceFree = [](Type *type) {
        return type != nullptr && (type->code() == TypeCode::String || !isGCTraced(type->code()));
    };
    const auto mayWrite = [&](node_handle_t node) {
        switch (nodeKindOf(node)) {
        case runtime::GCNodeKind::Oper: {
            const auto op     = nodeGraphOf(node)->operTarget(node);
            const auto traits = table.find(op->uri());
            return !traits.has_value() || (!traits->pure && !traits->stateOnly);
        }
        case runtime::GCNodeKind::Call:
        case runtime::GCNodeKind::Func:
        case runtime::GCNodeKind::Brch:
            return true;
        default:
            return false;
        }
    };
    // Condition 5, per graph.
    std::unordered_map<runtime::GraphDraft *, bool> unchainedWriter;
    const auto hasUnchainedWriter = [&](runtime::GraphDraft *draft) {
        if (auto it = unchainedWriter.find(draft); it != unchainedWriter.end()) {
            return it->second;
        }
        bool found = false;
        for (draft_node_ref_t id = 0; !found && id < draft->nodeSlotCount(); ++id) {
            if (draft->alive(id)) {
                node_handle_t node = draft->node(id);
                found              = mayWrite(node) && !chainedNodes_.contains(node);
            }
        }
        return unchainedWriter[draft] = found;
    };
    const auto peekGroup = context_->importModule("")->getExportedEntity(Reference("__var_peek__"));
    ASSERT(
        peekGroup.has_value() && peekGroup->isOperGroup(),
        "The built-in __var_peek__ is missing.");
    const std::vector<StaticArg> statics(1);

    for (node_handle_t read : syncedReads_) {
        runtime::GraphDraft *draft = nodeDraftOf(read);
        const draft_node_ref_t id  = nodeIdOf(read);
        if (!draft->alive(id) || id == draft->exitNode() || id == draft->outputNode() ||
            id == draft->returnNode()) {
            continue; // condition 2
        }
        draft_node_ref_t lastUser = id;
        bool keeps                = false;
        size_t users              = 0;
        for (auto inputs : {normUsersOf(read), withUsersOf(read)}) {
            for (draft_node_ref_t userId : inputs) {
                node_handle_t user = draft->node(userId);
                ++users;
                lastUser = std::max(lastUser, userId);
                if (!nodeIsKind(user, runtime::GCNodeKind::Oper) || !chainedNodes_.contains(user) ||
                    !table.isPure(nodeGraphOf(user)->operTarget(user)->uri()) ||
                    !referenceFree(nodeTypeOf(user))) {
                    keeps = true; // condition 3
                }
            }
        }
        if (keeps || users == 0) {
            continue;
        }
        for (draft_node_ref_t between = id + 1; !keeps && between < lastUser; ++between) {
            keeps = draft->alive(between) && mayWrite(draft->node(between)); // condition 4
        }
        if (keeps || hasUnchainedWriter(draft)) {
            continue;
        }
        node_handle_t cell = draft->node(normInputsOf(read).front());
        const auto peek =
            peekGroup->operGroup()->resolve({}, {nodeTypeOf(cell)}, statics, Modifier::None);
        ASSERT(peek.has_value(), "__var_peek__ rejected a variable.");
        nodeGraphOf(read)->retargetOperNode(read, *peek);
    }
    syncedReads_.clear();
}

void Builder::rejectDiscardedValue(const DraftGraphBuilder *graph) {
    if (auto it = bodyValueStatement_.find(graph); it != bodyValueStatement_.end()) {
        rejectUnusedPureResult(it->second.first, it->second.second);
    }
}

void Builder::rejectUnusedPureResult(const GCT::node_ptr_t &stmt, node_handle_t node) {
    if (node == nullptr || !nodeIsKind(node, runtime::GCNodeKind::Oper) ||
        discardedValues_.contains(node)) {
        return; // not an operator, or dropped on purpose (`let _`), or reported already
    }
    const auto op = nodeGraphOf(node)->operTarget(node);
    if (op == nullptr || !camel::core::OperatorTable::instance().isPure(op->uri())) {
        return;
    }
    // The call as written: `wait` around it changes nothing about its value.
    GCT::node_ptr_t call = stmt;
    if (call->type() == GCT::NodeKind::WAIT && call->size() == 1) {
        call = call->at(0);
    }
    if (call->type() != GCT::NodeKind::LINK || call->size() < 1) {
        return;
    }
    // The name the program wrote: the operator's symbol, or the function's name.
    GCT::node_ptr_t callee = call->at(0);
    while (callee->type() == GCT::NodeKind::WITH && callee->size() > 0) {
        callee = callee->at(0);
    }
    std::string written = callee->type() == GCT::NodeKind::DREF ? callee->name() : op->name();
    if (const char *symbol = operatorSymbolOf(written)) {
        written = symbol;
    }
    // When the operation is applied to a variable, the program most likely meant to change
    // it, as `xs->append<5>` did before 11.3: the fix is to bind the variable to the result.
    std::string fix;
    const std::string text = sourceTextOf(call);
    if (call->size() >= 2 && call->at(1)->type() == GCT::NodeKind::DREF &&
        nodeAt(call->at(1)->name()).has_value() &&
        isVariable(resolveNodeByRef(call->at(1)->name()))) {
        const std::string variable = call->at(1)->name();
        fix = text.empty() ? std::format(
                                 "; to change the variable '{0}', bind it to the result: "
                                 "'{0} = ...'",
                                 variable)
                           : std::format(
                                 "; to change the variable '{0}', bind it to the result: "
                                 "'{0} = {1}'",
                                 variable,
                                 text);
    } else {
        fix = "; use the result ('let ys = ...') or drop it on purpose with 'let _ = ...'";
    }
    diags_->of(SemanticDiag::UnusedPureResult).atOrigin(stmt->origin()).commit(written, fix);
    // Reported here; it is not also an unused `T!` (reportUnusedFallibleValues).
    discardedValues_.insert(node);
}

void Builder::rejectLiteralConversion(
    const std::string &group, FunctionType *funcType, const std::vector<Argument> &operands,
    const GCT::node_ptr_t &call) {
    static const std::unordered_set<std::string> kConversions =
        {"int", "i64", "i32", "float", "f64", "f32"};
    if (!kConversions.contains(group) || operands.size() != 1 || funcType == nullptr ||
        !funcType->hasExitType() || operands[0].expr->type() != GCT::NodeKind::DATA) {
        return;
    }
    // `int(1.0e30)`, `i32(5000000000)`, `int('12abc')`: the argument is written in the
    // program and the conversion is known not to fit, so it is refused here as a literal
    // given a type it cannot have is (`let b: i32 = 3000000000`). A computed argument stays
    // a fault when the program runs.
    const data_ptr_t data = operands[0].expr->data();
    Type *target          = funcType->exitType();
    if (data == nullptr) {
        return;
    }
    const bool text = data->type()->code() == TypeCode::String;
    if (!text && !data->type()->isPrimitive()) {
        return;
    }
    try {
        if (text) {
            // The rule of the conversion itself (camel/core/numeric_convert.h).
            const std::string &written = std::dynamic_pointer_cast<StringData>(data)->data();
            switch (target->code()) {
            case TypeCode::Int32:
                (void)camel::core::integerFromText<int32_t>(written, "i32");
                break;
            case TypeCode::Int64:
                (void)camel::core::integerFromText<int64_t>(written, "int");
                break;
            case TypeCode::Float32:
                (void)camel::core::floatFromText<float>(written, "f32");
                break;
            case TypeCode::Float64:
                (void)camel::core::floatFromText<double>(written, "float");
                break;
            default:
                break;
            }
        } else if (const auto value = staticArgOf(operands[0].node).value) {
            (void)target->castSlotFrom(*value, data->type());
        }
        return;
    } catch (const camel::core::error::RuntimeFault &) {
    }
    if (text) {
        diags_->of(SemanticDiag::LiteralStaticCastFailed)
            .atOrigin(call->origin())
            .commit(data->toString(), target->toString());
    } else {
        std::string limits = literalRangeViolation(data, target);
        if (limits.empty()) {
            limits = target->code() == TypeCode::Int32   ? "-2147483648 to 2147483647"
                     : target->code() == TypeCode::Int64 ? "-9223372036854775808 to "
                                                           "9223372036854775807"
                                                         : "the value does not fit the type";
        }
        diags_->of(SemanticDiag::LiteralOutOfRange)
            .atOrigin(call->origin())
            .commit(literalValueText(data), target->toString(), limits);
    }
    throw BuildAbortException();
}

void Builder::rejectUnappliedBinding(node_handle_t node, const GCT::node_ptr_t &origin) {
    const auto boundIt = boundWithArgs_.find(node);
    if (boundIt == boundWithArgs_.end()) {
        return;
    }
    const std::string given = strutil::join(boundIt->second, ", ", [&](const Argument &arg) {
        return valueTypeOf(nodeTypeOf(arg.node))->toString();
    });
    std::string name        = "the function";
    if (const auto callee = boundWithCallee_.find(node);
        callee != boundWithCallee_.end() && callee->second->type() == GCT::NodeKind::DREF) {
        name = callee->second->name();
    }
    std::string why;
    if (genericDrefs_.contains(node)) {
        why = "it is a generic function, whose type variables are fixed by a call";
    } else if (nodeIsKind(node, runtime::GCNodeKind::Dref)) {
        const auto &target = nodeGraphOf(node)->drefTarget(node);
        if (std::holds_alternative<graph_vec_ptr_t>(target)) {
            auto graphs = asCompileGraphVec(target);
            why         = std::format(
                "{} function(s) of that name, and not exactly one takes them [{}]; where "
                "several do, a declared type or a parameter must say which is meant",
                graphs ? graphs->size() : 0,
                graphs ? strutil::join(
                             *graphs,
                             "; ",
                             [](const graph_ptr_t &g) { return g->funcType()->toString(); })
                       : std::string());
        } else if (std::holds_alternative<oper_group_ptr_t>(target)) {
            const auto &ops = std::get<oper_group_ptr_t>(target);
            why             = std::format(
                "it is an operator whose signature depends on its arguments [{}], so it has "
                "no parameter types before it is called; write the call in a function, e.g. "
                "'(x: int) => {}<...>(x)'",
                strutil::join(
                    ops->resolvers(),
                    "; ",
                    [](const std::pair<std::string, resolver_ptr_t> &p) {
                        return p.second->signature();
                    }),
                name);
        } else {
            why = "it is a decorated function";
        }
    } else {
        why = "its type is '" + valueTypeOf(nodeTypeOf(node))->toString() + "'";
    }
    boundWithArgs_.erase(node);
    boundWithCallee_.erase(node);
    diags_->of(SemanticDiag::PartialApplicationRefused)
        .atOrigin(origin->origin())
        .commit(given, name, why);
    throw BuildAbortException();
}

node_handle_t
Builder::applyDecoratorAnno(const GCT::node_ptr_t &annoNode, node_handle_t funcValueNode) {
    ASSERT(annoNode && annoNode->type() == GCT::NodeKind::ANNO, "Expected ANNO node.");
    ASSERT(annoNode->size() == 1, "ANNO node should have exactly one lowered expression child.");
    ASSERT(funcValueNode != nullptr, "Decorator input function value is null.");

    // `@name` or `@name<args>`: either way the expression is the decorator to call.
    const auto &decorator = annoNode->at(0);

    const std::string tmpScopeName = "__decorator_tmp_scope_" + std::to_string(syntheticRefIndex_);
    const std::string tmpRefName   = "__decorator_tmp_ref_" + std::to_string(syntheticRefIndex_++);
    nodeScope_                     = nodeScope_->enter(tmpScopeName);
    nodeScope_->insert(tmpRefName, funcValueNode);

    GCT::node_ptr_t linkNode = tree_->makeLink(1);
    *linkNode << tree_->clone(decorator);
    *linkNode << tree_->makeDRef(tmpRefName);
    // A call of the decorator is reported at the decorator.
    linkNode->setOrigin(decorator->origin());
    linkNode->at(1)->setOrigin(decorator->origin());

    node_handle_t decoratedValue = visitLinkNode(linkNode);
    nodeScope_                   = nodeScope_->leave();
    return decoratedValue;
}

compile_graph_ptr_t Builder::buildDecoratedGraph(
    const std::string &funcName, const compile_graph_ptr_t &rawGraph,
    const std::vector<GCT::node_ptr_t> &annoNodes) {
    ASSERT(rawGraph != nullptr, "Raw function graph is null.");
    if (annoNodes.empty()) {
        return rawGraph;
    }

    // Decorators run at macro time: the graph applying them is a macro that yields the decorated
    // function once its inputs are static.
    // The factory is a separate graph, not a statement in the enclosing sync chain.
    // Otherwise applying a local decorator overwrites lastSyncedNode_ with a node
    // belonging to this factory, and the next enclosing call links across graphs.
    struct RestoreSync {
        bool &synced;
        node_handle_t &last;
        bool outerSynced;
        node_handle_t outerLast;
        ~RestoreSync() {
            synced = outerSynced;
            last   = outerLast;
        }
    } restoreSync{synced_, lastSyncedNode_, synced_, lastSyncedNode_};
    synced_                    = false;
    lastSyncedNode_            = {};
    graph_ptr_t decoratedGraph = enterScope(
        FunctionType::create(param_vec_t{}, param_vec_t{}, nullptr, Modifier::Macro),
        "__decorated__" + funcName + "_" + std::to_string(syntheticRefIndex_++));
    registerGraphOrigin(
        context_,
        decoratedGraph,
        annoNodes.back(),
        "gir.func.decorated.graph",
        {},
        {},
        true);

    // Seed value is the original function object.
    node_handle_t decoratedValue = createFuncDataNode(rawGraph, true, false);
    for (auto it = annoNodes.rbegin(); it != annoNodes.rend(); ++it) {
        decoratedValue = applyDecoratorAnno(*it, decoratedValue);
    }

    if (nodeTypeOf(decoratedValue)->code() != TypeCode::Function) {
        diags_->of(SemanticDiag::ArgumentsMismatch)
            .atOrigin(annoNodes.front()->origin())
            .commit("decorator must return function", nodeTypeOf(decoratedValue)->toString());
        throw BuildAbortException();
    }

    setGraphReturnFromResult(
        decoratedGraph,
        nodeIdOf(decoratedValue),
        nodeIdOf(decoratedValue),
        nodeTypeOf(decoratedValue));
    leaveScope();
    return decoratedGraph;
}

// ---- Decorators that grant attributes, decorated blocks (language reference 7.6) ------------

std::optional<camel::core::attr::AttrSetId> Builder::staticGrantOf(const GCT::node_ptr_t &anno) {
    if (auto it = staticGrants_.find(anno); it != staticGrants_.end()) {
        return it->second;
    }
    // Asked first where the decorator is written (visitFuncNode before it enters the
    // function, registerGenericTemplates): the names of the decorator and of its arguments are
    // resolved there.
    const auto compute = [&]() -> std::optional<camel::core::attr::AttrSetId> {
        const GCT::node_ptr_t expr = anno->at(0);
        GCT::node_ptr_t callee     = expr;
        std::vector<GCT::node_ptr_t> args;
        if (expr->type() == GCT::NodeKind::WITH) {
            callee = expr->at(0);
            for (size_t i = 1; i < expr->size(); ++i) {
                args.push_back(expr->at(i));
            }
        }
        // A name the program binds itself (a value, a function, a generic function) is not
        // the operator: the decorator is applied as any other.
        const auto programName = [&](const std::string &written) {
            const std::string name = moduleLevelName(written);
            return nodeAt(name).has_value() || templateScope_->get(name).has_value() ||
                   graphsAt(name).has_value() || decoratedGraphAt(name).has_value() ||
                   (unit_ != nullptr && unit_->values.contains(name));
        };
        // The entity an imported name stands for (design principles 10.2, layers 3 to 5).
        const auto imported =
            [&](const GCT::node_ptr_t &ref) -> std::optional<camel::core::module::entity> {
            const std::string written = ref->name();
            if (programName(written) || isOperatorName(written)) {
                return std::nullopt;
            }
            if (module_->hasImportedRef(written, ImportLayer::Named)) {
                return module_->getImportedEntity(written, ImportLayer::Named);
            }
            if (auto match = throughUsing(written, ref)) {
                return match->ownName.empty() ? match->entity : std::nullopt;
            }
            if (module_->hasImportedRef(written, ImportLayer::Ambient)) {
                return module_->getImportedEntity(written, ImportLayer::Ambient);
            }
            return std::nullopt;
        };
        if (callee->type() != GCT::NodeKind::DREF) {
            return std::nullopt;
        }
        const auto entity = imported(callee);
        if (!entity.has_value() || !entity->isOperGroup() ||
            entity->operGroup()->resolvers().size() != 1) {
            return std::nullopt;
        }
        const auto grant = camel::core::OperatorTable::instance().grantOf(
            entity->operGroup()->resolvers().front().first);
        if (grant == nullptr) {
            return std::nullopt;
        }
        // The with-arguments must be constants: literals, or constants a module exports.
        std::vector<data_ptr_t> values;
        for (const GCT::node_ptr_t &arg : args) {
            if (arg->type() == GCT::NodeKind::DATA) {
                values.push_back(arg->data());
                continue;
            }
            if (arg->type() != GCT::NodeKind::DREF) {
                return std::nullopt;
            }
            const auto value = imported(arg);
            if (!value.has_value() || !value->isValue()) {
                return std::nullopt;
            }
            values.push_back(value->value());
        }
        const std::optional<camel::core::attr::AttrValueId> granted =
            grant->value ? grant->value(values)
                         : std::optional<camel::core::attr::AttrValueId>(camel::core::attr::unit());
        if (!granted.has_value()) {
            return std::nullopt;
        }
        return camel::core::attr::single(camel::core::attr::key(grant->key), *granted);
    };
    std::optional<camel::core::attr::AttrSetId> result;
    try {
        result = compute();
    } catch (const BuildAbortException &) {
        result = std::nullopt; // reported, if at all, when the decorator is applied
    }
    staticGrants_.emplace(anno, result);
    return result;
}

std::pair<camel::core::attr::AttrSetId, size_t>
Builder::staticGrantsOf(const GCT::node_ptr_t &func) {
    std::vector<GCT::node_ptr_t> annos;
    for (size_t i = 2; i < func->size(); ++i) {
        if (func->at(i) && func->at(i)->type() == GCT::NodeKind::ANNO) {
            annos.push_back(func->at(i));
        }
    }
    camel::core::attr::AttrSetId granted = camel::core::attr::kNoAttrs;
    size_t count                         = 0;
    // The decorator written last applies first: a run of granting ones from there is applied
    // to the function as written, before any other decorator sees it.
    for (auto it = annos.rbegin(); it != annos.rend(); ++it) {
        const auto grant = staticGrantOf(*it);
        if (!grant.has_value()) {
            break;
        }
        granted = camel::core::attr::unite(granted, *grant);
        ++count;
    }
    return {granted, count};
}

std::vector<GCT::node_ptr_t> Builder::appliedDecoratorsOf(const GCT::node_ptr_t &func) {
    std::vector<GCT::node_ptr_t> annos;
    for (size_t i = 2; i < func->size(); ++i) {
        if (func->at(i) && func->at(i)->type() == GCT::NodeKind::ANNO) {
            annos.push_back(func->at(i));
        }
    }
    annos.resize(annos.size() - staticGrantsOf(func).second);
    return annos;
}

node_handle_t Builder::visitDecoratedBlock(const GCT::node_ptr_t &link, bool unused) {
    ENTER("DECORATED_BLOCK");
    // `@d { ... }` is the block as a function of no parameters, passed to the decorators and
    // called right where it is written (language reference 7.6). The block is lowered as a
    // plain block would be (its scope, its exits, its value); the function's value is the
    // block's, and is dropped when nothing takes the block's value.
    const GCT::node_ptr_t func = link->at(0);
    const GCT::node_ptr_t body = func->at(1);
    staticGrantsOf(func); // resolved here, where the decorators are written
    const std::vector<GCT::node_ptr_t> decorators = appliedDecoratorsOf(func);
    if (decorators.empty()) {
        // Every decorator grants attributes as the program is built: what they make is the
        // block's own function with the attributes, called here, which is the block itself
        // with the attributes. Built in place, as the same block without decorators is.
        AttrScope attrs(
            currentAttrs_,
            camel::core::attr::unite(currentAttrs_, staticGrantsOf(func).first));
        unusedStatement_     = unused ? body : nullptr;
        node_handle_t result = visitExecNode(body);
        LEAVE("DECORATED_BLOCK");
        return result;
    }
    if (unused) {
        droppedBodies_.insert(body);
    }
    compile_graph_ptr_t graph = visitFuncNode(func);
    blockGraphs_.insert(graph.get());
    node_handle_t value = createFuncDataNode(graph, true, false);
    for (auto it = decorators.rbegin(); it != decorators.rend(); ++it) {
        value = applyDecoratorAnno(*it, value);
    }
    // The call, as if written here: `(decorated)()`.
    const std::string scopeName = "__block_tmp_scope_" + std::to_string(syntheticRefIndex_);
    const std::string refName   = "__block_tmp_ref_" + std::to_string(syntheticRefIndex_++);
    nodeScope_                  = nodeScope_->enter(scopeName);
    nodeScope_->insert(refName, value);
    GCT::node_ptr_t call = tree_->makeLink(0);
    GCT::node_ptr_t ref  = tree_->makeDRef(refName);
    ref->setOrigin(link->origin());
    call->setOrigin(link->origin());
    *call << ref;
    node_handle_t result{};
    try {
        result = visitLinkNode(call);
    } catch (...) {
        nodeScope_ = nodeScope_->leave();
        throw;
    }
    nodeScope_ = nodeScope_->leave();
    LEAVE("DECORATED_BLOCK");
    return result;
}

graph_set_ptr_t
Builder::overloadsOfDecorated(const std::string &name, const compile_graph_ptr_t &decorated) {
    auto graphs = graphsAt(name);
    if (!graphs.has_value() || graphs.value()->size() < 2) {
        return nullptr;
    }
    for (const auto &g : *graphs.value()) {
        auto it = decoratedOf_.find(g.get());
        if (it != decoratedOf_.end() && it->second == decorated) {
            return graphs.value();
        }
    }
    return nullptr;
}

node_handle_t Builder::lowerDecoratedGraphValue(
    const compile_graph_ptr_t &decoratedGraph, const GCT::node_ptr_t &origin) {
    currGraph_->addDependencyGraph(decoratedGraph);
    if (!decoratedGraph->funcType()->hasExitType() ||
        decoratedGraph->funcType()->exitType()->code() != TypeCode::Function) {
        diags_->of(SemanticDiag::ArgumentsMismatch)
            .atOrigin(origin->origin())
            .commit(
                "decorated graph should return function",
                decoratedGraph->funcType()->toString());
        throw BuildAbortException();
    }
    node_handle_t decoratorFactoryValue = createFuncDataNode(decoratedGraph, true, false);
    node_handle_t decoratorInvoke = currGraph_->addCallNode(decoratedGraph->funcType()->exitType());
    linkNodes(LinkType::With, decoratorFactoryValue, decoratorInvoke);
    return decoratorInvoke;
}

std::string Builder::sourceTextOf(const GCT::node_ptr_t &expr) const {
    const auto *sourceContext = context_ ? context_->sourceContext().get() : nullptr;
    if (sourceContext == nullptr || expr == nullptr) {
        return {};
    }
    // The span of a node covers what the node itself was made from (the argument list of a
    // call, the operator of an expression): the expression is the union with its operands.
    camel::source::source_file_id_t fileId = camel::source::kInvalidSourceFileId;
    size_t start = 0, end = 0;
    bool consistent                                         = true;
    std::function<void(const GCT::node_ptr_t &, int)> cover = [&](const GCT::node_ptr_t &node,
                                                                  int depth) {
        if (node == nullptr || depth > 32) {
            return;
        }
        const auto *record = sourceContext->origin(node->origin());
        const auto *span   = record ? sourceContext->span(record->primarySpan) : nullptr;
        if (span != nullptr && span->valid() && span->endOffset > span->startOffset) {
            if (fileId == camel::source::kInvalidSourceFileId) {
                fileId = span->fileId;
                start  = span->startOffset;
                end    = span->endOffset;
            } else if (fileId != span->fileId) {
                consistent = false;
            } else {
                start = std::min(start, span->startOffset);
                end   = std::max(end, span->endOffset);
            }
        }
        for (size_t i = 0; i < node->size(); ++i) {
            cover(node->at(i), depth + 1);
        }
    };
    cover(expr, 0);
    const auto *file =
        fileId != camel::source::kInvalidSourceFileId ? sourceContext->sourceFile(fileId) : nullptr;
    if (!consistent || file == nullptr || end > file->content.size() || end - start > 60) {
        return {};
    }
    const std::string text = file->content.substr(start, end - start);
    // Quoted only when it is certainly one expression on one line.
    int depth = 0;
    for (const char c : text) {
        if (c == '\r' || c == '\n') {
            return {};
        }
        depth += (c == '(' || c == '[' || c == '{')   ? 1
                 : (c == ')' || c == ']' || c == '}') ? -1
                                                      : 0;
        if (depth < 0) {
            return {};
        }
    }
    return depth == 0 ? text : std::string();
}

std::string Builder::sourceLocationOf(uint64_t origin) const {
    const auto *sourceContext = context_ ? context_->sourceContext().get() : nullptr;
    const auto *record        = sourceContext ? sourceContext->origin(origin) : nullptr;
    const auto *span          = record ? sourceContext->span(record->primarySpan) : nullptr;
    const auto *file = span && span->valid() ? sourceContext->sourceFile(span->fileId) : nullptr;
    if (file == nullptr || span->startOffset > file->content.size()) {
        return {};
    }
    size_t line = 1, lineStart = 0;
    for (size_t i = 0; i < span->startOffset; ++i) {
        if (file->content[i] == '\n') {
            ++line;
            lineStart = i + 1;
        }
    }
    // The file name only: an error value must not differ with where the program is stored.
    const size_t slash = file->path.find_last_of("/\\");
    return std::format(
        "{}:{}:{}",
        slash == std::string::npos ? file->path : file->path.substr(slash + 1),
        line,
        span->startOffset - lineStart + 1);
}

void Builder::registerTreeOrigin(
    node_handle_t node, const GCT::node_ptr_t &origin, const char *kind) {
    registerNodeOrigin(context_, node, origin, kind);
}

node_handle_t
Builder::applyTagOperator(const char *op, const std::string &name, const GCT::node_ptr_t &origin) {
    // An ordinary call, built from a synthesized GCT so that capturing the operand from an
    // enclosing graph, overload resolution and origins are what they are for any call.
    const auto make = [&](GCT::node_ptr_t node) {
        node->setOrigin(origin->origin());
        return node;
    };
    GCT::node_ptr_t call = make(tree_->makeLink(1));
    *call << make(tree_->makeDRef(op)) << make(tree_->makeDRef(name));
    return visitLinkNode(call);
}

// ---- The enum (design principles 5.3) ----

namespace {

/// `Shape::Rect` gives ("Shape", "Rect"). A name that is not qualified gives an empty prefix.
std::pair<std::string, std::string> splitQualifiedName(const std::string &written) {
    const size_t last = written.rfind("::");
    if (last == std::string::npos) {
        return {{}, written};
    }
    return {written.substr(0, last), written.substr(last + 2)};
}

std::string lastSegmentOf(const std::string &path) {
    const size_t last = path.rfind("::");
    return last == std::string::npos ? path : path.substr(last + 2);
}

} // namespace

EnumType *Builder::enumTypeOfName(const Reference &name) {
    Type *resolved = nullptr;
    if (const auto declared = unit_->typeDecls.find(name); declared != unit_->typeDecls.end()) {
        resolved = declared->second;
    } else if (const auto imported = module_->getImportedType(name); imported.has_value()) {
        resolved = imported.value();
    }
    if (resolved == nullptr) {
        return nullptr;
    }
    if (EnumType *instance = asEnumType(resolved)) {
        return instance;
    }
    if (resolved->code() == TypeCode::TypeFunc) {
        // A generic enum written without its arguments (`Option`): the declaration's own
        // instance, whose arguments are its parameters; a value of it is made by inference.
        return asEnumType(static_cast<TypeFunctionType *>(resolved)->body());
    }
    return nullptr;
}

bool Builder::enumVariantOfName(
    const std::string &written, const GCT::node_ptr_t &origin, EnumType *&type, size_t &tag) {
    const auto [prefix, name] = splitQualifiedName(written);
    if (prefix.empty()) {
        return false;
    }
    EnumType *found = enumTypeOfName(Reference(prefix));
    if (found == nullptr) {
        return false; // a qualified name of something else (`io::println`)
    }
    const auto variant = found->decl()->findVariant(name);
    if (!variant.has_value()) {
        diags_->of(SemanticDiag::EnumPatternMismatch)
            .atOrigin(origin->origin())
            .commit(
                written,
                found->toString(),
                "'" + name + "' is not one of its variants (" + found->decl()->name() +
                    " declares " +
                    [&] {
                        std::string list;
                        for (const EnumVariant &each : found->decl()->variants()) {
                            if (!list.empty()) {
                                list += ", ";
                            }
                            list += each.name;
                        }
                        return list;
                    }() +
                    ")");
        throw BuildAbortException();
    }
    type = found;
    tag  = *variant;
    return true;
}

node_handle_t Builder::applyInternalOperator(
    const char *op, Type *typeArgument, const std::vector<GCT::node_ptr_t> &operands,
    const GCT::node_ptr_t &origin) {
    GCT::node_ptr_t callee = tree_->makeDRef(op);
    if (typeArgument != nullptr) {
        // The instance the call makes, as `f::<T>` gives one: the operator's result type is
        // that instance, and its slot is of it (a value of an enum type holds references).
        GCT::node_ptr_t application = tree_->makeTypeApplication();
        *application << callee << tree_->makeType(typeArgument, ImplMark::Graph);
        callee = application;
    }
    GCT::node_ptr_t call = tree_->makeLink(static_cast<uint32_t>(operands.size()));
    *call << callee;
    for (const auto &operand : operands) {
        *call << operand;
    }
    call->setOrigin(origin->origin());
    return visitLinkNode(call);
}

node_handle_t
Builder::visitEnumConstruction(const GCT::node_ptr_t &gct, EnumType *type, size_t tag) {
    const EnumDecl *decl       = type->decl();
    const EnumVariant &variant = decl->variants()[tag];
    const bool called          = gct->type() == GCT::NodeKind::LINK;
    const size_t argumentCount = called ? gct->size() - 1 : 0;

    std::vector<node_handle_t> fields;
    std::vector<Type *> fieldTypes; // what the arguments are, before the field types are known
    if (variant.shape == VariantShape::Struct) {
        if (argumentCount != 1) {
            diags_->of(SemanticDiag::EnumConstructionMismatch)
                .atOrigin(gct->origin())
                .commit(
                    decl->shortName() + "::" + variant.name,
                    decl->name(),
                    "its fields are given by name: '" + decl->shortName() + "::" + variant.name +
                        " { " +
                        [&] {
                            std::string list;
                            for (const std::string &field : variant.fieldNames) {
                                if (!list.empty()) {
                                    list += ", ";
                                }
                                list += field + ": ...";
                            }
                            return list;
                        }() +
                        " }'");
            throw BuildAbortException();
        }
        node_handle_t literal = visitOperand(gct->at(1), gct);
        Type *literalType     = valueTypeOf(nodeTypeOf(literal));
        if (literalType->code() != TypeCode::Struct) {
            diags_->of(SemanticDiag::EnumConstructionMismatch)
                .atOrigin(gct->at(1)->origin())
                .commit(
                    decl->shortName() + "::" + variant.name,
                    decl->name(),
                    "the value has type '" + literalType->toString() +
                        "', and the fields of a struct variant are given by name");
            throw BuildAbortException();
        }
        StructType *literalStruct = tt::as_ptr<StructType>(literalType);
        for (size_t i = 0; i < variant.fieldCount(); ++i) {
            const std::string &fieldName = variant.fieldNames[i];
            const auto position          = literalStruct->findField(fieldName);
            if (!position.has_value()) {
                diags_->of(SemanticDiag::EnumConstructionMismatch)
                    .atOrigin(gct->at(1)->origin())
                    .commit(
                        decl->shortName() + "::" + variant.name,
                        decl->name(),
                        "the value has no field '" + fieldName + "'");
                throw BuildAbortException();
            }
            Type *fieldType         = literalStruct->typeAt(*position);
            node_handle_t fieldNode = currGraph_->addAccsNode(fieldType, fieldName);
            linkNodes(LinkType::Norm, literal, fieldNode);
            registerNodeOrigin(context_, fieldNode, gct->at(1), "gir.accs");
            fields.push_back(fieldNode);
            fieldTypes.push_back(fieldType);
        }
        if (literalStruct->size() != variant.fieldCount()) {
            diags_->of(SemanticDiag::EnumConstructionMismatch)
                .atOrigin(gct->at(1)->origin())
                .commit(
                    decl->shortName() + "::" + variant.name,
                    decl->name(),
                    "the value has fields the variant does not: a struct variant takes exactly "
                    "the fields its declaration writes");
            throw BuildAbortException();
        }
    } else {
        if (argumentCount != variant.fieldCount()) {
            diags_->of(SemanticDiag::EnumVariantFieldCount)
                .atOrigin(gct->origin())
                .commit(
                    variant.name,
                    decl->name(),
                    std::to_string(variant.fieldCount()),
                    std::to_string(argumentCount));
            throw BuildAbortException();
        }
        for (size_t i = 0; i < argumentCount; ++i) {
            node_handle_t argument = visitOperand(gct->at(i + 1), gct);
            fields.push_back(argument);
            fieldTypes.push_back(valueTypeOf(nodeTypeOf(argument)));
        }
    }

    // The instance: the declaration's own when it is not generic, otherwise the arguments the
    // fields give (a field left open takes what the position expects, as `let x: Option<int>
    // = Option::None` does).
    EnumType *instance = type;
    if (decl->generic()) {
        type_bindings_t bindings;
        for (size_t i = 0; i < fields.size(); ++i) {
            unifyTypeVars(variant.fieldTypes[i], fieldTypes[i], bindings);
        }
        std::vector<Type *> arguments;
        arguments.reserve(decl->params().size());
        EnumType *expected = asEnumType(expectedValueType_);
        for (size_t i = 0; i < decl->params().size(); ++i) {
            Type *bound = findTypeBinding(bindings, decl->params()[i]);
            if (bound == nullptr && expected != nullptr && expected->decl() == decl &&
                expected->complete()) {
                bound = expected->params()[i];
            }
            if (bound == nullptr) {
                diags_->of(SemanticDiag::EnumTypeArgumentNotInferred)
                    .atOrigin(gct->origin())
                    .commit(decl->params()[i], decl->name(), variant.name);
                throw BuildAbortException();
            }
            arguments.push_back(bound);
        }
        instance = EnumType::get(const_cast<EnumDecl *>(decl), arguments);
    }

    // Each field as the variant of this instance has it.
    std::vector<GCT::node_ptr_t> operands;
    operands.push_back(tree_->makeData(
        tt::as_shared<Data>(std::make_shared<PrimaryData<int64_t>>(static_cast<int64_t>(tag)))));
    for (size_t i = 0; i < fields.size(); ++i) {
        Type *wanted          = instance->fieldType(tag, i);
        node_handle_t coerced = coerceTo(fields[i], wanted, gct);
        if (coerced == nullptr) {
            diags_->of(SemanticDiag::EnumFieldTypeMismatch)
                .atOrigin(gct->origin())
                .commit(
                    variant.shape == VariantShape::Struct ? "'" + variant.fieldNames[i] + "'"
                                                          : std::to_string(i),
                    decl->shortName() + "::" + variant.name,
                    wanted->toString(),
                    fieldTypes[i]->toString());
            throw BuildAbortException();
        }
        operands.push_back(tree_->makeDRef(bindTemporary(coerced)));
    }
    return applyInternalOperator("__enum_make__", instance, operands, gct);
}

node_handle_t
Builder::visitEnumMatch(const GCT::node_ptr_t &gct, node_handle_t scrutinee, EnumType *type) {
    using CaseType = GCT::CaseType;
    struct Arm {
        const GCT::node_ptr_t caseNode;
        size_t tag = 0;
        // The position of the field in the variant, and the name the arm binds it to.
        std::vector<std::pair<size_t, std::string>> bindings;
    };

    const std::string name      = bindTemporary(scrutinee);
    const node_handle_t tagNode = applyTagOperator("__enum_tag__", name, gct);
    const std::string tagName   = bindTemporary(tagNode);

    std::vector<Arm> arms;
    const GCT::node_ptr_t wildcard = [&] {
        for (size_t i = 1; i < gct->size(); ++i) {
            if (gct->at(i)->caseType() == CaseType::Else) {
                return gct->at(i);
            }
        }
        return GCT::node_ptr_t(nullptr);
    }();
    std::vector<bool> covered(type->variantCount(), false);
    for (size_t i = 1; i < gct->size(); ++i) {
        const GCT::node_ptr_t &caseNode = gct->at(i);
        if (caseNode->caseType() == CaseType::Else) {
            break; // the last case
        }
        if (caseNode->caseType() != CaseType::Enum) {
            // A bare name is a binding pattern everywhere else (`case v` over `T?` / `T!`), so
            // `case Empty` over an enum is read as a binding, not as the unit variant: say what
            // the name refers to and how the variant is written, instead of only that a case
            // must name a variant.
            std::string written = "case";
            std::string reason =
                "every case of a match over an enum names a variant, and the value a case "
                "does not name falls to 'case _'";
            if (caseNode->caseType() == CaseType::Bind) {
                written        = caseNode->binding();
                const auto tag = type->decl()->findVariant(written);
                if (tag.has_value()) {
                    const EnumVariant &variant = type->decl()->variants()[*tag];
                    const std::string how = variant.shape == VariantShape::Unit    ? "'"
                                            : variant.shape == VariantShape::Tuple ? "(...)'"
                                                                                   : " { ... }'";
                    reason = "'" + written +
                             "' is a variant of this enum, and a bare name in a pattern is a "
                             "binding; write the variant qualified: 'case " +
                             type->decl()->shortName() + "::" + written + how +
                             ", or 'case _' for every other value";
                } else {
                    reason = "'" + written +
                             "' is a bare name, and a bare name in a pattern is a binding; a "
                             "match over an enum names a variant: write 'case " +
                             type->decl()->shortName() + "::<variant>', or 'case _'";
                }
            }
            diags_->of(SemanticDiag::EnumPatternMismatch)
                .atOrigin(caseNode->origin())
                .commit(written, type->toString(), reason);
            throw BuildAbortException();
        }
        const GCT::Node::EnumCase &pattern = caseNode->enumCase();
        const auto [prefix, variantName]   = splitQualifiedName(*pattern.label);
        if (!prefix.empty() && lastSegmentOf(prefix) != lastSegmentOf(type->decl()->name())) {
            diags_->of(SemanticDiag::EnumPatternMismatch)
                .atOrigin(caseNode->origin())
                .commit(*pattern.label, type->toString(), "it is a variant of another enum");
            throw BuildAbortException();
        }
        const auto tag = type->decl()->findVariant(variantName);
        if (!tag.has_value()) {
            diags_->of(SemanticDiag::EnumPatternMismatch)
                .atOrigin(caseNode->origin())
                .commit(
                    *pattern.label,
                    type->toString(),
                    "'" + variantName + "' is not one of its variants");
            throw BuildAbortException();
        }
        const EnumVariant &variant = type->decl()->variants()[*tag];
        const bool structPattern   = caseNode->enumStructPattern();
        if (structPattern != (variant.shape == VariantShape::Struct)) {
            diags_->of(SemanticDiag::EnumPatternMismatch)
                .atOrigin(caseNode->origin())
                .commit(
                    *pattern.label,
                    type->toString(),
                    variant.shape == VariantShape::Struct
                        ? "this variant's fields are named: write 'case " + variantName + " { " +
                              [&] {
                                  std::string list;
                                  for (const std::string &field : variant.fieldNames) {
                                      if (!list.empty()) {
                                          list += ", ";
                                      }
                                      list += field;
                                  }
                                  return list;
                              }() +
                              " }'"
                        : "this variant's fields are in order: write 'case " + variantName + "(" +
                              [&] {
                                  std::string list;
                                  for (size_t f = 0; f < variant.fieldCount(); ++f) {
                                      if (!list.empty()) {
                                          list += ", ";
                                      }
                                      list += "_";
                                  }
                                  return list;
                              }() +
                              ")'");
            throw BuildAbortException();
        }
        Arm arm{caseNode, *tag, {}};
        if (structPattern) {
            for (size_t f = 0; f < pattern.count; ++f) {
                const std::string &fieldName = *pattern.fields[f];
                const auto position          = variant.findField(fieldName);
                if (!position.has_value()) {
                    diags_->of(SemanticDiag::EnumPatternMismatch)
                        .atOrigin(caseNode->origin())
                        .commit(
                            *pattern.label,
                            type->toString(),
                            "the variant has no field '" + fieldName + "'");
                    throw BuildAbortException();
                }
                arm.bindings.emplace_back(*position, *pattern.bindings[f]);
            }
            // A field written twice binds one position twice: the later binding was read
            // first, silently.
            for (size_t a = 0; a < arm.bindings.size(); ++a) {
                for (size_t b = a + 1; b < arm.bindings.size(); ++b) {
                    if (arm.bindings[a].first == arm.bindings[b].first) {
                        diags_->of(SemanticDiag::EnumPatternMismatch)
                            .atOrigin(caseNode->origin())
                            .commit(
                                *pattern.label,
                                type->toString(),
                                "the field '" + variant.fieldNames[arm.bindings[a].first] +
                                    "' is written twice");
                        throw BuildAbortException();
                    }
                }
            }
        } else {
            if (pattern.count != variant.fieldCount()) {
                diags_->of(SemanticDiag::EnumPatternMismatch)
                    .atOrigin(caseNode->origin())
                    .commit(
                        *pattern.label,
                        type->toString(),
                        "the variant has " + std::to_string(variant.fieldCount()) +
                            " field(s) and the pattern gives " + std::to_string(pattern.count) +
                            "; write each position, '_' for the one not bound");
                throw BuildAbortException();
            }
            for (size_t f = 0; f < pattern.count; ++f) {
                arm.bindings.emplace_back(f, *pattern.bindings[f]);
            }
        }
        if (covered[*tag]) {
            diags_->of(SemanticDiag::EnumVariantCaseRepeated)
                .atOrigin(caseNode->origin())
                .commit(variantName);
        }
        covered[*tag] = true;
        arms.push_back(std::move(arm));
    }
    if (wildcard == nullptr) {
        std::string missing;
        for (size_t tag = 0; tag < type->variantCount(); ++tag) {
            if (!covered[tag]) {
                if (!missing.empty()) {
                    missing += ", ";
                }
                missing += "'" + type->decl()->variants()[tag].name + "'";
            }
        }
        if (!missing.empty()) {
            diags_->of(SemanticDiag::EnumMatchNotExhaustive)
                .atOrigin(gct->origin())
                .commit(type->toString(), missing);
            throw BuildAbortException();
        }
    } else {
        // Naming every variant already covers every value: the wildcard is a case that can
        // never be taken. It stays in the graph (the request asked for it), but say so.
        bool everyVariantCovered = true;
        for (bool each : covered) {
            if (!each) {
                everyVariantCovered = false;
                break;
            }
        }
        if (everyVariantCovered) {
            diags_->of(SemanticDiag::MatchWildcardRedundant)
                .atOrigin(wildcard->origin())
                .commit(type->toString());
        }
    }

    const auto indexData = [&](size_t index) {
        return tree_->makeData(
            tt::as_shared<Data>(
                std::make_shared<PrimaryData<int64_t>>(static_cast<int64_t>(index))));
    };
    // The fields the case binds, each taken out of the value by the position of the variant the
    // case names: the variant is known here, so the type of each field is known too.
    const auto bindFields = [&](const Arm &arm) {
        for (const auto &[index, binding] : arm.bindings) {
            if (binding.empty()) {
                continue;
            }
            node_handle_t fieldNode = applyInternalOperator(
                "__enum_field__",
                nullptr,
                {indexData(arm.tag), indexData(index), tree_->makeDRef(name)},
                arm.caseNode);
            insertNode(binding, fieldNode);
        }
    };
    // The arm that runs when no case names the value: the wildcard the program wrote, or,
    // when it wrote every variant, an arm that cannot be reached. The second is needed all
    // the same: the branch it ends offers the join one value of the arms' type.
    std::function<compile_graph_ptr_t()> defaultArm = [&]() -> compile_graph_ptr_t {
        if (wildcard != nullptr) {
            return buildArmGraph(wildcard->at(0), [&] { return visitBodyNode(wildcard->at(0)); });
        }
        const Arm &last = arms.back();
        return buildArmGraph(last.caseNode, [&] {
            bindFields(last);
            return visitBodyNode(last.caseNode);
        });
    };

    // Every case of the value is one test of the tag: `tag == k`, an ordinary comparison,
    // whose true arm is the body of that case and whose false arm is the test of the next.
    // A branch on a boolean has no With inputs, so the case values of the GCT (variant
    // names) never reach a node.
    std::function<node_handle_t(size_t)> branchAt = [&](size_t k) -> node_handle_t {
        const Arm &arm          = arms[k];
        node_handle_t condition = applyInternalOperator(
            "__eq__",
            nullptr,
            {tree_->makeDRef(tagName), indexData(arm.tag)},
            arm.caseNode);
        std::vector<compile_graph_ptr_t> two(2);
        two[0] = buildArmGraph(arm.caseNode, [&] {
            bindFields(arm);
            return visitBodyNode(arm.caseNode);
        });
        if (k + 1 < arms.size()) {
            const GCT::node_ptr_t next = arms[k + 1].caseNode;
            two[1]                     = buildArmGraph(next, [&] { return branchAt(k + 1); });
        } else {
            two[1] = defaultArm();
        }
        return buildBranch(gct, condition, &two);
    };
    if (arms.empty()) {
        // `match e { case _ => ... }`: nothing is tested; the only arm is the value. The
        // branch has one arm and no case value, so the arm the runtime chooses is that one.
        node_handle_t always = currGraph_->addStaticDataNode(
            tt::as_shared<Data>(std::make_shared<PrimaryData<bool>>(true)));
        registerNodeOrigin(context_, always, gct, "gir.data");
        std::vector<compile_graph_ptr_t> one(1);
        one[0] = buildArmGraph(wildcard->at(0), [&] { return visitBodyNode(wildcard->at(0)); });
        return buildBranch(gct, always, &one);
    }
    return branchAt(0);
}

void Builder::reportConversionGaps(
    const std::vector<std::pair<std::string, FunctionType *>> &candidates,
    const std::vector<Argument> &withInputs, const std::vector<Argument> &normInputs,
    const type_vec_t &withTypes, const type_vec_t &normTypes, const GCT::node_ptr_t &call) {
    // Reported only when exactly one overload is a conversion away: with several, which one
    // the program means is not the compiler's to choose.
    const std::pair<std::string, FunctionType *> *target = nullptr;
    std::string what, fixes;
    for (const auto &candidate : candidates) {
        FunctionType *funcType = candidate.second;
        if (funcType->withTypesCount() != withTypes.size() ||
            funcType->normTypesCount() != normTypes.size()) {
            continue;
        }
        bool fits = true;
        std::string graphWhat, graphFixes;
        const auto check = [&](const std::vector<Argument> &inputs,
                               const type_vec_t &types,
                               bool isWith) {
            for (size_t i = 0; i < types.size() && fits; ++i) {
                Type *want       = isWith ? funcType->withTypeAt(i) : funcType->normTypeAt(i);
                const bool isVar = isWith ? funcType->withIsVarAt(i) : funcType->normIsVarAt(i);
                std::vector<ConversionGap> gaps;
                if (!collectConversionGaps(want, valueTypeOf(types[i]), "", gaps)) {
                    fits = false;
                    return;
                }
                if (gaps.empty()) {
                    continue;
                }
                const size_t nameIndex = (isWith ? 0 : funcType->withTypesCount()) + i;
                const std::string name = nameIndex < funcType->argNamesCount()
                                             ? std::string(funcType->argNameAt(nameIndex))
                                             : std::string();
                const std::string text = sourceTextOf(inputs[i].expr);
                std::string subject =
                    std::string(isWith ? "with-argument " : "argument ") + std::to_string(i + 1);
                if (!text.empty() || !name.empty()) {
                    subject += " (" + (text.empty() ? "" : "'" + text + "'") +
                               (text.empty() || name.empty() ? "" : ", ") +
                               (name.empty() ? "" : "for parameter '" + name + "'") + ")";
                }
                graphWhat += (graphWhat.empty() ? "" : "; ") + describeGaps(subject, gaps);
                if (isVar) {
                    // A conversion yields a value; a var parameter takes the variable itself.
                    graphFixes += std::string(graphFixes.empty() ? "" : " ") +
                                  "No conversion applies to argument " + std::to_string(i + 1) +
                                  ": a var parameter takes the variable itself, and a variable " +
                                  "has the widened type of its first value. Pass the value to a " +
                                  "parameter that is not var, or declare the var parameter with " +
                                  "the variable's type and convert the value where it is read.";
                    continue;
                }
                std::string conversions;
                for (const ConversionGap &gap : gaps) {
                    conversions += (conversions.empty() ? "" : ", ") +
                                   conversionTextOf(text.empty() ? "<argument>" : text, gap);
                }
                graphFixes +=
                    std::string(graphFixes.empty() ? "" : " ") +
                    "Where the value is known to fit, write the conversion: " + conversions + ".";
            }
        };
        check(withInputs, withTypes, true);
        check(normInputs, normTypes, false);
        if (!fits || graphWhat.empty()) {
            continue;
        }
        if (target) {
            return;
        }
        target = &candidate;
        what   = std::move(graphWhat);
        fixes  = std::move(graphFixes);
    }
    if (!target) {
        return;
    }
    diags_->of(SemanticDiag::ArgumentNeedsConversion)
        .atOrigin(call->origin())
        .commit(
            (target->first.empty() ? "" : target->first + ": ") + target->second->toString(),
            what,
            fixes);
    throw BuildAbortException();
}

void Builder::checkDeclaredReturn(
    const compile_graph_ptr_t &graph, Type *resultType, uint64_t origin,
    const GCT::node_ptr_t &value) {
    // Only a DECLARED type is checked: an undeclared one is set from the first value returned,
    // after which hasExitType() is true as well, so the check runs before that happens.
    if (graph == nullptr || graph->draft().exitNode() != runtime::kInvalidNodeRef) {
        return;
    }
    FunctionType *funcType = graph->funcType();
    if (funcType == nullptr || !funcType->hasExitType() || resultType == nullptr) {
        return;
    }
    Type *declared = funcType->exitType();
    // Not judged here: a body without a value (its last statement decides nothing about the
    // type), and types that are not resolved yet.
    if (declared == nullptr || declared == resultType || resultType->code() == TypeCode::Void ||
        declared->code() == TypeCode::Ref || resultType->code() == TypeCode::Ref) {
        return;
    }
    if (declared->assignableFrom(resultType)) {
        return;
    }
    std::vector<ConversionGap> gaps;
    const bool convertible = collectConversionGaps(declared, resultType, "", gaps) && !gaps.empty();
    auto diag              = diags_->of(
        convertible ? SemanticDiag::ReturnNeedsConversion : SemanticDiag::ReturnTypeMismatch);
    if (origin != 0) {
        diag.atOrigin(origin);
    } else if (auto sourceContext = context_ ? context_->sourceContext() : nullptr) {
        diag.atOrigin(sourceContext->debugMap().graphOrigin(graph->stableId()));
    }
    if (convertible) {
        const std::string text = sourceTextOf(value);
        std::string fixes;
        for (const ConversionGap &gap : gaps) {
            fixes += (fixes.empty() ? "" : ", ") +
                     conversionTextOf(text.empty() ? "<returned expression>" : text, gap);
        }
        diag.commit(
            declared->toString(),
            describeGaps(text.empty() ? "the value" : "the value ('" + text + "')", gaps),
            fixes,
            funcType->toString());
        throw BuildAbortException();
    }
    diag.commit(resultType->toString(), declared->toString(), funcType->toString());
    throw BuildAbortException();
}


node_handle_t Builder::visitExecNode(const GCT::node_ptr_t &gct) {
    ENTER("EXEC");
    // The permissions of this scope (they include those of the enclosing ones) are in force
    // for everything built in it, arms and lambdas included.
    NumericPermissionScope permissions(numericPermissions_, gct->numericPermissions());
    // The `using namespace` written in this block are in force in it (design principles 10.2).
    UsingScope usings(*this, GCT::Tree::of(gct)->usingsOf(gct));
    bool old                  = synced_;
    node_handle_t oldFuncNode = lastSyncedNode_;
    // A scope the lowering introduced (the operands of a composite literal, the names of a
    // declaration) is not a block: in a sync block it continues the chain, in written order,
    // like the arguments of a call statement. So does a sync block written inside a sync
    // block: its expressions are expressions of the enclosing block, in the same order.
    const bool operands = gct->operands() || (synced_ && gct->synced());
    if (!operands) {
        synced_         = gct->synced();
        lastSyncedNode_ = {};
    }
    registerGenericTemplates(gct);
    node_handle_t res{};
    // A statement in error is skipped and the following ones are still checked, so that one
    // compilation reports every independent error of the block. The builder is put back as
    // it was before the statement (an error may leave from inside a nested scope), and when
    // the block is done it fails as a whole: a block with a missing statement has no value
    // an enclosing expression could use, and a function with a missing statement must not
    // be analysed further (its dropped effects, its result).
    struct StatementState {
        Builder &self;
        compile_graph_ptr_t graph;
        node_scope_ptr_t nodeScope;
        graph_scope_ptr_t graphScope;
        decorated_scope_ptr_t decoratedScope;
        template_scope_ptr_t templateScope;
        type_bindings_t bindings;
        size_t depth;
        bool waited, synced, varied, declaresVariable, calleeAllowed;
        node_handle_t lastSynced;
        explicit StatementState(Builder &b)
            : self(b), graph(b.currGraph_), nodeScope(b.nodeScope_), graphScope(b.graphScope_),
              decoratedScope(b.decoratedScope_), templateScope(b.templateScope_),
              bindings(b.typeBindings_), depth(b.instantiationDepth_), waited(b.waited_),
              synced(b.synced_), varied(b.varied_), declaresVariable(b.declaresVariable_),
              calleeAllowed(b.genericCalleeAllowed_), lastSynced(b.lastSyncedNode_) {}
        void restore() {
            self.currGraph_            = graph;
            self.nodeScope_            = nodeScope;
            self.graphScope_           = graphScope;
            self.decoratedScope_       = decoratedScope;
            self.templateScope_        = templateScope;
            self.typeBindings_         = std::move(bindings);
            self.instantiationDepth_   = depth;
            self.waited_               = waited;
            self.synced_               = synced;
            self.varied_               = varied;
            self.declaresVariable_     = declaresVariable;
            self.genericCalleeAllowed_ = calleeAllowed;
            self.lastSyncedNode_       = lastSynced;
        }
    };
    bool failed                     = false;
    const GCT::Node *outerStatement = unusedStatement_;
    // A block that is itself a statement whose value nothing takes drops the value of its last
    // statement too.
    const bool valueDropped = gct == unusedStatement_ || droppedBodies_.contains(gct);
    std::pair<GCT::node_ptr_t, node_handle_t> lastStatement{};
    for (size_t i = 0; i < gct->size(); i++) {
        StatementState before(*this);
        try {
            const auto &child = gct->at(i);
            // Every statement but the last is one whose value nothing takes, and the last one
            // too when the block's value is dropped. The scopes the lowering introduced
            // (operands of a literal, the names of a declaration) have no statements; a sync
            // block inside a sync block continues the chain but has statements of its own.
            const bool unused = !gct->operands() && (i + 1 < gct->size() || valueDropped);
            unusedStatement_  = unused ? child : nullptr;
            any result        = visit(child);
            unusedStatement_  = nullptr;
            if (unused && result.has_value() && result.type() == typeid(node_handle_t)) {
                rejectUnusedPureResult(child, any_cast<node_handle_t>(result));
            }
            if (result.has_value() && result.type() == typeid(node_handle_t)) {
                res = any_cast<node_handle_t>(result);
                if (!gct->operands() && i + 1 == gct->size()) {
                    lastStatement = {child, res};
                }
            } else if (
                result.type() == typeid(graph_ptr_t) && child->type() == GCT::NodeKind::FUNC &&
                child->name().empty()) {
                // A function literal is a value like any other expression, e.g. a branch arm.
                res = valueNodeOf(result);
            }
        } catch (const BuildAbortException &e) {
            before.restore();
            unusedStatement_ = nullptr;
            failed           = true;
            continue;
        }
    }
    unusedStatement_    = outerStatement;
    lastBlockStatement_ = lastStatement;
    if (failed) {
        throw BuildAbortException();
    }
    if (gct->size() > 0 && res != nullptr && nodeIsKind(res, runtime::GCNodeKind::Dref) &&
        gct->at(gct->size() - 1)->type() == GCT::NodeKind::DREF && sameGraph(res, currGraph_)) {
        // A block that ends in the name of a function has that function's value: the value
        // of a branch arm (`if c then inc else dec`), of a block, of a function body. Left a
        // name, it gave the block the type void.
        const auto &last = gct->at(gct->size() - 1);
        res              = namedValueOf(res, last->name(), last);
    }
    if (!operands) {
        // A sync block that is not part of an enclosing chain: whoever takes its value takes
        // it after the block's last expression (a `return` has already done this for the
        // graph's exit). Without this the chain of a block ending in a plain value was not
        // reachable from anything and the block's effects were dropped.
        const bool returned =
            gct->size() > 0 && gct->at(gct->size() - 1)->type() == GCT::NodeKind::EXIT;
        if (synced_ && !returned && res != nullptr && lastSyncedNode_ != nullptr &&
            lastSyncedNode_ != res && sameGraph(res, currGraph_)) {
            node_handle_t gate = currGraph_->addGateNode(nodeTypeOf(res));
            linkNodes(LinkType::Norm, res, gate);
            linkNodes(LinkType::Ctrl, lastSyncedNode_, gate);
            res = gate;
        }
        synced_         = old;
        lastSyncedNode_ = oldFuncNode;
    }
    LEAVE("EXEC");
    return res;
}

void Builder::exportDeclarations(const GCT::node_ptr_t &gct, bool decorated) {
    // Whether a top-level function of this name carries decorators: its exported form is the
    // decorated function, which exists once its body is built (the second call).
    const auto isDecorated = [&](const std::string &name) {
        for (const auto &declaration : unit_->declarations) {
            if (declaration->graph == nullptr || declaration->names.front() != name) {
                continue;
            }
            if (!appliedDecoratorsOf(declaration->gct).empty()) {
                return true;
            }
        }
        return false;
    };
    bool failed = false;
    for (size_t i = 0; i < gct->exportCount(); ++i) {
        const Reference ref(gct->exportName(i));
        const std::string name = ref.toString();
        if (isDecorated(name) != decorated) {
            continue;
        }
        if (ref.plain() && camel::core::isOverloadableOperatorGroup(ref.ident())) {
            // `export func operator +`: every module that imports this one receives its
            // overloads (Module::importedOperatorOverloads).
            exportOperatorOverloads(ref.ident(), gct);
            continue;
        }
        if (decorated) {
            auto optDecorated = decoratedGraphAt(name);
            if (!optDecorated.has_value()) {
                continue; // its body is in error, which was reported
            }
            if (overloadsOfDecorated(name, *optDecorated) != nullptr) {
                // An exported name is one decorated function or a set of plain ones; the set
                // of both has no exported form yet. Exporting the decorated one alone dropped
                // the others without a word.
                diags_->of(SemanticDiag::ConstructNotSupported)
                    .atOrigin(gct->origin())
                    .commit(name, "a decorated function that has overloads cannot be exported yet");
                failed = true;
                continue;
            }
            module_->exportEntity(
                ref,
                camel::core::module::detail::EntityAccess::makeDecoratedGraph(
                    eraseCompileGraph(optDecorated.value())));
            continue;
        }
        if (auto optType = unit_->typeDecls.find(ref); optType != unit_->typeDecls.end()) {
            module_->exportType(ref, optType->second);
            continue;
        }
        if (auto value = unit_->values.find(name); value != unit_->values.end()) {
            TopLevelDeclaration &declaration = *value->second;
            if (declaration.isVar) {
                // Design principles 10.1: a `var` of the top level is written by the module's
                // own functions only, which are exported instead.
                camel::source::origin_id_t origin = gct->origin();
                for (const auto &child : *declaration.gct) {
                    if (child->type() == GCT::NodeKind::NREF && child->name() == name) {
                        origin = child->origin();
                    }
                }
                if (declaration.gct->type() == GCT::NodeKind::NREF) {
                    origin = declaration.gct->origin();
                }
                diags_->of(SemanticDiag::ExportedVariable).atOrigin(origin).commit(name);
                failed = true;
                continue;
            }
            // A value of the top level is a node of the root graph, which every module of the
            // program builds into: the importing module captures that very node, made when
            // a module first uses it (exportedValueNode).
            auto exported         = std::make_unique<ExportedValue>();
            exported->name        = name;
            exported->declaration = &declaration;
            module_->exportEntity(
                ref,
                camel::core::module::detail::EntityAccess::makeNode(exported.get()));
            unit_->exportedValues.push_back(std::move(exported));
            continue;
        }
        if (auto optGraph = graphsAt(name); optGraph.has_value()) {
            module_->exportEntity(
                ref,
                camel::core::module::detail::EntityAccess::makeGraphSet(
                    eraseCompileGraphSet(optGraph.value())));
            continue;
        }
        if (auto optTemplate = templateScope_->get(name); optTemplate.has_value()) {
            // A generic function has no graph to export: the template is exported, and there
            // is one instance per set of types in the program (instantiateGeneric).
            module_->exportEntity(
                ref,
                camel::core::module::detail::EntityAccess::makeGenericTemplate(
                    std::static_pointer_cast<void>(optTemplate.value())));
            continue;
        }
        if (isPoisonedName(name)) {
            failed = true;
            continue;
        }
        diags_->of(SemanticDiag::UnresolvedReference).atOrigin(gct->origin()).commit(name);
        failed = true;
    }
    if (failed) {
        throw BuildAbortException();
    }
}

} // namespace camel::compile::gir
