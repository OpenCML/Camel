// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Where a transform says what it built.
 *
 * A program's stdout belongs to the program. `println` is what its caller reads, and a library
 * kernel that borrows that stream puts two speakers on one channel: a run of a program that
 * transposes a function printed the transform's own walk among the program's numbers, and the only
 * place the *reason* a transpose was refused existed was that same stream, so a reader who captured
 * the program's output captured the transform's debugging with it and a reader who collected the
 * diagnostics got nothing.
 *
 * The walk goes to `std::cerr`, the diagnostic side, and never to `std::cout`.
 *
 * It is written only when the run asked for it, and the asking is the command line:
 * `--trace transforms` (`include/camel/core/trace.h`, whose registry holds the area name and the
 * switch, and whose `trace` is the one place the stream is decided). It is not unconditional: a
 * program that differentiates a small model would write **1988** such lines on a default run -- one
 * per operator per step -- so the walk would be in the product path, and a reading of the break
 * rate would carry all of it while the program's own four numbers were lost in it.
 *
 * The environment is not a way to ask. A run's behaviour is decided by the program, the command
 * line and the build options (design principles 9.3), and `test/tools/check-env-switches.mjs`
 * refuses a switch that reads the environment without a ledger line. A pass parameter is too
 * narrow: two entries that pin a reading write `args = ["${case}"]` and name no pass at all (the
 * `std::nvm` fallback), so the parameter has nowhere to hang. The command line is what is left, and
 * the first thing 9.3 names.
 *
 * What a refusal's reason is owed is `verdicts()` below: **what a transform refused, it says,
 * always**. The sentence for a refused transpose goes to `verdicts()` and not through the switch,
 * because a reader who never heard of `--trace` still has to be able to read why their derivative
 * was refused -- and that is one sentence per refusal, not one line per step.
 */

#pragma once

#include "camel/core/trace.h"

#include <iostream>
#include <ostream>
#include <streambuf>
#include <string>
#include <string_view>

namespace camel::transforms {

namespace detail {

/// A JSON string literal, escaped. The machine-readable readings (`READING_REFUSAL`, `LAW_APPLIED`)
/// write values into an object, and this is the one place a value becomes a literal: two escapers
/// would be two answers to "what does this name look like in the record".
inline void appendJsonString(std::string &out, std::string_view text) {
    out.push_back('"');
    for (const char ch : text) {
        switch (ch) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    out.push_back('"');
}

/// `,"name":"value"` -- the form every field after the first one takes.
inline void appendJsonMember(std::string &out, const char *name, std::string_view value) {
    out.push_back(',');
    appendJsonString(out, name);
    out.push_back(':');
    appendJsonString(out, value);
}

} // namespace detail

/// Whether this run asked for the readings of this area: the switch and the stream are the core's
/// (`camel/core/trace.h`, whose registry has one row for `transforms`), so this module is one
/// reader of a channel it does not own.
inline bool readingsAsked() {
    return camel::core::traceEnabled(camel::core::TraceArea::Transforms);
}

/// The stream a transform writes its step-by-step walk to. Never `std::cout`: see above. A
/// discarded stream when the run did not ask, so every call site stays one expression.
inline std::ostream &readings() { return camel::core::trace(camel::core::TraceArea::Transforms); }

/// The stream a transform writes its *verdict* to -- what it refused and why. Always `std::cerr`: a
/// reader who never heard of `--trace` still has to be able to read why their derivative was
/// refused.
inline std::ostream &verdicts() { return std::cerr; }

} // namespace camel::transforms