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
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The tensor module's operator catalog, one function per operator family.
 * Registration order matters within an export name: overloads are tried in
 * the order they appear here.
 */

#pragma once

#include "op_def.h"

namespace camel::tensor::ops {

std::vector<OpDef> elementwiseOps(); // arithmetic, comparison, unary maps, where, cast
std::vector<OpDef> creationOps();    // new, zeros, ones, full, random, range, eye, seed
std::vector<OpDef> layoutOps();      // shape, reshape, transpose, permute, concat, slice, ...
std::vector<OpDef> reductionOps();   // sum, mean, max, min, argmax, softmax family, layer_norm
std::vector<OpDef> linalgOps();      // matmul, linear
std::vector<OpDef> utilityOps();     // indexing, show, threading controls
std::vector<OpDef> gradientOps();    // operators derivative rules emit; zeros_like, numel, ...

/// Publishes the tangent space of tensors in the core derivative registry.
void registerTensorTangentSpace();

} // namespace camel::tensor::ops
