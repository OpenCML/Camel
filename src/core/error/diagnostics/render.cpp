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
 * Text rendering of diagnostics: the one-line form and the form with source excerpts.
 *
 * Positions are 0-based lines and columns counted in code points (what the parser reports and
 * what SourceManager::offsetToPos returns). An excerpt is laid out in display columns: a tab
 * advances to the next tab stop, an East Asian wide character takes two columns, a combining
 * mark none. The underline is computed in those columns, so it stays under the text it marks
 * whatever the line contains.
 */

#include "camel/core/error/diagnostics/render.h"

#include "camel/core/error/diagnostics.h"
#include "camel/utils/path.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <unordered_map>

#ifdef _WIN32
#include <io.h>
#include <stdio.h>
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace camel::core::error {

namespace {

std::atomic<DiagFormat> g_format{DiagFormat::Short};
std::atomic<ColorMode> g_color{ColorMode::Always};

bool isTerminal(int fd) {
#ifdef _WIN32
    return _isatty(fd) != 0;
#else
    return isatty(fd) != 0;
#endif
}

/// Query the visible terminal rather than the Windows scrollback buffer. Zero means that
/// the destination has no terminal size; those diagnostics are emitted without wrapping.
size_t terminalWidth(int fd) {
    if (fd < 0 || !isTerminal(fd)) {
        return 0;
    }
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info{};
    const auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (GetConsoleScreenBufferInfo(handle, &info)) {
        return static_cast<size_t>(info.srWindow.Right - info.srWindow.Left + 1);
    }
#else
    winsize size{};
    if (ioctl(fd, TIOCGWINSZ, &size) == 0) {
        return size.ws_col;
    }
#endif
    return 0;
}

bool autoColorFor(int fd) {
    const char *noColor = std::getenv("NO_COLOR");
    if (noColor != nullptr && noColor[0] != '\0') {
        return false;
    }
    return isTerminal(fd);
}

// ---- Styles ----
struct Style {
    bool on;
    std::string wrap(const char *code, const std::string &text) const {
        if (!on || text.empty()) {
            return text;
        }
        return std::string("\033[") + code + "m" + text + "\033[0m";
    }
    std::string bold(const std::string &t) const { return wrap("1", t); }
    std::string underline(const std::string &t) const { return wrap("4", t); }
    std::string gutter(const std::string &t) const { return wrap("1;34", t); }
    std::string secondary(const std::string &t) const { return wrap("1;36", t); }
    std::string severity(Severity s, const std::string &t) const {
        switch (s) {
        case Severity::Error:
            return wrap("1;31", t);
        case Severity::Warn:
            return wrap("1;33", t);
        case Severity::Info:
            return wrap("1;32", t);
        case Severity::Hint:
            return wrap("1;36", t);
        default:
            return wrap("1;35", t);
        }
    }
};

// ---- UTF-8 and display width ----
/// Decodes the code point starting at `i` and advances `i` past it. A byte that does not start a
/// valid sequence is returned as U+FFFD and consumed alone.
char32_t decodeUtf8(std::string_view s, size_t &i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    auto cont     = [&](size_t k) {
        return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80;
    };
    auto bits = [&](size_t k) { return static_cast<char32_t>(s[i + k] & 0x3F); };
    if (b0 < 0x80) {
        ++i;
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && cont(1)) {
        char32_t cp = (static_cast<char32_t>(b0 & 0x1F) << 6) | bits(1);
        i += 2;
        return cp;
    }
    if ((b0 & 0xF0) == 0xE0 && cont(1) && cont(2)) {
        char32_t cp = (static_cast<char32_t>(b0 & 0x0F) << 12) | (bits(1) << 6) | bits(2);
        i += 3;
        return cp;
    }
    if ((b0 & 0xF8) == 0xF0 && cont(1) && cont(2) && cont(3)) {
        char32_t cp =
            (static_cast<char32_t>(b0 & 0x07) << 18) | (bits(1) << 12) | (bits(2) << 6) | bits(3);
        i += 4;
        return cp;
    }
    ++i;
    return 0xFFFD;
}

size_t codePointWidth(char32_t cp) {
    if (cp == 0) {
        return 0;
    }
    // Combining marks, zero-width spaces and joiners, variation selectors.
    if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x200B && cp <= 0x200F) ||
        (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF) {
        return 0;
    }
    // East Asian Wide and Fullwidth blocks, and the emoji planes terminals draw double width.
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) ||
        (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
        (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
        (cp >= 0x1F900 && cp <= 0x1F9FF) || (cp >= 0x20000 && cp <= 0x3FFFD)) {
        return 2;
    }
    return 1;
}

/// One code point of a source line as it is drawn.
struct Cell {
    std::string text; // what is printed for it (a tab becomes spaces)
    size_t column;    // display column it starts at
    size_t width;     // display columns it takes
    bool blank;       // a space or a tab
};

std::vector<Cell> layoutLine(std::string_view line, size_t tabWidth) {
    std::vector<Cell> cells;
    size_t column = 0;
    for (size_t i = 0; i < line.size();) {
        const size_t begin = i;
        const char32_t cp  = decodeUtf8(line, i);
        Cell cell{std::string(line.substr(begin, i - begin)), column, 0, false};
        if (cp == U'\t') {
            cell.width = tabWidth - (column % tabWidth);
            cell.text  = std::string(cell.width, ' ');
            cell.blank = true;
        } else if (cp < 0x20 || cp == 0x7F) {
            // A control character would move the cursor; draw a space in its place.
            cell.width = 1;
            cell.text  = " ";
        } else if (cp == 0xFFFD && i - begin == 1) {
            cell.width = 1;
            cell.text  = "?";
        } else {
            cell.width = codePointWidth(cp);
            cell.blank = cp == U' ';
        }
        column += cell.width;
        cells.push_back(std::move(cell));
    }
    return cells;
}

// ---- Source text ----
using LineProvider = std::function<std::optional<std::string>(size_t)>;

/// Lines of a file read from disk, for a diagnostic whose file the source context does not
/// hold (a diagnostic built without one). Cached: several diagnostics usually share a file.
std::optional<std::string> diskLine(const std::string &path, size_t line) {
    static std::mutex mutex;
    static std::unordered_map<std::string, std::vector<std::string>> cache;
    std::lock_guard<std::mutex> lock(mutex);
    auto it = cache.find(path);
    if (it == cache.end()) {
        std::vector<std::string> lines;
        std::ifstream in(camel::utils::pathFromUtf8(path), std::ios::binary);
        if (in) {
            std::string text;
            while (std::getline(in, text)) {
                if (!text.empty() && text.back() == '\r') {
                    text.pop_back();
                }
                lines.push_back(std::move(text));
            }
            // A position right after the final newline is on a line of its own.
            lines.emplace_back();
        }
        it = cache.emplace(path, std::move(lines)).first;
    }
    if (line >= it->second.size()) {
        return std::nullopt;
    }
    return it->second[line];
}

LineProvider
lineProviderFor(const std::string &path, const camel::source::source_context_ptr_t &ctx) {
    if (path.empty()) {
        return [](size_t) { return std::optional<std::string>{}; };
    }
    if (ctx) {
        if (auto id = ctx->findFileId(path)) {
            const camel::source::SourceFile *file = ctx->sourceFile(*id);
            if (file != nullptr) {
                return [file](size_t line) -> std::optional<std::string> {
                    if (line >= file->lineStarts.size()) {
                        return std::nullopt;
                    }
                    size_t begin = file->lineStarts[line];
                    size_t end   = line + 1 < file->lineStarts.size() ? file->lineStarts[line + 1] - 1
                                                                      : file->content.size();
                    if (end > begin && file->content[end - 1] == '\r') {
                        --end;
                    }
                    return file->content.substr(begin, end - begin);
                };
            }
        }
    }
    return [path](size_t line) { return diskLine(path, line); };
}

// ---- Excerpt ----
struct MarkedLine {
    size_t line;      // 0-based
    std::string text; // drawn text, already cut to the width limit
    size_t markStart; // display column in `text`
    size_t markWidth; // 0: no underline on this line
};

/// Lays out one source line and the part of it to underline: code point columns
/// [startCp, endCp), or from the first non-blank when `fromFirstNonBlank`, to the end of the
/// line when `toEnd`. A range that is empty on this line still gets one caret.
MarkedLine markLine(
    size_t lineNo, std::string_view source, size_t startCp, size_t endCp, bool fromFirstNonBlank,
    bool toEnd, bool caretIfEmpty, const RenderOptions &options) {
    std::vector<Cell> cells = layoutLine(source, options.tabWidth);
    const size_t total      = cells.empty() ? 0 : cells.back().column + cells.back().width;
    auto columnOf           = [&](size_t cp) { return cp < cells.size() ? cells[cp].column : total; };

    size_t lastNonBlank = cells.size();
    while (lastNonBlank > 0 && cells[lastNonBlank - 1].blank) {
        --lastNonBlank;
    }
    if (fromFirstNonBlank) {
        startCp = 0;
        while (startCp < cells.size() && cells[startCp].blank) {
            ++startCp;
        }
    }
    if (toEnd) {
        endCp = std::max(lastNonBlank, std::min(startCp, cells.size()));
    }
    size_t markStart = columnOf(startCp);
    size_t markEnd   = std::max(columnOf(endCp), markStart);
    if (markEnd == markStart && caretIfEmpty) {
        // An empty range, or one that starts past the end of the line: point at the place.
        markEnd = markStart + (startCp < cells.size() ? std::max<size_t>(cells[startCp].width, 1) : 1);
    }

    // A long line is cut to a window that keeps the start of the marked range in view.
    size_t windowStart = 0;
    size_t windowEnd   = total;
    const size_t limit = std::max<size_t>(options.maxLineWidth, 20);
    static const std::string kCut = "...";
    if (total > limit) {
        const size_t lead = limit / 4;
        if (markStart > limit - lead) {
            windowStart = markStart - lead;
        }
        windowEnd = std::min(total, windowStart + limit);
    }

    MarkedLine out{lineNo, "", 0, 0};
    size_t shift = 0; // display columns removed in front of the window, minus the cut marker
    if (windowStart > 0) {
        out.text += kCut;
    }
    size_t drawnFrom = windowStart;
    bool first       = true;
    for (const Cell &cell : cells) {
        if (cell.column < windowStart || cell.column + cell.width > windowEnd) {
            continue;
        }
        if (first) {
            drawnFrom = cell.column;
            first     = false;
        }
        out.text += cell.text;
    }
    if (windowEnd < total) {
        out.text += kCut;
    }
    const size_t prefix = windowStart > 0 ? kCut.size() : 0;
    shift               = drawnFrom;
    auto place          = [&](size_t column) {
        column = std::clamp(column, drawnFrom, std::max(windowEnd, drawnFrom));
        return column - shift + prefix;
    };
    if (markEnd > markStart) {
        out.markStart = place(markStart);
        out.markWidth = std::max<size_t>(place(markEnd) - out.markStart, 1);
    }
    return out;
}

/// The lines of `range` to show, each with its underline. At most four lines are shown; of a
/// longer range the first two and the last, and `elidedAfter` is the index after which lines
/// were left out (npos: none).
std::vector<MarkedLine> markRange(
    const CharRange &range, const LineProvider &lineAt, const RenderOptions &options,
    size_t &elidedAfter) {
    elidedAfter = static_cast<size_t>(-1);
    std::vector<MarkedLine> out;
    const size_t first = range.start.line;
    const size_t last  = std::max(range.end.line, first);
    auto firstText     = lineAt(first);
    if (!firstText) {
        return out;
    }
    if (last == first || !lineAt(last)) {
        const bool single = last == first;
        out.push_back(
            markLine(
                first,
                *firstText,
                range.start.character,
                single ? range.end.character : 0,
                false,
                !single,
                true,
                options));
        return out;
    }
    std::vector<size_t> shown;
    if (last - first + 1 <= 4) {
        for (size_t l = first; l <= last; ++l) {
            shown.push_back(l);
        }
    } else {
        shown       = {first, first + 1, last};
        elidedAfter = 1;
    }
    for (size_t l : shown) {
        auto text = lineAt(l);
        if (!text) {
            continue;
        }
        if (l == first) {
            out.push_back(
                markLine(l, *text, range.start.character, 0, false, true, true, options));
        } else if (l == last) {
            out.push_back(
                markLine(l, *text, 0, range.end.character, true, false, false, options));
        } else {
            out.push_back(markLine(l, *text, 0, 0, true, true, false, options));
        }
    }
    return out;
}

std::string rtrim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    return s;
}

/// A located excerpt: the `-->` line and the marked source lines.
struct Block {
    std::string path;
    std::optional<CharRange> range;
    std::vector<MarkedLine> lines;
    size_t elidedAfter = static_cast<size_t>(-1);
};

Block makeBlock(
    const std::string &path, const std::optional<CharRange> &range,
    const camel::source::source_context_ptr_t &ctx, const RenderOptions &options) {
    Block block{path, range, {}, static_cast<size_t>(-1)};
    if (range) {
        block.lines = markRange(*range, lineProviderFor(path, ctx), options, block.elidedAfter);
    }
    return block;
}

void appendBlock(
    std::string &out, const Block &block, size_t gutterWidth, const Style &style, char mark,
    const std::function<std::string(const std::string &)> &paint) {
    const std::string pad(gutterWidth, ' ');
    if (block.path.empty() && !block.range) {
        return;
    }
    out += pad + style.gutter("-->") + " " + (block.path.empty() ? "<unknown>" : block.path);
    if (block.range) {
        out += ":" + std::to_string(block.range->start.line + 1) + ":" +
               std::to_string(block.range->start.character + 1);
    }
    out += "\n";
    if (block.lines.empty()) {
        return;
    }
    out += pad + " " + style.gutter("|") + "\n";
    for (size_t i = 0; i < block.lines.size(); ++i) {
        const MarkedLine &line = block.lines[i];
        std::string number     = std::to_string(line.line + 1);
        number.insert(0, gutterWidth > number.size() ? gutterWidth - number.size() : 0, ' ');
        out += rtrim(style.gutter(number + " |") + " " + line.text) + "\n";
        if (line.markWidth > 0) {
            out += pad + " " + style.gutter("|") + " " + std::string(line.markStart, ' ') +
                   paint(std::string(line.markWidth, mark)) + "\n";
        }
        if (i == block.elidedAfter) {
            out += pad + style.gutter("...") + "\n";
        }
    }
}

size_t digits(size_t n) {
    size_t d = 1;
    while (n >= 10) {
        n /= 10;
        ++d;
    }
    return d;
}

/// Appends `label: text`, wrapped at `width` display columns with the continuation lines
/// indented under the text. Line breaks already in the text are kept.
void appendLabelled(
    std::string &out, const std::string &label, const std::string &text, size_t width,
    const Style &style) {
    const std::string indent(label.size() + 2, ' ');
    out += style.bold(label + ":");
    size_t column = label.size() + 1;
    std::istringstream lines(text);
    std::string line;
    bool firstLine = true;
    while (std::getline(lines, line)) {
        if (!firstLine) {
            out += "\n" + indent.substr(1);
            column = indent.size() - 1;
        }
        firstLine = false;
        // Leading spaces of a line are layout (a list of overloads); keep them.
        size_t lead = 0;
        while (lead < line.size() && line[lead] == ' ') {
            ++lead;
        }
        std::istringstream words(line.substr(lead));
        std::string word;
        bool firstWord = true;
        while (words >> word) {
            const size_t w = displayWidth(word);
            if (width != 0 && !firstWord && column + 1 + w > width) {
                out += "\n" + indent.substr(1) + std::string(lead, ' ');
                column = indent.size() - 1 + lead;
            }
            out += (firstWord ? std::string(lead + 1, ' ') : std::string(" ")) + word;
            column += (firstWord ? lead : 0) + 1 + w;
            firstWord = false;
        }
    }
    out += "\n";
}

std::string severityWord(Severity s) {
    switch (s) {
    case Severity::Error:
        return "error";
    case Severity::Warn:
        return "warning";
    case Severity::Info:
        return "info";
    case Severity::Hint:
        return "hint";
    default:
        return "unknown";
    }
}

std::string labelPath(const DiagLabel &label, const Diagnostic &d, const std::string &ownPath) {
    if (!label.path.empty()) {
        return label.path;
    }
    if (d.sourceContext) {
        if (std::holds_alternative<camel::source::span_id_t>(label.range)) {
            auto p = d.sourceContext->pathForSpan(std::get<camel::source::span_id_t>(label.range));
            if (!p.empty()) {
                return p;
            }
        } else if (std::holds_alternative<camel::source::origin_id_t>(label.range)) {
            auto p =
                d.sourceContext->pathForOrigin(std::get<camel::source::origin_id_t>(label.range));
            if (!p.empty()) {
                return p;
            }
        }
    }
    return ownPath;
}

std::string renderShort(const Diagnostic &d, const Style &style) {
    const std::string path       = diagnosticPath(d);
    const std::string moduleName = diagnosticModuleName(d, path);
    const auto range             = resolveSourceRange(d.range, d.sourceContext);

    std::string message = d.message;
    for (const DiagLabel &label : d.related) {
        message += " [" + label.message;
        if (auto at = resolveSourceRange(label.range, d.sourceContext)) {
            const std::string where = labelPath(label, d, path);
            message += " at ";
            if (where != path && !where.empty()) {
                message += where + ":";
            }
            message += std::to_string(at->start.line + 1) + ":" +
                       std::to_string(at->start.character + 1);
        }
        message += "]";
    }
    if (d.relatedElided) {
        message += " [...]";
    }
    for (const std::string &note : d.notes) {
        message += " [" + note + "]";
    }

    return std::format(
        "({}){}:{}:{}: [{}]: {} {} (name={}, code=0x{})",
        style.bold(moduleName),
        style.underline(path),
        (range ? std::to_string(range->start.line + 1) : "?"),
        (range ? std::to_string(range->start.character + 1) : "?"),
        style.severity(d.severity, to_string(d.severity)),
        message,
        d.suggestion,
        d.name,
        Diagnostic::hex8(d.diagCode()));
}

std::string renderRich(const Diagnostic &d, const RenderOptions &options, const Style &style) {
    const std::string path = diagnosticPath(d);
    Block primary = makeBlock(path, resolveSourceRange(d.range, d.sourceContext), d.sourceContext, options);
    std::vector<Block> secondary;
    for (const DiagLabel &label : d.related) {
        secondary.push_back(
            makeBlock(
                labelPath(label, d, path),
                resolveSourceRange(label.range, d.sourceContext),
                d.sourceContext,
                options));
    }
    size_t maxLine = 0;
    auto consider  = [&](const Block &b) {
        for (const MarkedLine &l : b.lines) {
            maxLine = std::max(maxLine, l.line + 1);
        }
    };
    consider(primary);
    for (const Block &b : secondary) {
        consider(b);
    }
    const size_t gutterWidth = digits(maxLine) + 1;

    // A message may end in a line break (a list it closes with one); the excerpt follows the
    // last line of text directly.
    std::string message = d.message;
    while (!message.empty() && (message.back() == '\n' || message.back() == ' ')) {
        message.pop_back();
    }
    std::string out;
    out += style.severity(d.severity, severityWord(d.severity) + "[" + d.name + "]") +
           style.bold(": " + message) + "\n";
    appendBlock(out, primary, gutterWidth, style, '^', [&](const std::string &t) {
        return style.severity(d.severity, t);
    });
    for (size_t i = 0; i < d.related.size(); ++i) {
        appendLabelled(out, "note", d.related[i].message, options.wrapWidth, style);
        appendBlock(out, secondary[i], gutterWidth, style, '-', [&](const std::string &t) {
            return style.secondary(t);
        });
    }
    if (d.relatedElided) {
        appendLabelled(out, "note", "further locations of this kind are not shown", options.wrapWidth, style);
    }
    for (const std::string &note : d.notes) {
        appendLabelled(out, "note", note, options.wrapWidth, style);
    }
    if (d.promoted) {
        appendLabelled(
            out,
            "note",
            "this warning is reported as an error because of -Werror",
            options.wrapWidth,
            style);
    }
    if (!d.suggestion.empty()) {
        appendLabelled(out, "help", d.suggestion, options.wrapWidth, style);
    }
    while (!out.empty() && out.back() == '\n') {
        out.pop_back();
    }
    return out;
}

} // namespace

void setDiagnosticRenderConfig(DiagFormat format, ColorMode color) {
    g_format.store(format);
    g_color.store(color);
}

DiagFormat diagnosticFormat() { return g_format.load(); }

RenderOptions renderOptionsFor(const std::ostream &os) {
    RenderOptions options;
    options.format = g_format.load();
    const int fd = &os == &std::cout ? 1 : (&os == &std::cerr || &os == &std::clog ? 2 : -1);
    options.wrapWidth = terminalWidth(fd);
    switch (g_color.load()) {
    case ColorMode::Always:
        options.color = true;
        break;
    case ColorMode::Never:
        options.color = false;
        break;
    case ColorMode::Auto:
        if (&os == &std::cout) {
            options.color = autoColorFor(1);
        } else if (&os == &std::cerr || &os == &std::clog) {
            options.color = autoColorFor(2);
        } else {
            options.color = false;
        }
        break;
    }
    return options;
}

RenderOptions defaultRenderOptions() { return renderOptionsFor(std::cout); }

std::string renderDiagnostic(const Diagnostic &d, const RenderOptions &options) {
    const Style style{options.color};
    return options.format == DiagFormat::Rich ? renderRich(d, options, style)
                                              : renderShort(d, style);
}

size_t displayWidth(std::string_view text) {
    size_t width = 0;
    for (size_t i = 0; i < text.size();) {
        width += codePointWidth(decodeUtf8(text, i));
    }
    return width;
}

} // namespace camel::core::error
