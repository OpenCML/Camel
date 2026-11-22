// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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