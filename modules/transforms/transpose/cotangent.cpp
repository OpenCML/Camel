// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The cotangent side of a transposition -- `transpose/cotangent.h` declares it. Every answer here
 * is the type system's or the derivative registry's: no width, no operator name and no shape is
 * written down in this file.
 */

#include "transpose/cotangent.h"

#include "transpose.h"

#include "camel/core/derivative.h"
#include "camel/core/tangent_space.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/tree.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/graph.h"
#include "camel/utils/type.h"

#include <algorithm>
#include <span>
#include <string>
#include <vector>

namespace camel::transforms {

using camel::core::type::FunctionType;
using camel::core::type::Type;
using camel::runtime::gc_node_ref_t;

namespace rt = camel::runtime;

/// The addition a cotangent space sums with, asked of the derivative registry -- the one place a
/// space is declared (`TangentSpace::addUri`, `camel/core/derivative.h`, design principles 9.9).
///
/// `:op/add_d` at `float` and `:op/add_f` at `f32` are that registry's answers, and so are
/// `:tangent/add_arr` for an array whose elements the core holds one slot each and `tensor:add` for
/// a tensor: the two kinds share the array *code* and sum differently, so the answer is read from
/// the space and never spelled here from a type code. A narrow float is a float but it is **not** a
/// `float` and not the same slot -- `f32` is four bytes and `float` is eight, so a cotangent
/// written as one and read as the other is a reinterpretation of bits, not a conversion.
///
/// An empty answer is "this space registers no addition": a cotangent that cannot be summed is not
/// a cotangent, and the caller refuses by name. It is **not** a `float`.
std::string addUriOfSpace(Type *space) {
    if (space == nullptr) {
        return {};
    }
    const camel::core::TangentSpace *registered =
        camel::core::DerivativeRegistry::instance().findTangentSpace(space);
    return registered != nullptr ? registered->addUri : std::string{};
}

/// The sum a cotangent accumulates with, and the type it is written at: the tangent space of the
/// value the cotangent lands in (`tangentSpaceOf`) and that space's own addition. Handing an array
/// to `add_d` is the same lie as calling it a float -- it reads the array as a number and takes the
/// process down, which is what two element reads of one array did (a sum of two reads was enough;
/// one read was not). Every accumulation in this file asks this question; a site that spells
/// `:op/add_d` itself decided the answer without asking.
///
/// `addType` is that space and never a placeholder: a `float` written here first is a `float` read
/// if the answer is ever consulted before it is filled. An empty `addUri` is a value whose type has
/// no cotangent space -- and every caller has already asked `hasTangentSpace` of the value before a
/// cotangent of it exists -- so an empty answer is a defect of this walk and refused as one.
std::string addUriFor(const camel::runtime::GCGraph *function, gc_node_ref_t ref, Type *&addType) {
    const camel::runtime::GCNode *node = function->node(ref);
    const Type *type                   = node != nullptr ? node->dataType() : nullptr;
    const camel::core::TangentSpaceAnswer space =
        type == nullptr ? camel::core::TangentSpaceAnswer{} : camel::core::tangentSpaceOf(type);
    addType = space.hasSpace() ? space.space : nullptr;
    return addUriOfSpace(addType);
}

/// Why this transform has no cotangent for a result of type `result`, in the words the call site's
/// diagnosis uses; empty when it has one.
///
/// **The law and its refusal are one function on purpose.** A `bool` gate here and a paragraph of
/// prose in `transpose_operator.cpp` spelling the gate's rule a second time would let "the result has
/// no cotangent" be said about a pair that has one. A judgement and its message drift apart
/// exactly when they are written twice.
///
/// The law (design principles 9.9): **every result type
/// whose tangent space this file can add is accepted** -- a tuple or a struct when every member is,
/// an array when its space registers an array addition (`:tangent/add_arr`, `tensor:add`), a float
/// of any width when its space registers that width's addition (`:op/add_d`, `:op/add_f`). The
/// cotangent the caller passes is that space, not a `float` the space would have to be read into.
///
/// A refusal names **the position that does not hold a cotangent**, because "this result type is
/// not supported" is true of every failure and therefore says nothing: which member, and of which
/// type.
std::string resultCotangentRefusalImpl(const Type *result) {
    if (result == nullptr) {
        return "the result type is not known here";
    }
    const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(result);
    if (!space.hasSpace() || space.space == nullptr) {
        return "`" + result->toString() +
               "` has no tangent space, and a value with no tangent space holds no cotangent";
    }
    const camel::core::type::TypeCode code = result->code();
    if (code == camel::core::type::TypeCode::Tuple || code == camel::core::type::TypeCode::Struct) {
        // Member by member, because that is how the addition of an aggregate is defined
        // (`accumulate`), and the member that fails is named with its index.
        const auto count = camel::core::type::tree::arity(result);
        if (!count) {
            return "`" + result->toString() +
                   "` is not a type with members, and the cotangent of an " +
                   "aggregate is defined member by member";
        }
        for (size_t i = 0; i < *count; ++i) {
            const Type *const memberType = camel::core::type::tree::childType(result, i);
            const std::string inner      = resultCotangentRefusalImpl(memberType);
            if (!inner.empty()) {
                return "member " + std::to_string(i) + " of the result (`" +
                       (memberType != nullptr ? memberType->toString() : std::string("?")) +
                       "`): " + inner;
            }
        }
        return {};
    }
    if (addUriOfSpace(space.space).empty()) {
        return "the cotangent space of `" + result->toString() + "` is `" +
               space.space->toString() +
               "`, which registers no addition (`TangentSpace::addUri` is empty), and a cotangent "
               "that cannot be summed is not a cotangent this law can answer";
    }
    return {};
}

/// Whether this transform has a cotangent for a result of type `result`. The answer is the law's
/// (`resultCotangentRefusal`), not a second spelling of it.
bool hasResultCotangent(const Type *result) { return resultCotangentRefusalImpl(result).empty(); }

/// The type a transposed function's cotangent input lives in: the cotangent of the result, which is
/// the result's tangent space. A `float` result answers `float` and an `f32` result answers `f32`
/// (`Tangent<f32>` is `f32`), which is the query and not a width written down here -- so widening
/// what `transpose` accepts cannot leave a `float` where an `f32` is promised (design principles
/// 9.9, and the drift `jvp`'s four sites were fixed for).
///
/// `nullptr` for a result with no tangent space. The gate above (`hasResultCotangent`) refuses such
/// a function before a signature is built from it, so a caller that meets a nullptr here is inside
/// a build the gate already admitted: it refuses rather than answer with a `float`.
///
/// GUARD, so that the `nullptr` is not read as a reading nobody took: the gate asks
/// `tangentSpaceOf` of **the same type** (`function->exitType()`) and refuses the whole function
/// when there is no space, and every caller here is inside a member built after that answer. A
/// `nullptr` therefore says "a member is being built for a function the gate never admitted", which
/// is this walk's own defect and not a state a program can be in -- so it refuses by name instead
/// of answering a `float`. Anything that builds a signature without going through the gate (a
/// caller that writes `cotangentTypeOfResultType` into a new path) meets the gate's question only
/// if it asks it: check that it does before assuming this cannot answer `nullptr`.
Type *cotangentTypeOfResultType(const Type *result) {
    if (result == nullptr) {
        return nullptr;
    }
    const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(result);
    return space.hasSpace() ? space.space : nullptr;
}

Type *resultCotangentType(const FunctionType *function) {
    return cotangentTypeOfResultType(function != nullptr ? function->exitType() : nullptr);
}

Type *resultCotangentType(const camel::runtime::GCGraph *function) {
    return cotangentTypeOfResultType(
        function != nullptr && function->funcType() != nullptr ? function->funcType()->exitType()
                                                               : nullptr);
}

/// The law's refusal, asked from outside this file: the operator's call site
/// (`transpose_operator.cpp`) reports the same sentence the gate above decided on. The gate answers
/// here and the sentence is a plain definition beside it, because a name no header declares is a
/// symbol the other translation unit cannot call -- the module then links with an undefined symbol
/// and fails to load, which is the whole stdlib refusing to import rather than one wrong answer.
std::string resultCotangentRefusal(const Type *result) {
    return resultCotangentRefusalImpl(result);
}

/// Whether `type` is `()`, the empty tuple. The distinction this draws is the one the whole answer
/// shape turns on, and it is a difference of *types*, not of tangent spaces: an `int` parameter and
/// a
/// `()` parameter both have no tangent space, but an `int` is a value the caller still passes and
/// answers nothing, while `()` is a place with nothing to pass and an empty member of the answer
/// (`Tangent<int>` is `()`: `()` is what the half **answers**, not what it takes). Reading them as one thing either puts a port where nobody can fill it or drops a member
/// the declaration promised.
bool isUnitType(const Type *type) {
    if (type == nullptr || type->code() != camel::core::type::TypeCode::Tuple) {
        return false;
    }
    const auto *tuple = tt::as_ptr<camel::core::type::TupleType>(const_cast<Type *>(type));
    return tuple != nullptr && tuple->size() == 0;
}

/// **Whether the function a lifted signature was lifted from eats only tangents.** The lift law
/// writes `Lift<G>`'s list as `G`'s parameters and then each one's tangent, putting `()` where a
/// parameter of `G` has no tangent space (`lift.h#liftedParametersOf`). A `()` in the second half is
/// therefore the law's own record of a parameter of `G` that is a **value** `G` reads, and a list
/// with none of them is the record of a `G` all of whose parameters are linear inputs. That is the
/// question this reading is about: whether the *source function* eats only tangents, and not whether
/// the graph is a `Lift` -- the latter is true of every graph a forward transform wrote, and says
/// nothing about which reading its places take.
bool liftSourceEatsOnlyTangents(const LiftedParameters &places) {
    if (!places.isLift) {
        return false;
    }
    return std::find(places.places.begin(), places.places.end(), LiftedPlace::Unit) ==
           places.places.end();
}

/// **The reading this file takes of a graph's parameter list.** `lift.h#liftedParametersOf` answers
/// what the law says; which of its two readings a *transposed* signature takes is this file's, and
/// the law says as much (`lift.h`, "which of the two readings applies is not decided here").
///
/// A lifted signature whose source function eats only tangents is read the ordinary way, and that is
/// not a second law: `Lift<G>` for such a `G` writes every parameter of `G` after `G`'s own, and
/// `G` is linear, so `Lift<G>` is linear in **every** place of that list -- in the value half and in
/// the tangent half alike. `(x, dx) => (G x, G dx)` transposed is `(a, b) => (G* a, G* b)`: one port,
/// the cotangent of the pair, and one cotangent per place, which is exactly what `Transpose<F>` --
/// written from `F`'s type and nothing else -- promises. Read as a lift instead, the value half would
/// become a port the caller cannot fill (no type states that the signature is lifted) and the value
/// half's answer would be dropped, and the two counts would disagree with the promise by one.
///
/// A lifted signature whose source function does read a value keeps the lift reading. There the
/// value half is genuinely needed -- `G'(x)` depends on `x` -- and nothing in the type can carry
/// that port, so the count disagreement is a real refusal and the two numbers are reported.
LiftedParameters transposedPlacesOf(
    const camel::core::type::FunctionType *type, bool liftedSignature) {
    if (!liftedSignature) {
        return liftedParametersOf(type, false);
    }
    const LiftedParameters law = liftedParametersOf(type, true);
    if (liftSourceEatsOnlyTangents(law)) {
        return liftedParametersOf(type, false);
    }
    return law;
}

/// The zero a cotangent space starts from, asked of that space: the constant the registry holds
/// (`TangentSpace::zero`, written in the space's own width -- an `f32` zero is four bytes and a
/// `float` zero is eight, and one read as the other is a reinterpretation of bits), or the space's
/// own `zerosLikeUri` for a space whose zero is built from a value's shape (`:tangent/zeros_arr`
/// for an array of values, `tensor:zeros_like` for a tensor). Both are the registry's answers, so
/// no width and no operator name is written down here (design principles 9.9).
///
/// `shapeFrom` is the value the shape is read from, and it has to be a node of **this** graph: a
/// ref of the source graph written into a node of the new one reads whatever node sits at that
/// index, which would build a zero array from a number (`autodiff: a zero tangent array
/// needs an array with a tangent`). `kInvalidNodeRef` builds no shape at all: the type's stated
/// extents (`shapeCarrierOf`) are not a value to read, so the refusal below is the answer when the
/// caller has no value to read the shape from.
///
/// An invalid ref for a space the registry does not know, or one with neither a zero constant nor a
/// `zerosLikeUri`: a cotangent that has no zero cannot be accumulated, and the caller refuses by
/// name instead of handing the slot a `float` zero.
gc_node_ref_t
zeroOfSpaceOf(camel::runtime::GraphDraft &draft, Type *space, gc_node_ref_t shapeFrom) {
    if (space == nullptr) {
        return rt::kInvalidNodeRef;
    }
    const camel::core::TangentSpace *registered =
        camel::core::DerivativeRegistry::instance().findTangentSpace(space);
    if (registered == nullptr) {
        return rt::kInvalidNodeRef;
    }
    if (registered->zero.has_value()) {
        return draft.materializeStaticValue(*registered->zero, space, rt::kGCNodeFlagConstant);
    }
    if (registered->zerosLikeUri.empty()) {
        return rt::kInvalidNodeRef;
    }
    if (shapeFrom == rt::kInvalidNodeRef) {
        // No value of this walk's to read the shape from, and no zero built in its place: a zero
        // array answered a law's `shape(...)` read with a value that was not the argument, and the
        // answer to "no shape" is the refusal, not an invented array.
        return rt::kInvalidNodeRef;
    }
    const gc_node_ref_t zeros = draft.addOperNode(space, nullptr, registered->zerosLikeUri.c_str());
    draft.setNormInputs(zeros, std::span<const gc_node_ref_t>(&shapeFrom, 1));
    return zeros;
}

} // namespace camel::transforms
