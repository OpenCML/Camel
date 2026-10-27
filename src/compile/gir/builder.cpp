// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

#include <iostream>
#include "camel/utils/str.h"
#include "camel/utils/type.h"
#include "core/module/entity_internal.h"

#include "camel/core/operator_traits.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/other.h"
#include "camel/core/type/typevar.h"

#include <algorithm>
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
    rootGraph_ = createCompileGraph(
        context_->graphIds().allocate(), FunctionType::create(), nullptr, "__root__");
    if (auto sourceContext = context_ ? context_->sourceContext() : nullptr) {
        rootGraph_->setExtra<camel::source::SourceContext, kSourceContextExtraIndex>(
            sourceContext.get());
    }
}

void Builder::buildEntryCall() {
    // A program is entered at `main`; a library built as a program of its own (buildLibraryEntry)
    // at the function the caller runs, which is an ordinary top-level function of the module.
    const std::string entryName =
        libraryEntryFunction_.empty() ? std::string("main") : libraryEntryFunction_;
    compile_graph_ptr_t entryGraph = nullptr;
    if (auto decoratedMain = decoratedGraphAt(entryName)) {
        entryGraph = *decoratedMain;
    } else if (auto gv = graphsAt(entryName); gv.has_value() && !gv.value()->empty()) {
        entryGraph = gv.value()->front();
    }
    if (entryGraph) {
        // The entry graph stands for the entry function where a diagnostic is about the
        // program as a whole (a translation pass that finds nothing to translate): it
        // shares the origin of `main`, also after passes have inlined `main` into it.
        if (auto sourceContext = context_ ? context_->sourceContext() : nullptr) {
            const auto mainOrigin = sourceContext->debugMap().graphOrigin(entryGraph->graphId());
            if (mainOrigin != camel::source::kInvalidOriginId) {
                sourceContext->debugMap().registerGraphOrigin(rootGraph_->graphId(), mainOrigin);
                // Also as the origin of the entry function: the entry graph is rewritten (and
                // renamed) by passes, the question "where is main" outlives that. That question
                // is about the program the context runs, so only building that program records
                // it: a library built as a program of its own (`buildLibraryEntry`: a Camel pass
                // compiled on demand, such as stdlib/rules.cml) has its own entry function, and
                // recording it would move every later diagnostic about the running program to
                // that library's source.
                if (libraryEntryFunction_.empty()) {
                    sourceContext->debugMap().registerEntryOrigin(mainOrigin);
                }
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
                        ->addPortNode(isVar ? TupleType::cell(type) : type, name, true));
            }
            for (size_t i = 0; i < entryType->normTypesCount(); ++i) {
                const std::string name = nameAt(entryType->withTypesCount() + i, "__n", i);
                const bool isVar       = entryType->normIsVarAt(i);
                Type *type             = entryType->normTypeAt(i);
                rootTypeFactory.addNormArg(name, type, isVar);
                entryNormArgs.push_back(
                    rootGraph_
                        ->addPortNode(isVar ? TupleType::cell(type) : type, name, false));
            }
            if (!libraryEntryFunction_.empty()) {
                // A library built as a program of its own has the signature of the function it
                // runs: whoever calls its root (the pass runner) supplies the same arguments and
                // reads the same result as from a call of that function.
                rootTypeFactory.setExitType(entryType->exitType());
            }
            rootGraph_->setFuncType(rootTypeFactory.build());
        } else if (!libraryEntryFunction_.empty()) {
            FunctionTypeFactory rootTypeFactory;
            rootTypeFactory.setExitType(entryType->exitType());
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
    } else if (!libraryEntryFunction_.empty()) {
        // The caller checked that the module exports this function before asking for the build;
        // its body is in error if it has no graph.
        throw BuildAbortException();
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

void Builder::registerBuiltBody(const compile_graph_ptr_t &graph) {
    ASSERT(graph != nullptr, "A built body is a graph.");
    // A body is registered where it finishes, and a graph that is registered twice would be judged
    // twice by a pass over the built bodies (settleStreamLinearity counts the reads of each graph):
    // one graph is one body, once.
    for (const compile_graph_ptr_t &known : unit_->builtBodies) {
        if (known.get() == graph.get()) {
            return;
        }
    }
    unit_->builtBodies.push_back(graph);
}

compile_graph_ptr_t Builder::enterScope(
    FunctionType *funcType, const std::string &name,
    const std::vector<camel::source::origin_id_t> &parameterOrigins) {
    // A new graph is a new set of draft nodes: an access remembered for an earlier graph can
    // never be reused here (staticAccessCache_).
    staticAccessCache_.clear();
    std::string scopeName = name;
    if (name.empty()) {
        const bool atRoot = currGraph_ == rootGraph_;
        currGraph_        = createCompileGraph(
            context_->graphIds().allocate(), funcType, currGraph_, "", parameterOrigins);
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
            found = createCompileGraph(
                context_->graphIds().allocate(), funcType, currGraph_, name, parameterOrigins);
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

/// The type written with every type variable replaced by its positional name: two signatures
/// that differ only in the names of their type variables give one key.
std::string genericShapeText(Type *type, std::unordered_map<std::string, size_t> &seen) {
    if (type == nullptr) {
        return "?";
    }
    if (type->code() == camel::core::type::TypeCode::TypeVar) {
        const auto &name = static_cast<camel::core::type::TypeVarType *>(type)->name();
        auto found       = seen.find(name);
        size_t index     = found != seen.end() ? found->second : seen.size();
        if (found == seen.end()) {
            seen.emplace(name, index);
        }
        return "$" + std::to_string(index);
    }
    if (type->code() == camel::core::type::TypeCode::Function) {
        auto *func       = static_cast<FunctionType *>(type);
        std::string text = "(";
        for (size_t i = 0; i < func->withTypesCount(); ++i) {
            text += "with " + genericShapeText(func->withTypeAt(i), seen) + ", ";
        }
        for (size_t i = 0; i < func->normTypesCount(); ++i) {
            text += genericShapeText(func->normTypeAt(i), seen);
            text += i + 1 < func->normTypesCount() ? ", " : "";
        }
        text += ")";
        if (func->hasExitType()) {
            text += " => " + genericShapeText(func->exitType(), seen);
        }
        return text;
    }
    return type->toString();
}

/// Whether two generic declarations are one signature with the type variables renamed: the
/// second family writes the same call as the first (`generic_same_signature`).
bool sameGenericSignature(FunctionType *left, FunctionType *right) {
    if (left == nullptr || right == nullptr) {
        return false;
    }
    std::unordered_map<std::string, size_t> leftSeen;
    std::unordered_map<std::string, size_t> rightSeen;
    return genericShapeText(left, leftSeen) == genericShapeText(right, rightSeen);
}

void Builder::rejectRedeclaredFunction(
    const std::string &name, FunctionType *funcType, const GCT::node_ptr_t &gct, bool plain) {
    if (name.empty()) {
        return;
    }
    if (redeclaredFuncTypes_.contains(funcType)) {
        // Reported at the forward declaration of this function.
        throw BuildAbortException();
    }
    // A generic function of the name is a family of functions, one per type its parameters can
    // take, so an ordinary declaration of the same name in the same block is inside it: the
    // language declares a name once per block (design principles 4.4, 10.2). The generic
    // declarations were registered first, so this is where the ordinary one is refused.
    if (plain && templateScope_->has(name, false)) {
        redeclaredFuncTypes_.insert(funcType);
        diags_->of(SemanticDiag::Redeclaration).atOrigin(gct->origin()).commit(name);
        throw BuildAbortException();
    }
    // A second generic declaration whose parameters are the same types is the same family of
    // signatures, one with other names for the type variables, so the second is a
    // redeclaration too: the two write the same call (`generic_same_signature`).
    if (auto group = templateScope_->get(name, false); group.has_value() &&
        !genericGroupOf(group.value())->empty()) {
        for (const auto &other : *genericGroupOf(group.value())) {
            if (other != nullptr && sameGenericSignature(other->funcType, funcType)) {
                redeclaredFuncTypes_.insert(funcType);
                diags_->of(SemanticDiag::Redeclaration).atOrigin(gct->origin()).commit(name);
                throw BuildAbortException();
            }
        }
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
            // Two declarations of one name with the same parameters are told apart by their
            // conditions, if they write any: `when` is the applicability condition of an
            // overload (design principles 4.4), so two whose conditions exclude each other cover
            // one family of argument types each and are overloads (design principles 4.4: the same
            // check applies to every same-named overload that has `when`, ordinary functions with
            // bodies included).
            if (const auto *ownWhen = GCT::Tree::of(gct)->whenOf(gct)) {
                const auto other = whenClauses_.find(graph.get());
                if (other != whenClauses_.end()) {
                    SpeculativeInstantiation quiet(*this);
                    std::string overlappingType;
                    const ConditionSeparation separation = separateWhenConditions(
                        ownWhen->condition,
                        other->second.condition,
                        [&](const camel::core::type::type_bindings_t &bindings) {
                            return evaluateWhenCondition(
                                ownWhen->condition, funcType, bindings, {}, nullptr, unit_);
                        },
                        [&](const camel::core::type::type_bindings_t &bindings) {
                            return evaluateWhenCondition(
                                other->second.condition,
                                graph->funcType(),
                                bindings,
                                {},
                                nullptr,
                                unit_);
                        },
                        overlappingType);
                    if (separation == ConditionSeparation::Disjoint) {
                        continue; // one family of argument types each
                    }
                    if (separation == ConditionSeparation::Overlapping) {
                        redeclaredFuncTypes_.insert(funcType);
                        diags_->of(SemanticDiag::WhenDeclarationOverlap)
                            .atOrigin(gct->origin())
                            .commit(
                                name,
                                overlappingType,
                                graph->funcType()->toString() + " — when " +
                                    other->second.text,
                                funcType->toString() + " — when " + ownWhen->text);
                        throw BuildAbortException();
                    }
                }
            }
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
    // The body of this graph is complete, so its captures and its calls are known: a `var` of it
    // that no closure captured, no call was handed and that is not its own parameter becomes SSA
    // here (collapseNonEscapingVariableReads).
    collapseNonEscapingVariableReads();
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
                                 ? sourceContext->debugMap().graphOrigin(currGraph_->graphId())
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
        currGraph_ ? currGraph_->graphId() : runtime::kInvalidGraphId,
        nodeDraftOf(node)->nodeOrigin(nodeIdOf(node)),
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
        if (node->name().empty() && isGenericHere(node->at(0)->dataType(), node)) {
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
    case GCT::NodeKind::TMTCH:
        return visitTypeMatchNode(node);
    case GCT::NodeKind::SFOR:
        return visitStaticForNode(node);
    case GCT::NodeKind::SCOMP:
        return visitStaticComprehensionNode(node);
    case GCT::NodeKind::SLOOP:
        return visitStaticLoopNode(node);
    case GCT::NodeKind::SACC:
        return visitStaticAccessNode(node);
    default:
        ASSERT(false, "Unknown GCT NodeType");
    }
    return nullptr;
}

bool Builder::isPassDeclaration(const GCT::node_ptr_t &gct) {
    if (!gct->nativeDeclaration()) {
        return false;
    }
    const auto *when = GCT::Tree::of(gct)->whenOf(gct);
    if (when == nullptr) {
        return false;
    }
    // The text the pipeline reads, normalized the same way: `is_function::<T>()` with any
    // identifier for the type parameter (pass_declarations.cpp, passDeclarationApplies). A
    // body-less declaration without that condition declares an operator, as before.
    std::string text;
    for (const char c : when->text) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            text += c;
        }
    }
    const std::string prefix = "is_function::<";
    if (text.rfind(prefix, 0) != 0 || text.size() <= prefix.size() + 3 ||
        text.compare(text.size() - 3, 3, ">()") != 0) {
        return false;
    }
    const std::string parameter =
        text.substr(prefix.size(), text.size() - prefix.size() - 3);
    return !parameter.empty() && std::all_of(
                                     parameter.begin(), parameter.end(), [](char c) {
                                         return std::isalnum(static_cast<unsigned char>(c)) ||
                                                c == '_';
                                     });
}

std::vector<camel::source::origin_id_t> parameterPortOrigins(
    const context_ptr_t &context, const GCT::node_ptr_t &gct);

void_ptr_t Builder::visitDeclNode(const GCT::node_ptr_t &gct) {
    ENTER("DECL");
    if (gct->nativeDeclaration()) {
        if (isPassDeclaration(gct)) {
            // A pass is a function the pipeline applies to the program, not an operator of the
            // module: it has no executor, no signature to publish and nothing to export. The
            // declaration is the pipeline's (design principles 9.3); this records the name so
            // the export list does not look for an entity that will not exist.
            unit_->passDeclarations.insert(gct->name());
            LEAVE("DECL");
            return nullptr;
        }
        // A body-less declaration declares an operator of a C++ module (design principles 6.4):
        // it is checked and kept as an operator rule, not as a graph (step 1).
        // A `constas` parameter belongs to the declaration's compile-time identity, which the
        // native declaration mechanism does not carry (design principles 5.4). A pass-shaped
        // declaration is not an operator; it was handled above.
        FunctionType *funcType = tt::as_ptr<FunctionType>(gct->at(0)->dataType());
        if (funcType->hasConstas()) {
            diags_->of(SemanticDiag::ConstasInNativeDeclaration)
                .atOrigin(gct->origin())
                .commit(gct->name());
            throw BuildAbortException();
        }
        declareNativeOperator(gct);
        LEAVE("DECL");
        return nullptr;
    }
    GCT::node_ptr_t typeNode = gct->at(0);
    Type *type               = typeNode->dataType();
    if (!gct->isFunc()) {
        unit_->typeDecls[Reference(gct->name())] = type;
        // The tags written before the declaration (`#tag type P = ...`) belong to it, and so do
        // those written on the fields of the struct it writes (design principles 6.2). They are
        // read from C++ with tag::ofDeclaration / tag::ofField.
        const TagFacts tags = tagFactsOf(gct, camel::core::tag::Type);
        camel::core::tag::attachToDeclaration(
            *module_, Reference(gct->name()), tags.declaration);
        recordFieldTags(gct);
        LEAVE("DECL");
        return nullptr;
    }

    if (isGenericHere(type, gct)) {
        // A generic function is a template: its graphs are its instances.
        LEAVE("DECL");
        return nullptr;
    }
    FunctionType *funcType = tt::as_ptr<FunctionType>(type);
    rejectRedeclaredFunction(gct->name(), funcType, gct, true);
    rejectOperatorConflict(gct->name(), funcType, gct);
    compile_graph_ptr_t graph =
        enterScope(funcType, gct->name(), parameterPortOrigins(context_, gct));
    if (const auto *when = GCT::Tree::of(gct)->whenOf(gct)) {
        // The declaration's condition, kept while the declarations of one name are read: the
        // next declaration of the name is compared with this one before either body is built
        // (`rejectRedeclaredFunction`; design principles 4.4, and the rule on
        // `impl-declaration-arms`).
        whenClauses_[graph.get()] = *when;
    }
    // The declaration's parameter defaults are evaluated here, where the declaration is,
    // before any body is built: a call written above the definition fills them too, and every
    // call shares the one value (design principles 4.4).
    evaluateDefaults(gct, funcType, graph.get(), false);
    leaveScope();

    LEAVE("DECL");
    return nullptr;
}

/// The positions the parameters of a function are written at, `with` ones first, by slot. The
/// semantic parts the parameter list was parsed into carry them (one per parameter, with its
/// slot); the ports of the function's graph are made from this list, so that what is said about a
/// parameter -- the refusal of a function value that came in through it -- points at the
/// parameter itself.
///
/// A declaration is two nodes before it is one graph (design principles 6.4, step 1): the node the
/// declaration is read from and the node its body is built from may be entered in either order,
/// and the parameter list was parsed once, under the declaration they both come from. So the walk
/// follows the derivation chain and takes the first bundle that numbers its parameters.
std::vector<camel::source::origin_id_t> parameterPortOrigins(
    const context_ptr_t &context, const GCT::node_ptr_t &gct) {
    std::vector<camel::source::origin_id_t> origins;
    if (!context || !gct) {
        return origins;
    }
    auto sourceContext = context->sourceContext();
    if (!sourceContext) {
        return origins;
    }
    camel::source::origin_id_t origin = gct->origin();
    for (int hop = 0; hop < 8 && origin != camel::source::kInvalidOriginId; ++hop) {
        for (const camel::source::SemanticBundle *bundle :
             {sourceContext->gctSemantic(origin), sourceContext->astSemantic(origin)}) {
            if (bundle == nullptr) {
                continue;
            }
            for (const camel::source::SemanticPart &part : bundle->parts) {
                if (part.role != camel::source::SemanticRole::Parameter || part.slot < 0) {
                    continue;
                }
                const size_t slot = static_cast<size_t>(part.slot);
                if (origins.size() <= slot) {
                    origins.resize(slot + 1, camel::source::kInvalidOriginId);
                }
                if (origins[slot] == camel::source::kInvalidOriginId) {
                    origins[slot] = part.origin;
                }
            }
        }
        if (!origins.empty()) {
            return origins;
        }
        const camel::source::OriginRecord *record = sourceContext->origin(origin);
        origin = record != nullptr ? record->parent : camel::source::kInvalidOriginId;
    }
    return origins;
}

compile_graph_ptr_t Builder::visitFuncNode(const GCT::node_ptr_t &gct) {
    ENTER("FUNC");
    // The function type the position of this lambda expects (design principles 5.9). A body
    // clears the expected type, so it is read before the scope that does so.
    Type *const expectedHere = expectedValueType_;
    ExpectedValueScope functionScope(expectedValueType_, nullptr);
    std::string name         = gct->name();
    GCT::node_ptr_t typeLoad = gct->at(0);
    Type *type               = typeLoad->dataType();
    FunctionType *funcType   = tt::as_ptr<FunctionType>(type);
    if (gct->nativeDeclaration()) {
        // A body-less declaration declares an operator rule; there is no body to build
        // (visitDeclNode, step 1).
        LEAVE("FUNC");
        return nullptr;
    }
    // The tags written before the declaration (design principles 6.2) are resolved here, before
    // the generic early return: they hold for the declaration (`on func`) or for the nodes of its
    // body (`on nodes`). The body's are handed to buildFuncBody below; a tag on a lambda has no
    // declaration to belong to.
    const TagFacts tags = tagFactsOf(gct, camel::core::tag::Func);
    // An operator is declared once per signature, and each overload has its own facts; every
    // other declaration has one entry under its name.
    const std::string signature =
        camel::core::isOverloadableOperatorGroup(name) ? funcType->toString() : std::string{};
    if (!name.empty() && tags.declaration != camel::core::attr::kNoAttrs) {
        camel::core::tag::attachToDeclaration(
            *module_, Reference(name), signature, tags.declaration);
    }
    if (!name.empty()) {
        // What the declaration grants: the permission tags written before it.
        camel::core::tag::attachPermissionsToDeclaration(
            *module_, Reference(name), signature, tags.permissions);
    }
    if (isGenericHere(type, gct)) {
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
    if (name.empty()) {
        // A lambda's parameters that write no type take it from the function type of the
        // position the lambda stands in (design principles 5.9).
        funcType = completeLambdaSignature(gct, funcType, expectedHere);
    }
    rejectRedeclaredFunction(name, funcType, gct, true);
    rejectOperatorConflict(name, funcType, gct);
    staticGrantsOf(gct); // its decorators are resolved here, where they are written
    compile_graph_ptr_t graph = enterScope(funcType, name, parameterPortOrigins(context_, gct));
    if (const auto *when = GCT::Tree::of(gct)->whenOf(gct)) {
        // The condition belongs to the function, not to its instance: it is evaluated when
        // this graph is selected by a call, where the types and the `with` arguments of the
        // call are known (design principles 4.4).
        whenClauses_[graph.get()] = *when;
        // A condition that is malformed -- not a bool, a name the declaration does not make
        // visible, a query an open type cannot answer -- is a declaration-site error, and is
        // reported here rather than only when a call happens to select the function. A
        // condition that reads a `with` parameter is left for the call, which alone knows the
        // value.
        const WhenResult early =
            evaluateWhenCondition(when->condition, funcType, typeBindings_, {});
        if (early.outcome != WhenOutcome::Holds && early.outcome != WhenOutcome::Fails &&
            early.outcome != WhenOutcome::NeedsWithValue) {
            reportWhenProblem(early, name, when->condition);
        }
    }
    if (camel::core::tag::declaresMacro(tags.declaration)) {
        graph->draft().setFlags(static_cast<uint8_t>(
            graph->draft().flags() | camel::runtime::kGraphFlagMacro));
    }
    buildFuncBody(gct, graph, funcType, name, tags);
    // A declaration tag whose argument names a function (`#vjp(cube_rule)`) keeps that graph
    // in the reference table of the function's graph: the fact is a graph reference, and the
    // reader finds it in the frozen graph (GCGraph::ruleGraphs).
    attachRuleGraphs(graph, tags.declaration);
    // A declaration fact that has to live on the graph itself: a scheduler reads
    // `#independent` from the exit node of the function it is about to call (design 3.6).
    if (!name.empty()) {
        camel::core::tag::markIndependentElements(graph->draft(), tags.declaration);
    }
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
        attachRuleGraphs(decorated, tags.declaration);
        // The decorated function has the declaration's parameters: a call that leaves a
        // defaulted one out fills the same value through it.
        if (const auto found = defaults_.find(graph.get()); found != defaults_.end()) {
            defaults_[decorated.get()] = found->second;
        }
        insertDecoratedGraph(name, decorated);
        decoratedOf_[graph.get()] = decorated;
    }

    LEAVE("FUNC");
    return graph;
}

FunctionType *Builder::completeLambdaSignature(
    const GCT::node_ptr_t &gct, FunctionType *funcType, Type *expected) {
    const auto *unwritten = GCT::Tree::of(gct)->unwrittenParamsOf(gct);
    if (unwritten == nullptr || unwritten->empty()) {
        return funcType;
    }
    // A lambda takes the types it does not write from the function type of the position it
    // stands in (design principles 5.9). That expected type reaches this point through the one
    // channel of the expected type (`expected`): an argument to a parameter of function type,
    // the value of a declared type, or the result of a function whose result type is a function
    // type.
    if (expected == nullptr) {
        const std::string name(funcType->argNameAt((*unwritten)[0].index));
        diags_->of(SemanticDiag::LambdaParameterWithoutType)
            .atOrigin((*unwritten)[0].origin)
            .commit(name, "nothing here expects a function type");
        throw BuildAbortException();
    }
    if (expected->code() != TypeCode::Function) {
        const std::string name(funcType->argNameAt((*unwritten)[0].index));
        diags_->of(SemanticDiag::LambdaParameterWithoutType)
            .atOrigin((*unwritten)[0].origin)
            .commit(
                name,
                std::format(
                    "the type expected here, '{}', is not a function type", expected->toString()));
        throw BuildAbortException();
    }
    const auto *expectedFunc = tt::as_ptr<FunctionType>(expected);
    const size_t withCount   = funcType->withTypesCount();
    const size_t total       = withCount + funcType->normTypesCount();
    const auto report        = [&](const GCT::Tree::UnwrittenParam &param,
                           const std::string &reason) -> void {
        const std::string name(funcType->argNameAt(param.index));
        diags_->of(SemanticDiag::LambdaParameterWithoutType)
            .atOrigin(param.origin)
            .commit(name, reason);
        throw BuildAbortException();
    };
    if (expectedFunc->withTypesCount() != withCount ||
        expectedFunc->normTypesCount() != funcType->normTypesCount()) {
        report(
            (*unwritten)[0],
            std::format(
                "the expected type '{}' takes {} parameters and the lambda {}",
                expectedFunc->toString(),
                expectedFunc->withTypesCount() + expectedFunc->normTypesCount(),
                total));
    }
    const auto typeAt = [&](size_t index) -> Type * {
        return index < withCount ? funcType->withTypeAt(index)
                                 : funcType->normTypeAt(index - withCount);
    };
    const auto nameAt = [&](size_t index) -> std::string {
        return funcType->hasArgNames() ? std::string(funcType->argNameAt(index)) : std::string{};
    };
    FunctionTypeFactory factory;
    factory.setHasCompileInfo(funcType->hasArgNames());
    // The result type: the one written, or the one the expected function type gives. An
    // expected result that still holds a type variable says nothing, and the body decides.
    Type *exitType = funcType->hasExitType() ? funcType->exitType() : nullptr;
    if (exitType == nullptr && expectedFunc->hasExitType() &&
        !containsTypeVar(expectedFunc->exitType())) {
        exitType = expectedFunc->exitType();
    }
    factory.setExitType(exitType);
    size_t hole = 0;
    for (size_t i = 0; i < total; ++i) {
        Type *type   = typeAt(i);
        bool isHole  = hole < unwritten->size() && (*unwritten)[hole].index == i;
        if (isHole) {
            const GCT::Tree::UnwrittenParam &param = (*unwritten)[hole++];
            Type *const wanted                     = i < withCount
                                                         ? expectedFunc->withTypeAt(i)
                                                         : expectedFunc->normTypeAt(i - withCount);
            if (wanted == nullptr) {
                report(param, "the expected function type does not say what this parameter is");
            }
            if (containsTypeVar(wanted)) {
                std::vector<std::string> names;
                collectTypeVars(wanted, names);
                report(
                    param,
                    std::format(
                        "the expected type '{}' still holds the type variable '{}', which this "
                        "position does not bind",
                        expectedFunc->toString(),
                        names.empty() ? std::string("?") : names.front()));
            }
            type = wanted;
        }
        if (i < withCount) {
            factory.addWithArg(nameAt(i), type, funcType->withIsVarAt(i), funcType->withNeedsValueAt(i));
        } else {
            factory.addNormArg(nameAt(i), type, funcType->normIsVarAt(i - withCount));
        }
    }
    return factory.buildOpen();
}

void Builder::stampPermissions(node_handle_t operNode) const {
    if (operNode != nullptr) {
        nodeDraftOf(operNode)->setNodePermissions(nodeIdOf(operNode), numericPermissions_);
        nodeDraftOf(operNode)->setNodeAttrs(nodeIdOf(operNode), currentAttrs_);
    }
}

void Builder::buildFuncBody(
    const GCT::node_ptr_t &gct, const compile_graph_ptr_t &graph, FunctionType *funcType,
    const std::string &name, const TagFacts &bodyTags) {
    // The function's graph has the permissions of its own body (which include those of the
    // code it is written in), not those of the place that asked for it to be built (a call
    // that instantiates a generic function, a block that declares it). The permission tags
    // written before the declaration (`#[contract] func f`) grant to the body too.
    // A lambda is different: its body is an expression written where the lambda is, so it also
    // inherits the permissions in force there (`#[reassociate] func f` grants to the lambda in
    // f's body, design principles 7.5). A named function is built on demand, possibly from a
    // call site, so it must not take the permissions of the place that built it.
    const camel::core::sem::PermissionMask inherited =
        name.empty() ? numericPermissions_ : camel::core::sem::kNoPermissions;
    NumericPermissionScope permissions(
        numericPermissions_,
        static_cast<camel::core::sem::PermissionMask>(inherited | bodyTags.permissions));
    // The attributes its decorators grant join those in force where it is written (each key's
    // combine rule, camel/core/node_attrs.h), for everything written in it; so do the facts of
    // the tags written before it, which hold for the nodes of its body (`on nodes`).
    AttrScope attrs(
        currentAttrs_,
        camel::core::attr::unite(
            camel::core::attr::unite(currentAttrs_, staticGrantsOf(gct).first), bodyTags.nodes));
    // A function (or a lambda) written inside a `match type` arm has returns of its own.
    StaticBodyScope *outerStaticBody = std::exchange(activeStaticBody_, nullptr);
    const size_t outerStaticDepth    = std::exchange(staticDepth_, 0);
    // A nested function is a boundary for rest parameters too: `..ts` of an enclosing
    // expansion cannot be spread inside another graph.
    RestBody *outerRestBody         = std::exchange(activeRestBody_, nullptr);
    const size_t outerRestBindings  = restBindings_.size();
    // The body EXEC is where the tags of the function are the body region's, not a block's.
    struct FunctionBodyScope {
        const GCT::Node *&slot;
        const GCT::Node *outer;
        FunctionBodyScope(const GCT::Node *&s, const GCT::Node *body) : slot(s), outer(s) {
            slot = body;
        }
        ~FunctionBodyScope() { slot = outer; }
    } bodyScope(functionBody_, gct->at(1));
    // A nested function is a boundary for the compile-time names of an enclosing expansion
    // too: the `n` of a `static for` and the value of an enclosing `constas` parameter are not
    // names of its body. Its own `constas` values (when it is an instance being built) are
    // bound below.
    const size_t outerStaticValues = staticValueScopes_.size();
    staticValueScopes_.clear();
    try {
        registerGraphOrigin(context_, graph, gct, "gir.func.graph");
        const auto withPorts = graph->draft().withPorts();
        for (size_t i = 0; i < withPorts.size(); ++i) {
            node_handle_t port = graph->draft().ref(withPorts[i]);
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
            node_handle_t port = graph->draft().ref(normPorts[i]);
            insertNode(graph->nodePortName(port), port);
            if (i < funcType->normTypesCount() && funcType->normIsVarAt(i)) {
                variables_.insert(port);
            }
            if (recorder_) {
                recordDeclaration(graph->nodePortName(port), port, gct);
            }
        }
        bodyStates_[graph.get()] = BodyState::Open;
        // The defaults of the declaration's parameters, in the scope of its body: a
        // declaration at a module's top level was evaluated already, where it is written
        // (visitDeclNode), and an instance of a generic one takes its template's.
        evaluateDefaults(gct, funcType, graph.get(), true);
        // A parameter destructured as a tuple: the port is the tuple, and the names take its
        // elements (`((a, b)) => ...` binds as `let (a, b) = p` does). The parameter is one
        // value, so nothing about the call changes; only the names of the body do.
        if (const auto *tuples = GCT::Tree::of(gct)->tupleParamsOf(gct)) {
            for (const auto &tupleParam : *tuples) {
                const std::string paramName(funcType->argNameAt(tupleParam.index));
                const bool isWith = tupleParam.index < funcType->withTypesCount();
                Type *paramType   = isWith
                                        ? funcType->withTypeAt(tupleParam.index)
                                        : funcType->normTypeAt(
                                              tupleParam.index - funcType->withTypesCount());
                if (paramType->code() != TypeCode::Tuple ||
                    tt::as_ptr<TupleType>(paramType)->size() != tupleParam.names.size()) {
                    diags_->of(SemanticDiag::TupleUnpackingCountMismatch)
                        .atOrigin(gct->origin())
                        .commit();
                    throw BuildAbortException();
                }
                const auto *tuple = tt::as_ptr<TupleType>(paramType);
                const auto port   = nodeAt(paramName);
                ASSERT(port.has_value(), "A tuple parameter has its port.");
                for (size_t i = 0; i < tupleParam.names.size(); ++i) {
                    node_handle_t access = graph->addAccsNode(tuple->typeAt(i), i);
                    linkNodes(LinkType::Norm, *port, access);
                    registerNodeOrigin(context_, access, gct, "gir.tuple.param");
                    insertNode(tupleParam.names[i], access);
                    if (recorder_) {
                        recordDeclaration(tupleParam.names[i], access, gct);
                    }
                }
            }
        }
        if (!funcType->hasExitType()) {
            inferredExitGraphs_.insert(graph.get()); // the body sets it from now on
        }
        // The values of the instance's `constas` parameters: the body reads them as static
        // values, much as a `static for` reads its round's `n` (design principles 5.4). They
        // are not ports, so they are not names of the graph; the scope is the body's.
        if (!constasBindings_.empty()) {
            staticValueScopes_.emplace_back();
            for (const auto &[constasName, constasValue] : constasBindings_) {
                node_handle_t constant = graph->addStaticDataNode(constasValue.data);
                rememberConstant(constant, constasValue.data);
                registerNodeOrigin(context_, constant, gct, "gir.data.static.constas");
                staticValueScopes_.back().push_back(
                    {constasName, constasKeyOf(constasValue.data), constant});
            }
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
        // The tags written before the function are the placement of its body as a whole: a
        // consumer that asks the body region (region 0) finds them without walking the nodes
        // (docs/design/gpu/plan.cn.md 2.2). Only the tags written on this declaration, not
        // those of the scope it is written in, which the nodes carry themselves.
        if (bodyTags.nodes != camel::core::attr::kNoAttrs) {
            graph->draft().setRegionAttrs(0, bodyTags.nodes);
        }
        // A body declared `: void` drops the value of its last statement.
        if (!inferredExitGraphs_.contains(graph.get()) &&
            isVoidGraphExitType(funcType->exitType())) {
            rejectDiscardedValue(graph.get());
        }
        bodyStates_[graph.get()] = BodyState::Built;
        registerBuiltBody(graph);
        // A function that wrote no clause about resources promises what its body reaches
        // (design principles 3.3, effects plan 5). What it reaches is measured here, while the
        // body is in hand, and written onto its own signature -- the open one, which no other
        // function shares and which every value of this function is made from, so that a value
        // passed to a position that asks for resources is judged against the same set the
        // declaration is. A measured set that cannot be written as a clause (`anyOf`: a class
        // the clause has no name for, the conservative reading of a call through a value that
        // writes no clause) leaves the signature as it is rather than promising too little.
        if (graph->funcType() != nullptr &&
            graph->funcType()->resources().mode == camel::core::sem::ResourceSignature::Mode::Generic) {
            // Its signature carries an inferred set from here on: this is what the settled
            // measurement is written onto once every body has been measured. A measurement that
            // cannot be written as a clause is not written now, and is not written then either.
            inferredSignatures_.insert(graph.get());
            if (const auto measured = footprintOf(graph.get());
                measured.has_value() && measured->anyOf.empty()) {
                graph->setResources(*measured);
            }
        }
        if (recorder_) {
            recorder_->functionBuilt(gct->origin(), name, graph->graphId(), funcType);
        }
    } catch (const camel::core::mm::MemoryExhausted &e) {
        // Say which function was being compiled; the allocator does not know.
        bodyStates_[graph.get()] = BodyState::Built;
        activeStaticBody_        = outerStaticBody;
        staticDepth_             = outerStaticDepth;
        activeRestBody_          = outerRestBody;
        restBindings_.resize(outerRestBindings);
        staticValueScopes_.resize(outerStaticValues);
        leaveScope();
        throw e.during(
            std::format("compiling function '{}'", name.empty() ? graph->name() : name),
            gct->origin());
    } catch (...) {
        // The caller entered the scope of `graph`; an error leaves it on the way out.
        bodyStates_[graph.get()] = BodyState::Built;
        failedBodies_.insert(graph.get());
        activeStaticBody_        = outerStaticBody;
        staticDepth_             = outerStaticDepth;
        activeRestBody_          = outerRestBody;
        restBindings_.resize(outerRestBindings);
        staticValueScopes_.resize(outerStaticValues);
        leaveScope();
        throw;
    }
    activeStaticBody_ = outerStaticBody;
    staticDepth_      = outerStaticDepth;
    activeRestBody_   = outerRestBody;
    restBindings_.resize(outerRestBindings);
    staticValueScopes_.resize(outerStaticValues);
}


node_handle_t Builder::visitExecNode(const GCT::node_ptr_t &gct, Type *tailExpected) {
    ENTER("EXEC");
    // The tags written before the block hold for the nodes written in it (`#kernel { ... }`,
    // design principles 6.2): they join the attributes in force where the block is written.
    const TagFacts blockTags = tagFactsOf(gct, camel::core::tag::Nodes);
    // The permissions of this scope (they include those of the enclosing ones) are in force
    // for everything built in it, arms and lambdas included. A permission written as a tag
    // (`#[contract] { ... }`) reaches the nodes through this one column.
    NumericPermissionScope permissions(
        numericPermissions_,
        static_cast<camel::core::sem::PermissionMask>(
            numericPermissions_ | blockTags.permissions));
    AttrScope attrs(
        currentAttrs_,
        camel::core::attr::unite(currentAttrs_, blockTags.nodes));
    // A tagged block is a Block region: the facts it carries are the region's, and the nodes
    // written in it are put in the region by the draft (addNode follows the insert region).
    // The region is an annotation only: it changes no order and reaches no scheduler.
    struct InsertRegionScope {
        runtime::GraphDraft *draft = nullptr;
        uint32_t outer             = 0;
        ~InsertRegionScope() {
            if (draft != nullptr) {
                draft->setInsertRegion(outer);
            }
        }
    } regionScope;
    regionScope.draft = &currGraph_->draft();
    regionScope.outer = regionScope.draft->insertRegion();
    if (blockTags.nodes != camel::core::attr::kNoAttrs && gct != functionBody_) {
        const uint32_t block = regionScope.draft->addRegion(
            runtime::GCRegionKind::Block, regionScope.outer);
        regionScope.draft->setRegionAttrs(block, blockTags.nodes);
        regionScope.draft->setInsertRegion(block);
    }
    // The `using namespace` written in this block are in force in it (design principles 10.2).
    UsingScope usings(*this, GCT::Tree::of(gct)->usingsOf(gct));
    bool old                  = synced_;
    node_handle_t oldFuncNode = lastSyncedNode_;
    // A scope the lowering introduced (the operands of a composite literal, the names of a
    // declaration) is not a block: in a sync block it continues the chain, in written order,
    // like the arguments of a call statement. So does a sync block written inside a sync
    // block: its expressions are expressions of the enclosing block, in the same order.
    const bool operands = gct->operands() || (synced_ && gct->synced()) ||
                          (synced_ && gct == syncedBlockOverride_);
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
        bool synced, varied, declaresVariable, calleeAllowed;
        node_handle_t lastSynced;
        explicit StatementState(Builder &b)
            : self(b), graph(b.currGraph_), nodeScope(b.nodeScope_), graphScope(b.graphScope_),
              decoratedScope(b.decoratedScope_), templateScope(b.templateScope_),
              bindings(b.typeBindings_), depth(b.instantiationDepth_),
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
            self.synced_               = synced;
            self.varied_               = varied;
            self.declaresVariable_     = declaresVariable;
            self.genericCalleeAllowed_ = calleeAllowed;
            self.lastSyncedNode_       = lastSynced;
        }
    };
    bool failed                     = false;
    const GCT::Node *outerStatement = unusedStatement_;
    // The statements of this block that a `fail` written in it leaves behind, by the entry they
    // belong to in statementsAfterFail_ (nothing when the block has met no `fail` yet, or the
    // entry the last one opened).
    std::optional<size_t> afterFailEntry;
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
            any result;
            // The body's value, in the position of the body (see visitBodyNode): the same
            // expected-type channel every other position uses, and checkValue converts a
            // literal or empty array there (design principles 5.1).
            // A function declaration is a statement, not a value: it leaves the block's value
            // as it was (an empty-named FUNC is a lambda written in place, and is a value).
            const bool declaration =
                child->type() == GCT::NodeKind::FUNC && !child->name().empty();
            const bool tailPosition = tailExpected != nullptr && !gct->operands() &&
                                      i + 1 == gct->size() && !declaration;
            if (tailPosition) {
                result = visitInPosition(child, tailExpected);
            } else if (!gct->operands()) {
                // A statement whose value nothing takes is in no position: the type the
                // block's value stands at does not reach inside it, or a `let` written before
                // the value of a `T?` arm would build its initializer in that `T?`. An
                // operand scope (`gct->operands()`) is in the position of the element or
                // field it builds, and keeps it.
                ExpectedValueScope statementScope(expectedValueType_, nullptr);
                result = visit(child);
            } else {
                result = visit(child);
            }
            unusedStatement_ = nullptr;
            if (result.has_value() && result.type() == typeid(StaticTuple)) {
                // A build-time tuple is the block's value only as a runtime tuple: a tail
                // comprehension materializes here, and an unused one is held to the same
                // unused-pure-value rule as before.
                result = valueNodeOf(result);
            }
            if (unused && result.has_value() && result.type() == typeid(node_handle_t)) {
                rejectUnusedPureResult(child, any_cast<node_handle_t>(result));
            }
            // A statement written after a `fail` in the same block (design principles 3.9; the
            // author's ruling of 2026-10-26): the `fail` never returns, so where it is executed
            // nothing written after it in the block is reached. Recorded here and reported when
            // the body is complete, because whether the `fail` runs at all is a fact about the
            // whole body: a `fail` whose value nothing wants does not run, and the statements
            // after it are reached then (FailNotExecuted says so).
            if (!gct->operands() && result.has_value() && result.type() == typeid(node_handle_t)) {
                if (afterFailEntry.has_value()) {
                    statementsAfterFail_[*afterFailEntry].statements.push_back(child->origin());
                }
                node_handle_t built = any_cast<node_handle_t>(result);
                if (isFailCall(built)) {
                    afterFailEntry = statementsAfterFail_.size();
                    statementsAfterFail_.push_back({currGraph_, built, {}});
                }
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
        if (synced_ && !returned && res != nullptr && sameGraph(res, currGraph_) &&
            valueNeedsSyncEdge(res, lastSyncedNode_)) {
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


