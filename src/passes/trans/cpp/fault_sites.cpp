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
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "fault_sites.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/error/diagnostics/render.h"
#include "camel/core/source/manager.h"

#include <array>
#include <format>
#include <limits>

namespace camel::passes::cpp {

using camel::core::error::DiagFormat;
using camel::core::error::Diagnostic;
using camel::core::error::DiagnosticBuilder;
using camel::core::error::RenderOptions;
using camel::core::error::RuntimeDiag;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GCGraph;

uint64_t nodeOriginNear(const GCGraph *graph, gc_node_ref_t ref) {
    if (const uint64_t origin = graph->nodeOrigin(ref); origin != camel::source::kInvalidOriginId) {
        return origin;
    }
    for (const auto inputs : {graph->withInputsOf(ref), graph->normInputsOf(ref)}) {
        for (gc_node_ref_t input : inputs) {
            if (const uint64_t origin = graph->nodeOrigin(input);
                origin != camel::source::kInvalidOriginId) {
                return origin;
            }
        }
    }
    return camel::source::kInvalidOriginId;
}

uint64_t graphOriginOf(const camel::core::context::Context &context, const GCGraph *graph) {
    if (graph == nullptr) {
        return camel::source::kInvalidOriginId;
    }
    if (const auto source = context.sourceContext()) {
        const auto origin = source->debugMap().graphOrigin(graph->stableId());
        if (origin != camel::source::kInvalidOriginId) {
            return origin;
        }
    }
    for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
        if (*it == nullptr) {
            continue;
        }
        if (const uint64_t origin = graph->nodeOrigin(it.ref());
            origin != camel::source::kInvalidOriginId) {
            return origin;
        }
    }
    return camel::source::kInvalidOriginId;
}

std::string SiteTable::locationOf(const Diagnostic &d) {
    const auto range = camel::core::error::resolveSourceRange(d.range, d.sourceContext);
    if (!range) {
        return "";
    }
    std::string path   = camel::core::error::diagnosticPath(d);
    const size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) {
        path.erase(0, slash + 1);
    }
    return std::format("{}:{}:{}", path, range->start.line + 1, range->start.character + 1);
}

std::string SiteTable::valueMark(size_t k) {
    return std::string(1, '\x1f') + static_cast<char>('0' + k);
}

std::string SiteTable::siteFor(
    const GCGraph *graph, gc_node_ref_t node, RuntimeDiag kind,
    const std::vector<std::string> &args) {
    const auto key = std::make_tuple(
        static_cast<const void *>(graph),
        static_cast<uint16_t>(node),
        static_cast<uint32_t>(kind));
    if (auto it = index_.find(key); it != index_.end()) {
        return sites_[it->second].name;
    }

    // The templates take up to ten arguments; those not given are empty.
    std::array<std::string, 10> a;
    for (size_t i = 0; i < args.size() && i < a.size(); ++i) {
        a[i] = args[i];
    }
    auto builder = DiagnosticBuilder::of(kind, nullptr);
    const uint64_t origin = nodeOriginNear(graph, node);
    if (origin != camel::source::kInvalidOriginId) {
        builder.atOrigin(origin);
    }
    Diagnostic d = builder.commit(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
    d.sourceContext = context_->sourceContext();

    // What `camel` prints when its output is not a terminal: no colours, no wrapping.
    RenderOptions rich;
    rich.format    = DiagFormat::Rich;
    rich.color     = false;
    rich.wrapWidth = 0;
    RenderOptions brief = rich;
    brief.format        = DiagFormat::Short;

    std::string where = "an unknown place";
    if (const auto range = camel::core::error::resolveSourceRange(d.range, d.sourceContext)) {
        where = std::format("{}:{}", range->start.line + 1, range->start.character + 1);
    }
    std::string base = "at";
    if (const auto range = camel::core::error::resolveSourceRange(d.range, d.sourceContext)) {
        base = std::format("at_{}_{}", range->start.line + 1, range->start.character + 1);
    }

    Site site{
        .name     = names_.unique(base),
        .text     = camel::core::error::renderDiagnostic(d, rich),
        .brief    = camel::core::error::renderDiagnostic(d, brief),
        .comment  = std::format("{} at {}", d.name, where),
        .place    = false,
        .kind     = d.name.substr(d.name.rfind(':') + 1),
        .message  = d.message,
        .location = locationOf(d),
    };
    index_.emplace(key, sites_.size());
    sites_.push_back(std::move(site));
    return sites_.back().name;
}

std::string SiteTable::unlocated(RuntimeDiag kind, const std::vector<std::string> &args) {
    std::array<std::string, 10> a;
    for (size_t i = 0; i < args.size() && i < a.size(); ++i) {
        a[i] = args[i];
    }
    Diagnostic d = DiagnosticBuilder::of(kind, nullptr)
                       .commit(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
    RenderOptions rich;
    rich.format    = DiagFormat::Rich;
    rich.color     = false;
    rich.wrapWidth = 0;
    RenderOptions brief = rich;
    brief.format        = DiagFormat::Short;
    Site site{
        .name     = names_.unique("unlocated"),
        .text     = camel::core::error::renderDiagnostic(d, rich),
        .brief    = camel::core::error::renderDiagnostic(d, brief),
        .comment  = d.name + ", located by the value it reports",
        .place    = false,
        .kind     = d.name.substr(d.name.rfind(':') + 1),
        .message  = d.message,
        .location = "",
    };
    sites_.push_back(std::move(site));
    return sites_.back().name;
}

std::string SiteTable::placeFor(const GCGraph *graph, gc_node_ref_t node) {
    const auto key = std::make_tuple(
        static_cast<const void *>(graph),
        static_cast<uint16_t>(node),
        std::numeric_limits<uint32_t>::max());
    if (auto it = index_.find(key); it != index_.end()) {
        return sites_[it->second].name;
    }
    // A diagnostic at the place with a message of one line and no suggestion: its rich form
    // is that line and the excerpt, its short form the place, the message and the name.
    auto builder          = DiagnosticBuilder::of(RuntimeDiag::RuntimeError, nullptr);
    const uint64_t origin = nodeOriginNear(graph, node);
    if (origin != camel::source::kInvalidOriginId) {
        builder.atOrigin(origin);
    }
    Diagnostic d    = builder.commit(std::string());
    d.message       = "-";
    d.suggestion    = "";
    d.sourceContext = context_->sourceContext();
    RenderOptions rich;
    rich.format    = DiagFormat::Rich;
    rich.color     = false;
    rich.wrapWidth = 0;
    RenderOptions brief = rich;
    brief.format        = DiagFormat::Short;
    const std::string text  = camel::core::error::renderDiagnostic(d, rich);
    const std::string line  = camel::core::error::renderDiagnostic(d, brief);
    const size_t firstBreak = text.find('\n');
    const size_t startEnd   = line.find(": [Error]: ");

    std::string base = "at";
    if (const auto range = camel::core::error::resolveSourceRange(d.range, d.sourceContext)) {
        base = std::format("at_{}_{}", range->start.line + 1, range->start.character + 1);
    }
    Site site{
        .name    = names_.unique(base),
        .text    = firstBreak == std::string::npos ? std::string() : text.substr(firstBreak + 1),
        .brief   = startEnd == std::string::npos ? line : line.substr(0, startEnd),
        .comment  = "a call of an operator of a runtime module",
        .place    = true,
        .kind     = "",
        .message  = "",
        .location = locationOf(d),
    };
    index_.emplace(key, sites_.size());
    sites_.push_back(std::move(site));
    return sites_.back().name;
}

std::string SiteTable::definitions() const {
    std::string out;
    for (const Site &site : sites_) {
        if (site.place) {
            out += std::format(
                "// {}\nconst rt::Place {}{{\n    {},\n    {},\n    {}}};\n",
                site.comment,
                site.name,
                cppStringLiteral(site.text),
                cppStringLiteral(site.brief),
                cppStringLiteral(site.location));
            continue;
        }
        out += std::format(
            "// {}\nconst rt::Site {}{{\n    {},\n    {},\n    {},\n    {},\n    {}}};\n",
            site.comment,
            site.name,
            cppStringLiteral(site.text),
            cppStringLiteral(site.brief),
            cppStringLiteral(site.kind),
            cppStringLiteral(site.message),
            cppStringLiteral(site.location));
    }
    return out;
}

} // namespace camel::passes::cpp
