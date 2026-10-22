// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/global_config.h"

bool gCamelInspectionMode = false;

void GlobalConfig::SetInspectionMode(bool enabled) { gCamelInspectionMode = enabled; }

bool GlobalConfig::IsInspectionMode() { return gCamelInspectionMode; }
