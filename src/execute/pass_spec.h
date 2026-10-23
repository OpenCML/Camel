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

/// Why a specification was refused. The first four are what a call of an ordinary function is
/// refused for; `Syntax` covers a specification that is not a call at all (a positional value, a
/// missing `)`, an argument that is not a constant).
enum class ParamProblemKind { Syntax, Unknown, Duplicate, WrongType, Missing };

struct ParamProblem {
    ParamProblemKind kind = ParamProblemKind::Syntax;
    /// Syntax: the reason, without a position (the position is `column`).
    std::string text;
    std::string name;     // the parameter the problem is about
    std::string found;    // WrongType: the type written
    std::string expected; // WrongType: the type declared
    size_t column = 0;    // 0-based column of the highlighted text in the specification
    size_t length = 0;    // how much of the specification to highlight
};

struct WrittenParam {
    std::string key;
    PassParamValue value;
    size_t column = 0; // 0-based column of the key in the specification
};

struct ParsedSpec {
    std::string name; // trimmed, parameters removed
    std::vector<WrittenParam> params;
};

/// Splits the specification into the pass name and the written parameters, reading each value
/// as a Camel constant expression with the language's own parser.
std::optional<ParamProblem> parseSpec(const std::string &spec, ParsedSpec &out);

/// Checks the written parameters against the declaration and fills `args` (defaults for the
/// parameters not written). A default is never evaluated here: the declaration already holds
/// its value, which `PassArgs` copies.
std::optional<ParamProblem> bindArgs(
    const std::string &passName, const std::vector<PassParam> &declared, const ParsedSpec &parsed,
    PassArgs &args);

/// The declaration as one line, `safe_fp: bool = false, ...`, or "no parameters".
std::string describeParams(const std::vector<PassParam> &declared);

} // namespace camel::execute::detail