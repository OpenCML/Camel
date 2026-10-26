// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Derivative metadata of operators and types.
 *
 * Differentiation is not part of the core: an autodiff engine is an ordinary
 * package operator that rewrites graphs (see modules/autodiff). What the core
 * provides is the neutral place where the owners of operators and types
 * publish how they differentiate, so that any engine can use them and no
 * engine has to know the modules:
 *
 *   - a reverse-mode rule (vector-Jacobian product) per operator URI, written
 *     against the abstract VjpBuilder the engine implements;
 *   - a tangent space per type code: the tangent type of a value, how two
 *     tangents add, and what the zero tangent is.
 *
 * Composite types (structs, tuples, arrays) have no entry: the two questions
 * below derive their answers from their element types. Which of the two a
 * caller asks is decided by *who the answer is for*, not by the aggregate's
 * shape:
 *
 *   - `gradientTypeOf` answers for the user: the gradient type of the
 *     parameter `P` a `grad`/`with_grad` call differentiates, which is what the
 *     call hands back and what `grad of P` names. A number and a boolean are
 *     their own gradient (the value zero), a `string` and a function have none,
 *     and a struct or a tuple has one exactly when every member has, the
 *     member's gradient standing in its place -- a discrete member (`i64[..]`,
 *     an integer scalar) is kept at its own type and its gradient is the zero
 *     of that shape and element type, so the parameter tree and the gradient
 *     tree pair structurally. A struct and a tuple answer by the same rule.
 *   - `machineryTangentTypeOf` answers for the machinery: the tangent of a
 *     value the transform itself carries (a loop's state, a captured tuple, an
 *     aggregate on the reverse tape). The user never sees those, and only the
 *     members that need a gradient are kept, a discrete member and a function
 *     member being dropped. That is an implementation choice, not a language
 *     rule; see `keepsInTangent` in src/core/derivative.cpp.
 *
 * The builtin scalar
 * operators and types register here from the builtin module; tensor, nn and
 * math register their own when loaded.
 */

#pragma once

#include "camel/core/node_attrs.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/type/base.h"
#include "camel/runtime/graph.h"

#include <deque>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace camel::core {

using vjp_node_t = camel::runtime::gc_node_ref_t;

/// The graph a rule emits its backward computation into. Node handles name values of that graph:
/// primal values (a call's inputs and output) as well as nodes the rule added.
class VjpBuilder {
  public:
    virtual ~VjpBuilder() = default;

    /// Static type of a node.
    virtual type::Type *nodeType(vjp_node_t node) const = 0;
    /// The value of a node that is a compile-time constant.
    virtual std::optional<slot_t> staticValueOf(vjp_node_t node) const = 0;
    /// A constant node.
    virtual vjp_node_t addStatic(slot_t value, type::Type *type) = 0;
    /// An operator node with the given norm inputs. The operator is resolved by URI when the
    /// graph runs.
    virtual vjp_node_t
    addOper(type::Type *type, std::string_view uri, std::span<const vjp_node_t> normInputs) = 0;
    /// The same, carrying `attrs` (for example the forward node's `tensor.kernel` choice) to the
    /// emitted node. The default keeps the attributes away, so a rule that has none behaves as
    /// before; an engine that stores attributes overrides it.
    virtual vjp_node_t addOper(
        type::Type *type,
        std::string_view uri,
        std::span<const vjp_node_t> normInputs,
        camel::core::attr::AttrSetId attrs) {
        (void)attrs;
        return addOper(type, uri, normInputs);
    }
    /// Marks `members` as one fusion group named `name` under the library's attribute `key`
    /// (design principles 9.6: the group is an attribute of the nodes, the operators stay the
    /// program's). A rule calls it after it emitted a chain of operators that one kernel can run
    /// in a single pass, so the graph records the chain where it was built. The default does
    /// nothing: an engine without a place for groups still differentiates, and the nodes run one
    /// by one.
    virtual void markFusionGroup(
        camel::core::attr::AttrKey key,
        std::string_view name,
        std::span<const vjp_node_t> members) {
        (void)key;
        (void)name;
        (void)members;
    }
    /// Element `index` of an aggregate (tuple or struct) node. Rules for operators returning
    /// several values use it to read the gradient of one result and to hand one gradient of a
    /// multi-result gradient operator to each input.
    virtual vjp_node_t addAccess(vjp_node_t aggregate, size_t index);
    /// Gradient accumulated so far for a node, if any.
    virtual std::optional<vjp_node_t> gradientOf(vjp_node_t primal) const = 0;
    /// Adds `gradient` to the gradient of `primal`. Ignored for values without a tangent space.
    virtual void accumulateGradient(vjp_node_t primal, vjp_node_t gradient) = 0;
    /// True when a differentiated input flows into `node`, so a gradient reaching it must be
    /// carried back. False for a compile-time constant and for anything computed from data
    /// alone. A rule uses it to tell an input whose contribution has to be returned from one
    /// that is a constant: the two have the same type and the same shape.
    virtual bool isActive(vjp_node_t node) const = 0;

    vjp_node_t addStaticFloat(double value);
};

/// One operator call being differentiated.
struct VjpCall {
    std::string_view uri;
    /// The call's normal inputs, in order.
    std::span<const vjp_node_t> inputs;
    /// The call's `with` inputs, in order. Most operators read these as configuration and no
    /// gradient flows through them, but an operator may take a value this way (`append`).
    std::span<const vjp_node_t> withInputs;
    vjp_node_t output;
    /// Source origin of the operator node, so that a rule which cannot carry this call's
    /// gradient can refuse at the operator instead of at the `grad` call that ran the
    /// transformation (`kInvalidOriginId` when unknown). See `tensor::ops::refuseVjp`.
    uint64_t origin = 0;
    /// The forward node's attributes (camel/core/node_attrs.h), so a rule can carry an
    /// implementation choice (tensor.kernel) to the gradient operators it emits. Zero means the
    /// node had none.
    camel::core::attr::AttrSetId attrs = camel::core::attr::kNoAttrs;
};

/// Emits the backward computation of `call`: reads the gradient of its output and accumulates
/// gradients into its inputs.
using VjpRule = void (*)(VjpBuilder &builder, const VjpCall &call);

/// The rule of operators whose result is locally constant (comparisons, shape queries,
/// constructors, stop_gradient): no gradient flows through them.
void noGradient(VjpBuilder &builder, const VjpCall &call);

struct TangentSpace {
    /// Tangent type of a value of the primal type, or nullptr when that value has no tangent
    /// (e.g. an integer tensor). Null means the tangent type is the primal type itself.
    type::Type *(*tangentType)(type::Type *primal) = nullptr;
    /// The values of the code this space answers for, or null when it answers for all of them.
    /// Two kinds share the array code: a tensor is a numeric array, and an array of structs,
    /// tuples or arrays is an array of values the core holds one slot each. Their tangents are
    /// different things -- a tensor's is the tensor itself, summed and zeroed by `tensor:add`
    /// and `tensor:zeros_like`, an array of values' is the array of its elements' tangents,
    /// summed and zeroed by `:tangent/add_arr` and `:tangent/zeros_arr` -- so a module that
    /// registers for a shared code declares which of its values it owns, and the space it
    /// displaces stays in place for the rest (design principles 9.9, `ArrayType`).
    bool (*owns)(const type::Type *type) = nullptr;
    /// Operator adding two tangents: (a, b) => a + b.
    std::string addUri;
    /// The zero tangent as a constant, for types that have one (scalars).
    std::optional<slot_t> zero;
    /// Otherwise, an operator producing the zero tangent of a primal value: (x) => 0.
    std::string zerosLikeUri;
    /// Operator taking one element's tangent back into a tangent of the whole value:
    /// (acc, index, element) => `acc` with `element` added where `index` puts it. This is the
    /// reverse of taking a value apart along its outermost dimension, which is what a round of a
    /// loop over an array is, and it has to agree with the kind's own reading of that dimension:
    /// an array whose elements the core holds one slot each scatters a slot, a tensor scatters a
    /// row of its own dtype (design principles 9.9).
    std::string scatterUri;
};

class DerivativeRegistry {
  public:
    static DerivativeRegistry &instance();

    /// Registers (or replaces) the reverse-mode rule of `uri`.
    void setRule(std::string_view uri, VjpRule rule);
    /// The rule of `uri`, or nullptr.
    VjpRule findRule(std::string_view uri) const;

    /// Registers a tangent space for the values of type code `code`. A space that owns all of
    /// them (no `owns` predicate) replaces what is registered for the code; a space that owns
    /// some of them is added in front of it, and answers only for the values it owns. A code has
    /// one space that owns all of it and any number that own a subset, in registration order.
    void setTangentSpace(type::TypeCode code, TangentSpace space);
    /// The tangent space of `type`: the most recently registered space of its code that owns it,
    /// or nullptr when values of that type have no tangent.
    const TangentSpace *findTangentSpace(const type::Type *type) const;

    /// Registers a predicate for the values of type code `code` that says they are discrete (an
    /// integer or a boolean held by a module type, such as a tensor of integers): their
    /// derivative is zero by definition (design principles 9.9), even though they have no
    /// tangent space of their own. The builtin scalar codes are recognised without one.
    void setDiscretePredicate(type::TypeCode code, bool (*predicate)(const type::Type *));
    /// True for values whose derivative is zero by definition: integers and booleans, scalar or
    /// held by a module type that registered a predicate. A module type without one (a Map, an
    /// enum) is not discrete, and a value of it computed from a parameter must be refused
    /// rather than silently differentiated to zero.
    bool isDiscrete(const type::Type *type) const;

  private:
    struct StringHash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, VjpRule, StringHash, std::equal_to<>> rules_;
    std::unordered_map<type::TypeCode, std::deque<TangentSpace>> tangents_;
    std::unordered_map<type::TypeCode, bool (*)(const type::Type *)> discrete_;
};

/// The gradient type of the parameter `P` a user differentiates: what `grad` and `with_grad` hand
/// back, and what `grad of P` names. A number and a boolean are their own gradient, with the value
/// zero; a `string` and a function have none; and a struct or a tuple has one exactly when every
/// member has, the member's gradient standing in its place. The two aggregate shapes answer by the
/// same rule, so a tuple model `with <m: (f32[..], int)>` gets `(f32[..], int)` back with the
/// `int` at zero. Null when the parameter has no gradient at all.
type::Type *gradientTypeOf(type::Type *parameter);

/// Tangent type of a value the *machinery* carries -- a loop's state, a captured tuple, an
/// aggregate on the reverse tape -- or null when the value carries no gradient. Only the members
/// that need a gradient are kept; a discrete member (whose gradient is zero whatever the program
/// does) and a function member are dropped. The user never sees these aggregates: this is the
/// transform's own choice, and `grad`/`with_grad` answer with `gradientTypeOf` instead.
type::Type *machineryTangentTypeOf(type::Type *primal);

/// True for structs and tuples, whose gradients and tangents are handled element by element.
bool isAggregate(const type::Type *t);
size_t aggregateSize(type::Type *aggregate);
type::Type *aggregateElement(type::Type *aggregate, size_t index);
/// Position of element `index` of an aggregate in the gradient `gradientTypeOf` names: the
/// element's own place, or nullopt when the aggregate has no gradient. Every element of an
/// aggregate that has a gradient has one, so this is the element's index (or nothing at all).
std::optional<size_t> gradientElementIndex(type::Type *aggregate, size_t index);
/// Position of element `index` of an aggregate in the tangent the machinery carries
/// (`machineryTangentTypeOf`), or nullopt when that tangent does not keep the element (a discrete
/// member, a function member, a member with no tangent).
std::optional<size_t> machineryTangentElementIndex(type::Type *aggregate, size_t index);

} // namespace camel::core
