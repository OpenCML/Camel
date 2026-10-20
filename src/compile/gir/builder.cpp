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
        // The tail statement of an enclosing body, kept across a statement that fails.
        const GCT::Node *outerTailStatement = tailStatement_;
        try {
            const auto &child = gct->at(i);
            // Every statement but the last is one whose value nothing takes, and the last one
            // too when the block's value is dropped. The scopes the lowering introduced
            // (operands of a literal, the names of a declaration) have no statements; a sync
            // block inside a sync block continues the chain but has statements of its own.
            const bool unused = !gct->operands() && (i + 1 < gct->size() || valueDropped);
            unusedStatement_  = unused ? child : nullptr;
            any result;
            if (gct == tailExpectedFor_ && i + 1 == gct->size() && !gct->operands()) {
                // The body's value, in the position of the body (see visitBodyNode). A literal
                // written here takes that position's type (design principles 5.1), which is
                // how an arm's value takes the type of the whole `if` / `match`.
                tailStatement_ = child;
                ExpectedValueScope tailScope(expectedValueType_, tailExpectedType_);
                result         = visit(child);
                tailStatement_ = outerTailStatement;
            } else {
                result = visit(child);
            }
            unusedStatement_ = nullptr;
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
            tailStatement_ = outerTailStatement;
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

} // namespace camel::compile::gir
