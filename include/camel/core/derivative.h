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
 * Author: Camel Contributors
 * Created: Sep. 29, 2026
 * Updated: Sep. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
 * Composite types (structs, tuples, arrays) have no entry; engines derive
 * their tangent spaces from their element types. The builtin scalar
 * operators and types register here from the builtin module; tensor, nn and
 * math register their own when loaded.
 */

#pragma once

#include "camel/core/rtdata/base.h"
#include "camel/core/type/base.h"
#include "camel/runtime/graph.h"

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
    /// Gradient accumulated so far for a node, if any.
    virtual std::optional<vjp_node_t> gradientOf(vjp_node_t primal) const = 0;
    /// Adds `gradient` to the gradient of `primal`. Ignored for values without a tangent space.
    virtual void accumulateGradient(vjp_node_t primal, vjp_node_t gradient) = 0;

    vjp_node_t addStaticFloat(double value);
};

/// One operator call being differentiated.
struct VjpCall {
    std::string_view uri;
    std::span<const vjp_node_t> inputs;
    vjp_node_t output;
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
    /// Operator adding two tangents: (a, b) => a + b.
    std::string addUri;
    /// The zero tangent as a constant, for types that have one (scalars).
    std::optional<slot_t> zero;
    /// Otherwise, an operator producing the zero tangent of a primal value: (x) => 0.
    std::string zerosLikeUri;
};

class DerivativeRegistry {
  public:
    static DerivativeRegistry &instance();

    /// Registers (or replaces) the reverse-mode rule of `uri`.
    void setRule(std::string_view uri, VjpRule rule);
    /// The rule of `uri`, or nullptr.
    VjpRule findRule(std::string_view uri) const;

    /// Registers (or replaces) the tangent space of the values of type code `code`.
    void setTangentSpace(type::TypeCode code, TangentSpace space);
    /// The tangent space of `type`'s code, or nullptr when values of that type have no tangent.
    const TangentSpace *findTangentSpace(const type::Type *type) const;

  private:
    struct StringHash {
        using is_transparent = void;
        size_t operator()(std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, VjpRule, StringHash, std::equal_to<>> rules_;
    std::unordered_map<type::TypeCode, TangentSpace> tangents_;
};

} // namespace camel::core
