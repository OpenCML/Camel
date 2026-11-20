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
#include "camel/core/trace.h"
#include <format>

namespace {
// The `[DBG-W]` and `[DBG-FOLD]` lines of the fold are one reading of the run's tracing channel,
// `--trace fold` (`include/camel/core/trace.h`). It was `CAMEL_LOG_DEBUG`, an environment switch,
// until the author's ruling of 2026-11-16: the environment is not where a run's settings come from
// (design principles 9.3), and one kind of reading has one way to ask for it.
namespace folddebug {
inline std::ostream &out() { return camel::core::trace(camel::core::TraceArea::Fold); }
} // namespace folddebug
} // namespace

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
    // (docs/handover/plans/2026-11-16-direct-ssa-construction.cn.md, stage S1: same-region local
    // `var`s). The decision is the scan's, taken once, before this body was built, so it is the
    // same for every build of the body and does not depend on how the body is built.
    //
    // A `var` whose value is a counted handle is a value in one more shape: a closure that only
    // reads the name captures the handle itself and the cell is not there to be shared. A cell
    // holding a handle is the shape the author answered on 2026-11-18 (`9d29939c4`: "a cell holding
    // a Ref is a holder, and the cell should go"): the plan gave the cell's hold no row and the run
    // found no handle to count, so a box behind such a cell was released while the name still held
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
    // 5.6, the author's 2026-11-03 ruling): for one cell, a write is ordered after every read since
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
    node_handle_t node = currGraph_->addAccsNode(partType, part);
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
    linkNodes(LinkType::Norm, call, node);
    return node;
}

node_handle_t Builder::valueVersionOf(node_handle_t variable) {
    // A branch whose arms wrote the variable carries the write as its own value (design principles
    // 3.5, the ruling of 2026-11-15: `if c then { x = 1 } else { x = 2 }` is `x = if c then 1 else
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
    const bool wrapped = value->second.hasReturn || value->second.variables.size() > 1;
    for (size_t k = 0; k < value->second.variables.size(); ++k) {
        if (value->second.variables[k] != variable) {
            continue;
        }
        if (!wrapped) {
            return *modifier;
        }
        return callValuePartOf(
            *modifier,
            static_cast<uint32_t>(value->second.hasReturn ? 1 + k : k));
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
    // version, and the control input this helper used to stand in for it is gone with it
    // (Builder::valueVersionOf).
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
    // enter the value namespace (design principles 6.2, the author's ruling of 2026-10-30), so a
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
        // what the position asks for. A tagged position is no exception (the author's ruling of
        // 2026-10-26 on `never-argument-scope`): `never` is the bottom type and enters any
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
    node_handle_t castNode = currGraph_->addCastNode(targetType);
    linkNodes(LinkType::Norm, valueNode, castNode);
    registerNodeOrigin(context_, castNode, gct, "gir.cast");
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

node_handle_t Builder::visitVariNode(const GCT::node_ptr_t &gct) {
    ENTER("VARI");
    bool old           = varied_;
    bool oldDeclares   = declaresVariable_;
    varied_            = true;
    declaresVariable_  = true;
    node_handle_t node = valueNodeOf(visit(gct->at(0)));
    varied_            = old;
    declaresVariable_  = oldDeclares;
    LEAVE("VARI");
    return node;
}

oper_group_ptr_t Builder::internalOperatorGroup(camel::core::OperId oper) {
    const auto group = context_->importModule("")->nativeOperatorGroup(oper);
    GUARD(
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
    const auto op = group->resolve({}, {nodeTypeOf(input)}, statics);
    if (!op.has_value()) {
        // ASSERT is an empty statement under NDEBUG, so the line below dereferenced an empty
        // optional and this was a 139 -- the second one tonight that a sentence would have
        // prevented (the first was Variant::updateRefs). The reading says which operand and what
        // node it is: a variable operator needs a version that carries a value, and `void` is not
        // one.
        throw std::runtime_error(
            std::format(
                "a variable operator got an operand with no value type: `:var/get` needs a version "
                "that carries a value, and the node it was given is a node of kind {} with none",
                static_cast<int>(nodeKindOf(input))));
    }
    node_handle_t node = currGraph_->addOperNode(*op, 0);
    stampPermissions(node);
    linkNodes(LinkType::Norm, input, node);
    return node;
}

node_handle_t Builder::secondNameOf(node_handle_t value) {
    // A name bound to a value, where there is no cell to read it out of: a `var x = <the state>`
    // hands the state to a new cell and `:var/new` counts what it is given, but a `let` has no cell,
    // and the name it binds has to be a read of its own -- a node of the value at this statement,
    // not the state it was read from (design principles 10.5). Otherwise every use of the name
    // would take the state where the use stood, and `let kept = xs` followed by `xs[i] = i + 1`
    // would read the write (`Builder::valueOf`, the round's value at the place of the read).
    Type *type = nodeTypeOf(value) != nullptr ? valueTypeOf(nodeTypeOf(value)) : nullptr;
    if (type == nullptr) {
        return value;
    }
    if (type->code() == TypeCode::Array) {
        // An array carries storage a write further down the round reaches in place, and there is no
        // count on this path to stop it: the name leaves with its own storage instead
        // (`:struct/copy_arr`, the operator the loop rewrite gives a carried array, and the same
        // answer `shareForSecondName` gives for an array the reads do not share).
        return variableOperator(camel::core::oper::CopyArray, value);
    }
    // Every other value has no storage a write could reach, so the value itself is what the name
    // holds, and the keeper gives the value a place of its own to be read from: the GATE of
    // docs/design/effects/edges.cn.md 5.3 and operator_table.h#kWellKnownCount, a region of one data
    // port reading the value, passed through. No sync predecessor: what the name needs is a place
    // for the value, not an order for it. It carries no count, as a value with no storage needs none.
    node_handle_t keeper = currGraph_->addGateNode(nodeTypeOf(value));
    linkNodes(LinkType::Norm, value, keeper);
    return keeper;
}

node_handle_t
Builder::secondNameOfCarriedName(const GCT::node_ptr_t &initializer, node_handle_t value) {
    // A `var x = <the state>` hands the state to a new cell, and `:var/new` counts what it is
    // given; a `let` has no cell, so the read itself is where the count has to be made.
    if (initializer == nullptr || initializer->type() != GCT::NodeKind::DREF ||
        initializer->size() != 0) {
        return value;
    }
    // A carried state reaches the body as one of two things: a port that stands for a cell
    // (`loopStatePorts_`, the cell form) or a name the body's scope declares as the port itself
    // (`SsaEnv::declarePort`, the value form stage S3 built).
    const auto bound = nodeAt(moduleLevelName(initializer->name()));
    const bool carried = (bound.has_value() && loopStatePorts_.contains(bound.value())) ||
                         ssa_.portOf(nodeScope_, initializer->name()) != nullptr;
    if (!carried) {
        return value;
    }
    return secondNameOf(value);
}

node_handle_t Builder::variableAfterCall(node_handle_t variable, node_handle_t call) {
    // The variable as it stands after `call`: `:var/after(cell, call)`. Its second argument is the
    // call, whose value may be `void` and is not read -- the edge is what makes the call run when
    // the read takes this version (docs/design-principles.cn.md 3.1, 3.5) -- and its result is the
    // cell itself, so the read above it is a read of the variable after that write.
    //
    // Its first argument is the version the variable had before that call when there was one: the
    // write written before a call through a function value must keep running, because the call is
    // only a possible writer of the variable (the conservative writer set), and the read takes what
    // the last real write left. The cell itself when there is none.
    node_handle_t before = variable;
    if (const auto byCall = writeBeforeCall_.find(call); byCall != writeBeforeCall_.end()) {
        if (const auto found = byCall->second.find(variable); found != byCall->second.end()) {
            before = found->second;
        }
    }
    const auto group = internalOperatorGroup(camel::core::oper::VarAfter);
    const std::vector<StaticArg> statics(1);
    const auto op = group->resolve({}, {nodeTypeOf(before), nodeTypeOf(call)}, statics);
    if (!op.has_value()) {
        throw std::runtime_error(
            std::format(
                "a variable after a call rejected its operands: the variable is {} and the call is "
                "{}",
                nodeTypeOf(before) != nullptr ? nodeTypeOf(before)->toString() : "<no type>",
                nodeTypeOf(call) != nullptr ? nodeTypeOf(call)->toString() : "<no type>"));
    }
    node_handle_t node = currGraph_->addOperNode(*op, 0);
    stampPermissions(node);
    linkNodes(LinkType::Norm, before, node);
    linkNodes(LinkType::Norm, call, node);
    return node;
}

node_handle_t Builder::newVariable(node_handle_t value) {
    // The variable takes the widened type of its initializer (:var/new), so later assignments
    // may store values with a different refinement (e.g. shape).
    node_handle_t variable = variableOperator(camel::core::oper::VarNew, value);
    variables_.insert(variable);
    return variable;
}

node_handle_t Builder::newVariableOfType(Type *type) {
    // The type the cell holds is the operator's one `with` argument: no operand, no value, and the
    // read of the cell has a type from the moment the cell is made.
    const auto group = internalOperatorGroup(camel::core::oper::VarNewEmpty);
    const std::vector<StaticArg> statics(0);
    const auto op = group->resolve({type}, {}, statics);
    GUARD(op.has_value(), "The empty cell operator rejected the type it was given.");
    node_handle_t variable = currGraph_->addOperNode(*op, 0);
    stampPermissions(variable);
    variables_.insert(variable);
    return variable;
}

node_handle_t Builder::readVariable(node_handle_t variable) {
    return readVariableThrough(variable, camel::core::oper::VarGet);
}

node_handle_t Builder::readVariableThrough(node_handle_t variable, camel::core::OperId oper) {
    // The version of the variable the last write made: that write's own value. Every write
    // produces one (valueVersionOf): an assignment gives the cell it wrote into, and a call that
    // wrote a captured variable gives the access node of the part of its value that is that
    // variable's new version. The read takes it as its data port, and that edge is the read's
    // whole tie to the write -- it orders the read after it and it is what makes the write
    // execute (docs/design/effects/edges.cn.md 1, 5.6; docs/design-principles.cn.md 3.5). A
    // writer whose value holds no version of this variable -- a branch whose writes meet at a
    // JOIN, an indirect call -- gives the cell and no version of its own: the read keeps the
    // cell as its data port and the write orders it, which a control input stands in for
    // (Builder::orderAfterLastWriter). Before any write the version is the cell :var/new made.
    node_handle_t version = valueVersionOf(variable);
    if (version != nullptr) {
        // A version that carries the variable's value rather than its cell -- a branch whose arms
        // wrote it, so the JOIN is the write (design principles 3.5, the ruling of 2026-11-15) --
        // is the read itself: there is nothing to take out of a cell, and `:var/get` would be an
        // operator on a node that holds no cell. The data edge from the write is the whole tie the
        // read needs, so it is the node the read stands for and the write's place orders whoever
        // reads it.
        Type *versionType = nodeTypeOf(version);
        if (versionType != nullptr && valueTypeOf(versionType) == versionType) {
            // The version is a node of the region it was built in, and a read stands in one:
            // `recordWrite` keeps a read the write cannot be ordered against pending for the
            // region (`readRegion != exitRegion`, builder_expr.cpp), and a read cannot take a
            // value out of a region it is not in for the same reason, one side later. The JOIN of
            // a statement branch written in a loop body is a node of that arm, and a read after the
            // loop is not in it: an edge from there to here is the branch-arm invariant
            // (src/runtime/branch_arms.cpp), which the graph is refused for at encode. The read
            // takes the cell instead -- the shape a read after a loop of a `var` has when no
            // branch carried it -- and the region it stands in orders it.
            auto &readDraft              = currGraph_->transaction().edit();
            const uint32_t versionRegion = readDraft.nodeRegion(nodeIdOf(version));
            const uint32_t readRegion    = readDraft.insertRegion();
            if (readDraft.regionWithin(readRegion, versionRegion)) {
                return version;
            }
            version = nullptr;
        }
    }
    if (version == nullptr) {
        // The conservative writer set may have counted a call of this graph as a possible writer of
        // the variable before this graph had captured it (a call through a function value writes a
        // variable this body had not touched yet): the read written after that call takes the
        // variable as it stands after it. Program order is what says "after": the builder writes
        // the body in the order it is written, so a claim met here was made by a call written
        // before this read.
        const auto origin           = captureOriginOf_.find(variable);
        const node_handle_t claimed = claimedWriterOf(
            currGraph_.get(),
            origin == captureOriginOf_.end() ? variable : origin->second);
        if (claimed != nullptr && sameGraph(claimed, currGraph_)) {
            version = variableAfterCall(variable, claimed);
        }
    }
    if (version == nullptr) {
        version = variable;
    }
    node_handle_t value = variableOperator(oper, version);
    pendingUsesOf(variable).push_back({value, true});
    if (version == variable) {
        if (auto modifier = modifierOf(variable); modifier.has_value()) {
            if (sameGraph(*modifier, currGraph_) && linkCheek(*modifier, value)) {
                writeControlLink(*modifier, value);
            }
        }
    }
    // SSA (collapseNonEscapingVariableReads): this read may later take its value from the write
    // that made it instead of from the cell, so the writer it belongs to is recorded here, while
    // the builder still knows it. The read node counts as one of the cell's known users.
    if (sameGraph(variable, currGraph_)) {
        variableReadSource_[value] = VariableRead{version, variable};
        knownCellUsers_.insert(value);
    }
    // A read is not a statement of a sync block: it does not join the chain. What orders it is the
    // version it takes as its data port and the writes recorded after it (design principles 3.5,
    // docs/design/effects/edges.cn.md 5.5). A `var` parameter may alias the caller's other
    // argument, so its read also waits on the preceding statement without taking over that
    // statement's place.
    if (synced_ && lastIndirectCall_ != nullptr && sameGraph(lastIndirectCall_, currGraph_) &&
        linkCheek(lastIndirectCall_, value) && nodeTakesAnOrder(value) &&
        nodeTakesAnOrder(lastIndirectCall_) && !hasDirectDataDependency(lastIndirectCall_, value)) {
        linkNodes(LinkType::Order, lastIndirectCall_, value);
    }
    if (synced_) {
        // A read is not a statement of the sync block's chain (the ruling this tree carries): it
        // takes its place from the version it reads, so the link the chain would write here is not
        // written at all.
        syncedReads_.push_back(value);
        const bool hasCaptureVersion = version != variable && captureOriginOf_.contains(variable);
        if (nodeKindOf(variable) == camel::runtime::GCNodeKind::Port && !hasCaptureVersion) {
            if (lastSyncedNode_ != nullptr && shouldAddSyncCtrlLink(lastSyncedNode_, value)) {
                writeControlLink(lastSyncedNode_, value);
            }
        }
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

void Builder::collapseNonEscapingVariableReads() {
    if (variableReadSource_.empty() || currGraph_ == nullptr) {
        return;
    }
    // A read nothing uses is no statement and runs for nobody (`let after = c` that is never read):
    // it is not on the chain any more, so nothing would reach it, and the writer's order edge to it
    // would name a node the graph does not keep. It goes before the reads that are folded, with the
    // order edge that kept the next write after it.
    {
        auto &graphDraft = currGraph_->transaction().edit();
        std::vector<node_handle_t> unused;
        for (const auto &entry : variableReadSource_) {
            const node_handle_t read = entry.first;
            // A read is recorded while this builder is in the graph that made it, and the table
            // outlives that graph: the graph is handed over and released, and the builder with it.
            // Asking `sameGraph` about such a read asserts that it has an owner (builder_support.h,
            // `nodeGraphOf`), which is exactly what is gone here, so the read asks the draft
            // whether it names it at all -- by address and by epoch, which is what a handle names
            // a draft by. A read of another graph is not this graph's read; it is skipped here and
            // dropped at the border by `forgetNodesOfOtherGraphs`, because an outer graph's reads
            // are still needed by the outer graph's own turn.
            if (!graphDraft.isNamedBy(read)) {
                continue;
            }
            if (graphDraft.header(nodeIdOf(read)) == nullptr || !graphDraft.alive(nodeIdOf(read))) {
                continue;
            }
            const draft_node_ref_t readId = nodeIdOf(read);
            if (readId == graphDraft.exitNode() || readId == graphDraft.outputNode() ||
                readId == graphDraft.returnNode()) {
                continue;
            }
            bool used = false;
            for (const auto &users :
                 {graphDraft.normUsersOf(readId),
                  graphDraft.withUsersOf(readId),
                  graphDraft.ctrlUsersOf(readId),
                  graphDraft.syncUsersOf(readId)}) {
                used = used || !users.empty();
            }
            if (!used) {
                unused.push_back(read);
            }
        }
        // The deletion goes through the edit interface: `eraseUnreadNodes` asks the permission and
        // hands the place the node held in its block's chain to the statement written after it
        // (`closeChainOver`, `spliceOrderEdgesOver`), so this builder erases by no second path.
        // Which reads may go is the membership test above, and it stays here: a read with no user
        // of any kind, sequencing edges included. A node the graph names (`namesNode`) keeps
        // itself, and then the read keeps its record too.
        for (const node_handle_t read : unused) {
            const draft_node_ref_t only[1] = {nodeIdOf(read)};
            if (!graphDraft.eraseUnreadNodes(only)) {
                continue;
            }
            variableReadSource_.erase(read);
        }
    }
    // The reads recorded for this graph, grouped by the cell they read. A cell of an outer graph
    // belongs to that graph's own turn, so it is skipped here.
    std::unordered_map<node_handle_t, std::vector<std::pair<node_handle_t, node_handle_t>>> byCell;
    auto &draft = currGraph_->transaction().edit();
    for (const auto &[read, info] : variableReadSource_) {
        // Same reason as above: the draft is asked whether it is the one the cell names, because a
        // cell of a graph that has been released has no builder left to answer `sameGraph`.
        if (!draft.isNamedBy(info.cell)) {
            continue;
        }
        byCell[info.cell].push_back({read, info.source});
    }
    // `variableReadSource_` is hashed on the handle's address, so each cell's reads arrive here in
    // the allocator's order, and the fold below hands them to the graph in it. Sorted by the name
    // the program gives each read (its graph, then its id -- `walksBefore`, builder_helpers.h), the
    // stretches are the program's; the cell's own order is what the walk outside asks about.
    for (auto &[cell, reads] : byCell) {
        std::sort(reads.begin(), reads.end(), [](const auto &left, const auto &right) {
            return walksBefore(left.first, right.first);
        });
    }
    // Every exit of this fold is counted, because three times now a condition was written,
    // compiled,
    // and had no effect -- each time found by guessing at readings. The counts go to stderr when
    // CAMEL_LOG_DEBUG is set: initial (the value `:var/new` made, only for a variable never
    // written), loop (carried state, not folded), join (a branch's value merge point), write (the
    // value a write wrote), nothing (a source the fold does not stand for, or the read kept for
    // another reason).
    static int foldTookInitial_ = 0;
    static int foldSawLoop_     = 0;
    static int foldTookJoin_    = 0;
    static int foldTookWrite_   = 0;
    static int foldTookNothing_ = 0;
    // The reads that took the value a straight-line write wrote, found by writesOf_ and not by the
    // version record (the branch that a read of a still-cell source takes).
    static int foldTookFold_ = 0;
    struct FoldCountsReport {
        ~FoldCountsReport() {
            folddebug::out() << std::format(
                "[DBG-FOLD] initial={} loop={} join={} write={} nothing={} fold={}\n",
                foldTookInitial_,
                foldSawLoop_,
                foldTookJoin_,
                foldTookWrite_,
                foldTookNothing_,
                foldTookFold_);
        }
    };
    static FoldCountsReport foldCountsReport_;
    // The cells by the name the program gives them, not the order their hashes fall in: the fold
    // builds one node after another and the graph numbers its nodes in creation order.
    std::vector<node_handle_t> folded;
    folded.reserve(byCell.size());
    for (const auto &entry : byCell) {
        folded.push_back(entry.first);
    }
    std::sort(folded.begin(), folded.end(), walksBefore);
    for (const node_handle_t cell : folded) {
        std::vector<std::pair<node_handle_t, node_handle_t>> &reads = byCell.at(cell);
        // A `var` parameter of this graph is a resource shared with the caller, and a captured cell
        // is shared with a closure: both keep their cell. A `var` that only a write put in
        // `escapingVariables_` is not the same thing: the assignment is the write, it does not
        // exist after the call, so being written is not a way of leaving the body. It is given to
        // the fold here, and what keeps it a resource then has to be a real escape (written into
        // `escapingVariables_` by a capture or a user function's `var` parameter) or a write the
        // fold cannot stand for -- a read whose version is a carried state (the LOOP) is not
        // folded below, so one such read is enough to keep the cell:
        //   straight-line (foldable): every read's version is a write of the same body, and no
        //   read's version is a LOOP. Anything else -- a loop's carried state, a read whose version
        //   is a merge point the fold does not accept, an arm's region -- is not straight-line.
        if (nodeKindOf(cell) == camel::runtime::GCNodeKind::Port) {
            continue;
        }
        if (escapingVariables_.contains(cell) && !writeOnlyEscaping_.contains(cell)) {
            continue;
        }
        const camel::runtime::gc_node_ref_t cellId = nodeIdOf(cell);
        if (draft.header(cellId) == nullptr) {
            continue;
        }
        // Every user of the cell must be one this builder recorded: one of its reads, or one of its
        // stores. Any other user -- a `:var/take`, a call the `var` was handed to, a capture port
        // -- is a use this conversion does not model, so the cell keeps its cell.
        bool known = true;
        for (auto users : {normUsersOf(cell), withUsersOf(cell), ctrlUsersOf(cell)}) {
            for (draft_node_ref_t user : users) {
                if (!knownCellUsers_.contains(nodeDraftOf(cell)->ref(user))) {
                    known = false;
                    break;
                }
            }
            if (!known) {
                break;
            }
        }
        if (!known) {
            continue;
        }
        // The value of a cell nothing has written yet is the one it was made with (:var/new's
        // input).
        const auto cellInputs = draft.normInputsOf(cellId);
        for (auto &[read, source] : reads) {
            const camel::runtime::gc_node_ref_t readId = nodeIdOf(read);
            if (draft.header(readId) == nullptr) {
                continue;
            }
            // A read a capture site handed to a direct call -- the call's `with` input -- is a read
            // like any other: it is an edge of the graph from the moment the call was built
            // (addCaptureSite), so the fold below rewrites the call's input with the rest of the
            // read's users and the cell's unit goes with it (dropCarriedVariableUnits).
            // A read of a value that can be *kept* stays a read. `markNonRetainingReads` turns a
            // read nobody keeps into `:var/peek` and lets the next append change the array in
            // place; a read that can be kept stays `:var/get` and the memory plan copies instead.
            // That decision is made once the whole program is built, and this fold runs at the end
            // of one graph (`leaveScope`), so it cannot ask it: it must leave every reference-
            // holding value inside its read. Handing the reader the array the variable still owns
            // makes a later in-place append show through it -- `new_values/peek.cml`'s
            // `let s = acc` kept `[1, 2]` before, and folding its read made it follow `acc` to
            // `[1, 2, 3]`. A number, a bool or a string has no place to keep, and folds as before.
            const Type *readType = nodeTypeOf(read);
            if (readType != nullptr && readType->code() != TypeCode::String &&
                isGCTraced(readType->code())) {
                ++foldTookNothing_;
                folddebug::out() << std::format(
                    "[DBG-W] read={} writes=- candidate=none kind=- region=- took=kept\n",
                    static_cast<unsigned>(readId));
                continue;
            }
            // A write gives the cell of this version; what a read wants is the value written, and
            // a write holds it as its second operand. A writer that is not one of those -- the JOIN
            // a branch's writes meet at, say -- gives a cell and no value of its own, so that read
            // keeps its cell: the writer it takes its version from is still the one the design
            // names, and reading it is what the design says a read does.
            camel::runtime::gc_node_ref_t version = readId;
            if (source == cell) {
                // A read wants the value written last before it. No write before it means the
                // variable has not been written yet, and then it is the value `:var/new` made.
                node_handle_t candidate = nullptr;
                size_t writesSeen       = 0;
                if (const auto it = writesOf_.find(cell); it != writesOf_.end()) {
                    writesSeen = it->second.size();
                    // A read inside an arm, a loop body or a loop condition of a cell that is
                    // written somewhere does not see `:var/new`'s value. In a loop it sees the
                    // value the previous turn carried, which the LOOP hands on and no write of the
                    // body stands for: `write_in_loop.cml` (`x = x + 1`) read the value `:var/new`
                    // made on every turn, so the turns wrote `1` and the read after the loop
                    // answered `1` instead of `3`. In an arm it may see a value another arm wrote,
                    // or the one a write before the branch produced: `warnings_visible.cml` (`var c
                    // = 0; inc(c); if c > 0 then { c += 10 }`) read the initial `0` inside the arm
                    // and answered `1` where `11` is what the program says. So such a read keeps
                    // its `:var/get`, and that is decided before the initial value is handed out.
                    if (writesSeen > 0) {
                        const auto readRegionId = draft.nodeRegion(readId);
                        const auto readKind     = draft.region(readRegionId).kind;
                        if (readKind == camel::runtime::GCRegionKind::Arm ||
                            readKind == camel::runtime::GCRegionKind::LoopBody ||
                            readKind == camel::runtime::GCRegionKind::LoopCond) {
                            ++foldTookNothing_;
                            folddebug::out() << std::format(
                                "[DBG-W] read={} writes={} readregion={} candidate=- kind=- "
                                "region=- took=region-read\n",
                                static_cast<unsigned>(readId),
                                writesSeen,
                                static_cast<int>(readRegionId));
                            continue;
                        }
                    }
                    // A write this fold cannot name: a call that wrote the cell through a `var`
                    // argument leaves its value in no node's input, so a read after it can take
                    // neither that value nor the initial one -- the call has already run. Whether a
                    // write came before the read and whether that write can be named are two
                    // questions: only "no write before it" licenses `:var/new`'s value.
                    bool aCallWroteBeforeTheRead = false;
                    for (const node_handle_t exit : it->second) {
                        if (nodeIdOf(exit) >= readId) {
                            continue;
                        }
                        if (nodeKindOf(exit) != camel::runtime::GCNodeKind::Oper) {
                            aCallWroteBeforeTheRead = true;
                            continue;
                        }
                        const auto ins = draft.normInputsOf(nodeIdOf(exit));
                        if (ins.empty() || ins[0] != cellId) {
                            continue;
                        }
                        candidate = exit;
                    }
                    if (aCallWroteBeforeTheRead) {
                        ++foldTookNothing_;
                        folddebug::out() << std::format(
                            "[DBG-W] read={} writes={} candidate=none kind=- region=- "
                            "took=call-write\n",
                            static_cast<unsigned>(readId),
                            writesSeen);
                        continue;
                    }
                }
                if (candidate == nullptr) {
                    if (cellInputs.empty()) {
                        ++foldTookNothing_;
                        continue;
                    }
                    version = cellInputs[0];
                    ++foldTookInitial_;
                    folddebug::out() << std::format(
                        "[DBG-W] read={} writes={} candidate=none kind=- region=- took=initial\n",
                        static_cast<unsigned>(readId),
                        writesSeen);
                } else {
                    // The candidate's region, by id: `region()` takes a region id, and a node's is
                    // `nodeRegion`. An arm's or a loop's write is a write of that region, and the
                    // fold does not stand for it here (a region is laid out as one piece).
                    const auto regionId = draft.nodeRegion(nodeIdOf(candidate));
                    const auto kind     = draft.region(regionId).kind;
                    const bool refused  = kind == camel::runtime::GCRegionKind::Arm ||
                                          kind == camel::runtime::GCRegionKind::LoopCond ||
                                          kind == camel::runtime::GCRegionKind::LoopBody ||
                                          kind == camel::runtime::GCRegionKind::Dead;
                    folddebug::out() << std::format(
                        "[DBG-W] read={} writes={} candidate={} kind={} region={} took={}\n",
                        static_cast<unsigned>(readId),
                        writesSeen,
                        static_cast<unsigned>(nodeIdOf(candidate)),
                        static_cast<int>(nodeKindOf(candidate)),
                        static_cast<int>(regionId),
                        refused ? "nothing" : "fold");
                    if (refused) {
                        ++foldTookNothing_;
                        continue;
                    }
                    const auto w = draft.normInputsOf(nodeIdOf(candidate));
                    version      = w[1];
                    ++foldTookFold_;
                }
            } else if (nodeKindOf(source) == camel::runtime::GCNodeKind::Loop) {
                // A write inside a loop is carried state: the version a read after the loop takes
                // is the loop, which is not a value the fold can stand in for (schedule item 3).
                // Folding it would answer with the value from before the loop.
                ++foldSawLoop_;
                continue;
            } else {
                if (nodeKindOf(source) == camel::runtime::GCNodeKind::Join) {
                    // A branch's JOIN selects between the values its arms give. When it carries a
                    // value -- its type is the value's and not a cell's -- the read takes the join
                    // itself: that is the branch's result, which is what a write inside a branch is
                    // (`if c then { x = 1 }` merges 1 and the value from before the branch).
                    Type *sourceType = nodeTypeOf(source);
                    if (sourceType == nullptr || valueTypeOf(sourceType) != sourceType) {
                        ++foldTookNothing_;
                        continue;
                    }
                    version = nodeIdOf(source);
                    ++foldTookJoin_;
                } else {
                    if (nodeKindOf(source) != camel::runtime::GCNodeKind::Oper) {
                        ++foldTookNothing_;
                        continue;
                    }
                    const auto written = draft.normInputsOf(nodeIdOf(source));
                    if (written.size() < 2) {
                        ++foldTookNothing_;
                        continue;
                    }
                    version = written[1];
                    ++foldTookWrite_;
                }
            }
            if (version == cellId || draft.header(version) == nullptr) {
                continue;
            }
            // A GATE that waits on this read keeps that wait: the read is the statement the block's
            // value is handed out after, so folding it away would take the wait with it and leave
            // the GATE waiting for nothing, which the commit refuses (src/runtime/graph.cpp, the
            // GATE check; docs/design/effects/edges.cn.md 5.3 gives the GATE the statement before
            // it as its wait). The read stays, and it reads what it read before.
            bool waitedOnByGate = false;
            for (const auto &users : {draft.ctrlUsersOf(readId), draft.syncUsersOf(readId)}) {
                for (camel::runtime::gc_node_ref_t user : users) {
                    const camel::runtime::DraftNodeHeader *userHeader = draft.header(user);
                    if (userHeader != nullptr &&
                        userHeader->kind == camel::runtime::GCNodeKind::Gate) {
                        waitedOnByGate = true;
                        break;
                    }
                }
                if (waitedOnByGate) {
                    break;
                }
            }
            if (waitedOnByGate) {
                continue;
            }
            // The read stood in a chain, and its place in it is the version's now: what ordered the
            // read orders the version, and what the read ordered is ordered by the version
            // (docs/design/effects/edges.cn.md 4). `eraseNode` splices a chain *over* the node it
            // removes (A -> B -> C becomes A -> C), which keeps the ordering only when the read had
            // a predecessor; a read at the head of a chain -- the one a BRCH keeps to say "read
            // this before the branch" -- has none, so its edge would go with it and the branch
            // would lose the order a later pass reads as its execution order
            // (order_edges_regions.gir).
            draft.replaceOrderEdgesAt(readId, version);
            // The read's users take the version's value; the read node is not needed any more.
            draft.replaceAllValueUses(readId, version);
            draft.eraseNode(readId);
        }
    }
}

void Builder::dropCarriedVariableUnits() {
    if (currGraph_ == nullptr) {
        return;
    }
    auto &draft = currGraph_->transaction().edit();
    // The users of a node, of every kind. They are read from the draft and not from a record this
    // builder keeps: a capture, a `var` argument, a region result and a chain all name a node
    // through the draft, and an edge is the only thing an erasure can take away.
    const auto usersOf = [&](camel::runtime::gc_node_ref_t id,
                             std::vector<camel::runtime::gc_node_ref_t> *out) {
        bool any = false;
        for (auto users :
             {draft.normUsersOf(id),
              draft.withUsersOf(id),
              draft.ctrlUsersOf(id),
              draft.syncUsersOf(id),
              draft.orderUsersOf(id)}) {
            any = any || !users.empty();
            if (out != nullptr) {
                out->insert(out->end(), users.begin(), users.end());
            }
        }
        return any;
    };
    const auto isRoot = [&](camel::runtime::gc_node_ref_t id) {
        return id == draft.exitNode() || id == draft.outputNode() || id == draft.returnNode();
    };
    // A node a region hands out is a result of that region: an arm's value or a body's value is
    // named there, and erasing it would leave the region naming a node the graph does not hold.
    const auto isRegionResult = [&](camel::runtime::gc_node_ref_t id) {
        for (size_t r = 0; r < draft.regionCount(); ++r) {
            if (draft.region(static_cast<uint32_t>(r)).result == id) {
                return true;
            }
        }
        return false;
    };
    // The cells whose unit may be left unread: the ones a branch or a loop carried, and the ones a
    // closure captured by value. The second is the same question with one more answer: every read
    // of such a cell took the value (the capture is the read, and the fold gave it the value the
    // write wrote), so what is left is the unit -- `:var/new`, its store and the GATEs that handed
    // the statement's value out -- and nobody uses it. Design principles 3.5 (197): the closure
    // holds the value, so the cell is not a resource of the body and there is nothing left for it
    // to be. The `let` writing of the same program has none of those nodes, which is what the pair
    // of writings is compared on.
    std::vector<node_handle_t> cells;
    for (const auto &[cell, carried] : carriedVersions_) {
        if (draft.isNamedBy(cell) && draft.isNamedBy(carried)) {
            cells.push_back(cell);
        }
    }
    // Both tables below are hashed on the handle's ADDRESS -- `std::hash<DraftNodeRef>` is
    // `hash<const void*>(draft) * 31 + id` (`runtime/node_ref.h`) -- so walking them walks the
    // allocator's order, and the units dropped further down are created in that order. Two runs of
    // one program then number the nodes of this body differently (the reading `wins1` measured on
    // 2026-11-20: "node 1/2/3 differ between runs, close to uniform"). A graph numbers its nodes in
    // creation order, so the table is sorted by a name the program gives the node: the graph it
    // belongs to, then the id that graph gave it (`walksBefore`, builder_helpers.h). The id alone
    // does not answer it -- it names a node inside one graph, and these tables hold handles of more
    // than one, so two cells with the same id would then keep the order their hashes fell in.
    std::sort(cells.begin(), cells.end(), walksBefore);
    const size_t carriedCount = cells.size();
    for (const node_handle_t cell : valueCapturedCells_) {
        if (draft.isNamedBy(cell)) {
            cells.push_back(cell);
        }
    }
    // `valueCapturedCells_` is the same kind of table. Its cells follow the carried ones -- the two
    // questions are asked in that order and the order is kept -- and each stretch is sorted inside
    // itself.
    std::sort(cells.begin() + static_cast<std::ptrdiff_t>(carriedCount), cells.end(), walksBefore);
    for (const node_handle_t cell : cells) {
        // A cell this graph no longer holds has no unit left to drop, and asking it for its kind
        // is a read of a header the erasure took away: `carriedVersions_` and the capture table
        // are tables of handles the builder keeps across bodies, and a rewrite that dissolves the
        // place a cell was written in -- the branch or the loop body it was carried out of -- takes
        // the cell's node with it and leaves the handle. So the liveness question comes first, as
        // it does for every node the stores below walk.
        const camel::runtime::gc_node_ref_t cellId = nodeIdOf(cell);
        if (draft.header(cellId) == nullptr || !draft.alive(cellId)) {
            continue;
        }
        // The cell is a cell of this graph that is not a resource any more. A captured cell, a
        // `var` parameter and one handed to a call as a `var` argument are `escapingVariables_`;
        // a read still recorded for it is the fold's business and is not folded here, so such a
        // cell keeps its unit.
        if (nodeKindOf(cell) != camel::runtime::GCNodeKind::Oper ||
            escapingVariables_.contains(cell) || captureOriginOf_.contains(cell)) {
            continue;
        }
        // Its stores are the only users it has: a read, a capture, a `var` argument or anything
        // else the builder did not record there keeps the cell (and its unit) as it is.
        const auto writes = writesOf_.find(cell);
        std::unordered_set<camel::runtime::gc_node_ref_t> stores;
        bool onlyStores = true;
        if (writes == writesOf_.end() || writes->second.empty()) {
            // A write the lowering never made: a write inside a loop body is the loop's carried
            // state (visitLoopNode), so the cell has no store. What is left of such a cell is its
            // own unit and the value the cell was made with, and both go -- the `let` writing has
            // neither. A cell anything at all still uses keeps them, as any other cell does.
            if (usersOf(cellId, nullptr) || isRoot(cellId) || isRegionResult(cellId)) {
                continue;
            }
        } else {
            for (node_handle_t write : writes->second) {
                if (!draft.isNamedBy(write)) {
                    onlyStores = false;
                    break;
                }
                stores.insert(nodeIdOf(write));
            }
            if (onlyStores) {
                std::vector<camel::runtime::gc_node_ref_t> users;
                usersOf(cellId, &users);
                for (camel::runtime::gc_node_ref_t user : users) {
                    if (!stores.contains(user)) {
                        onlyStores = false;
                    }
                }
            }
            if (!onlyStores) {
                continue;
            }
        }
        // The block's own GATEs go with the store. The arm of a `sync` block hands its value out
        // through GATEs that wait for the statement before them, and now that the branch takes the
        // value the write wrote, nothing reads those GATEs. They are collected as a set: a GATE
        // that only waits on what is already in the set, or on nothing at all, is part of the unit,
        // and a GATE may wait on the store while the store waits on it.Only a GATE or a COPY in the
        // region of a store is taken this way: an operator or a call stands for a statement the
        // program wrote, and a node a region hands out is that region's result and is not a unit's
        // part.
        std::unordered_set<camel::runtime::gc_node_ref_t> doomed = stores;
        for (bool changed = true; changed;) {
            changed = false;
            for (camel::runtime::gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
                if (!draft.alive(id) || doomed.contains(id) || isRoot(id) || isRegionResult(id)) {
                    continue;
                }
                const camel::runtime::DraftNodeHeader *header = draft.header(id);
                if (header == nullptr || (header->kind != camel::runtime::GCNodeKind::Gate &&
                                          header->kind != camel::runtime::GCNodeKind::Copy)) {
                    continue;
                }
                bool inStoreRegion = false;
                for (camel::runtime::gc_node_ref_t store : stores) {
                    inStoreRegion =
                        inStoreRegion || draft.nodeRegion(store) == draft.nodeRegion(id);
                }
                if (!inStoreRegion) {
                    continue;
                }
                std::vector<camel::runtime::gc_node_ref_t> users;
                usersOf(id, &users);
                bool onlyDoomed = true;
                for (camel::runtime::gc_node_ref_t user : users) {
                    if (doomed.contains(user)) {
                        continue;
                    }
                    const camel::runtime::DraftNodeHeader *userHeader = draft.header(user);
                    if (userHeader == nullptr || !draft.alive(user)) {
                        continue;
                    }
                    onlyDoomed = false;
                }
                if (onlyDoomed) {
                    doomed.insert(id);
                    changed = true;
                }
            }
        }
        // What is left of the unit is a set nothing outside it reads: the stores, the GATEs that
        // handed the statement's value out, and (below) the cell and the value it was made with.
        // The fold gave every reader of the cell the value the write wrote, so what a store wrote
        // is never read out of the cell; a `var` write is a value, not an effect (design principles
        // 3.5, 3.1), so the store goes with the unit. What a store may still have is a *sequencing*
        // user -- the chain of the block it stood in, the GATE that handed the statement's value
        // out, the JOIN or the LOOP that carried it -- and those are not readers: the erase hands
        // the place on and splices the orderings over the node (docs/design/effects/edges.cn.md
        // 1.5, 5.3). The permission belongs to the draft interface and not to this builder
        // (`GraphDraft::eraseUnreadNodes`): a node outside the set that still reads one of these,
        // or a node the graph names, keeps the whole unit as it was, and the call says so.
        std::vector<camel::runtime::gc_node_ref_t> going(doomed.begin(), doomed.end());
        std::sort(going.begin(), going.end());
        std::vector<node_handle_t> chained;
        chained.reserve(going.size());
        for (const camel::runtime::gc_node_ref_t node : going) {
            chained.push_back(nodeDraftOf(cell)->ref(node));
        }
        if (!draft.eraseUnreadNodes(going)) {
            continue;
        }
        // The nodes went through the interface, so this builder's record of which of them stood in
        // a chain is the only thing left to let go of.
        for (const node_handle_t &node : chained) {
            chainedNodes_.erase(node);
        }
        writesOf_.erase(cell);
        knownCellUsers_.erase(cell);
        // The cell is not a variable of this body any more: what is left of it is erased below,
        // and a registry that still names it would ask the draft about a node it no longer holds
        // (cellsOfBody walks `variables_` and asks each one's kind).
        variables_.erase(cell);
        if (const auto modifiers = nodeModifierMaps_.find(currGraph_.get());
            modifiers != nodeModifierMaps_.end()) {
            modifiers->second.erase(nodeIdOf(cell));
        }
        // What is left of the unit loses its last user one node at a time: the cell, then the value
        // it was made with, then that value's own source. Only a value of this unit is taken -- a
        // data node or a copy -- so nothing that can do something on its own is dropped here.
        for (camel::runtime::gc_node_ref_t node = cellId;;) {
            const camel::runtime::DraftNodeHeader *header = draft.header(node);
            if (header == nullptr || !draft.alive(node)) {
                break;
            }
            camel::runtime::gc_node_ref_t below = 0;
            const auto inputs                   = draft.normInputsOf(node);
            if (inputs.size() == 1) {
                below = inputs[0];
            }
            if (node != cellId && header->kind != camel::runtime::GCNodeKind::Data &&
                header->kind != camel::runtime::GCNodeKind::Copy) {
                break;
            }
            // The same permission asked of one node, and the same erase: the cell goes first, then
            // the value it was made with, then that value's own source. A node anything still
            // reads keeps itself and the value below it -- that value is shared with something the
            // program wrote, and the unit is what carries it.
            const camel::runtime::gc_node_ref_t only[1] = {node};
            if (!draft.eraseUnreadNodes(only)) {
                break;
            }
            if (below == 0) {
                break;
            }
            node = below;
        }
    }
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

/// The type of a `with` argument as far as it is known before the callee is chosen: a literal has
/// the type it is written with, a name has the type it was bound at; anything else takes its type
/// from the parameter it enters (nullptr).
Type *Builder::writtenArgumentType(const GCT::node_ptr_t &argument) {
    if (argument->type() == GCT::NodeKind::DATA) {
        // A composite literal takes its element types from the position it stands in.
        Type *literal = argument->data() != nullptr ? argument->data()->type() : nullptr;
        return literal != nullptr && !isComposite(literal->code()) ? literal : nullptr;
    }
    if (argument->type() == GCT::NodeKind::DREF) {
        if (const auto node = nodeAt(moduleLevelName(argument->name())); node.has_value()) {
            return valueTypeOf(nodeTypeOf(*node));
        }
    }
    return nullptr;
}

node_handle_t Builder::visitWithNode(const GCT::node_ptr_t &gct) {
    ENTER("WITH");
    // `f<w>(x)`: the callee of the call is the target of the binding.
    genericCalleeAllowed_ =
        std::exchange(genericCalleeAllowed_, false) &&
        (gct->at(0)->type() == GCT::NodeKind::DREF || gct->at(0)->type() == GCT::NodeKind::TAPP);
    // The callee of a binding is a name: `name<value>` binds the parameters of `name`
    // (visitDRefNode), it does not make the rule value that early.
    const bool outerRuleBindingCallee = std::exchange(ruleBindingCallee_, true);
    // The explicit type arguments of a generic callee are applied knowing which `with` arguments
    // follow them.
    const size_t outerWithWritten = typeApplicationWithWritten_;
    const auto outerWithNames     = typeApplicationWithNames_;
    const auto outerWithTypes     = typeApplicationWithTypes_;
    if (gct->at(0)->type() == GCT::NodeKind::TAPP) {
        typeApplicationWithWritten_ = gct->size() - 1;
        typeApplicationWithNames_.clear();
        typeApplicationWithTypes_.clear();
        for (size_t i = 1; i < gct->size(); ++i) {
            typeApplicationWithTypes_.push_back(writtenArgumentType(gct->at(i)));
        }
        if (const auto *written = GCT::Tree::of(gct)->argumentNamesOf(gct)) {
            typeApplicationWithNames_ = *written;
        }
    }
    any targetNodeRes           = visit(gct->at(0));
    typeApplicationWithWritten_ = outerWithWritten;
    typeApplicationWithNames_   = outerWithNames;
    typeApplicationWithTypes_   = outerWithTypes;
    ruleBindingCallee_          = outerRuleBindingCallee;
    genericCalleeAllowed_       = false;
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
    FunctionType *argumentSignature     = signatureForArgumentContext(targetNode, gct->size() - 1);
    const bool parameterTypesFollowCall = inCall && argumentSignature != nullptr;
    // A call of a rest function is expanded at the call (inlineRestCall), so a with-argument
    // that names a generic function or a set of overloads stays a compile-time callable: every
    // call of it in the body picks the instance for that call's arguments (`tree_map<twice>`,
    // `tree_map<bump>`). Making it a function value here would need the one type it does not
    // have yet, and a set of overloads has no single value at all.
    const auto restInfo = genericDrefs_.find(targetNode);
    const bool restCall = restInfo != genericDrefs_.end() && restInfo->second.one() != nullptr &&
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
    const auto refuseNonConstant = [&](size_t slot, const Argument &argument) {
        if (argumentSignature == nullptr || slot >= argumentSignature->withTypesCount() ||
            !argumentSignature->withNeedsValueAt(slot)) {
            return;
        }
        if (argument.node != nullptr && constantOfNode(argument.node) != nullptr) {
            return;
        }
        const GCT::node_ptr_t &expr = argument.expr;
        diags_->of(SemanticDiag::FactArgumentNotConstant)
            .atOrigin(expr != nullptr && expr->origin() != 0 ? expr->origin() : gct->origin())
            .commit(
                std::string(argumentSignature->argNameAt(slot)),
                argumentSignature->withTypeAt(slot)->toString());
        throw BuildAbortException();
    };
    // The angled values are not the callee: a rule bound inside them (`f->rewrite<[r<7>]>`) is a
    // use of the rule as a value, whatever the binding itself stands beside.
    struct CalleeContext {
        bool &flag;
        bool outer;
        ~CalleeContext() { flag = outer; }
    } calleeContext{ruleBindingCallee_, ruleBindingCallee_};
    ruleBindingCallee_ = false;
    for (size_t i = 1; i < gct->size(); i++) {
        const size_t slot = slotOf(i - 1);
        Type *wanted      = argumentSignature && slot < argumentSignature->withTypesCount()
                                ? argumentSignature->withTypeAt(slot)
                                : nullptr;
        ExpectedValueScope argumentScope(
            expectedValueType_,
            wanted && !containsTypeVar(wanted) ? wanted : nullptr);
        const std::optional<std::string> name =
            withNames != nullptr && i - 1 < withNames->size() ? (*withNames)[i - 1] : std::nullopt;
        if (gct->at(i)->type() == GCT::NodeKind::SPRD) {
            // `..ws` among the `with` arguments: the pack's elements are `with` arguments of their
            // own, exactly as `..e` gives the ordinary arguments of a call. `f<..ws>(..xs)` is how
            // a pack is forwarded to another function's pack, so the elements stand where they
            // stand.
            if (i + 1 != gct->size()) {
                const GCT::node_ptr_t operand = gct->at(i)->at(0);
                diags_->of(SemanticDiag::RestArgumentNotLast)
                    .atOrigin(operand->origin() != 0 ? operand->origin() : gct->origin())
                    .commit(
                        operand->type() == GCT::NodeKind::DREF ? operand->name()
                                                               : std::string("expression"));
                throw BuildAbortException();
            }
            const GCT::node_ptr_t operand = gct->at(i)->at(0);
            for (const node_handle_t &element : expandRestArgument(
                     operand,
                     operand->type() == GCT::NodeKind::DREF ? operand->name()
                                                            : std::string("expression"),
                     gct)) {
                inputs.push_back({element, operand, std::nullopt});
            }
            continue;
        }
        if (!restCall) {
            const GCT::NodeKind kind = gct->at(i)->type();
            // A literal whose type the signature still leaves open takes its type from the
            // position (`reduce<(a, b) => a + b, 0>`), and a lambda that writes no parameter
            // type needs the completed signature for it; both are built at the call, after the
            // ordinary arguments are typed. A lambda that writes every type is built here, as
            // before: nothing about it waits for the call.
            bool deferred = kind == GCT::NodeKind::DATA;
            if (!deferred && kind == GCT::NodeKind::FUNC) {
                const auto &lambda    = gct->at(i);
                const auto *unwritten = GCT::Tree::of(lambda)->unwrittenParamsOf(lambda);
                deferred              = unwritten == nullptr || !unwritten->empty();
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
            // A `with` argument stands where an ordinary argument stands (design principles 5.1):
            // a literal takes its type from the parameter, `[]` its element type, and a `T`
            // enters a `T?`. A value that does not fit is left as it is, for the call to report.
            const bool concrete = wanted != nullptr && !containsTypeVar(wanted) &&
                                  wanted->code() != TypeCode::Function;
            node_handle_t built = concrete && kind == GCT::NodeKind::DATA
                                      ? visitInPosition(gct->at(i), wanted)
                                      : visitOperand(gct->at(i), gct);
            if (concrete && kind != GCT::NodeKind::DATA && built != nullptr &&
                asTagged(wanted) != nullptr &&
                asTagged(valueTypeOf(nodeTypeOf(built))) == nullptr &&
                !valueTypeOf(nodeTypeOf(built))->equals(wanted)) {
                if (node_handle_t entered = coerceTo(built, wanted, gct->at(i))) {
                    built = entered;
                }
            }
            inputs.push_back({built, gct->at(i), name});
            refuseNonConstant(slot, inputs.back());
            continue;
        }
        any res               = visit(gct->at(i));
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
    // statement it is written in (1.6). Its span is derived where the binding is used as a
    // value (partialApplicationOf), so an ordinary `f<w>(x)` allocates nothing extra.
    boundWithOrigin_.try_emplace(targetNode, gct);
    if (!calleeContext.outer && isRuleBindingTarget(targetNode)) {
        // `name<value>` where the name is the function a rule with `with` parameters is written
        // as: the values bind the rule's parameters and the value is the `Rule` (design
        // principles 5.5). What `f<w>` makes of a function -- a function value that has the
        // parameters left -- is not what a rule's name stands for, so this is a call and not a
        // partial application.
        node_handle_t value = ruleBindingValueOf(targetNode, gct);
        LEAVE("WITH");
        return value;
    }
    LEAVE("WITH");
    return targetNode;
}

node_handle_t Builder::visitAccsNode(const GCT::node_ptr_t &gct) {
    ENTER("ACCS");
    node_handle_t tgtNode = valueNodeOf(visit(gct->at(0)));
    GUARD(tgtNode != nullptr, "Access node target is null.");
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
        // `??`, `!!`, `->` and `match` only (design principles 3.9).
        diags_->of(SemanticDiag::TaggedValueNotOpened)
            .atOrigin(gct->origin())
            .commit(tgtType->toString(), "a member cannot be read from it");
        throw BuildAbortException();
    }

    switch (tgtType->code()) {
    case TypeCode::Tuple: {
        const auto &tupleType = tt::as_ptr<TupleType>(tgtType);
        const auto *gctTree   = GCT::Tree::of(gct);
        const size_t names    = gctTree != nullptr ? gctTree->unpackCount(gct) : 0;
        // A destructuring declaration binds one name per element: `let (x, y) = t` needs a tuple of
        // two, whichever way the count is wrong (design principles 3.9). Reported here, at the
        // declaration, and not as an element read past the end -- a name too few used to drop the
        // remaining elements in silence, and a name too many reported an index the source never
        // wrote.
        // One answer per declaration: every name of it carries the count, and the first reports.
        if (names != 0 && gct->accessIndex() == 0 && names != tupleType->size()) {
            diags_->of(SemanticDiag::TupleUnpackingCountMismatch).atOrigin(gct->origin()).commit();
            throw BuildAbortException();
        }
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
        const auto *strTy = tt::as_ptr<camel::core::type::StructType>(tgtType);
        if (gct->accessByIndex()) {
            // `.N` is a tuple's element; a struct's members are read by name. Asking for the name
            // of a positional access dereferences the name it does not have -- a null pointer the
            // assert above guards in a debug build and release builds dropped, which is how `p.1`
            // on a struct segfaulted the compiler instead of being refused.
            diags_->of(SemanticDiag::InvalidAccessIndex)
                .atOrigin(gct->origin())
                .commit(std::to_string(gct->accessIndex()));
            throw BuildAbortException();
        }
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
        // A composite that is none of the three above has no element to read: a function value is
        // the case that reaches here, and it is not a corner -- `split` answers the forward half,
        // so `let (y, lin) = split(f)` destructures a *function*, and the read lands on a type
        // whose members are parameters, not elements (`(float, float) => float`). This used to be a
        // `runtime_error`, which the driver reports as a defect of the compiler; a program that
        // asks a function for its element is a program with a mistake in it, and it is told which
        // type it asked (2026-11-02, the split ABI: sixteen cases in test/cases/transforms reached
        // it).
        diags_->of(SemanticDiag::TypeNotIndexable)
            .atOrigin(gct->origin())
            .commit(tgtType->toString());
        throw BuildAbortException();
    }

    // A member read of a compile-time struct value is that field's value, known here as well: the
    // third name of `fields T` binds such a value (Builder::staticFieldTagsNode builds a field's
    // tags as a compile-time struct), and `tags.width.w` has to be a compile-time constant -- it
    // may be the key of a `t.[…]` or a type argument in turn. The read stays in the graph, because
    // the printing pass spells it and recompiling has to give the same graph back; the value it
    // reads is recorded where the compiler keeps one, as `std::opt` folds the same access. A field
    // whose value has another type than the type declares keeps its read alone.
    data_ptr_t readConstant = nullptr;
    if (!gct->accessByIndex() && tgtType->code() == TypeCode::Struct) {
        if (const data_ptr_t subject = constantOfNode(tgtNode);
            subject != nullptr && subject->type() != nullptr &&
            subject->type()->code() == TypeCode::Struct) {
            if (const auto object = std::dynamic_pointer_cast<StructData>(subject);
                object != nullptr && object->resolved()) {
                if (const data_ptr_t field = object->get(gct->accessKey());
                    field != nullptr && field->type() != nullptr && field->type() == elemType) {
                    readConstant = field;
                }
            }
        }
    }

    node_handle_t accsNode{};
    if (gct->accessByIndex()) {
        accsNode = currGraph_->addAccsNode(elemType, static_cast<uint32_t>(gct->accessIndex()));
    } else {
        accsNode = currGraph_->addAccsNode(elemType, gct->accessKey());
    }
    // `typeof` infers a type and evaluates nothing, and the graph it builds is dropped: a value
    // that lives in the graph the type is used in is borrowed by name, not by node. Linking the
    // outer node here would leave an edge in this graph pointing at a node it does not own, which
    // outlives the graph, so a port carrying that node's type stands in for it.
    node_handle_t accsSource = tgtNode;
    if (nodeGraphOf(tgtNode) != currGraph_.get()) {
        static unsigned long long typeofStandIns = 0;
        accsSource                               = currGraph_->addPortNode(
            nodeTypeOf(tgtNode),
            "$typeof" + std::to_string(++typeofStandIns),
            false);
    }
    linkNodes(LinkType::Norm, accsSource, accsNode);
    registerNodeOrigin(context_, accsNode, gct, "gir.accs");
    if (readConstant != nullptr) {
        rememberConstant(accsNode, readConstant);
    }
    LEAVE("ACCS");
    return accsNode;
}

std::string Builder::bindTemporary(node_handle_t node) {
    std::string name = "$tagged" + std::to_string(tagTemporaries_++);
    insertNode(name, node);
    return name;
}

// ---------------------------------------------------------------------------------------------
// The value environment of a `var` built as SSA
// (docs/handover/plans/2026-11-16-direct-ssa-construction.cn.md, stage S1). A `var` whose every
// read and every write lies in the region that
// declares it is built as a value: the name is bound to the value in `nodeScope_`, and an
// assignment rebinds it (`ssaAssignmentOf`). Nothing reads or writes a cell, so nothing counts
// units and nothing has to be folded afterwards.
// ---------------------------------------------------------------------------------------------

namespace {
/// Whether a node opens a region of the graph: an `if`/`match` arm, a loop's body, a closure's
/// body, a block that carries tags, a static loop. A `var` read or written across one of them is
/// carried into it -- a join's φ or a loop head's port -- which is what stages S2 and S3 add; until
/// they do, such a name keeps its cell.
bool opensRegion(GCT::NodeKind kind) {
    switch (kind) {
    case GCT::NodeKind::CASE:
    case GCT::NodeKind::EXEC:
    case GCT::NodeKind::LOOP:
    case GCT::NodeKind::FUNC:
    case GCT::NodeKind::TMTCH:
    case GCT::NodeKind::TCASE:
    case GCT::NodeKind::SFOR:
    case GCT::NodeKind::SLOOP:
    case GCT::NodeKind::SACC:
    case GCT::NodeKind::SCOMP:
        return true;
    default:
        return false;
    }
}

/// What a region does with a `var` that is read and written from outside it: an arm hands the value
/// to the branch's JOIN (stage S2), a loop body hands it to the loop head's port (stage S3), and
/// every other region -- a closure's body, a block of its own, a static loop's body, a loop's
/// condition -- has nowhere to put it, so a name used across one of those keeps its cell.
enum class SsaRegion : uint8_t { Other, Arm, LoopBody };

/// One region the walk below is inside: the depth of the body it holds, and what it can carry.
struct OpenRegion {
    size_t contentDepth = 0;
    SsaRegion kind      = SsaRegion::Other;
};

/// The scan's walk: a node, the region depth it stands at, the call it is an argument of (null when
/// it is not one of a call's arguments) and that argument's index, whether it stands in a statement,
/// whether it is a loop's body, and whether it is written -- an assignment's target -- or read.
using SsaWalk =
    std::function<void(const GCT::Node *, size_t, const GCT::Node *, size_t, bool, bool, bool)>;
} // namespace

std::unordered_map<const GCT::Node *, Builder::SsaAnswer> Builder::scanBodyForSsa(
    const GCT::Node *body) {
    // One walk of the body, before any of it is built: every `var` declaration in it, and whether
    // the declaration may be built as a value. The answer is about the declaration and nothing
    // else, so two builds of the same body decide the same way. A name belongs to the declaration
    // the innermost scope binds it to, and a use rules the declaration out when it stands in
    // another region (the value would have to be carried into it) or at a `var` parameter (the
    // callee writes the caller's cell back, and a value has nowhere to write).
    std::unordered_map<const GCT::Node *, SsaAnswer> eligible;
    std::unordered_map<const GCT::Node *, size_t> declaredIn;
    // The regions the walk is inside, outermost first: the chain between a declaration and a use is
    // what says whether the value can be carried (a branch's JOIN or a loop head), and the
    // innermost arm decides which join's one-value limit a write counts against.
    std::vector<OpenRegion> regions;
    std::map<size_t, std::set<const GCT::Node *>> branchWrites;
    std::vector<std::unordered_map<std::string, const GCT::Node *>> scopes(1);
    const auto lookup = [&](const std::string &name) -> const GCT::Node * {
        for (size_t i = scopes.size(); i-- > 0;) {
            const auto found = scopes[i].find(name);
            if (found != scopes[i].end()) {
                return found->second; // null for a `let`, a `catch` binding or a parameter
            }
        }
        return nullptr;
    };
    const auto use = [&](const GCT::Node *decl,
                         size_t depth,
                         const GCT::Node *call,
                         size_t index,
                         bool written) {
        SsaAnswer &answer = eligible[decl];
        if (declaredIn[decl] != depth) {
            // Across a region the value has to be carried into it. The regions that carry one are
            // the arm of a statement branch, whose JOIN carries it (stage S2), and a loop body,
            // whose head carries it (stage S3); every region between the declaration and the use
            // has to be one of those, one inside the other, and nothing else. A closure body, a
            // block of its own, a static loop's body and a loop's condition have nowhere to put the
            // value, so a name used across one of them keeps its cell.
            const size_t declared   = declaredIn[decl];
            bool carried            = depth > declared;
            bool carriedAsAHandle   = depth > declared;
            for (const OpenRegion &region : regions) {
                if (region.contentDepth <= declared) {
                    continue;
                }
                if (region.kind == SsaRegion::Other) {
                    carried = false;
                    // A read of a handle across such a region is not a use that has to be carried:
                    // the closure captures the value, which is what a handle is (design principles
                    // 3.5, and the author's answer `9d29939c4`). A write is: it has to land
                    // somewhere the closure and the body share, and a value is not such a place.
                    if (written) {
                        carriedAsAHandle = false;
                    }
                }
            }
            if (!carried) {
                answer.asValue = false;
            }
            if (!carriedAsAHandle) {
                answer.asHandle = false;
            }
            if (carried && !regions.empty() && regions.back().kind == SsaRegion::Arm) {
                // One join carries one value: a branch whose arms write two names carries neither,
                // settled after the walk. A loop head carries as many values as its body writes, so
                // a write inside a loop body is not counted here.
                branchWrites[depth].insert(decl);
            }
        }
        if (call != nullptr && argumentMayWriteBack(call, index, scopes)) {
            answer.asValue  = false; // written by the callee through its `var` parameter
            answer.asHandle = false;
        }
    };
    SsaWalk walk = [&](const GCT::Node *node,
            size_t depth,
            const GCT::Node *call,
            size_t index,
            bool statement,
            bool loopBody,
            bool written) {
            if (node == nullptr || node->type() == GCT::NodeKind::TYPE) {
                return;
            }
            if (node->type() == GCT::NodeKind::DREF) {
                if (const GCT::Node *decl = lookup(node->name())) {
                    use(decl, depth, call, index, written);
                }
                return;
            }
            const bool declares =
                node->type() == GCT::NodeKind::VARI || node->type() == GCT::NodeKind::NREF;
            // The declaration's own region is the one it is written in, so a `var` in an arm is at
            // the arm's depth and a use of it in an enclosing block is across a region.
            const bool region  = node != body && opensRegion(node->type());
            const size_t inner = declares ? depth : (region ? depth + 1 : depth);
            // The arms of a statement branch are a depth whose writes are carried (S2), and a loop
            // body is another (S3). A branch whose value is used fills the join's only value slot
            // with its own value, so its arms are not carried -- the red line, settled before any
            // carry is written.
            const bool statementBranch = statement && (node->type() == GCT::NodeKind::CASE ||
                                                       node->type() == GCT::NodeKind::TCASE);
            // Anything written directly in a region body is a statement of that body.
            const bool childStatement = region || node == body;
            if (region) {
                scopes.emplace_back();
                // A loop node is a region of the walk like its body is: what the head carries is
                // read in the body and in the loop's own initial values, and the head hands it out
                // after the loop. Its condition is a region of its own inside it and carries
                // nothing (S3's boundary: a name used in a loop's condition keeps its cell).
                const bool carries = loopBody || node->type() == GCT::NodeKind::LOOP;
                regions.push_back(
                    OpenRegion{
                        inner,
                        statementBranch ? SsaRegion::Arm
                                        : (carries ? SsaRegion::LoopBody : SsaRegion::Other)});
            }
            if (declares) {
                // `var x = e` is a `VARI` over the one name it declares. Any other shape (the names
                // of a destructuring declaration) is left to the declarations below, which bind
                // each of those names as a name this scan does not build as a value.
                const GCT::Node *nameNode = node;
                const bool single         = node->type() == GCT::NodeKind::NREF ||
                                            (node->size() == 1 && node->at(0) != nullptr &&
                                             node->at(0)->type() == GCT::NodeKind::NREF);
                if (!single) {
                    for (size_t i = 0; i < node->size(); ++i) {
                        walk(node->at(i), inner, nullptr, 0, childStatement, false, false);
                    }
                    if (region) {
                        scopes.pop_back();
                        regions.pop_back();
                    }
                    return;
                }
                if (node->type() == GCT::NodeKind::VARI) {
                    nameNode = node->at(0);
                }
                // The initializer is read before the name stands for anything.
                const GCT::Node *initializer = nameNode->size() > 0 ? nameNode->at(0) : nullptr;
                // A `var` whose initializer is a type (`var x: T = ...`, or a cell whose value is
                // written where it stands) is the cell itself, not a value over it.
                const bool typed =
                    initializer != nullptr && initializer->type() == GCT::NodeKind::TYPE;
                if (!typed && initializer != nullptr) {
                    walk(initializer, depth, nullptr, 0, false, false, false);
                }
                if (node->type() == GCT::NodeKind::VARI && !typed) {
                    eligible[nameNode]   = SsaAnswer{};
                    declaredIn[nameNode] = depth;
                }
                scopes.back()[nameNode->name()] =
                    node->type() == GCT::NodeKind::VARI && !typed ? nameNode : nullptr;
                if (region) {
                    scopes.pop_back();
                    regions.pop_back();
                }
                return;
            }
            const bool isCall = node->type() == GCT::NodeKind::LINK ||
                                node->type() == GCT::NodeKind::WITH ||
                                node->type() == GCT::NodeKind::TAPP;
            // A `var` an in-place operator rewrites keeps its cell (S1's scope, ahead of S3b): the
            // update takes the value's storage and hands it back by rebinding the name, which needs
            // a cell to write. Two shapes, both read off the GCT node: the plain write of a place
            // (`v[i] = e`, `v.f = e`) and the write of a call over the variable itself.
            //   ASSIGN(Plain, <path>, e)      `v[i] = e`, `v.f = e`
            //   ASSIGN(Plain, v, LINK(method, v, ...))
            //                                 `v = v->append(x)`, `v = append(v, x)`
            // The operator form `v = v + 1` (a `__`-prefixed callee) builds a new value and is not
            // one of these, so it stays a value. A shape this misses only keeps a cell, which is
            // the cheap direction; the other way round drops an in-place update, which `--copies`
            // sees.
            if (node->type() == GCT::NodeKind::ASSIGN &&
                node->assignmentForm() == GCT::AssignmentForm::Plain) {
                const GCT::Node *target = node->assignmentTarget();
                const bool plainTarget = target != nullptr && target->type() == GCT::NodeKind::DREF;
                if (!plainTarget) {
                    std::function<void(const GCT::Node *)> keepCell = [&](const GCT::Node *place) {
                        if (place == nullptr) {
                            return;
                        }
                        if (place->type() == GCT::NodeKind::DREF) {
                            if (const GCT::Node *decl = lookup(place->name())) {
                                eligible[decl].asValue  = false;
                                eligible[decl].asHandle = false;
                            }
                        }
                        for (size_t i = 0; i < place->size(); ++i) {
                            keepCell(place->at(i));
                        }
                    };
                    keepCell(target);
                }
                // A self-referential write through a method (`v = v->append(x)`) is deliberately
                // NOT excluded here, and that is measured, not an omission: making such a name a
                // cell lets the in-place form change its storage *and* the assignment hand out a
                // value, so the append happens twice -- `new_values.macro_append.{nvm,fvm,macro}`
                // printed `[0, 1, 25, 3]` where the program says `[0, 1, 25, 2]`. Keeping the cell
                // is not "putting it back where it was"; it is a change of semantics. So the name
                // stays a value and its update is copied (`--copies` says so at
                // `element_assigned_append`); making that append in-place again is S3b's work.
            }
            for (size_t i = 0; i < node->size(); ++i) {
                // A loop's body is the region its head carries values around (S3): the walk says
                // so for that child and for nothing else -- a loop's condition, initial values and
                // sequence stand where the loop is written.
                const bool childLoopBody =
                    node->type() == GCT::NodeKind::LOOP && node->loopBody() == node->at(i);
                // The target of an assignment is written, and a write is what a region that carries
                // nothing cannot hold (see `use`). A place's write (`v[i] = e`) keeps its cell by
                // the answer above; what this says is that the name itself is written, so a read
                // across a closure is the only use a handle's cell does not have to be kept for.
                const bool childWritten =
                    node->type() == GCT::NodeKind::ASSIGN &&
                    node->assignmentForm() != GCT::AssignmentForm::Rebuilt &&
                    node->assignmentTarget() == node->at(i);
                if (isCall) {
                    walk(node->at(i), inner, node, i, childStatement, childLoopBody, childWritten);
                } else {
                    walk(node->at(i), inner, nullptr, 0, childStatement, childLoopBody, childWritten);
                }
            }
            if (region) {
                scopes.pop_back();
                regions.pop_back();
            }
        };
    walk(body, 0, nullptr, 0, false, false, false);
    // A branch that writes two declarations carries neither: its JOIN has one value slot. The
    // census over the corpus found that shape only in a generator's carried state, which a capture
    // keeps a cell anyway; if a plain branch ever writes two names, S2 has to grow the tuple form.
    for (const auto &[depth, names] : branchWrites) {
        if (names.size() >= 2) {
            for (const GCT::Node *decl : names) {
                eligible[decl].asValue  = false;
                eligible[decl].asHandle = false;
            }
        }
    }
    return eligible;
}

Builder::CalleeValue Builder::calleeValueOf(const GCT::Node *callee) {
    CalleeValue value;
    if (callee == nullptr) {
        return value;
    }
    // An operator the compiler made for this node (`:op/assn_i`, `:enum/field`): the id is what
    // names it, and its overloads are a group rather than a function value. Whether such a group
    // writes an argument back is the group's own business -- for the assignment it is the rebinding
    // the builder writes (`ssaAssignmentOf`), not a `var` parameter of a call -- so it is not a
    // write-back this scan counts.
    if (GCT::Tree::of(callee)->operRefOf(callee).has_value()) {
        value.kind = CalleeValueKind::Operator;
        return value;
    }
    const std::string &written = callee->name();
    // A name the compiler wrote for a built-in operator it means itself (`core::compilerNamedOperator`:
    // the `filter` and `map` of a comprehension, the `range` an interval is). The built-in module is
    // where the build resolves it (visitDRefNode), and what it names is a group, so the answer is the
    // same as for any other operator.
    if (written.starts_with(kBuiltinPrefix)) {
        if (context_ != nullptr) {
            const auto entity =
                context_->importModule("")->getNativeEntity(Reference(written.substr(
                    kBuiltinPrefix.size())));
            if (entity.has_value() && entity->isOperGroup()) {
                value.kind = CalleeValueKind::Operator;
            }
        }
        return value;
    }
    // This module's own functions: their graphs exist from the moment the module is declared, and
    // the signature each carries is the one its declaration wrote (design principles 10.2, layers
    // 1 and 2; a name this body binds was answered before this is asked).
    if (const ModuleUnit *unit = currentModuleUnit(); unit != nullptr) {
        if (auto graphs = unit->graphScope->get(moduleLevelName(written), false);
            graphs.has_value()) {
            for (const auto &graph : **graphs) {
                if (graph != nullptr) {
                    value.signatures.push_back(graph->funcType());
                }
            }
            if (!value.signatures.empty()) {
                value.kind = CalleeValueKind::Function;
                return value;
            }
        }
    }
    // The signatures the values a name can stand for carry, in one place: the graphs of the
    // functions declared under it, and the template of a generic one (its declared type is what
    // the name stands for until a call instantiates it). A decorated name is deliberately left
    // out: what it stands for is the decorated graph, whose type is not the declaration's, and a
    // call written as a `var` argument reads the declaration's marks -- reading the decorated
    // type here would turn a cell into a value.
    const auto collectSignatures = [&value](
                                       const graph_scope_ptr_t &graphs,
                                       const template_scope_ptr_t &templates,
                                       const std::string &name) -> bool {
        value.signatures.clear();
        if (graphs != nullptr) {
            if (auto found = graphs->get(name, false); found.has_value()) {
                for (const auto &graph : **found) {
                    if (graph != nullptr) {
                        value.signatures.push_back(graph->funcType());
                    }
                }
            }
        }
        if (value.signatures.empty() && templates != nullptr) {
            if (auto found = templates->get(name);
                found.has_value() && static_cast<bool>(found.value())) {
                // Every overload of the name is a template of its own; the union of their
                // declared types is what the name stands for until a call instantiates one.
                for (const auto &tmpl : *found.value().group()) {
                    if (tmpl == nullptr) {
                        continue;
                    }
                    const FunctionType *signature = tmpl->funcType;
                    if (signature == nullptr && tmpl->func != nullptr && tmpl->func->size() > 0 &&
                        tmpl->func->at(0) != nullptr) {
                        signature = tt::as_ptr<FunctionType>(tmpl->func->at(0)->dataType());
                    }
                    if (signature != nullptr) {
                        value.signatures.push_back(signature);
                    }
                }
            }
        }
        return !value.signatures.empty();
    };
    // The entity an import layer brings, read the same way.
    const auto collectFromEntity = [&value](const camel::core::module::entity &entity) -> bool {
        if (entity.isOperGroup()) {
            value.kind = CalleeValueKind::Operator;
            return true;
        }
        if (camel::core::module::detail::EntityAccess::isGraphSet(entity)) {
            for (const auto &graph : *asCompileGraphSet(
                     camel::core::module::detail::EntityAccess::graphSet(entity))) {
                if (graph != nullptr) {
                    value.signatures.push_back(graph->funcType());
                }
            }
            if (!value.signatures.empty()) {
                value.kind = CalleeValueKind::Function;
                return true;
            }
        }
        if (camel::core::module::detail::EntityAccess::isGenericTemplate(entity)) {
            for (const auto &tmpl : genericTemplatesOfEntity(entity)) {
                if (tmpl == nullptr) {
                    continue;
                }
                const FunctionType *signature = tmpl->funcType;
                if (signature == nullptr && tmpl->func != nullptr && tmpl->func->size() > 0 &&
                    tmpl->func->at(0) != nullptr) {
                    signature = tt::as_ptr<FunctionType>(tmpl->func->at(0)->dataType());
                }
                if (signature != nullptr) {
                    value.signatures.push_back(signature);
                }
            }
            if (!value.signatures.empty()) {
                value.kind = CalleeValueKind::Function;
                return true;
            }
        }
        return false; // a decorated one or an exported value: the type is not read here
    };
    const ModuleUnit *unit = currentModuleUnit();
    // This module's own functions, by the name the module declares them under (design principles
    // 10.2, layers 1 and 2; a name this body binds was answered before this is asked).
    if (unit != nullptr &&
        collectSignatures(
            unit->graphScope, unit->templateScope,
            moduleLevelName(written))) {
        value.kind = CalleeValueKind::Function;
        return value;
    }
    // A function declared in a body of this one, in the scopes between here and the module's top
    // level: those are functions the program made where it stands, and their declarations carry
    // the same types.
    if (unit != nullptr) {
        for (graph_scope_ptr_t scope = graphScope_; scope != nullptr && scope != unit->graphScope;
             scope                   = scope->outer()) {
            if (auto found = scope->get(written, false); found.has_value()) {
                for (const auto &graph : **found) {
                    if (graph != nullptr) {
                        value.signatures.push_back(graph->funcType());
                    }
                }
                if (!value.signatures.empty()) {
                    value.kind = CalleeValueKind::Function;
                    return value;
                }
            }
        }
    }
    // Layers 3 to 5, in the order visitDRefNode reads them: a name imported by name, then a name
    // a `using namespace` in force brings, then a spelling an import brings (operators take their
    // overloads from every layer), then the ambient ones.
    std::optional<camel::core::module::entity> entity;
    if (module_->hasImportedRef(written, ImportLayer::Named)) {
        entity = importedEntity(written, ImportLayer::Named);
    }
    if (!entity.has_value()) {
        if (auto match = throughUsing(written, const_cast<GCT::Node *>(callee));
            match.has_value()) {
            if (!match->ownName.empty()) {
                // A name of this module, opened by a `using namespace` of one of its namespaces.
                if (unit != nullptr &&
                    collectSignatures(
                        unit->graphScope, unit->templateScope,
                        match->ownName)) {
                    value.kind = CalleeValueKind::Function;
                    return value;
                }
            } else if (match->entity.has_value()) {
                if (collectFromEntity(*match->entity)) {
                    return value;
                }
            }
        }
    }
    if (!entity.has_value()) {
        if (isOperatorName(written) || module_->hasImportedRef(written)) {
            entity = importedEntity(written);
        } else if (module_->hasImportedRef(written, ImportLayer::Ambient)) {
            entity = importedEntity(written, ImportLayer::Ambient);
        }
    }
    if (!entity.has_value()) {
        return value;
    }
    collectFromEntity(*entity);
    return value;
}

bool Builder::argumentMayWriteBack(
    const GCT::Node *call, size_t index,
    const std::vector<std::unordered_map<std::string, const GCT::Node *>> &scopes) {
    // A `var` parameter is two-way: the callee reads the argument and writes into it, so the
    // argument cannot be a value. Which position that is, the callee's function type says -- the
    // `var` marks its parameters carry -- so the answer is read off the value the callee name
    // stands for and never off the spelling of the name.
    if (call == nullptr || index == 0) {
        return false; // the callee itself is named, not handed
    }
    if (call->type() != GCT::NodeKind::LINK && call->type() != GCT::NodeKind::WITH) {
        return false;
    }
    const bool byWith       = call->type() == GCT::NodeKind::WITH;
    const GCT::Node *callee = call->at(0);
    if (callee == nullptr) {
        return true; // nothing to name the callee by: answered as one that may write
    }
    if (callee->type() == GCT::NodeKind::TAPP && callee->size() > 0) {
        callee = callee->at(0); // `f::<T>(x)`: the function is still the first child
    }
    if (callee->type() == GCT::NodeKind::WITH && callee->size() > 0) {
        callee = callee->at(0); // `f<w>(x)`: the callee is under the `with` bindings
    }
    if (callee == nullptr || callee->type() != GCT::NodeKind::DREF) {
        return true; // a lambda written where the callee stands: its parameters are not read here
    }
    for (size_t i = scopes.size(); i-- > 0;) {
        if (scopes[i].contains(callee->name())) {
            return true; // a name this body binds: a function value whose parameters are unknown
        }
    }
    const CalleeValue value = calleeValueOf(callee);
    if (value.kind == CalleeValueKind::Unknown) {
        // Nothing of this program is named so in a way this scan can see, and this scan cannot say
        // what the callee reads and writes: answered as one that may write, because a write-back
        // lost to a value is a wrong program while a cell costs one unit.
        return true;
    }
    if (value.kind == CalleeValueKind::Operator) {
        return false;
    }
    const size_t at = index - 1;
    for (const FunctionType *signature : value.signatures) {
        if (signature == nullptr) {
            continue;
        }
        const size_t count = byWith ? signature->withTypesCount() : signature->normTypesCount();
        if (at < count && (byWith ? signature->withIsVarAt(at) : signature->normIsVarAt(at))) {
            return true;
        }
    }
    return false;
}

bool Builder::ssaDeclarationIsEligible(
    const GCT::node_ptr_t &decl, const std::string &name, bool aHandle) {
    // A module's top level is not a region of a body: its `var`s are cells every function of the
    // module reaches, and they stay cells until stage S5. A declaration the scan never saw is not
    // one of the body's own, and is answered as a cell.
    if (regionBodies_.empty() || currGraph_ == rootGraph_ || decl == nullptr) {
        return false;
    }
    const GCT::Node *body = regionBodies_.back();
    auto scanned          = ssaScan_.find(body);
    if (scanned == ssaScan_.end()) {
        scanned = ssaScan_.emplace(body, scanBodyForSsa(body)).first;
    }
    const auto found = scanned->second.find(decl);
    if (found == scanned->second.end()) {
        return false;
    }
    return aHandle ? found->second.asHandle : found->second.asValue;
}

bool Builder::nameIsSsa(const std::string &name) const {
    return ssa_.find(nodeScope_, name) != nullptr;
}

} // namespace camel::compile::gir
