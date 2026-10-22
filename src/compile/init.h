// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <string>

#include "camel/common/scope.h"
#include "camel/core/data.h"

using data_ptr_t = camel::core::data::data_ptr_t;

extern Scope<std::string, data_ptr_t> globalRootScope;

void initGlobalRootScope();
