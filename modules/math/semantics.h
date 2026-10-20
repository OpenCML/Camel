/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

/**
 * Declares the numeric semantics of every operator of the `math` module
 * (camel/core/operator_semantics.h; design principles 7.1). Called by MathModule::load after the
 * operators' traits are registered: the table checks a declaration against the traits.
 *
 * A rejected declaration is a defect of the module, so this throws std::logic_error naming the
 * module, as the tensor module does.
 */
void declareMathOperatorSemantics();