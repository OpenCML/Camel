// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Neural-network executor factory.
 *
 * The first nn module demo exposes graph-generating macro operators used by
 * std::macro to synthesize small training functions.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/execute/executor.h"

executor_ptr_t createNnExecutor(camel::core::context::context_ptr_t ctx);
