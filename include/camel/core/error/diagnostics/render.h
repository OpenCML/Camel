// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Text rendering of a diagnostic.
 *
 * Two text formats exist. `Short` is the one-line form tools and the test runner parse:
 *
 *     (module)path:line:col: [Error]: message suggestion (name=..., code=0x...)
 *
 * `Rich` is what a person reads: the message, the source line with the range underlined, the
 * secondary locations each with its own excerpt, and the notes and the suggestion as
 * `note:` / `help:` lines. A report that carries a machine-applicable fix (DiagFix) prints that
 * too, the way rustc prints a suggestion: a `help:` line with the fix's title, and under it the
 * source line the fix leaves behind with `+` under the text it writes (or, for a fix that only
 * cuts text, the line as it stood with `-` under what it cuts).
 *
 * The format and the colour policy are process-wide settings (a tool chooses them once from its
 * command line). The default is Short with colours, which is what every tool printed before the
 * rich form existed; only a tool that opts in gets the rich form.
 */

#pragma once

#include <cstddef>
#include <iosfwd>
#include <ostream>
#include <string>
#include <string_view>

namespace camel::core::error {

struct Diagnostic;

enum class DiagFormat { Short, Rich };

/// Auto: colours only when the stream written to is a terminal (and NO_COLOR is not set).
enum class ColorMode { Auto, Always, Never };

struct RenderOptions {
    DiagFormat format = DiagFormat::Short;
    bool color        = true;
    /// Display columns a source excerpt may take before it is cut around the marked range.
    size_t maxLineWidth = 120;
    /// Display columns of a tab stop in a source excerpt.
    size_t tabWidth = 4;
    /// Display columns at which `help:` and `note:` text is wrapped; zero disables wrapping.
    /// renderOptionsFor uses the visible terminal width when it can be read.
    size_t wrapWidth = 0;
};

/// Sets the process-wide text format, colour policy and diagnostic document format of diagnostics.
/// `json` is what the command line's `--output-format json` asks for; the reports that are not a
/// `Diagnostics` dump read it (`camel/execute/entry_args.h` for the value an entry graph returns,
/// the pass driver for the error value a pass ends with).
void setDiagnosticRenderConfig(DiagFormat format, ColorMode color, bool json = false);
DiagFormat diagnosticFormat();
/// Whether diagnostics are written as JSON documents rather than text.
bool diagnosticJson();

/// The options for text written to `os`: the process-wide format, and colours according to the
/// colour policy (under Auto, only std::cout / std::cerr attached to a terminal get colours).
RenderOptions renderOptionsFor(const std::ostream &os);
/// The options for text whose destination is not known: Auto is decided for standard output.
RenderOptions defaultRenderOptions();

std::string renderDiagnostic(const Diagnostic &d, const RenderOptions &options);

/// Number of terminal columns `text` (UTF-8) occupies: 2 for East Asian wide and fullwidth
/// characters, 0 for combining marks, 1 otherwise. Tabs count as one column here; an excerpt
/// expands them before measuring.
size_t displayWidth(std::string_view text);

} // namespace camel::core::error
