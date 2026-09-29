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
 * Tangent types.
 *
 * The tangent of a value is what its gradient is. Leaf types publish their
 * tangent space in the core DerivativeRegistry (float, Tensor, ...). Structs
 * and tuples map element-wise and drop the elements that have no tangent
 * (integers, strings, functions), so the gradient of a model struct is a
 * struct of the same shape holding only its trainable members. Arrays map
 * their element type.
 */

#pragma once

#include "camel/core/type/base.h"

#include <optional>

namespace camel::autodiff {

namespace type = camel::core::type;

/// Tangent type of values of type `primal`, or nullptr when they have no tangent.
type::Type *tangentTypeOf(type::Type *primal);

/// True for structs and tuples, whose tangents are handled element by element.
bool isAggregate(const type::Type *t);
size_t aggregateSize(type::Type *aggregate);
type::Type *aggregateElement(type::Type *aggregate, size_t index);

/// Position of element `index` of an aggregate in its tangent, or nullopt when the element has no
/// tangent.
std::optional<size_t> tangentElementIndex(type::Type *aggregate, size_t index);

} // namespace camel::autodiff
