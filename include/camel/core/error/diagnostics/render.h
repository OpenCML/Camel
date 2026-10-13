/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Text rendering of a diagnostic.
 *
 * Two text formats exist. `Short` is the one-line form tools and the test runner parse:
 *
 *     (module)path:line:col: [Error]: message suggestion (name=..., code=0x...)
 *
 * `Rich` is what a person reads: the message, the source line with the range underlined, the
 * secondary locations each with its own excerpt, and the notes and the suggestion as
 * `note:` / `help:` lines.
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
    /// Display columns at which `help:` and `note:` text is wrapped.
    size_t wrapWidth = 100;
};

/// Sets the process-wide text format and colour policy of diagnostics.
void setDiagnosticRenderConfig(DiagFormat format, ColorMode color);
DiagFormat diagnosticFormat();

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
