// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The value of an expression: the data a literal carries (literalDataInType, visitDataNode,
 * nativeValueNode), a name read and a dereference (visitNRefNode, visitDRefNode), and a cast
 * (visitCastNode), with the reading of which written value a name gives and which write a node
 * follows (modifierOf, recordWrite, valueVersionOf, orderAfterLastWriter).
 *
 * A body's variables are in builder_variables.cpp, `with` and the access expression in
 * builder_with.cpp, and the layout of a body for direct SSA construction in builder_ssa.cpp.
 */

#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"
#include "camel/core/trace.h"
#include <format>


#include "camel/compile/gir/static_function.h"
#include "camel/core/data/composite/struct.h"
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
#include "camel/core/type/ref.h"

#include <atomic>
#include <cctype>
#include <stdexcept>
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
    // `null` is the compile-time constant of the nullable type it stands in (design principles
    // 4.4, 5.1): it is made in the position's type while it is made, so a position that requires a
    // constant -- a parameter default is the one that needs it today -- gets a value of the type,
    // not a cast of one (coerceTo registers the same constant for the positions that already hold
    // the literal's own node). Only a `T?` has a `null`: where no nullable type is expected the
    // literal keeps the type it has on its own, and the place it stands in reports the mismatch.
    if (data->isNull() && data->type()->code() == TypeCode::Void) {
        TupleType *tagged = asTagged(expected);
        if (tagged == nullptr || !tagged->isNullable()) {
            return nullptr;
        }
        const data_ptr_t value = Data::null(tagged);
        node_handle_t result   = currGraph_->addStaticDataNode(value);
        rememberConstant(result, value);
        // No private copy for a varied graph: a null is never written through, so the one
        // constant node serves every round.
        registerNodeOrigin(context_, result, gct, "gir.data.null");
        return result;
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
            GUARD(
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
                    data,
                    position,
                    gct,
                    refNames,
                    refTypes,
                    holes,
                    holeSlots);
            }
            if (positioned == nullptr && dataType->code() == TypeCode::Array && !refTypes.empty()) {
                // The element type of the literal: what its literal elements fixed (`[x, 5]` is
                // an `int[]`), else what the computed elements join to (`[x, null]`, `[a, b]` with
                // an `int` and an `int?`; design principles 3.8, 5.1). An element that is not of
                // it enters it, or is reported: an `int` value is not a `float`. A literal whose
                // element type the position names is built with it already (`let xs: float[] =
                // [n, m]`), so a nested literal never reads the type of an outer position.
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
                const auto *written = GCT::Tree::of(gct)->literalElementsOf(data);
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
                    // A value the element type does not take as it stands: nothing is converted
                    // here beyond what entering the type means (`int` into `int?` is a cast, an
                    // `int` into an `f32` is not a conversion at all -- design principles 3.8,
                    // 5.1), and what does not fit is reported at the element itself, where the
                    // literal wrote it, not at the name it is read from.
                    const GCT::Tree::LiteralElement *element = nullptr;
                    if (written != nullptr && i < holes.size() && holes[i] < written->size()) {
                        element = &(*written)[holes[i]];
                    }
                    const camel::source::origin_id_t at =
                        element != nullptr && element->origin != camel::source::kInvalidOriginId
                            ? element->origin
                            : gct->origin();
                    diags_->of(SemanticDiag::ElementTypeMismatch)
                        .atOrigin(at)
                        .commit("array", have->toString(), targetElem->toString());
                    throw BuildAbortException();
                }
            }
            auto filledType =
                positioned != nullptr
                    ? position
                    : (dataType->resolved()
                           ? dataType
                           : tt::as_ptr<CompositeType>(dataType->clone())->resolve(refTypes));
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
            // The computed element enters the slot the position gives it, and where the two types
            // stand on the edge between `T` and `T[..]` the value is converted as it enters
            // (design principles 5.4): the slot of an open rank holds a rank-zero array, and a
            // FILL links its element nodes as they are. This is the last entrance that did not go
            // through `Builder::coerceTo` -- a call argument was the first -- so it is converted
            // here, by the same question and the same call, rather than left to be read as an
            // array that is not there.
            const auto filledSlotType = [&](size_t slot) -> Type * {
                if (filledType->code() == TypeCode::Array) {
                    const auto *arrayType = tt::as_ptr<ArrayType>(filledType);
                    return arrayType != nullptr ? arrayType->elemType() : nullptr;
                }
                if (filledType->code() == TypeCode::Struct) {
                    const auto *structType = tt::as_ptr<StructType>(filledType);
                    return structType != nullptr && slot < structType->size()
                               ? structType->typeAt(slot)
                               : nullptr;
                }
                if (filledType->code() == TypeCode::Tuple) {
                    const auto *tupleType = tt::as_ptr<TupleType>(filledType);
                    return tupleType != nullptr && slot < tupleType->size()
                               ? tupleType->typeAt(slot)
                               : nullptr;
                }
                return nullptr;
            };
            for (size_t h = 0; h < holeSlots.size() && h < refNodes.size(); ++h) {
                Type *slotType = filledSlotType(holeSlots[h]);
                Type *elementType =
                    slotType != nullptr ? valueTypeOf(nodeTypeOf(refNodes[h])) : nullptr;
                if (slotType == nullptr || elementType == nullptr) {
                    continue;
                }
                if (isRankZeroEdge(slotType, elementType)) {
                    if (node_handle_t entered = coerceTo(refNodes[h], slotType, gct);
                        entered != nullptr) {
                        refNodes[h] = entered;
                    }
                    continue;
                }
                // A nested literal in the slot is the named type the position states (design
                // principles 5.2), not the unnamed type its own fields give it: without this the
                // fill put a `{ v: f32 }` where the field is `Inner`, and the read of the field
                // read a value of another type. The copy is field by field, the same one a
                // position makes for an unnamed struct value.
                if (slotType->code() == TypeCode::Struct &&
                    elementType->code() == TypeCode::Struct && !slotType->equals(elementType)) {
                    auto *slotStruct    = tt::as_ptr<StructType>(slotType);
                    auto *elementStruct = tt::as_ptr<StructType>(elementType);
                    if (slotStruct != nullptr && elementStruct != nullptr && slotStruct->named() &&
                        !elementStruct->named()) {
                        if (node_handle_t copied = convertStructValue(refNodes[h], slotStruct, gct);
                            copied != nullptr) {
                            refNodes[h] = copied;
                        }
                    }
                }
            }
            auto payload = runtime::makeFillPayload(fillKindForType(filledType), holeSlots);
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

node_handle_t Builder::nativeValueNode(const GCT::node_ptr_t &gct, Type *declared) {
    // A value declared without a right side is the one the C++ module of the declaration registers
    // under its name, of the type the declaration writes, or an error of the same kind as a
    // body-less function whose operator is missing or has another signature.
    const std::string &ident = gct->name();
    auto entity              = module_->getNativeEntity(Reference(ident));
    if (!entity.has_value()) {
        // The declaration may be written in a file that is not the C++ module itself (a file named
        // as the module): the C++ module of that name supplies it.
        for (const auto &supplier : context_->importedModules()) {
            if (supplier != module_ && supplier->name() == module_->name()) {
                entity = supplier->getNativeEntity(Reference(ident));
                if (entity.has_value()) {
                    break;
                }
            }
        }
    }
    if (!entity.has_value() || !entity->isValue()) {
        diags_->of(SemanticDiag::NativeValueNotLoaded).atOrigin(gct->origin()).commit(ident);
        throw BuildAbortException();
    }
    const data_ptr_t &data = entity->value();
    if (!declared->equals(data->type())) {
        diags_->of(SemanticDiag::NativeValueMismatch)
            .atOrigin(gct->origin())
            .commit(ident, declared->toString(), data->type()->toString());
        throw BuildAbortException();
    }
    node_handle_t node = currGraph_->addStaticDataNode(data);
    rememberConstant(node, data);
    registerNodeOrigin(context_, node, gct, "gir.data.native");
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
    // A cell whose value is written where the declaration stands: the child is the type the cell
    // holds (`typeof <the first write>`), not a value, so there is no initializer to evaluate
    // here (design principles 5.4).
    const bool typedCell = gct->size() > 0 && gct->at(0)->type() == GCT::NodeKind::TYPE;
    node_handle_t node{};
    try {
        if (Type *declared = GCT::Tree::of(gct)->nativeValueTypeOf(gct)) {
            node = nativeValueNode(gct, declared);
        } else if (typedCell) {
            node = newVariableOfType(visitTypeNode(gct->at(0)));
        } else {
            node = functionValueOf(valueNodeOf(visit(gct->at(0))), gct);
            // A `let` names a value, not the name it was read from: a state the loop head carries
            // is read out here, and what the name takes is a second name of it (secondNameOf), so
            // the writes further down the body copy instead of changing what it holds.
            if (!isVar) {
                node = secondNameOfCarriedName(gct->at(0), node);
            }
        }
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
        // A native value has nothing written to the right of the name, and neither has a cell
        // that is made with only its type.
        if (gct->size() > 0 && !typedCell) {
            rejectUnappliedBinding(node, gct->at(0));
            if (genericDrefs_.contains(node)) {
                // `let g = id`: no context to take the types from.
                (void)genericValueFor(node, nullptr, gct->at(0));
            }
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
        type != nullptr && gct->size() > 0 && !typedCell && isUntypedArrayType(type) &&
        !ident.empty() && !std::isdigit(static_cast<unsigned char>(ident.front())) &&
        ident.front() != '$') {
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
    // `var` x = e` binds x to a new variable holding the value of e; `let x = e` binds it to
    // that value. A cell that was made with only its type is already the variable.
    //
    // A `var` whose every read and every write lies in the region that declares it is built as a
    // value: the name is bound to the value, an assignment rebinds it, and no cell is made
    // ( same-region local
    // `var`s). The decision is the scan's, taken once, before this body was built, so it is the
    // same for every build of the body and does not depend on how the body is built.
    //
    // A `var` whose value is a counted handle is a value in one more shape: a closure that only
    // reads the name captures the handle itself and the cell is not there to be shared. A cell
    // holding a handle would have no row for its hold in the plan and the run would find no handle
    // to count, so a box behind such a cell is released while the name still holds
    // it -- `state::get` of a disposed handle. A handle is a value and a name of one needs no cell
    // (design principles 3.5), so the cell goes rather than being taught to hold.
    Type *heldValue = node != nullptr ? valueTypeOf(nodeTypeOf(node)) : nullptr;
    const bool aHandle = isVar && !typedCell && RefType::is(heldValue);
    const bool ssaValue = isVar && !typedCell && ssaDeclarationIsEligible(gct, ident, aHandle);
    node_handle_t bound = (isVar && !typedCell && !ssaValue) ? newVariable(node) : node;
    if (isVar && !ssaValue) {
        // The cell is created by its declaration: that is its place in the source.
        registerNodeOrigin(context_, bound, gct, "gir.var");
    }
    bool success = insertNode(ident, bound);
    if (success && ssaValue) {
        // The name stands for the value it was given: the declaration the reads take, and the entry
        // a write replaces. The type the entry keeps is the type the cell would have held, which is
        // `:var/new`'s value type, so a later write enters it as a store into that cell did.
        Type *held = bound != nullptr ? valueTypeOf(nodeTypeOf(bound)) : nullptr;
        ssa_.declare(nodeScope_, ident, gct, held != nullptr ? held->widened() : nullptr);
    }
    if (success && isVar && !ssaValue) {
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
        auto &draft = currGraph_->transaction().edit();
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
    return graph->view().ref(it->second);
}

void Builder::setModifier(node_handle_t input, node_handle_t modifier) {
    GUARD(input != nullptr && modifier != nullptr, "Cannot record modifier for a null node.");
    auto *inputGraph = nodeGraphOf(input);
    GUARD(
        inputGraph == nodeGraphOf(modifier),
        "Modifier tracking must stay within a single compile graph.");
    noteVariable(input);
    nodeModifierMaps_[inputGraph][nodeIdOf(input)] = nodeIdOf(modifier);
}

void Builder::recordWrite(node_handle_t variable, node_handle_t exit, bool throughCallee) {
    if (!throughCallee) {
        writesOf_[variable].push_back(exit);
        // A write written here is the variable's new version: the value a branch carried for it
        // stands for the write of that branch only, and stops being the version past this one.
        carriedVersions_.erase(variable);
    }
    // `A(i); modify(i); B(i)` gives A -> modify -> B (design principles log 2.6). The second half
    // follows from setModifier. The first half is the read this write meets: every read takes the
    // version the write before it produced, and a read written before the first write reads the
    // cell `:var/new` made (docs/design/effects/edges.cn.md 5.6). So the writes written after a
    // read are ordered after it, and that holds whether the write is a statement of a `sync`
    // block or not: where a read is written is what decides the value it sees, and a body that is
    // not synchronised does not move a read past a write written below it.
    //
    // This is where the rule is written and it is the whole of it (docs/design/effects/edges.cn.md
    // 5.6): for one cell, a write is ordered after every read since
    // the write before it. A chain place is not the carrier of that order -- a read is not a
    // statement of the chain, and the pure node a read stands beside may leave the chain -- so the
    // order is written here, once, by the write. Rewrites and inlining keep the edge: it is an
    // order edge of the pair, and replacing either end hands it to the replacement (4.6).
    auto &uses = pendingUsesOf(variable);
    {
        // A read is not on a sync chain, so the write is ordered after it by an order edge: the
        // read has to see the value as it was before the write changes it (docs/design/effects/
        // edges.cn.md 5.6). An order the read's value already carries to the write over the data
        // path is not written again: `x = x + 1` and the `x += 1` that is the same thing hand the
        // read's value to the operator the write stores, so the write runs after the read already
        // (design principles 2.1, 3.5).
        //
        // A write that is a region itself -- the JOIN a branch's arms write to, or a LOOP -- takes
        // the read as a control input instead, the way it takes the last write of its parts
        // (mergeArmVariables links an arm's writer to the JOIN as a control input). A region is
        // laid out as one piece: what its regions read from outside and what the region node
        // itself is ordered by come before its parts, and everything the region node is ordered
        // after comes last (src/execute/graph_runtime_support.cpp, the JOIN and LOOP blocks), so
        // an order edge into the region node would run the read after the writes it has to see the
        // cell before. A read this write cannot be ordered against -- a read in the region around
        // the write, with the write in an arm or a loop body -- stays pending: the write that
        // meets it is the region's own, and the region can order it.
        std::vector<VariableUse> pending;
        const camel::runtime::GCNodeKind exitKind = nodeKindOf(exit);
        const bool regionExit                     = exitKind == camel::runtime::GCNodeKind::Join ||
                                                    exitKind == camel::runtime::GCNodeKind::Loop;
        for (const VariableUse &use : uses) {
            // A read of the cell is the `:var/get` and the call that read a captured variable
            // (docs/design/effects/edges.cn.md 5.6: both are reads). The write written after
            // either of them is ordered after it, so the read sees the cell as it was before the
            // write changed it.
            const bool read =
                use.isRead || nodeKindOf(use.node) == camel::runtime::GCNodeKind::Call;
            if (!read || !sameGraph(use.node, currGraph_)) {
                // An operation that used the variable itself and is not a read -- the JOIN of a
                // branch that touched it -- is not ordered by a write: it stays pending so that
                // the next write meets it, and a region merges it around its own (pendingUsesOf,
                // mergeArmVariables).
                if (!synced_) {
                    pending.push_back(use);
                }
                continue;
            }
            if (regionExit) {
                if (!hasDirectDataDependency(use.node, exit) && !dataPathReaches(use.node, exit)) {
                    writeControlLink(use.node, exit);
                    continue;
                }
                pending.push_back(use);
                continue;
            }
            // A read that stands in the region around this write -- the write in an arm or a
            // loop body, the read in a region that encloses it -- is one this write cannot be
            // ordered against: an order edge from it would enter the region (4.6). It stays
            // pending for the write the region itself is, which takes it as a control input
            // (`regionExit` above, `mergeArmVariables`). The target's generator machine holds
            // exactly that shape: the dispatch's `:var/get` is a control input of the JOIN, and
            // no order edge enters a region.
            //
            // A write that is a statement of a `sync` block is the exception the same rule gives
            // another carrier: the branch is a statement of the chain, so the order the read has
            // with it belongs on the node that owns the arm (`rehomeOrderEdges` moves the edge
            // there), and the edge is written here as it was. Moving the read to the JOIN as well
            // would take that edge away: `order_edges_regions` shape 2 loses the one order edge
            // it pins on the `BRCH`.
            const uint32_t readRegion = nodeDraftOf(use.node)->nodeRegion(nodeIdOf(use.node));
            const uint32_t exitRegion = nodeDraftOf(exit)->nodeRegion(nodeIdOf(exit));
            if (readRegion != exitRegion && !synced_) {
                pending.push_back(use);
                continue;
            }
            if (nodeTakesAnOrder(use.node) && nodeTakesAnOrder(exit) &&
                !hasDirectDataDependency(use.node, exit) && !dataPathReaches(use.node, exit)) {
                linkNodes(LinkType::Order, use.node, exit);
                continue;
            }
            pending.push_back(use);
        }
        uses = std::move(pending);
    }
    // A store is one of the cell's known users (collapseNonEscapingVariableReads): a user the
    // builder did not record -- a:var/take, a call handed the `var` -- makes the cell escape.
    knownCellUsers_.insert(exit);
    // The write before this one is ordered before it, and it is not this write's value: every
    // write of a variable takes the cell :var/new made as its position, never the write before it
    // (docs/design/effects/edges.cn.md 5.6; docs/design-principles.cn.md 3.1). An order edge only
    // orders, so a write nothing reads still does not run (design principles 3.5, F3). Both ends
    // have to be nodes an order can sit on, which is what 4.6 asks: a branch's writes meet at its
    // JOIN, and the arm's own write carries the order from the write before the branch.
    if (auto previous = modifierOf(variable); previous.has_value()) {
        if (sameGraph(*previous, currGraph_) && linkCheek(*previous, exit) &&
            nodeTakesAnOrder(*previous) && nodeTakesAnOrder(exit) &&
            !hasDirectDataDependency(*previous, exit)) {
            linkNodes(LinkType::Order, *previous, exit);
        }
    }
    if (throughCallee && nodeKindOf(exit) == camel::runtime::GCNodeKind::Call) {
        // The version this write was recorded over. A call through a function value is only a
        // possible writer of a variable (the conservative writer set), so the write written before
        // it has to keep running, and the version a read takes after the call names it: an order
        // edge alone does not keep a write alive (design principles 3.5, F3).
        //
        // Two calls in a row are two writes of the variable, and the second one's "before" is the
        // first call's version, not the cell: what the read after the second takes is the variable
        // as both calls left it (`:var/after(:var/after(cell, call1), call2)`). valueVersionOf
        // answers that for a preceding call as it answers it for a write (valueVersionOf's own
        // `:var/after` row), so the chain is made here and nowhere else.
        if (auto previous = modifierOf(variable);
            previous.has_value() && sameGraph(*previous, currGraph_)) {
            if (node_handle_t version = valueVersionOf(variable); version != nullptr) {
                writeBeforeCall_[exit][variable] = version;
            }
        }
    }
    setModifier(variable, exit);
    // A write of a captured variable is what callers of this graph have to see.
    if (!throughCallee) {
        if (auto captured = captureOriginOf_.find(variable); captured != captureOriginOf_.end()) {
            captures_[nodeGraphOf(variable)].written.insert(captured->second);
        }
    }
}

uint32_t Builder::captureWriteMaskOf(const compile_graph_ptr_t &callee) const {
    const auto table = capturedValues_.find(callee.get());
    if (table == capturedValues_.end() || table->second.empty()) {
        return 0;
    }
    // In the mask, bit k is the callee's k-th capture, in the order its ports were made: that is
    // the order a call passes them in, after the declared `with` arguments (linkCaptureSites), and
    // what the run time reads the versions from (GCFuncRef::reserved). Only a variable is written,
    // so a capture of anything else never has its bit set.
    const std::set<node_handle_t> written = capturedWritesOf(callee.get());
    uint32_t mask                         = 0;
    for (size_t k = 0; k < table->second.size(); ++k) {
        if (!written.contains(table->second[k].origin)) {
            continue;
        }
        // One word of payload carries the mask. A function that captures more than this and
        // writes one of the later captures cannot hand its versions on: loud, not silent.
        GUARD(
            k < 32,
            "A call can carry the new version of at most 32 captured variables; this callee "
            "captures more.");
        mask |= uint32_t{1} << k;
    }
    return mask;
}

Type *Builder::callValueTypeOf(const compile_graph_ptr_t &callee, uint32_t written) const {
    Type *exit = callee->funcType()->exitType();
    if (written == 0) {
        return exit;
    }
    // The value is a tuple: the return value first, then the new version of each written
    // variable, in the callee's capture order. A `void` callee has no return value to put
    // first, and one that writes exactly one variable hands that version on directly, not
    // wrapped in a tuple of one element (the value of a captured cell is a cell, which is what
    // a read of the variable takes).
    const bool hasExit = !isVoidGraphExitType(exit);
    std::vector<Type *> parts;
    if (hasExit) {
        parts.push_back(exit);
    }
    const auto table = capturedValues_.find(callee.get());
    if (table != capturedValues_.end()) {
        for (size_t k = 0; k < table->second.size(); ++k) {
            if ((written & (uint32_t{1} << k)) == 0) {
                continue;
            }
            parts.push_back(nodeTypeOf(table->second[k].port));
        }
    }
    if (parts.size() == 1) {
        return parts.front();
    }
    return TupleType::create(parts);
}

node_handle_t Builder::callValuePartOf(node_handle_t call, uint32_t part) {
    CallValue &value = callValues_[call];
    if (part < value.parts.size() && value.parts[part] != nullptr) {
        return value.parts[part];
    }
    // The part's type is the tuple's element; a call whose value is one variable's version (a
    // `void` callee writing one) hands that version on itself and needs no access node.
    Type *type     = nodeTypeOf(call);
    Type *partType = type;
    if (type != nullptr && type->code() == TypeCode::Tuple) {
        partType = static_cast<const TupleType *>(type)->typeAt(part);
    }
    node_handle_t node = memberReadNodeOf(call, partType, part);
    // The node reads the call's value, and that value exists from the call on, so the node belongs
    // to the call's region and not to the region the first reader happens to stand in. It was made
    // in the region being built, which for the first reader inside a branch arm is that arm: a
    // second reader in the other arm was then handed the same node by the cache above and read a
    // node that runs only when the first arm is taken (defect A3, `if c then v else v` over a `var`
    // a call wrote). Nothing leaves a region here -- the input is already outside it, and a call
    // inside the arm keeps its node in the arm -- so a graph whose reads all stood in the region of
    // their call is unchanged.
    const uint32_t callRegion = nodeDraftOf(call)->nodeRegion(nodeIdOf(call));
    if (nodeDraftOf(node)->nodeRegion(nodeIdOf(node)) != callRegion) {
        nodeDraftOf(node)->setNodeRegion(nodeIdOf(node), callRegion);
    }
    if (value.parts.size() <= part) {
        value.parts.resize(part + 1, nullptr);
    }
    value.parts[part] = node;
    return node;
}

node_handle_t Builder::valueVersionOf(node_handle_t variable) {
    // A branch whose arms wrote the variable carries the write as its own value (design principles
    // 3.5: `if c then { x = 1 } else { x = 2 }` is `x = if c then 1 else
    // 2`). That join is recorded when the branch is built, because a JOIN that carries a value is
    // not by itself a write of this variable -- the value of `let r = if c then ...` is a join too,
    // and it is no version of anything. A later write of the same variable on a straight line
    // replaces it, and `recordWrite` drops the record then.
    if (const auto carried = carriedVersions_.find(variable); carried != carriedVersions_.end()) {
        return carried->second;
    }
    const auto modifier = modifierOf(variable);
    if (!modifier.has_value() || !sameGraph(*modifier, currGraph_)) {
        return nullptr;
    }
    // A write whose value is not the cell it wrote into gives that value: the assignment of a
    // variable, or a call whose value is one variable's version.
    Type *type = nodeTypeOf(*modifier);
    if (type != nullptr && valueTypeOf(type) != type) {
        return *modifier;
    }
    // A call writes captured variables by writing into the cells it was given; its value is the
    // tuple its parts are taken from (callValueTypeOf), and the part of the variable it wrote is
    // the version of it.
    const auto value = callValues_.find(*modifier);
    if (value == callValues_.end()) {
        // The call's captured writes are not in `callValues_` at all: its callee is a function
        // value, so there is no mask to build the parts from. The writer registry recorded the call
        // as this variable's write all the same (recordWrite, throughCallee), so the call is the
        // variable's new version: the read takes `:var/after(cell, call)`, which is the cell as it
        // stands after that call, and the edge from the call is what makes it run (3.1). Whatever
        // the call returns -- nothing, or a value of its own -- is not read here.
        if (nodeKindOf(*modifier) == camel::runtime::GCNodeKind::Call) {
            return variableAfterCall(variable, *modifier);
        }
        return nullptr;
    }
    const bool wrapped = value->second.hasReturn || value->second.versionCount > 1;
    for (const CallValue::Version &version : value->second.versions) {
        if (version.variable != variable) {
            continue;
        }
        if (!wrapped) {
            return *modifier;
        }
        return callValuePartOf(
            *modifier, value->second.hasReturn ? 1 + version.position : version.position);
    }
    // The call is this variable's writer -- recordWrite recorded it through the callee -- but the
    // parts of the call's value say nothing about it: the parts come from the callee's
    // captured-value table, and a write recorded through a function value has no row there. The
    // call is then the variable's version as a whole: `:var/after(cell, call)`.
    if (nodeKindOf(*modifier) == camel::runtime::GCNodeKind::Call) {
        return variableAfterCall(variable, *modifier);
    }
    return nullptr;
}

void Builder::orderAfterLastWriter(node_handle_t variable, node_handle_t node) {
    // A control input, and it has to stay one until the read gets the data edge it is owed: for
    // a call that reads a captured `var` this is the edge that makes the write the read uses
    // execute (design principles 3.5, F3), and an order edge would leave the read with the value
    // from before it. The two uses of this helper are one link today, and telling them apart
    // needs that data edge. The read is a data edge from the nearest write and writes are
    // ordered by order edges -- design principles 3.5 (a write through a captured variable is a
    // write to that cell, tracked where it happens).
    //
    // A write that produced a version of its own has that data edge already: the read takes the
    // version, so no control input stands in for it here (Builder::valueVersionOf).
    if (valueVersionOf(variable) != nullptr) {
        return;
    }
    if (auto modifier = modifierOf(variable); modifier.has_value()) {
        if (sameGraph(*modifier, currGraph_) && shouldAddSyncCtrlLink(*modifier, node)) {
            writeControlLink(*modifier, node);
        }
    }
}

std::any Builder::visitDRefNode(const GCT::node_ptr_t &gct) {
    std::any resolved = resolveDRefNode(gct);
    // The name of a rule's binding function is not a function value: the rule is not a `Rule`
    // until its `with` parameters are bound, and the name is what binds them (design principles
    // 5.5). So a use of it as a value is the call `name()`, whatever it was written beside: the
    // angled values of `name<value>` are taken here too (`boundWithArgs_`). Where the name is the
    // callee of a call or of a binding, the visit is left alone: the call is made there, with the
    // arguments it has.
    if (ruleBindingCallee_ || resolved.type() != typeid(node_handle_t)) {
        return resolved;
    }
    node_handle_t node = any_cast<node_handle_t>(resolved);
    if (isRuleBindingTarget(node)) {
        return ruleBindingValueOf(node, gct);
    }
    return resolved;
}

namespace {

/// The name of the conversion function of a scalar type, or null for a type that has none.
const char *conversionNameOf(const Type *type) {
    switch (type->code()) {
    case TypeCode::Int32:
        return "i32";
    case TypeCode::Int64:
        return "int";
    case TypeCode::Float32:
        return "f32";
    case TypeCode::Float64:
        return "float";
    case TypeCode::BFloat16:
        return "bf16";
    case TypeCode::Float16:
        return "f16";
    case TypeCode::Complex64:
        return "c64";
    case TypeCode::Complex128:
        return "c128";
    case TypeCode::String:
        return "string";
    default:
        return nullptr;
    }
}

} // namespace

std::any Builder::resolveDRefNode(const GCT::node_ptr_t &gct) {
    if (const auto oper = tree_->operRefOf(gct); oper.has_value()) {
        // A DREF the compiler made for an operator it knows by id: the group is the built-in
        // module's, which registered it; no name of the program takes part.
        ENTER("DREF");
        genericCalleeAllowed_        = false;
        refOrigin_                   = gct->origin();
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
        GUARD(
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
    string written = gct->name();
    // A type variable of the instance being built, written as a function (`T(x)` in `sum::<T>`),
    // is the conversion of the type it is bound to: `f32(x)` where `T = f32`. One rule with the
    // names of the types themselves, so it converts an array elementwise where they do (design
    // principles 5.1). A local name of that spelling shadows it.
    if (!nodeAt(moduleLevelName(written)).has_value()) {
        if (Type *bound = camel::core::type::findTypeBinding(typeBindings_, written)) {
            if (const char *conversion = conversionNameOf(bound)) {
                written = conversion;
            }
        }
    }
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
    refOrigin_                = gct->origin();
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
                if (std::optional<ImplKernel> kernel = implKernelOf(graphs); kernel.has_value()) {
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
        // declaration made, which is no graph (step 1). A name the C++ module it
        // declares brings (its constants among them) is this module's own too, and is looked up
        // here, before what an import brings under the same name (design principles 10.1;
        // rule 1: the `.cml` and the `.cmo` are one module).
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
    //
    // A name brought by name is the name the program means, whatever it is: the reference
    // written here with an import of it in force has that import's entity and no other layer's
    // (`import { range } from tensor` is tensor's `range`; the built-in of the same spelling is
    // the prelude, layer 5, and a name of layer 3 keeps the first layer having it -- design
    // principles 6.x, "local > this module > imported by name > using > built-in").
    std::optional<camel::core::module::entity> named;
    if (module_->hasImportedRef(written, ImportLayer::Named)) {
        named = importedEntity(written, ImportLayer::Named);
    }
    if (named.has_value()) {
        importedName = true;
        opt          = named;
    } else if (auto match = throughUsing(written, gct)) {
        // A `using namespace` is layer 4: it is above the prelude (layer 5) and below a name
        // imported by name (layer 3), so it is reached before the merged layer that the name's
        // built-in takes part in. Without this order a name the prelude has -- every built-in --
        // would answer through the merge and a `using namespace` could never supply it.
        if (!match->ownName.empty()) {
            // A name of this module, opened by a `using namespace` of one of its namespaces.
            GCT::node_ptr_t resolved = tree_->makeDRef(match->ownName);
            resolved->setOrigin(gct->origin());
            LEAVE("DREF");
            return visitDRefNode(resolved);
        }
        importedName = true;
        opt          = match->entity;
    } else if (isOperatorName(written) || module_->hasImportedRef(written)) {
        // Operator groups for a spelling an import did not bring by name (`+`, and the operators
        // a module of operators brings) take their overloads from every layer (design
        // principles 10.2; the graph model, N3); a name the prelude has and no layer above it
        // does is the built-in itself.
        importedName = true;
        opt          = importedEntity(written);
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
            genericDrefs_[drefNode] =
                generic_ref_t(std::make_shared<GenericGroup>(genericTemplatesOfEntity(e)));
            registerNodeOrigin(context_, drefNode, gct, "gir.dref.generic");
            LEAVE("DREF");
            return drefNode;
        } else if (camel::core::module::detail::EntityAccess::isGraphSet(e)) {
            auto graphs = asCompileGraphSet(camel::core::module::detail::EntityAccess::graphSet(e));
            // A direct call of an imported `#impl` function is a call of its kernel too (the
            // kernels are the same compile graphs in this program): the exported function is
            // the kernel's name for callers, while its value stays the semantic body.
            if (calleePosition && !insideImplBody()) {
                if (std::optional<ImplKernel> kernel = implKernelOf(graphs); kernel.has_value()) {
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
    // A name used as a value that is a tag. A tag keeps the name it is written with and does not
    // enter the value namespace (design principles 6.2), so a
    // tag and a value of one name may coexist and the tag is reached only through `#`. Saying that
    // beats telling the writer to check that a name the program declares is declared.
    if (tagNamed(name).has_value()) {
        diags_->of(SemanticDiag::TagNameAsValue).atOrigin(gct->origin()).commit(name);
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
    if (const ModuleUnit *unit = currentModuleUnit();
        unit != nullptr && unit->values.contains(name)) {
        // A value of the module's top level that has not been built yet: the name is the
        // module's own and `buildTopLevelValueNamed` builds it on demand, wherever it is
        // written (program.cpp, "the top level of a module does not depend on the order it is
        // written"). A body read ahead of that build must see it.
        return true;
    }
    if (written.find("::") != std::string::npos) {
        // A qualified name: a variant of an enum or a name of an imported module. Their
        // resolution needs the type or the module, which the build of the selected case
        // does; for a case that is not selected the name is at least qualified.
        return true;
    }
    if (const ModuleUnit *unit = currentModuleUnit();
        unit != nullptr && unit->nativeOperatorGroups.contains(name)) {
        // A body-less declaration of this module that it does not export (a private kernel): a
        // case that is not selected must see it as the selected case does.
        return true;
    }
    if (module_ != nullptr) {
        // A body-less declaration of this module: a name the module itself declared for a
        // native operator (step 1). `visitDRefNode` looks it up here, before what
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
    // A type written as `typeof e` (`let a: typeof(f()) = g()`, `x as typeof(f)`) is inferred
    // before anything is built with it, so that the expected type a literal takes is the inferred
    // one, and so that `e` is not evaluated by being written there (design principles 5.4).
    Type *written       = gct->targetType();
    const bool inferred = GCT::isTypeOfPlaceholder(written);
    if (inferred) {
        written = resolveTypeNode(written, gct);
    }
    ExpectedValueScope expectedScope(expectedValueType_, boundType(written));
    if (const auto &operand = gct->at(0);
        operand->type() == GCT::NodeKind::DATA && (inferred || containsTypeVar(written)) &&
        asTagged(written) == nullptr && asCollectionTypeOrPrototype(written) == nullptr) {
        // A literal converted to a type written with a type variable (`let zero: T = 0`,
        // `0 as T`). The GCT converts a literal to a concrete type itself; here the type is
        // only known now, so the same conversion is applied now.
        const data_ptr_t &data = operand->data();
        Type *targetType       = boundType(written);
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
    // The declared type is a type expression like any other position (design principles 5.6): a
    // `typeof` written in it is resolved here, without evaluating the expression it names.
    Type *targetType = resolveTypeNode(boundType(written), gct);
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
    GUARD(valueNode != nullptr, "Cast node value is null.");
    Type *sourceType = nodeTypeOf(valueNode);
    if (valueTypeOf(sourceType)->code() == TypeCode::Never) {
        // A value of `never` never exists (design principles 3.9): the position takes the node as
        // it stands, because there is nothing to convert -- and nothing that could fail to be
        // what the position asks for. A tagged position is no exception:
        // `never` is the bottom type and enters any
        // expected position without a conversion, tag or no tag, so a CAST is not built for it
        // here either.
        LEAVE("CAST");
        return valueNode;
    }
    if (targetType->code() == TypeCode::Array && asTagged(targetType) == nullptr) {
        // `T[..]` includes rank zero, which is the element itself (design principles 5.4): a value
        // of that element's type is the value the position names -- the same value, not an array
        // holding it. Nothing is converted, and the value keeps the element type: the type has no
        // separate rank-zero array, and a CAST node typed `T[..]` would say the value is an array
        // the run cannot read (`T[]` states one dimension and is not this case; rankZeroType()
        // answers nullptr for it).
        Type *rankZero = tt::as_ptr<ArrayType>(targetType)->rankZeroType();
        if (rankZero != nullptr && rankZero->assignableFrom(valueTypeOf(sourceType))) {
            LEAVE("CAST");
            return valueNode;
        }
    }
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
                // The value may still be a literal the position converts element by element: a
                // composite literal whose field is `T[..]` becomes the rank-zero value of the
                // element there (design principles 5.4), and a position converts its literal in
                // one place (`Builder::coerceTo`). Refusing here would refuse a value the
                // declaration itself makes, so the position is asked first.
                if (node_handle_t entered = coerceTo(valueNode, targetType, gct->at(0));
                    entered != nullptr) {
                    LEAVE("CAST");
                    return entered;
                }
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
    node_handle_t castNode = conversionNodeOf(valueNode, targetType, gct);
    // A conversion that can fail is a statement of a `sync` block like a call: it runs where it
    // is written, in the chain, with or without a reader of its value (design principles 3.4).
    if (synced_ && camel::runtime::castMayFault(targetType, sourceType)) {
        linkSyncStatement(lastSyncedNode_, castNode);
        lastSyncedNode_ = castNode;
        chainedNodes_.insert(castNode);
    }
    LEAVE("CAST");
    return castNode;
}

} // namespace camel::compile::gir
