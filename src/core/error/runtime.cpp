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
 * Created: Mar. 07, 2026
 * Updated: Mar. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/error/runtime.h"

#include "camel/compile/gir/graph.h"
#include "camel/compile/gir/nodes.h"
#include "camel/core/context/context.h"
#include "camel/core/source/manager.h"

#include <filesystem>
#include <format>

namespace camel::core::error {

namespace {

std::string formatIndexedMessage(const std::string &fmt, const std::vector<std::string> &args) {
    std::string out;
    out.reserve(fmt.size() + args.size() * 16);

    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '{') {
            out.push_back(fmt[i]);
            continue;
        }

        size_t j       = i + 1;
        size_t idx     = 0;
        bool hasDigits = false;
        while (j < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[j]))) {
            hasDigits = true;
            idx       = idx * 10 + static_cast<size_t>(fmt[j] - '0');
            ++j;
        }
        if (hasDigits && j < fmt.size() && fmt[j] == '}') {
            if (idx < args.size()) {
                out += args[idx];
            } else {
                out.append(fmt, i, j - i + 1);
            }
            i = j;
            continue;
        }

        out.push_back(fmt[i]);
    }

    return out;
}

camel::source::origin_id_t resolveOriginFromSite(
    camel::source::source_context_ptr_t sourceContext, const ExecutionSite &site) {
    if (!sourceContext) {
        return camel::source::kInvalidOriginId;
    }

    if (site.cachedOrigin != camel::source::kInvalidOriginId) {
        return site.cachedOrigin;
    }

    const auto &debugMap = sourceContext->debugMap();

    if (site.node) {
        auto origin = sourceContext->resolveGirNodeOrigin(site.node);
        if (origin != camel::source::kInvalidOriginId) {
            return origin;
        }
    }

    if (!site.stableId.empty()) {
        if (site.kind == ExecutionSiteKind::Node || site.kind == ExecutionSiteKind::TaskNode) {
            auto origin = debugMap.nodeOrigin(site.stableId);
            if (origin != camel::source::kInvalidOriginId) {
                return origin;
            }
        }
        auto graphOrigin = debugMap.graphOrigin(site.stableId);
        if (graphOrigin != camel::source::kInvalidOriginId) {
            return graphOrigin;
        }
    }

    if (site.kind == ExecutionSiteKind::BytecodePc || site.kind == ExecutionSiteKind::JitPc) {
        auto origin = debugMap.pcOrigin(site.pc);
        if (origin != camel::source::kInvalidOriginId) {
            return origin;
        }
    }

    if (site.graph) {
        auto graphOrigin = debugMap.graphOrigin(site.graph->stableId());
        if (graphOrigin != camel::source::kInvalidOriginId) {
            return graphOrigin;
        }
    }

    return sourceContext->currentRuntimeOrigin();
}

std::string defaultModuleName(const ExecutionSite &site, const std::string &sourcePath) {
    if (!sourcePath.empty()) {
        return std::filesystem::path(sourcePath).stem().string();
    }
    if (site.graph) {
        return site.graph->name();
    }
    return "runtime";
}

} // namespace

RuntimeFault::RuntimeFault(
    RuntimeDiag diag, std::vector<std::string> formatArgs, std::string detail,
    std::exception_ptr cause, ExecutionSiteHint hint, RuntimeFaultPolicy policy)
    : diag_(diag), formatArgs_(std::move(formatArgs)), detail_(std::move(detail)), cause_(cause),
      hint_(std::move(hint)), policy_(policy) {}

const char *RuntimeFault::what() const noexcept {
    if (!what_.empty()) {
        return what_.c_str();
    }
    const auto info = getDiagInfo(diag_);
    what_           = std::string(info.name);
    if (!detail_.empty()) {
        what_ += ": " + detail_;
    }
    return what_.c_str();
}

ResolvedRuntimeLocation RuntimeLocationResolver::resolve(const ExecutionSite &site) const {
    ResolvedRuntimeLocation resolved;
    resolved.origin = resolveOriginFromSite(sourceContext_, site);
    if (!sourceContext_ || resolved.origin == camel::source::kInvalidOriginId) {
        return resolved;
    }

    resolved.hasRange   = true;
    resolved.range      = sourceContext_->resolveOrigin(resolved.origin);
    resolved.sourcePath = sourceContext_->pathForOrigin(resolved.origin);
    return resolved;
}

Diagnostic
RuntimeErrorReporter::report(const RuntimeFault &fault, const ExecutionSite &site) const {
    const auto info = getDiagInfo(fault.diag());
    uint32_t key    = static_cast<uint32_t>(fault.diag());
    Diagnostic d;
    d.type          = diagTypeOf(fault.diag());
    d.specific      = extractSpecific(key);
    d.severity      = extractSeverity(key);
    d.name          = std::string(to_string(d.type)) + "::" + info.name;
    d.message       = formatIndexedMessage(info.message, fault.formatArgs());
    d.suggestion    = formatIndexedMessage(info.suggestion, fault.formatArgs());
    d.sourceContext = sink_ ? sink_->sourceContext() : nullptr;

    auto resolved = resolver_.resolve(site);
    if (resolved.origin != camel::source::kInvalidOriginId) {
        d.range = resolved.origin;
    }
    if (!resolved.sourcePath.empty()) {
        d.modulePath = resolved.sourcePath;
    }
    d.moduleName = defaultModuleName(site, resolved.sourcePath);

    if (sink_) {
        return sink_->add(std::move(d));
    }
    return d;
}

ExecutionSite makeGraphExecutionSite(
    camel::source::source_context_ptr_t sourceContext, GIR::Graph *graph, size_t frameDepth,
    std::string taskLabel) {
    ExecutionSite site;
    site.kind       = ExecutionSiteKind::Unknown;
    site.graph      = graph;
    site.frameDepth = frameDepth;
    site.taskLabel  = std::move(taskLabel);
    if (graph) {
        site.stableId = graph->stableId();
        if (sourceContext) {
            site.cachedOrigin = sourceContext->debugMap().graphOrigin(site.stableId);
        }
    }
    return site;
}

ExecutionSite makeNodeExecutionSite(
    camel::source::source_context_ptr_t sourceContext, GIR::Graph *graph, GIR::Node *node,
    size_t frameDepth, std::string taskLabel, ExecutionSiteKind kind) {
    ExecutionSite site =
        makeGraphExecutionSite(sourceContext, graph, frameDepth, std::move(taskLabel));
    site.kind = kind;
    site.node = node;
    if (node) {
        site.stableId = node->debugEntityId();
        if (sourceContext) {
            site.cachedOrigin = sourceContext->resolveGirNodeOrigin(node);
        }
    }
    return site;
}

ExecutionSite makePcExecutionSite(
    camel::source::source_context_ptr_t sourceContext, GIR::Graph *graph, size_t pc,
    size_t frameDepth, std::string taskLabel, ExecutionSiteKind kind) {
    ExecutionSite site =
        makeGraphExecutionSite(sourceContext, graph, frameDepth, std::move(taskLabel));
    site.kind = kind;
    site.pc   = pc;
    if (sourceContext) {
        site.cachedOrigin = sourceContext->debugMap().pcOrigin(pc);
    }
    return site;
}

Diagnostic reportRuntimeFault(
    camel::core::context::Context &ctx, const RuntimeFault &fault, const ExecutionSite &site) {
    auto reporter = ctx.runtimeErrorReporter();
    if (!reporter) {
        return DiagnosticBuilder::of(fault.diag()).commit(fault.what());
    }
    return reporter->report(fault, site);
}

} // namespace camel::core::error
