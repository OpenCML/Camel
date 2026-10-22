// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once
#include "camel/core/context/context.h"
#include "camel/execute/executor.h"
executor_ptr_t createAlgoExecutor(camel::core::context::context_ptr_t ctx);
