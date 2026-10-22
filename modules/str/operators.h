// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"
#include <string>
#include <unordered_map>

std::unordered_map<std::string, operator_t> getStrOpsMap();
