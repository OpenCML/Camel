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
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/error/runtime.h"

#include "camel/core/context/context.h"
#include "camel/core/context/frame.h"
#include "camel/core/source/manager.h"
#include "camel/runtime/graph.h"
#include "camel/utils/path.h"

#include <cctype>
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

    if (site.runtimeGraph) {
        auto origin = debugMap.runtimeGraphOrigin(reinterpret_cast<uintptr_t>(site.runtimeGraph));
        if (origin != camel::source::kInvalidOriginId) {
            return origin;
        }
    }

    return sourceContext->currentRuntimeOrigin();
}

std::string defaultModuleName(const ExecutionSite &site, const std::string &sourcePath) {
    if (!sourcePath.empty()) {
        return camel::utils::pathToUtf8(camel::utils::pathFromUtf8(sourcePath).stem());
    }
    if (site.runtimeGraph) {
        return site.runtimeGraph->name();
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
    Diagnostic d = describe(fault, site);
    if (sink_) {
        return sink_->add(std::move(d));
    }
    return d;
}

Diagnostic
RuntimeErrorReporter::describe(const RuntimeFault &fault, const ExecutionSite &site) const {
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

    // A fault that names a place of the program itself is reported there. The site only says
    // which node was running, and for an operator that works on a whole function (a macro
    // such as grad) that is the call of the operator, not the place the fault is about.
    ExecutionSite located = site;
    if (fault.hint().cachedOrigin != camel::source::kInvalidOriginId) {
        located.cachedOrigin = fault.hint().cachedOrigin;
    }
    auto resolved = resolver_.resolve(located);
    if (resolved.origin != camel::source::kInvalidOriginId) {
        d.range = resolved.origin;
    }
    if (!resolved.sourcePath.empty()) {
        d.modulePath = resolved.sourcePath;
    }
    d.moduleName = defaultModuleName(site, resolved.sourcePath);
    return d;
}

ExecutionSite makeGraphExecutionSite(
    camel::source::source_context_ptr_t sourceContext, camel::runtime::GCGraph *graph,
    size_t frameDepth, std::string taskLabel) {
    ExecutionSite site;
    site.kind         = ExecutionSiteKind::Unknown;
    site.runtimeGraph = graph;
    site.frameDepth   = frameDepth;
    site.taskLabel    = std::move(taskLabel);
    if (graph && sourceContext) {
        site.cachedOrigin =
            sourceContext->debugMap().runtimeGraphOrigin(reinterpret_cast<uintptr_t>(graph));
    }
    return site;
}

ExecutionSite makePcExecutionSite(
    camel::source::source_context_ptr_t sourceContext, const camel::core::context::Frame *frame,
    size_t pc, size_t frameDepth, std::string taskLabel, ExecutionSiteKind kind) {
    ExecutionSite site = makeGraphExecutionSite(
        sourceContext,
        frame ? const_cast<camel::runtime::GCGraph *>(frame->runtimeGraph()) : nullptr,
        frameDepth,
        std::move(taskLabel));
    site.kind = kind;
    site.pc   = pc;
    if (sourceContext) {
        site.cachedOrigin = sourceContext->debugMap().pcOrigin(pc);
    }
    return site;
}

namespace {
thread_local uint32_t tlsFaultIsolationDepth = 0;
// DescribedFaultScope objects open on the calling thread.
thread_local uint32_t tlsDescribedFaultScopes = 0;
uint32_t describedFaultScopes() { return tlsDescribedFaultScopes; }
} // namespace

Diagnostic reportRuntimeFault(
    camel::core::context::Context &ctx, const RuntimeFault &fault, const ExecutionSite &site) {
    auto reporter = ctx.runtimeErrorReporter();
    if (!reporter) {
        return DiagnosticBuilder::of(fault.diag()).commit(fault.what());
    }
    if (describedFaultScopes() != 0 ||
        (faultIsolationDepth() != 0 && isIsolatableFault(fault.diag()))) [[unlikely]] {
        return reporter->describe(fault, site);
    }
    return reporter->report(fault, site);
}

// ---- Fault isolation ----


bool isIsolatableFault(RuntimeDiag diag) {
    switch (diag) {
    // Faults of the program's own computation.
    case RuntimeDiag::RuntimeError:
    case RuntimeDiag::DivisionByZero:
    case RuntimeDiag::IndexOutOfRange:
    case RuntimeDiag::SliceOutOfRange:
    case RuntimeDiag::ConversionOutOfRange:
    case RuntimeDiag::KeyNotFound:
    case RuntimeDiag::FieldNotFound:
    // Checked before the call that does not fit: unwinding gives everything back.
    case RuntimeDiag::MaxRecursionDepthExceeded:
    // An operator refused its operands.
    case RuntimeDiag::TensorDimensionMismatch:
    case RuntimeDiag::InvalidNumberText:
    case RuntimeDiag::FormatFailed:
    case RuntimeDiag::OperandTypeUnsupported:
    case RuntimeDiag::InvalidArgument:
    // A library operator that is not declared `T!` failed.
    case RuntimeDiag::FileOpenFailed:
    case RuntimeDiag::FileSystemError:
    case RuntimeDiag::JsonEncodeUnsupported:
    case RuntimeDiag::RegexInvalid:
    case RuntimeDiag::TimeFormatFailed:
        return true;
    default:
        // Defects and limits of the compiler (PassInternalError, UnrecognizedOperatorURI,
        // UnsupportedBytecode, OperatorNotImplemented, MemoryExhausted, ...),
        // diagnostics that are not raised while a graph runs, and everything not listed.
        // DESIGN-QUESTION(attempt-memory-exhausted): memory exhaustion is not isolated (it
        // is an exception of the allocator, not a RuntimeFault, and never gets here either).
        return false;
    }
}

bool isIsolatedFault(const Diagnostic &d) {
    // RuntimeDiag values are the `specific` part of the code (severity bits included).
    return !d.persisted && d.type == DiagType::RuntimeDiag && d.severity == Severity::Error &&
           isIsolatableFault(static_cast<RuntimeDiag>(d.specific));
}

uint32_t faultIsolationDepth() { return tlsFaultIsolationDepth; }

FaultIsolationScope::FaultIsolationScope() { ++tlsFaultIsolationDepth; }
FaultIsolationScope::~FaultIsolationScope() { --tlsFaultIsolationDepth; }

InheritedFaultIsolation::InheritedFaultIsolation(uint32_t depth) : saved_(tlsFaultIsolationDepth) {
    tlsFaultIsolationDepth = depth;
}
InheritedFaultIsolation::~InheritedFaultIsolation() { tlsFaultIsolationDepth = saved_; }

DescribedFaultScope::DescribedFaultScope() { ++tlsDescribedFaultScopes; }
DescribedFaultScope::~DescribedFaultScope() { --tlsDescribedFaultScopes; }

} // namespace camel::core::error
