// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The pass specification of the pipeline, `name(key=value, ...)`: parsing and binding to the
// parameters a pass declares. Internal to src/execute; the public API is pass/params.h.

#pragma once

#include "camel/execute/pass/params.h"

#include <optional>
#include <string>
#include <vector>

namespace camel::execute::detail {

struct WrittenParam {
    std::string key;
    PassParamValue value;
    size_t column = 0; // 1-based column of the key in the specification
};

struct ParsedSpec {
    std::string name; // trimmed, parameters removed
    std::vector<WrittenParam> params;
};

/// Splits the specification into the pass name and the written parameters, reading each value
/// as a Camel constant expression with the language's own parser. Returns the problem as text
/// (with the column), or nullopt when the specification is well formed.
std::optional<std::string> parseSpec(const std::string &spec, ParsedSpec &out);

/// Checks the written parameters against the declaration and fills `args` (defaults for the
/// parameters not written). Returns the problem as text, or nullopt.
std::optional<std::string> bindArgs(
    const std::string &passName, const std::vector<PassParam> &declared, const ParsedSpec &parsed,
    PassArgs &args);

/// The declaration as one line, `safe_fp: bool = false, ...`, or "no parameters".
std::string describeParams(const std::vector<PassParam> &declared);

} // namespace camel::execute::detail
