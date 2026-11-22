// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The debugger links against libcamel, which now owns the GIR JSON
 * serialization implementation. This translation unit remains only to keep
 * the tool target structure stable while the implementation lives in the
 * compile GIR subsystem.
 */

#include "gir_json.h"
