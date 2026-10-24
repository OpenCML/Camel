// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The expression nodes of the GIR builder: data, names and dereferences (visitDataNode,
 * visitNRefNode, visitDRefNode), the variables of a body with their reads and writes, casts,
 * `with` and access nodes.
 */

#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"

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

namespace {
// Reports that the name at `gct` stands for the functions `graphs`.
void reportFunctionReference(
    camel::compile::SemanticRecorder *recorder, const GCT::node_ptr_t &gct, const std::string &name,
    const std::optional<graph_set_ptr_t> &graphs) {
    std::vector<runtime::GraphId> ids;
    if (graphs.has_value() && *graphs) {
        for (const auto &g : **graphs) {
            if (g) {
                ids.push_back(g->graphId());
            }
        }
    }
    recorder->functionReferenced(gct->origin(), name, std::move(ids));
}
} // namespace

node_handle_t Builder::literalDataInType(const GCT::node_ptr_t &gct, Type *expected) {
    // A literal in a position takes the position's type while it is made (design principles
    // 5.1): the value written is converted exactly, and a composite literal element by element.
    // Making it in its own type first would leave that literal unused in the graph; every
    // other conversion of a value into its position is coerceTo (checkValue).
    if (gct == nullptr || gct->type() != GCT::NodeKind::DATA || expected == nullptr) {
        return nullptr;
    }
    const data_ptr_t &data = gct->data();
    if (expected->assignableFrom(data->type())) {
        return nullptr;
    }
    if (isComposite(data->type()->code())) {
        if (!tt::as_shared<CompositeData>(data)->resolved()) {
            return nullptr;
        }
        const data_ptr_t typed = compositeDataInType(data, expected, gct);
        if (typed == nullptr) {
            return nullptr;
        }
        node_handle_t result = currGraph_->addStaticDataNode(typed);
        rememberConstant(result, typed);
        if (varied_ && currGraph_->outerGraph().get() != nullptr) {
            node_handle_t copyNode = currGraph_->addCopyNode(typed->type());
            linkNodes(LinkType::Norm, result, copyNode);
            result = copyNode;
        }
        registerNodeOrigin(context_, result, gct, "gir.data.literal");
        return result;
    }
    if (!expected->isPrimitive()) {
        return nullptr;
    }
    const literal::Value *exact = literalValueOfGct(gct);
    if (exact == nullptr || !literal::kindEnters(*exact, expected)) {
        return nullptr;
    }
    return literalInType(*exact, expected, gct, true);
}

node_handle_t Builder::visitDataNode(const GCT::node_ptr_t &gct) {
    ENTER("DATA");
    const data_ptr_t &data = gct->data();

    node_handle_t node{};
    TypeCode dataType = data->type()->code();
    if (isComposite(dataType)) {
        auto composedData = tt::as_shared<CompositeData>(data);
        if (!composedData->resolved()) {
            const auto &dataType = tt::as_ptr<CompositeType>(data->type());
            auto holes           = composedData->holes();
            type_vec_t refTypes;
            std::vector<node_handle_t> refNodes;
            std::vector<std::string> refNames;
            for (const auto &ref : composedData->refs()) {
                const auto &refNode = elementNodeOf(std::string(ref), gct);
                refNames.push_back(std::string(ref));
                refTypes.push_back(nodeTypeOf(refNode));
                refNodes.push_back(refNode);
            }
            ASSERT(
                holes.size() == refNodes.size(),
                "Composite holes must match unresolved reference count.");
            // The position this literal stands in gives it its type while it is made, element by
            // element, even when one of its elements is computed: it is a FILL then, not one
            // constant, and the elements it wrote kept their default types before this
            // (`{ n: x, t: (1.0, 2.0) }` was a `(float, float)` field where the declaration says
            // `(f32, f32)`; design principles 5.1). A position the literal does not fit leaves it
            // the type its own elements give it, and the place it was written in reports the
            // mismatch, as it did.
            Type *position        = expectedValueType_;
            data_ptr_t positioned = nullptr;
            // Where the computed elements sit in the value the fill reads: the literal's own type
            // states them, and the position may state the same fields in another order.
            std::vector<size_t> holeSlots = holes;
            if (position != nullptr && !containsTypeVar(position)) {
                positioned = compositeWithHolesInType(
                    data, position, gct, refNames, refTypes, holes, holeSlots);
            }
            if (positioned == nullptr && dataType->code() == TypeCode::Array &&
                !refTypes.empty()) {
                // The element type of the literal: what its literal elements fixed (`[x, 5]` is
                // an `int[]`), else what the computed elements join to (`[x, null]`, `[a, b]` with
                // an `int` and an `int?`; design principles 3.8, 5.1). An element that is not of
                // it enters it, or is reported: an `int` value is not a `float`. A literal whose
                // element type the position names is built with it already (`let xs: float[] =
                // [n, m]`), so a nested literal never reads the type of an outer position (H24).
                Type *targetElem = nullptr;
                if (dataType->resolved()) {
                    Type *elemType = tt::as_ptr<ArrayType>(dataType)->elemType();
                    if (elemType != nullptr && elemType->code() != TypeCode::Void) {
                        targetElem = elemType;
                    }
                }
                if (targetElem == nullptr) {
                    targetElem = valueTypeOf(refTypes.front());
                    for (size_t i = 1; i < refTypes.size() && targetElem != nullptr; ++i) {
                        targetElem = joinTypes(targetElem, valueTypeOf(refTypes[i]));
                    }
                }
                for (size_t i = 0; i < refTypes.size(); ++i) {
                    Type *have = valueTypeOf(refTypes[i]);
                    if (targetElem == nullptr || targetElem->assignableFrom(have)) {
                        continue;
                    }
                    if (node_handle_t entered = coerceTo(refNodes[i], targetElem, gct)) {
                        refNodes[i] = entered;
                        refTypes[i] = targetElem;
                        continue;
                    }
                    // Reported at the element (the literal itself carries no position).
                    const camel::source::origin_id_t at = nodeDraftOf(refNodes[i])->nodeOrigin(nodeIdOf(refNodes[i])) != 0
                                                              ? nodeDraftOf(refNodes[i])->nodeOrigin(nodeIdOf(refNodes[i]))
                                                              : gct->origin();
                    diags_->of(SemanticDiag::ElementTypeMismatch)
                        .atOrigin(at)
                        .commit("array", have->toString(), targetElem->toString());
                    throw BuildAbortException();
                }
            }
            auto filledType = positioned != nullptr
                                  ? position
                                  : (dataType->resolved()
                                         ? dataType
                                         : tt::as_ptr<CompositeType>(dataType->clone())
                                               ->resolve(refTypes));
            // The literal part of the array takes the element type the computed part gave the
            // array: a constant element written as an unnamed struct in another order is copied
            // into the named type it enters (design principles 5.2), so every slot is of the
            // array's element type.
            data_ptr_t literalData = positioned != nullptr ? positioned : data;
            if (positioned == nullptr && filledType->code() == TypeCode::Array &&
                !filledType->equals(data->type())) {
                if (data_ptr_t converted = data->convertTo(filledType)) {
                    literalData = converted;
                }
            }
            auto payload    = runtime::makeFillPayload(fillKindForType(filledType), holeSlots);
            node_handle_t srcNode = currGraph_->addStaticDataNode(literalData, filledType);
            node                  = currGraph_->addFillNode(filledType, payload);
            linkNodes(LinkType::Norm, srcNode, node);
            // An array written in place with some elements computed: the constant elements
            // stay known to the operators the array is passed to (StaticArg::elements).
            if (auto array = std::dynamic_pointer_cast<ArrayData>(literalData);
                array && filledType->code() == TypeCode::Array) {
                Type *elemType = tt::as_ptr<ArrayType>(filledType)->elemType();
                std::vector<std::optional<slot_t>> elements(array->raw().size());
                for (size_t i = 0; i < elements.size(); ++i) {
                    const auto &element = array->raw()[i];
                    if (!element || element->type() != elemType) {
                        continue;
                    }
                    if (auto l = std::dynamic_pointer_cast<LongData>(element)) {
                        elements[i] = camel::core::rtdata::toSlot(l->data());
                    } else if (auto n = std::dynamic_pointer_cast<IntData>(element)) {
                        elements[i] = camel::core::rtdata::toSlot(n->data());
                    } else if (auto d = std::dynamic_pointer_cast<DoubleData>(element)) {
                        elements[i] = camel::core::rtdata::toSlot(d->data());
                    } else if (auto f = std::dynamic_pointer_cast<FloatData>(element)) {
                        elements[i] = camel::core::rtdata::toSlot(f->data());
                    } else if (auto b = std::dynamic_pointer_cast<BoolData>(element)) {
                        elements[i] = camel::core::rtdata::toSlot(b->data());
                    }
                }
                for (size_t h = 0; h < holes.size(); ++h) {
                    if (holes[h] < elements.size()) {
                        elements[holes[h]] = nodeTypeOf(refNodes[h]) == elemType
                                                 ? staticArgOf(refNodes[h]).value
                                                 : std::nullopt;
                    }
                }
                arrayElementStatics_[node] = std::move(elements);
            }
            std::vector<camel::source::origin_id_t> mergedInputs;
            for (const auto &refNode : refNodes) {
                linkNodes(LinkType::With, refNode, node);
                if (auto *sourceContext = context_ ? context_->sourceContext().get() : nullptr) {
                    auto origin = sourceContext->resolveGirNodeOrigin(draftDebugKey(refNode), "");
                    if (origin != camel::source::kInvalidOriginId) {
                        mergedInputs.push_back(origin);
                    }
                }
            }
            registerNodeOrigin(
                context_,
                node,
                gct,
                "gir.data.fill",
                {
                    semanticPart(
                        camel::source::SemanticRole::ValueProducer,
                        context_->sourceContext() ? context_->sourceContext()->resolveGirNodeOrigin(
                                                        draftDebugKey(srcNode),
                                                        "")
                                                  : camel::source::kInvalidOriginId,
                        -1,
                        "base"),
                },
                std::move(mergedInputs));

            LEAVE("DATA");
            return node;
        }
    }

    node = currGraph_->addStaticDataNode(data);
    rememberConstant(node, data);
    if (varied_ && currGraph_->outerGraph().get() != nullptr) {
        node_handle_t copyNode = currGraph_->addCopyNode(data->type());
        linkNodes(LinkType::Norm, node, copyNode);
        node = copyNode;
    }

    registerNodeOrigin(context_, node, gct, "gir.data");

    LEAVE("DATA");
    return node;
}

node_handle_t Builder::visitNRefNode(const GCT::node_ptr_t &gct) {
    ENTER("NREF");
    const string &ident = gct->name();
    // Names declared inside the initializer are not part of this declaration.
    const bool isVar = std::exchange(declaresVariable_, false);
    // A function named as the initializer is bound as its function value (`let g = f`), so
    // that the name can be captured and returned like any other value. A name without a
    // single function value (an operator, a set of overloads) stays bound to what it names
    // and can be called where it was bound.
    const node_scope_ptr_t declScope = nodeScope_;
    node_handle_t node{};
    try {
        node = functionValueOf(valueNodeOf(visit(gct->at(0))), gct);
    } catch (const BuildAbortException &) {
        // The initializer is in error, so the name is not declared. Uses of it are
        // consequences of that error, not errors of their own (see poisonedNames_).
        declaresVariable_ = isVar;
        if (!declScope->has(ident, false)) {
            poisonedNames_[declScope].insert(ident);
        }
        throw;
    }
    declaresVariable_ = isVar;
    // `let g = f<w>` where `f<w>` is not one function: the name would be bound to `f` and the
    // arguments lost.
    try {
        rejectUnappliedBinding(node, gct->at(0));
        if (genericDrefs_.contains(node)) {
            // `let g = id`: no context to take the types from.
            (void)genericValueFor(node, nullptr, gct->at(0));
        }
    } catch (const BuildAbortException &) {
        if (!declScope->has(ident, false)) {
            poisonedNames_[declScope].insert(ident);
        }
        throw;
    }
    // Design principles 3.8: the empty literal `[]` takes its element type from where it
    // stands (an annotation, a parameter, the other arm of a branch, `as`). A name bound to
    // it without one has no element type; say so here, where the type can be written. (A
    // name the compiler made for an element of a literal starts with a digit.)
    if (Type *type = node != nullptr ? valueTypeOf(nodeTypeOf(node)) : nullptr;
        type != nullptr && isUntypedArrayType(type) && !ident.empty() &&
        !std::isdigit(static_cast<unsigned char>(ident.front())) && ident.front() != '$') {
        diags_->of(SemanticDiag::EmptyArrayWithoutType)
            .atOrigin(gct->at(0)->origin())
            .commit(ident);
        if (!declScope->has(ident, false)) {
            poisonedNames_[declScope].insert(ident);
        }
        throw BuildAbortException();
    }
    if (ident == GCT::kDiscardName) {
        // `let _ = e` (design principles 3.9): the value is evaluated where the statement
        // stands and dropped on purpose. No name is bound, so the statement can be repeated.
        if (node != nullptr) {
            discardedValues_.insert(node);
        }
        LEAVE("NREF");
        return node;
    }
    // `var x = e` binds x to a new variable holding the value of e; `let x = e` binds it to
    // that value.
    node_handle_t bound = isVar ? newVariable(node) : node;
    if (isVar) {
        // The cell is created by its declaration: that is its place in the source.
        registerNodeOrigin(context_, bound, gct, "gir.var");
    }
    bool success = insertNode(ident, bound);
    if (success && isVar) {
        originNames_.try_emplace(bound, ident);
    }
    if (!success) {
        diags_->of(SemanticDiag::Redeclaration).atOrigin(gct->origin()).commit(ident);
        throw BuildAbortException();
    }
    // The source name of the value, for the readers of the graph (`std::cml` prints it, the
    // debugger and diagnostics show it). A name the compiler made (a literal's element, a
    // capture) is not source, and a node that already has a name keeps it (`let y = x` binds
    // two names to one value; the first name wins).
    if (!ident.empty() && !std::isdigit(static_cast<unsigned char>(ident.front())) &&
        ident.front() != '$') {
        runtime::GraphDraft &draft = currGraph_->draft();
        if (draft.nodeName(bound.id) == camel::core::kEmptySym) {
            draft.setNodeName(bound.id, camel::core::internSymbol(ident));
        }
    }
    if (currGraph_ == rootGraph_ && !ident.empty() &&
        !std::isdigit(static_cast<unsigned char>(ident.front())) && ident.front() != '$') {
        // A value of the top level of the module: what the functions that use it capture.
        topLevelValues_[bound] = {module_.get(), ident};
    }
    if (recorder_ && !nodeIsKind(bound, runtime::GCNodeKind::Dref)) {
        recordDeclaration(ident, bound, gct);
    }
    LEAVE("NREF");
    return node;
}

std::optional<node_handle_t> Builder::modifierOf(node_handle_t node) const {
    if (node == nullptr) {
        return std::nullopt;
    }
    auto *graph  = nodeGraphOf(node);
    auto graphIt = nodeModifierMaps_.find(graph);
    if (graphIt == nodeModifierMaps_.end()) {
        return std::nullopt;
    }
    auto it = graphIt->second.find(nodeIdOf(node));
    if (it == graphIt->second.end()) {
        return std::nullopt;
    }
    return graph->draft().ref(it->second);
}

void Builder::setModifier(node_handle_t input, node_handle_t modifier) {
    ASSERT(input != nullptr && modifier != nullptr, "Cannot record modifier for a null node.");
    auto *inputGraph = nodeGraphOf(input);
    ASSERT(
        inputGraph == nodeGraphOf(modifier),
        "Modifier tracking must stay within a single compile graph.");
    noteVariable(input);
    nodeModifierMaps_[inputGraph][nodeIdOf(input)] = nodeIdOf(modifier);
}

void Builder::recordWrite(
    node_handle_t variable, node_handle_t entry, node_handle_t exit, bool throughCallee) {
    // `A(i); wait modify(i); B(i)` gives A -> modify -> B (design principles log 2.6). The second
    // half follows from setModifier. The first: the waited writer is ordered after every
    // operation that directly used the variable's value before it: the operations that took a
    // value read from it (the read itself when nothing took it), and the calls and branches
    // that used the variable through a capture. `wait` orders by this use of the variable, not
    // by position: in `A(i + 1); wait modify(i)` the direct user is `+`, not A. In a sync block
    // the block's chain already orders them. A writer that is neither waited nor in a sync
    // block is not ordered after earlier users; they stay pending for the next waited writer.
    auto &uses = pendingUsesOf(variable);
    if (waited_ && !synced_) {
        std::unordered_set<node_handle_t> before;
        for (const VariableUse &use : uses) {
            bool taken = false;
            if (use.isRead) {
                for (auto users : {normUsersOf(use.node), withUsersOf(use.node)}) {
                    for (draft_node_ref_t user : users) {
                        before.insert(nodeDraftOf(use.node)->ref(user));
                        taken = true;
                    }
                }
            }
            if (!taken) {
                before.insert(use.node);
            }
        }
        for (node_handle_t node : before) {
            if (node != exit && shouldAddSyncCtrlLink(node, entry)) {
                linkNodes(LinkType::Ctrl, node, entry);
            }
        }
    }
    if (waited_ || synced_) {
        uses.clear();
    }
    setModifier(variable, exit);
    if (!throughCallee) {
        if (ArmBuild *arm = activeArm()) {
            arm->directWrites.insert(variable);
        }
    }
    // A write of a captured variable is what callers of this graph have to see.
    if (!throughCallee) {
        if (auto captured = captureOriginOf_.find(variable); captured != captureOriginOf_.end()) {
            captures_[nodeGraphOf(variable)].written.insert(captured->second);
        }
    }
}

void Builder::orderAfterLastWriter(node_handle_t variable, node_handle_t node) {
    // A control input, and it has to stay one until the read gets the data edge it is owed: for
    // a call that reads a captured `var` this is the edge that makes the write the read uses
    // execute (design principles 3.5, F3), and an order edge would leave the read with the value
    // from before it. The two uses of this helper are one link today, and telling them apart
    // needs that data edge. The read is a data edge from the nearest write and writes are
    // ordered by order edges -- docs/handover/reviews/questions-2026-10-23.md,
    // "h242a-capture-order-edge".
    if (auto modifier = modifierOf(variable); modifier.has_value()) {
        if (sameGraph(*modifier, currGraph_) && shouldAddSyncCtrlLink(*modifier, node)) {
            linkNodes(LinkType::Ctrl, *modifier, node);
        }
    }
}

std::any Builder::visitDRefNode(const GCT::node_ptr_t &gct) {
    if (const auto oper = tree_->operRefOf(gct); oper.has_value()) {
        // A DREF the compiler made for an operator it knows by id: the group is the built-in
        // module's, which registered it; no name of the program takes part.
        ENTER("DREF");
        genericCalleeAllowed_ = false;
        refOrigin_            = gct->origin();
        const oper_group_ptr_t group = internalOperatorGroup(*oper);
        node_handle_t drefNode{currGraph_->addDrefNode(
            std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{group})};
        registerNodeOrigin(context_, drefNode, gct, "gir.dref");
        if (recorder_) {
            std::vector<std::string> signatures;
            for (const auto &[uri, resolver] : group->resolvers()) {
                signatures.push_back(resolver->signature());
            }
            recorder_->operatorReferenced(gct->origin(), group->name(), std::move(signatures));
        }
        LEAVE("DREF");
        return drefNode;
    }
    if (const std::string written = gct->name(); written.starts_with(kBuiltinPrefix)) {
        ENTER("DREF");
        genericCalleeAllowed_   = false;
        const std::string plain = written.substr(kBuiltinPrefix.size());
        const auto entity       = context_->importModule("")->getNativeEntity(Reference(plain));
        ASSERT(
            entity.has_value() && entity->isOperGroup(),
            "A built-in operator the compiler names does not exist: " + plain);
        node_handle_t drefNode{currGraph_->addDrefNode(
            std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{entity->operGroup()})};
        registerNodeOrigin(context_, drefNode, gct, "gir.dref");
        LEAVE("DREF");
        return drefNode;
    }
    ENTER("DREF");
    // `..ts` names the rest parameter of the expansion being built: the build-time tuple of the
    // arguments left over, not a value. `indices ts`, `ts.[j]` and `..ts` read it directly; any
    // other use is StaticTupleEscapes (valueNodeOf).
    if (const RestBinding *binding = restBindingNamed(gct->name()); binding != nullptr) {
        StaticTuple tuple = binding->tuple;
        tuple.name        = binding->name;
        tuple.origin      = gct;
        LEAVE("DREF");
        return tuple;
    }
    // `n` of a `static for`: a compile-time string, not a name of the enclosing scope. A use
    // of it as a value (not as the key of `t.[n]`) becomes a constant; the `j` of `indices`
    // binds the round's integer node instead of a string. A binding of the body with the same
    // name shadows it, as an ordinary local shadows anything outside it.
    if (auto staticValue = staticValueNamed(gct->name());
        staticValue.has_value() && !nodeAt(gct->name()).has_value()) {
        if (node_handle_t valueNode = staticValueNodeNamed(gct->name())) {
            LEAVE("DREF");
            return valueNode;
        }
        const data_ptr_t text = tt::as_shared<Data>(std::make_shared<StringData>(*staticValue));
        node_handle_t node    = currGraph_->addStaticDataNode(text);
        rememberConstant(node, text);
        registerNodeOrigin(context_, node, gct, "gir.data.static.for");
        LEAVE("DREF");
        return node;
    }
    // The name as written, and the name of the module it stands for when it is one (from the
    // innermost namespace out; design principles 10.2, layers 1 and 2).
    const string written = gct->name();
    if (written.find("::") != std::string::npos) {
        // `Shape::Empty`, a variant without fields: the value itself (design principles 5.3).
        EnumType *enumType = nullptr;
        size_t tag         = 0;
        if (enumVariantOfName(written, gct, enumType, tag)) {
            refOrigin_          = gct->origin();
            node_handle_t built = visitEnumConstruction(gct, enumType, tag);
            LEAVE("DREF");
            return built;
        }
    }
    const string name = moduleLevelName(written);
    // The position the name is read in, kept before the flag is cleared: in the callee position
    // a call of an `#impl` function is a call of its kernel, anywhere else the name is the
    // function value, which is the semantic body (design principles 10.4).
    const bool calleePosition = std::exchange(genericCalleeAllowed_, false);
    refOrigin_            = gct->origin();
    if (camel::core::isOverloadableOperatorGroup(written)) {
        // An operator (`a + b` is `__add__(a, b)`): its overloads from every layer are one
        // set (design principles 10.2), not those of the first layer that has the name.
        node_handle_t drefNode = operatorDrefOf(written, gct);
        LEAVE("DREF");
        return drefNode;
    }
    auto optNode = nodeAt(name);
    if (optNode.has_value()) {
        node_handle_t node = optNode.value();
        if (!sameGraph(node, currGraph_)) {
            // A name bound to an operator or to a set of overloads has no value that a
            // capture could hold (it was captured as a port of type void, and the call of
            // it then failed with "Function of type 'void' cannot be called").
            rejectNameWithoutValue(node, name, gct);
            // A value can only be captured from a graph that encloses this one (the root
            // graph for a value of a module's top level). A rest function expanded at a call
            // in another module is built in that caller's graph, so a name that is a value of
            // the function's own module has no path to it: say so instead of asserting.
            bool enclosed = false;
            for (auto outer = currGraph_; outer != nullptr && !enclosed;
                 outer      = outer->outerGraph()) {
                enclosed = outer.get() == nodeGraphOf(node);
            }
            if (!enclosed) {
                diags_->of(SemanticDiag::RestFunctionNeedsExpansion)
                    .atOrigin(gct->origin())
                    .commit(
                        captureNameOf(node, name),
                        "its body names a value of its own module, which a call in another "
                        "module cannot reach; a rest function expanded at a foreign call may "
                        "only name functions and its own arguments");
                throw BuildAbortException();
            }
            node = captureInto(currGraph_, node, name);
        }
        if (recorder_ && !nodeIsKind(node, runtime::GCNodeKind::Dref)) {
            recorder_->valueReferenced(
                gct->origin(),
                name,
                declarationOf(name),
                valueTypeOf(nodeTypeOf(node)));
        }
        LEAVE("DREF");
        return valueOf(node);
    }
    auto &graph = currGraph_;
    if (auto optTemplate = templateScope_->get(name); optTemplate.has_value()) {
        // The name of a generic function stands for a template, which has no graph. Called,
        // the arguments choose the instance; anywhere else a function is expected the
        // position does (design principles 5.5; genericValueFor), and until then the name
        // is this marker node.
        node_handle_t drefNode = graph->addDrefNode(
            std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{
                std::make_shared<graph_set_t>()});
        genericDrefs_[drefNode] = optTemplate.value();
        registerNodeOrigin(context_, drefNode, gct, "gir.dref.generic");
        if (recorder_) {
            recorder_->genericReferenced(gct->origin(), name, optTemplate.value()->func->origin());
        }
        LEAVE("DREF");
        return drefNode;
    }
    if (auto named = graphsAt(name); named.has_value() && !decoratedGraphAt(name).has_value()) {
        // A function with decorators of the top level is what its decorators make of it,
        // which exists once its body is built: built now when it is not.
        for (const auto &candidate : **named) {
            auto known = topLevelFunctions_.find(candidate.get());
            if (known == topLevelFunctions_.end()) {
                continue;
            }
            if (!appliedDecoratorsOf(known->second->gct).empty()) {
                ensureFunctionBuilt(candidate);
            }
        }
    }
    auto optDecorated = decoratedGraphAt(name);
    if (optDecorated.has_value() && overloadsOfDecorated(name, *optDecorated) != nullptr) {
        // A decorated function that shares its name with other functions: the name stands
        // for the set, and a call chooses among them (visitLinkNode).
        optDecorated.reset();
    }
    if (optDecorated.has_value()) {
        node_handle_t drefNode = graph->addDrefNode(
            std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{optDecorated.value()});
        registerNodeOrigin(context_, drefNode, gct, "gir.dref.decorated");
        if (recorder_) {
            reportFunctionReference(recorder_, gct, name, graphsAt(name));
        }
        LEAVE("DREF");
        return drefNode;
    }
    auto optGraphs = graphsAt(name);
    if (optGraphs.has_value()) {
        auto graphs = optGraphs.value();
        if (!graphs->empty()) {
            // An `#impl` function called directly is a call of its kernel (design principles
            // 10.4): the operator path makes one OPER node, and the body stays the semantic
            // function for whatever asks to look inside. Anywhere but the callee position the
            // name is the function value, which is the body graph (a pass differentiating,
            // fusing or exporting sees the semantics, not the kernel).
            if (calleePosition && !insideImplBody()) {
                if (std::optional<ImplKernel> kernel = implKernelOf(graphs);
                    kernel.has_value()) {
                    node_handle_t drefNode{graph->addDrefNode(
                        std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{
                            kernel->group})};
                    registerNodeOrigin(context_, drefNode, gct, "gir.dref.impl");
                    LEAVE("DREF");
                    return drefNode;
                }
            }
            node_handle_t drefNode{graph->addDrefNode(
                std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{graphs})};
            registerNodeOrigin(context_, drefNode, gct, "gir.dref");
            if (recorder_) {
                reportFunctionReference(recorder_, gct, name, graphs);
            }
            LEAVE("DREF");
            return drefNode;
        }
    }
    if (module_ != nullptr) {
        // A body-less declaration of this module: the name stands for the operator group the
        // declaration made, which is no graph (handover H19, step 1). A name the C++ module it
        // declares brings (its constants among them) is this module's own too, and is looked up
        // here, before what an import brings under the same name (design principles 10.1;
        // handover H94, ruling 1: the `.cml` and the `.cmo` are one module).
        for (const std::string &candidate : {name, written}) {
            auto native = ownNativeEntity(Reference(candidate));
            if (!native.has_value()) {
                continue;
            }
            if (native->isOperGroup()) {
                node_handle_t drefNode{graph->addDrefNode(
                    std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{
                        native->operGroup()})};
                registerNodeOrigin(context_, drefNode, gct, "gir.dref.native");
                LEAVE("DREF");
                return drefNode;
            }
            if (native->isValue()) {
                // A constant: the constant itself, as if it were written here as a literal
                // (`math::pi` is where it is declared; design principles 10.1).
                const GCT::node_ptr_t literal = tree_->makeData(native->value());
                literal->setOrigin(gct->origin());
                node_handle_t node = visitDataNode(literal);
                if (recorder_) {
                    recorder_->valueReferenced(
                        gct->origin(),
                        name,
                        camel::source::kInvalidOriginId,
                        valueTypeOf(nodeTypeOf(node)));
                }
                LEAVE("DREF");
                return node;
            }
        }
    }
    // Layers 3 to 5 (design principles 10.2): imported by name (or through a module or
    // namespace prefix), then through a `using namespace`, then what comes along unasked.
    // Operators take their overloads from every layer.
    std::optional<camel::core::module::entity> opt;
    bool importedName = false;
    // A layer has the name when it brings something of it: an import by name of a name the
    // module does not export leaves the name to the next layers (and is reported only when
    // none has it).
    std::optional<camel::core::module::entity> named;
    if (!isOperatorName(written) && module_->hasImportedRef(written, ImportLayer::Named)) {
        importedName = true;
        named        = importedEntity(written, ImportLayer::Named);
    }
    if (isOperatorName(written) || (named.has_value() && named->isOperGroup())) {
        // Operator groups (`+`, and the operators a module of operators brings) take their
        // overloads from every layer (design principles 10.2; the graph model, N3).
        importedName = importedName || module_->hasImportedRef(written);
        if (importedName) {
            opt = importedEntity(written);
        }
    } else if (named.has_value()) {
        opt = named;
    } else if (auto match = throughUsing(written, gct)) {
        if (!match->ownName.empty()) {
            // A name of this module, opened by a `using namespace` of one of its namespaces.
            GCT::node_ptr_t resolved = tree_->makeDRef(match->ownName);
            resolved->setOrigin(gct->origin());
            LEAVE("DREF");
            return visitDRefNode(resolved);
        }
        importedName = true;
        opt          = match->entity;
    } else if (module_->hasImportedRef(written, ImportLayer::Ambient)) {
        importedName = true;
        opt          = importedEntity(written, ImportLayer::Ambient);
    }
    if (importedName) {
        // An import of this name does not imply that the imported module exported it.
        if (!opt.has_value()) {
            diags_->of(SemanticDiag::ImportNameNotExported).atOrigin(gct->origin()).commit(name);
            throw BuildAbortException();
        }
        const auto &e = opt.value();
        if (e.isValue()) {
            // A constant a C++ module exports (`math::pi`; design principles 10.1): the
            // constant itself, here, as the same value written as a literal would be. It has
            // no node on the root graph and needs no capture.
            const GCT::node_ptr_t literal = tree_->makeData(e.value());
            literal->setOrigin(gct->origin());
            node_handle_t node = visitDataNode(literal);
            if (recorder_) {
                recorder_->valueReferenced(
                    gct->origin(),
                    name,
                    camel::source::kInvalidOriginId,
                    valueTypeOf(nodeTypeOf(node)));
            }
            LEAVE("DREF");
            return node;
        }
        if (camel::core::module::detail::EntityAccess::isNode(e)) {
            // A value exported by another module: a node of the root graph (design principles
            // 10.1), used like a value of this module's top level: on the root graph itself,
            // through a capture port in a function.
            node_handle_t node = exportedValueNode(
                *static_cast<ExportedValue *>(camel::core::module::detail::EntityAccess::node(e)));
            if (nodeIsKind(node, runtime::GCNodeKind::Dref)) {
                // A name bound to a set of overloads or to an operator has no value of its own.
                rejectNameWithoutValue(node, name, gct);
            }
            node = captureInto(currGraph_, node, name);
            if (recorder_) {
                recorder_->valueReferenced(
                    gct->origin(),
                    name,
                    camel::source::kInvalidOriginId,
                    valueTypeOf(nodeTypeOf(node)));
            }
            LEAVE("DREF");
            return node;
        } else if (camel::core::module::detail::EntityAccess::isGenericTemplate(e)) {
            // A generic function of another module: like a local one it is a marker until a
            // call or the position it stands in chooses the instance, which is built in this
            // module (instantiateGeneric). A name the other module declares as several generic
            // functions brings all of them, and a call takes the one its arguments fit.
            node_handle_t drefNode = graph->addDrefNode(
                std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{
                    std::make_shared<graph_set_t>()});
            genericDrefs_[drefNode] = generic_ref_t(std::make_shared<GenericGroup>(
                genericTemplatesOfEntity(e)));
            registerNodeOrigin(context_, drefNode, gct, "gir.dref.generic");
            LEAVE("DREF");
            return drefNode;
        } else if (camel::core::module::detail::EntityAccess::isGraphSet(e)) {
            auto graphs = asCompileGraphSet(camel::core::module::detail::EntityAccess::graphSet(e));
            // A direct call of an imported `#impl` function is a call of its kernel too (the
            // kernels are the same compile graphs in this program): the exported function is
            // the kernel's name for callers, while its value stays the semantic body.
            if (calleePosition && !insideImplBody()) {
                if (std::optional<ImplKernel> kernel = implKernelOf(graphs);
                    kernel.has_value()) {
                    node_handle_t drefNode{graph->addDrefNode(
                        std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{
                            kernel->group})};
                    registerNodeOrigin(context_, drefNode, gct, "gir.dref.impl");
                    LEAVE("DREF");
                    return drefNode;
                }
            }
            node_handle_t drefNode{graph->addDrefNode(
                std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{graphs})};
            registerNodeOrigin(context_, drefNode, gct, "gir.dref");
            if (recorder_) {
                reportFunctionReference(recorder_, gct, name, graphs);
            }
            LEAVE("DREF");
            return drefNode;
        } else if (e.isOperGroup()) {
            auto ops = e.operGroup();
            node_handle_t drefNode{graph->addDrefNode(
                std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{ops})};
            registerNodeOrigin(context_, drefNode, gct, "gir.dref");
            if (recorder_) {
                std::vector<std::string> signatures;
                for (const auto &[uri, resolver] : ops->resolvers()) {
                    signatures.push_back(resolver->signature());
                }
                recorder_->operatorReferenced(gct->origin(), name, std::move(signatures));
            }
            LEAVE("DREF");
            return drefNode;
        } else if (camel::core::module::detail::EntityAccess::isDecoratedGraph(e)) {
            auto decorated = camel::core::module::detail::EntityAccess::decoratedGraph(e);
            node_handle_t drefNode{graph->addDrefNode(
                std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{decorated})};
            registerNodeOrigin(context_, drefNode, gct, "gir.dref.decorated");
            if (recorder_) {
                recorder_->functionReferenced(gct->origin(), name, {});
            }
            LEAVE("DREF");
            return drefNode;
        }
    }
    if (isPoisonedName(name)) {
        throw BuildAbortException();
    }
    diags_->of(SemanticDiag::UnresolvedReference).atOrigin(gct->origin()).commit(name);
    throw BuildAbortException();
}

bool Builder::valueNameExists(const std::string &written, const GCT::node_ptr_t &gct) {
    if (written.starts_with(kBuiltinPrefix)) {
        return true;
    }
    if (camel::core::isOverloadableOperatorGroup(written) || isOperatorName(written)) {
        // The operator group exists even if no overload takes these operands; a call picks
        // an overload, and its diagnostic belongs to the code that is built.
        return true;
    }
    const std::string name = moduleLevelName(written);
    if (nodeAt(name).has_value() || templateScope_->get(name).has_value() ||
        graphsAt(name).has_value() || decoratedGraphAt(name).has_value()) {
        return true;
    }
    if (written.find("::") != std::string::npos) {
        // A qualified name: a variant of an enum or a name of an imported module. Their
        // resolution needs the type or the module, which the build of the selected case
        // does; for a case that is not selected the name is at least qualified.
        return true;
    }
    if (module_ != nullptr) {
        // A body-less declaration of this module: a name the module itself declared for a
        // native operator (handover H19, step 1). `visitDRefNode` looks it up here, before what
        // an import brings under the same name; a name in a `case` that is not selected must
        // see the same names, or the case would resolve differently once it is selected.
        for (const std::string &candidate : {name, written}) {
            auto native = module_->getNativeEntity(Reference(candidate));
            if (native.has_value() && native->isOperGroup()) {
                return true;
            }
        }
    }
    if (!isOperatorName(written) && module_->hasImportedRef(written, ImportLayer::Named)) {
        if (importedEntity(written, ImportLayer::Named).has_value()) {
            return true;
        }
    }
    if (isOperatorName(written) || module_->hasImportedRef(written)) {
        if (module_->hasImportedRef(written) && importedEntity(written).has_value()) {
            return true;
        }
    }
    if (throughUsing(written, gct)) {
        return true;
    }
    return module_->hasImportedRef(written, ImportLayer::Ambient) &&
           importedEntity(written, ImportLayer::Ambient).has_value();
}

node_handle_t Builder::visitCastNode(const GCT::node_ptr_t &gct) {
    ENTER("CAST");
    ExpectedValueScope expectedScope(expectedValueType_, boundType(gct->targetType()));
    if (const auto &operand = gct->at(0);
        operand->type() == GCT::NodeKind::DATA && containsTypeVar(gct->targetType()) &&
        asTagged(gct->targetType()) == nullptr &&
        asCollectionTypeOrPrototype(gct->targetType()) == nullptr) {
        // A literal converted to a type written with a type variable (`let zero: T = 0`,
        // `0 as T`). The GCT converts a literal to a concrete type itself; here the type is
        // only known now, so the same conversion is applied now.
        const data_ptr_t &data = operand->data();
        Type *targetType       = boundType(gct->targetType());
        const bool literal =
            !isComposite(data->type()->code()) || tt::as_shared<CompositeData>(data)->resolved();
        if (literal) {
            data_ptr_t converted;
            // A numeric literal takes the type the instantiation gives it, exactly (`let
            // zero: T = 0` with `T = float` is 0.0), including into a float from an integer
            // (design principles 5.1).
            const literal::Value *exact = literalValueOfGct(operand);
            if (exact != nullptr && targetType != nullptr &&
                literal::kindEnters(*exact, targetType)) {
                const auto data = exact->toData(targetType);
                if (!data.has_value()) {
                    rejectLiteralValue(*exact, targetType, operand);
                }
                converted = *data;
            } else if (
                gct->annotation() || targetType->castSafetyFrom(data->type()) == CastSafety::Safe) {
                converted = data->convertTo(targetType);
            }
            if (!converted) {
                rejectLiteralData(data, targetType, gct->origin(), gct->at(0));
            }
            node_handle_t node = currGraph_->addStaticDataNode(converted);
            rememberConstant(node, converted);
            if (varied_ && currGraph_->outerGraph().get() != nullptr) {
                node_handle_t copyNode = currGraph_->addCopyNode(converted->type());
                linkNodes(LinkType::Norm, node, copyNode);
                node = copyNode;
            }
            registerNodeOrigin(context_, node, gct, "gir.data");
            LEAVE("CAST");
            return node;
        }
    }
    Type *targetType = boundType(gct->targetType());
    if (targetType != nullptr && targetType->code() == TypeCode::Function) {
        // A function type written for a function: it says which function a name stands for
        // (the instance of a generic function, one of several overloads, a partial
        // application), and it must be the type of any other value. Nothing is converted.
        auto *wanted       = tt::as_ptr<FunctionType>(targetType);
        node_handle_t node = valueNodeOf(visit(gct->at(0)));
        if (genericDrefs_.contains(node)) {
            node = genericValueFor(node, wanted, gct->at(0));
        } else if (nodeIsKind(node, runtime::GCNodeKind::Dref)) {
            node_handle_t chosen{};
            if (boundWithArgs_.contains(node)) {
                chosen = partialApplicationOf(node, gct->at(0), wanted);
            }
            if (chosen == nullptr) {
                chosen = overloadValueFor(node, wanted, gct->at(0));
            }
            node = chosen != nullptr ? chosen : functionValueOf(node, gct->at(0));
            rejectNameWithoutValue(node, "the name", gct->at(0));
        } else {
            node = functionValueOf(node, gct->at(0));
        }
        Type *actual = valueTypeOf(nodeTypeOf(node));
        if (!targetType->assignableFrom(actual)) {
            diags_->of(SemanticDiag::DynamicCastForbidden)
                .atOrigin(gct->origin())
                .commit(actual->toString(), targetType->toString());
            throw BuildAbortException();
        }
        LEAVE("CAST");
        return node;
    }
    // A collection type written with a type variable is only checked now that it is replaced.
    rejectInvalidCollectionKey(targetType, gct);
    node_handle_t valueNode{};
    if (TupleType *tagged = asTagged(targetType);
        tagged != nullptr && gct->at(0)->type() == GCT::NodeKind::DATA) {
        // A literal put into `T?` / `T!`: it is first the literal of the payload type, by the
        // rule for any annotated literal (`let x: float? = 1`); `null` stays `null`.
        data_ptr_t data = gct->at(0)->data();
        const bool literal =
            !isComposite(data->type()->code()) || tt::as_shared<CompositeData>(data)->resolved();
        Type *payload = tagged->payloadType();
        if (literal && data->type()->code() != TypeCode::Void && !payload->equals(data->type()) &&
            (gct->annotation() || payload->castSafetyFrom(data->type()) == CastSafety::Safe)) {
            if (data_ptr_t converted = data->convertTo(payload)) {
                valueNode = currGraph_->addStaticDataNode(converted);
                rememberConstant(valueNode, converted);
                registerNodeOrigin(context_, valueNode, gct->at(0), "gir.data");
            } else if (
                const std::string notFit = literalNotFitReason(data, payload); !notFit.empty()) {
                rejectLiteralData(data, payload, gct->origin(), gct->at(0));
            }
        }
    }
    if (valueNode == nullptr) {
        valueNode = valueNodeOf(visit(gct->at(0)));
    }
    ASSERT(valueNode != nullptr, "Cast node value is null.");
    Type *sourceType = nodeTypeOf(valueNode);
    if (gct->annotation() && asTagged(targetType) == nullptr) {
        // The type a declaration is written with (`let x: T = e`; GCT Builder::declaredValue):
        // `e` must already have it. A value of that very type passes as it is (converting a
        // value to its own type does nothing, design principles 5.1); one of a type the
        // declared type accepts is cast to it, so that the name has the declared type; any
        // other is refused, as numbers are not converted implicitly (design principles 5.1).
        Type *have = valueTypeOf(sourceType);
        if (targetType->equals(have)) {
            LEAVE("CAST");
            return valueNode;
        }
        if (!targetType->assignableFrom(have)) {
            // An unnamed struct value whose fields are the declared type's fields, by name
            // but in another order, is copied into the declared type (design principles 5.2);
            // a value of another named type never is (`as` is written where it is made).
            bool converted = false;
            if (targetType->code() == TypeCode::Struct && have->code() == TypeCode::Struct) {
                auto *targetStruct = tt::as_ptr<StructType>(targetType);
                auto *haveStruct   = tt::as_ptr<StructType>(have);
                if (targetStruct != nullptr && haveStruct != nullptr && targetStruct->named() &&
                    !haveStruct->named()) {
                    if (node_handle_t copied = convertStructValue(valueNode, targetStruct, gct);
                        copied != nullptr) {
                        valueNode = copied;
                        converted = true;
                    }
                }
            }
            if (!converted) {
                diags_->of(SemanticDiag::DeclaredTypeMismatch)
                    .atOrigin(gct->origin())
                    .commit(have->toString(), targetType->toString());
                throw BuildAbortException();
            }
        }
    }
    if (asTagged(targetType) != nullptr &&
        targetType->castSafetyFrom(sourceType) != CastSafety::Safe) {
        // Several levels at once (`let x: int?! = 5`): one CAST per level.
        if (node_handle_t entered = coerceTo(valueNode, targetType, gct)) {
            LEAVE("CAST");
            return entered;
        }
    }
    // Safe conversions always; an Unsafe one only into a module type or an array, which then
    // check the value when the conversion runs (Type::castSlotFrom: an array checks its shape).
    // Primitive conversions that may lose information stay refused.
    const CastSafety safety = targetType->castSafetyFrom(sourceType);
    if (safety != CastSafety::Safe &&
        !(safety == CastSafety::Unsafe &&
          (targetType->isOtherType() || targetType->code() == TypeCode::Array))) {
        // An explicit `as` between struct types whose fields are the same by name but in
        // another order (or between two named types) is a copy, field by field (design
        // principles 5.2). A shape that does not match at all stays refused.
        if (targetType->code() == TypeCode::Struct && sourceType->code() == TypeCode::Struct) {
            if (node_handle_t copied =
                    convertStructValue(valueNode, tt::as_ptr<StructType>(targetType), gct);
                copied != nullptr) {
                LEAVE("CAST");
                return copied;
            }
        }
        diags_->of(SemanticDiag::DynamicCastForbidden)
            .atOrigin(gct->origin())
            .commit(sourceType->toString(), targetType->toString());
        throw BuildAbortException();
    }
    node_handle_t castNode = currGraph_->addCastNode(targetType);
    linkNodes(LinkType::Norm, valueNode, castNode);
    registerNodeOrigin(context_, castNode, gct, "gir.cast");
    LEAVE("CAST");
    return castNode;
}

node_handle_t Builder::visitVariNode(const GCT::node_ptr_t &gct) {
    ENTER("VARI");
    bool old          = varied_;
    bool oldDeclares  = declaresVariable_;
    varied_           = true;
    declaresVariable_ = true;
    node_handle_t node = valueNodeOf(visit(gct->at(0)));
    varied_            = old;
    declaresVariable_  = oldDeclares;
    LEAVE("VARI");
    return node;
}

node_handle_t Builder::visitWaitNode(const GCT::node_ptr_t &gct) {
    ENTER("WAIT");
    bool old        = waited_;
    waited_         = true;
    node_handle_t node = valueNodeOf(visit(gct->at(0)));
    waited_            = old;
    LEAVE("WAIT");
    return node;
}

oper_group_ptr_t Builder::internalOperatorGroup(camel::core::OperId oper) {
    const auto group = context_->importModule("")->nativeOperatorGroup(oper);
    ASSERT(
        group != nullptr,
        "The built-in module did not register the operator the compiler emits: " +
            std::string(camel::core::OperatorTable::instance().uri(oper)));
    return group;
}

GCT::node_ptr_t Builder::internalOperatorDref(camel::core::OperId oper) {
    // The id is the reference; the entity name is only the node's label (a GCT dump, a
    // diagnostic), as it was when the DREF was made by name.
    return tree_->makeDRef(oper, internalOperatorGroup(oper)->name());
}

node_handle_t Builder::variableOperator(camel::core::OperId oper, node_handle_t input) {
    const auto group = internalOperatorGroup(oper);
    const std::vector<StaticArg> statics(1);
    const auto op = group->resolve({}, {nodeTypeOf(input)}, statics, Modifier::None);
    ASSERT(op.has_value(), "Variable operator rejected its operand.");
    node_handle_t node = currGraph_->addOperNode(*op, 0);
    stampPermissions(node);
    linkNodes(LinkType::Norm, input, node);
    return node;
}

node_handle_t Builder::newVariable(node_handle_t value) {
    // The variable takes the widened type of its initializer (:var/new), so later assignments
    // may store values with a different refinement (e.g. shape).
    node_handle_t variable = variableOperator(camel::core::oper::VarNew, value);
    variables_.insert(variable);
    return variable;
}

node_handle_t Builder::readVariable(node_handle_t variable) {
    return readVariableThrough(variable, camel::core::oper::VarGet);
}

node_handle_t Builder::readVariableThrough(
    node_handle_t variable, camel::core::OperId oper) {
    node_handle_t value = variableOperator(oper, variable);
    pendingUsesOf(variable).push_back({value, true});
    if (auto modifier = modifierOf(variable); modifier.has_value()) {
        if (sameGraph(*modifier, currGraph_) && linkCheek(*modifier, value)) {
            linkNodes(LinkType::Ctrl, *modifier, value);
        }
    }
    // In a sync block a read happens where it is written, like a call.
    if (synced_) {
        if (lastSyncedNode_ != nullptr && shouldAddSyncCtrlLink(lastSyncedNode_, value)) {
            linkNodes(LinkType::Ctrl, lastSyncedNode_, value);
        }
        lastSyncedNode_ = value;
        syncedReads_.push_back(value);
        chainedNodes_.insert(value);
    }
    return value;
}

node_handle_t Builder::peekVariable(node_handle_t variable) {
    // The value of the variable without the store's right to its array: a path assignment
    // reading the value it writes must not take the array away from the set that follows.
    // It is a read all the same: ordered after the variable's last writer and, in a sync
    // block, in the block's chain.
    return readVariableThrough(variable, camel::core::oper::VarPeek);
}

std::optional<std::string> Builder::immutableNameOf(const GCT::node_ptr_t &expr) {
    GCT::node_ptr_t root = expr;
    while (root && root->type() == GCT::NodeKind::ACCS) {
        root = root->at(0);
    }
    if (!root || root->type() != GCT::NodeKind::DREF) {
        return std::nullopt;
    }
    const std::string name = moduleLevelName(root->name());
    // A name without a node is a function or an operator.
    const auto node = nodeAt(name);
    if (!node.has_value() || isVariable(*node)) {
        return std::nullopt;
    }
    return name;
}

node_handle_t Builder::visitWithNode(const GCT::node_ptr_t &gct) {
    ENTER("WITH");
    // `f<w>(x)`: the callee of the call is the target of the binding.
    genericCalleeAllowed_ =
        std::exchange(genericCalleeAllowed_, false) &&
        (gct->at(0)->type() == GCT::NodeKind::DREF || gct->at(0)->type() == GCT::NodeKind::TAPP);
    any targetNodeRes     = visit(gct->at(0));
    genericCalleeAllowed_ = false;
    // As in visitLinkNode: a lambda written in place gives its graph.
    node_handle_t targetNode = valueNodeOf(targetNodeRes);
    // Whether the value bound here is the callee of the call being built (`withArgsOfCall_`).
    const bool inCall = std::exchange(withArgsOfCall_, false);
    // A callee with a signature of its own: the only kind whose with-parameter types the call
    // itself completes. `reduce<(a, b) => a + b, 0>`: the element type comes from the array,
    // and the type of the result of the reduction from the position the call stands at, so the
    // lambda and the literal take their types there. A name is not deferred: a generic function
    // or a set of overloads bound as a with-argument stays a name until the callee resolves it.
    vector<Argument> inputs;
    FunctionType *argumentSignature = signatureForArgumentContext(targetNode);
    const bool parameterTypesFollowCall = inCall && argumentSignature != nullptr;
    // A call of a rest function is expanded at the call (inlineRestCall), so a with-argument
    // that names a generic function or a set of overloads stays a compile-time callable: every
    // call of it in the body picks the instance for that call's arguments (`tree_map<twice>`,
    // `tree_map<bump>`). Making it a function value here would need the one type it does not
    // have yet, and a set of overloads has no single value at all.
    const auto restInfo = genericDrefs_.find(targetNode);
    const bool restCall = restInfo != genericDrefs_.end() &&
                          restInfo->second.one() != nullptr &&
                          restInfo->second.one()->funcType->hasRest();
    const auto isCallableName = [&](node_handle_t node) {
        if (genericDrefs_.contains(node)) {
            return true;
        }
        if (node == nullptr || !nodeIsKind(node, runtime::GCNodeKind::Dref)) {
            return false;
        }
        const auto &target = nodeGraphOf(node)->drefTarget(node);
        if (std::holds_alternative<oper_group_ptr_t>(target)) {
            return true;
        }
        if (std::holds_alternative<graph_vec_ptr_t>(target)) {
            auto graphs = asCompileGraphVec(target);
            return graphs && graphs->size() > 1;
        }
        return false;
    };
    const auto *withNames = GCT::Tree::of(gct)->argumentNamesOf(gct);
    // A `with` argument may be written by name (`f<rounds: 8>`, design principles 4.4): the
    // name chooses the parameter the literal takes its type from, and is kept on the argument
    // for the call to bind (bindArguments). A binding used as a value has no names to
    // resolve them with, and is refused there.
    const auto slotOf = [&](size_t written) -> size_t {
        if (withNames == nullptr || written >= withNames->size() || !(*withNames)[written]) {
            return written;
        }
        if (argumentSignature != nullptr) {
            const std::string &name = *(*withNames)[written];
            for (size_t i = 0; i < argumentSignature->withTypesCount(); ++i) {
                if (name == argumentSignature->argNameAt(i)) {
                    return i;
                }
            }
        }
        return written;
    };
    for (size_t i = 1; i < gct->size(); i++) {
        const size_t slot = slotOf(i - 1);
        Type *wanted      = argumentSignature && slot < argumentSignature->withTypesCount()
                                ? argumentSignature->withTypeAt(slot)
                                : nullptr;
        ExpectedValueScope argumentScope(
            expectedValueType_,
            wanted && !containsTypeVar(wanted) ? wanted : nullptr);
        const std::optional<std::string> name =
            withNames != nullptr && i - 1 < withNames->size() ? (*withNames)[i - 1]
                                                              : std::nullopt;
        if (!restCall) {
            const GCT::NodeKind kind = gct->at(i)->type();
            // A literal whose type the signature still leaves open takes its type from the
            // position (`reduce<(a, b) => a + b, 0>`), and a lambda that writes no parameter
            // type needs the completed signature for it; both are built at the call, after the
            // ordinary arguments are typed. A lambda that writes every type is built here, as
            // before: nothing about it waits for the call.
            bool deferred = kind == GCT::NodeKind::DATA;
            if (!deferred && kind == GCT::NodeKind::FUNC) {
                const auto &lambda = gct->at(i);
                const auto *unwritten =
                    GCT::Tree::of(lambda)->unwrittenParamsOf(lambda);
                deferred = unwritten == nullptr || !unwritten->empty();
            }
            if (parameterTypesFollowCall && deferred && wanted != nullptr &&
                containsTypeVar(wanted)) {
                // Built at the call, where the ordinary arguments are typed and the signature's
                // variables have their types (visitLinkNode). A lambda's parameters come from
                // the parameter it is passed to (design principles 5.9). The name goes with it:
                // the binding puts it at its parameter's place before the literal is built.
                inputs.push_back({nullptr, gct->at(i), name});
                continue;
            }
            inputs.push_back({visitOperand(gct->at(i), gct), gct->at(i), name});
            continue;
        }
        any res              = visit(gct->at(i));
        node_handle_t argNode = res.type() == typeid(graph_ptr_t)
                                    ? createFuncDataNode(any_cast<graph_ptr_t>(res), true, false)
                                    : valueNodeOf(res);
        if (!isCallableName(argNode)) {
            argNode = functionValueOf(argNode, gct->at(i));
        }
        inputs.push_back({argNode, gct->at(i), name});
    }
    auto &bound = boundWithArgs_[targetNode];
    bound.insert(bound.end(), inputs.begin(), inputs.end());
    boundWithCallee_.try_emplace(targetNode, gct->at(0));
    // The `f<w>` expression the binding was made by: a partial application is located there,
    // at the callee and the angled arguments together (design principles 5.1), not at the
    // statement it is written in (H9 1.6). Its span is derived where the binding is used as a
    // value (partialApplicationOf), so an ordinary `f<w>(x)` allocates nothing extra.
    boundWithOrigin_.try_emplace(targetNode, gct);
    LEAVE("WITH");
    return targetNode;
}

node_handle_t Builder::visitAccsNode(const GCT::node_ptr_t &gct) {
    ENTER("ACCS");
    node_handle_t tgtNode = valueNodeOf(visit(gct->at(0)));
    ASSERT(tgtNode != nullptr, "Access node target is null.");
    if (EnumType *enumType = asEnumType(nodeTypeOf(tgtNode))) {
        // A value of an enum has the fields of the variant it is, and which one that is the
        // compiler does not know (design principles 3.8, 5.3): they are taken out by matching.
        std::string example;
        for (const EnumVariant &variant : enumType->variants()) {
            if (variant.fieldCount() > 0) {
                example = enumType->decl()->shortName() + "::" + variant.name;
                example += variant.shape == VariantShape::Struct
                               ? " { " + variant.fieldNames.front() + ", ... }"
                               : "(...)";
                break;
            }
        }
        diags_->of(SemanticDiag::EnumFieldAccess)
            .atOrigin(gct->origin())
            .commit(
                gct->accessByIndex() ? std::to_string(gct->accessIndex()) : gct->accessKey(),
                enumType->toString(),
                example);
        throw BuildAbortException();
    }
    if (!nodeTypeOf(tgtNode)->isComposite()) {
        diags_->of(SemanticDiag::TypeNotIndexable)
            .atOrigin(gct->origin())
            .commit(nodeTypeOf(tgtNode)->toString());
        throw BuildAbortException();
    }

    const auto tgtType = nodeTypeOf(tgtNode);
    Type *elemType     = nullptr;
    if (asTagged(tgtType) != nullptr) {
        // A `T?` / `T!` has the layout of a tuple and is not one: its parts are reached by
        // `??`, `!!`, `?->` and `match` only (design principles 3.9).
        diags_->of(SemanticDiag::TaggedValueNotOpened)
            .atOrigin(gct->origin())
            .commit(tgtType->toString(), "a member cannot be read from it");
        throw BuildAbortException();
    }

    switch (tgtType->code()) {
    case TypeCode::Tuple: {
        const auto &tupleType = tt::as_ptr<TupleType>(tgtType);
        // A tuple has a fixed length: an element past its end is a compile-time error (it was
        // read unchecked, which crashed the compiler in release builds).
        if (gct->accessIndex() >= tupleType->size()) {
            diags_->of(SemanticDiag::InvalidAccessIndex)
                .atOrigin(gct->origin())
                .commit(std::to_string(gct->accessIndex()));
            throw BuildAbortException();
        }
        elemType = tupleType->typeAt(gct->accessIndex());
        break;
    }
    case TypeCode::Array: {
        // `.N` reads a tuple element and `.name` a struct field. On an array no back end
        // implements either: the array was read as if it were a tuple, without a bounds
        // check, and the program printed whatever was there. An element is read with `[N]`.
        diags_->of(SemanticDiag::TypeNotIndexable)
            .atOrigin(gct->origin())
            .commit(tgtType->toString());
        throw BuildAbortException();
    }
    case TypeCode::Struct: {
        const auto *strTy    = tt::as_ptr<camel::core::type::StructType>(tgtType);
        const auto &optIndex = strTy->findField(gct->accessKey());
        if (!optIndex.has_value()) {
            diags_->of(SemanticDiag::InvalidAccessIndex)
                .atOrigin(gct->origin())
                .commit(gct->accessKey());
            throw BuildAbortException();
        }
        elemType = strTy->typeAt(optIndex.value());
        break;
    }
    default:
        ASSERT(false, "Unexpected target type in ACCS node.");
    }

    node_handle_t accsNode{};
    if (gct->accessByIndex()) {
        accsNode = currGraph_->addAccsNode(elemType, static_cast<uint32_t>(gct->accessIndex()));
    } else {
        accsNode = currGraph_->addAccsNode(elemType, gct->accessKey());
    }
    linkNodes(LinkType::Norm, tgtNode, accsNode);
    registerNodeOrigin(context_, accsNode, gct, "gir.accs");
    LEAVE("ACCS");
    return accsNode;
}

std::string Builder::bindTemporary(node_handle_t node) {
    std::string name = "$tagged" + std::to_string(tagTemporaries_++);
    insertNode(name, node);
    return name;
}

} // namespace camel::compile::gir
