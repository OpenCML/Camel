// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

// Lexical and syntax-only checks need source positions and diagnostics, not a runtime context.
#include "camel/core/error/diagnostics.h"
#include <iosfwd>

int checkFrontend(
    std::istream &input, const camel::core::error::diagnostics_ptr_t &diagnostics, bool lexicalOnly,
    bool json, std::ostream &output);
