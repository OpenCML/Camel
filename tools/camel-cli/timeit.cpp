/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and
 * conditions of the MIT license. You may obtain a copy of
 * the MIT license at: [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT
 * WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 02, 2026
 * Updated: May. 03, 2026
 * Supported by: National Key Research and Development Program of China
 */

/**
 * Timeit mode implementation for camel-cli.
 *
 * Responsibilities:
 * - resolve the timed pass in the resolved pass list;
 * - rebuild a fresh compile/runtime state for each iteration;
 * - measure only the requested pass window;
 * - emit compact sample lines plus a single summary table.
 */

#include "timeit.h"

#include "camel/utils/windows_parser_guard.h"
#include "nlohmann/json.hpp"

#include "antlr4-runtime/antlr4-runtime.h"

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/listener.h"
#include "camel/core/mm.h"
#include "camel/core/module/userdef.h"
#include "camel/core/type.h"
#include "camel/execute/pass/base.h"
#include "camel/init.h"
#include "camel/parse/antlr/OpenCMLLexer.h"
#include "camel/parse/antlr/OpenCMLParser.h"
#include "camel/parse/ast/builder.h"
#include "camel/parse/cst_dumper.h"
#include "camel/parse/parse.h"
#include "camel/utils/install_layout.h"
#include "camel/utils/log.h"
#include "camel/utils/memperf.h"
#include "config.h"
#include "passes/trans/dot/graphviz.h"
#include "passes/trans/tns/topo_node_seq.h"
#include "service/codegen/source/generator.h"
#include "service/profiler/advanced/advanced_tracer.h"
#include "service/profiler/core/trace.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <queue>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif

namespace mm = camel::core::mm;
using namespace antlr4;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace std;
using namespace camel::core::error;
using namespace camel::parse;
namespace Run = CmdLineArgs::Run;

namespace fs = std::filesystem;

namespace {

struct RunInvocation {
    std::unique_ptr<istream> input;
    diagnostics_ptr_t diagnostics;
    context_ptr_t ctx;
    parser_ptr_t parser;
    std::shared_ptr<UserDefinedModule> mainModule;
    std::string entryDir;
};

class ScopedStreamRedirect {
  public:
    ScopedStreamRedirect(std::ostream &stream, std::streambuf *replacement)
        : stream_(stream), original_(stream.rdbuf(replacement)) {}

    ScopedStreamRedirect(const ScopedStreamRedirect &)            = delete;
    ScopedStreamRedirect &operator=(const ScopedStreamRedirect &) = delete;

    ~ScopedStreamRedirect() { restore(); }

    void restore() {
        if (original_ != nullptr) {
            stream_.rdbuf(original_);
            original_ = nullptr;
        }
    }

  private:
    std::ostream &stream_;
    std::streambuf *original_;
};

class ScopedFdRedirect {
  public:
    explicit ScopedFdRedirect(FILE *stream)
#ifdef _WIN32
        : fd_(_fileno(stream)) {
#else
        : fd_(fileno(stream)) {
#endif
        if (fd_ < 0) {
            return;
        }

#ifdef _WIN32
        int nullFd = -1;
        if (_sopen_s(&nullFd, "NUL", _O_WRONLY, _SH_DENYNO, _S_IREAD | _S_IWRITE) != 0 ||
            nullFd < 0) {
            return;
        }
        savedFd_ = _dup(fd_);
        if (savedFd_ < 0) {
            _close(nullFd);
            return;
        }
        fflush(stream);
        if (_dup2(nullFd, fd_) == 0) {
            active_ = true;
        }
        _close(nullFd);
#else
        const int nullFd = open("/dev/null", O_WRONLY);
        if (nullFd < 0) {
            return;
        }
        savedFd_ = dup(fd_);
        if (savedFd_ < 0) {
            close(nullFd);
            return;
        }
        fflush(stream);
        if (dup2(nullFd, fd_) >= 0) {
            active_ = true;
        }
        close(nullFd);
#endif
    }

    ScopedFdRedirect(const ScopedFdRedirect &)            = delete;
    ScopedFdRedirect &operator=(const ScopedFdRedirect &) = delete;

    ~ScopedFdRedirect() { restore(); }

    void restore() {
        if (!active_ || savedFd_ < 0 || fd_ < 0) {
            return;
        }
        fflush(nullptr);
#ifdef _WIN32
        (void)_dup2(savedFd_, fd_);
        _close(savedFd_);
#else
        (void)dup2(savedFd_, fd_);
        close(savedFd_);
#endif
        savedFd_ = -1;
        active_  = false;
    }

  private:
    int fd_{-1};
    int savedFd_{-1};
    bool active_{false};
};

struct TimeitSummary {
    std::vector<double> samplesMs;
    double meanMs   = 0.0;
    double ci95Ms   = 0.0;
    double minMs    = 0.0;
    double maxMs    = 0.0;
    double stddevMs = 0.0;
};

std::string resolveEntryDir(const std::string &inputPath) {
    return fs::absolute(fs::path(inputPath)).parent_path().string();
}

std::vector<std::string> buildSearchPathsForEntry(const std::string &entryDir) {
    auto searchPaths = camel::utils::buildModuleSearchPaths(
        entryDir,
        camel::utils::ModuleSearchPathOptions{.stdlibOverride = Run::stdLibPath});
    auto installRoot = camel::utils::resolveInstallRoot();
    std::size_t n    = 0;
    for (const auto &p : searchPaths) {
        if (!p.empty()) {
            ++n;
        }
    }
    const std::string stdlibNote =
        Run::stdLibPath.empty() ? "(CAMEL_STD_LIB unset, use default)" : Run::stdLibPath;
    LogInfoPathList(
        "Main",
        [&] {
            return std::format(
                "run | module paths | {} entries | CAMEL_HOME={} | entry_dir={} | stdlib={}",
                n,
                installRoot.string(),
                entryDir,
                stdlibNote);
        },
        searchPaths);
    return searchPaths;
}

RunInvocation makeRunInvocation(const std::string &inputPath) {
    RunInvocation invocation;
    auto file = std::make_unique<std::ifstream>(inputPath);
    if (!file->is_open()) {
        throw std::runtime_error(std::format("Error: Cannot open file {}", inputPath));
    }
    invocation.input = std::move(file);
    CAMEL_LOG_INFO_S("Main", "Reading from file '{}'.", inputPath);

    invocation.diagnostics = make_shared<Diagnostics>("main", inputPath);
    invocation.diagnostics->setConfig(
        DiagsConfig{
            .total_limit         = -1,
            .per_severity_limits = {{Severity::Error, 0}},
        });

    invocation.entryDir = resolveEntryDir(inputPath);
    auto searchPaths    = buildSearchPathsForEntry(invocation.entryDir);
    invocation.ctx      = Context::create(
        EntryConfig{
            .entryDir    = invocation.entryDir,
            .entryFile   = inputPath,
            .searchPaths = std::move(searchPaths),
        },
        DiagsConfig{
            .total_limit         = -1,
            .per_severity_limits = {{Severity::Error, 0}},
        });

    invocation.parser = std::make_shared<CamelParser>(invocation.diagnostics);
    invocation.mainModule =
        make_shared<UserDefinedModule>("main", inputPath, invocation.ctx, invocation.parser);
    invocation.ctx->setMainModule(invocation.mainModule);
    return invocation;
}

void emitRuntimeFailure(
    const context_ptr_t &ctx, bool useJsonFormat, std::ostream &os, std::string_view fallback) {
    const auto &diags = ctx->runtimeDiagSink();
    if (diags->hasErrors()) {
        diags->dump(os, useJsonFormat);
    } else {
        os << fallback << endl;
    }
}

std::optional<size_t> resolveTimedPassIndex(
    const std::vector<std::string> &passes, const std::string &timedPass, std::ostream &os) {
    std::optional<size_t> found;
    for (size_t i = 0; i < passes.size(); ++i) {
        if (passes[i] != timedPass) {
            continue;
        }
        if (found.has_value()) {
            os << "pass '" << timedPass
               << "' appears multiple times in the resolved pass list; timeit requires a unique "
                  "target."
               << endl;
            return std::nullopt;
        }
        found = i;
    }
    if (!found.has_value()) {
        os << "pass '" << timedPass << "' is not present in the resolved pass list." << endl;
    }
    return found;
}

TimeitSummary summarizeTimeit(const std::vector<double> &samplesMs) {
    TimeitSummary summary;
    summary.samplesMs = samplesMs;
    if (samplesMs.empty()) {
        return summary;
    }

    summary.meanMs      = std::accumulate(samplesMs.begin(), samplesMs.end(), 0.0) /
                          static_cast<double>(samplesMs.size());
    auto [minIt, maxIt] = std::minmax_element(samplesMs.begin(), samplesMs.end());
    summary.minMs       = *minIt;
    summary.maxMs       = *maxIt;

    if (samplesMs.size() == 1) {
        return summary;
    }

    double variance = 0.0;
    for (double sample : samplesMs) {
        const double delta = sample - summary.meanMs;
        variance += delta * delta;
    }
    variance /= static_cast<double>(samplesMs.size() - 1);
    summary.stddevMs = std::sqrt(variance);
    summary.ci95Ms   = 1.96 * (summary.stddevMs / std::sqrt(static_cast<double>(samplesMs.size())));
    return summary;
}

std::string formatTimeitCopyLine(const TimeitSummary &summary) {
    if (std::abs(summary.meanMs) >= 1000.0) {
        return std::format("{:.2f}  {:.2f} s", summary.meanMs / 1000.0, summary.ci95Ms / 1000.0);
    }
    return std::format("{:.2f}  {:.2f} ms", summary.meanMs, summary.ci95Ms);
}

std::string formatTimeitDuration(double valueMs) {
    if (std::abs(valueMs) >= 1000.0) {
        return std::format("{:.4f} s", valueMs / 1000.0);
    }
    return std::format("{:.4f} ms", valueMs);
}

void printTimeitSummaryTable(
    std::ostream &os, const std::string &passName, unsigned int iterations,
    const TimeitSummary &summary) {
    const std::vector<std::pair<std::string, std::string>> rows = {
        {"pass name", passName},
        {"iterations", std::to_string(iterations)},
        {"mean", formatTimeitDuration(summary.meanMs)},
        {"95% CI", formatTimeitDuration(summary.ci95Ms)},
        {"minimum", formatTimeitDuration(summary.minMs)},
        {"maximum", formatTimeitDuration(summary.maxMs)},
        {"copy text", formatTimeitCopyLine(summary)},
    };

    std::size_t metricWidth = std::string("metric").size();
    std::size_t valueWidth  = std::string("value").size();
    for (const auto &[metric, value] : rows) {
        metricWidth = std::max(metricWidth, metric.size());
        valueWidth  = std::max(valueWidth, value.size());
    }

    const std::string border =
        "+" + std::string(metricWidth + 2, '-') + "+" + std::string(valueWidth + 2, '-') + "+";
    os << border << '\n';
    os << "| " << std::left << std::setw(static_cast<int>(metricWidth)) << "metric"
       << " | " << std::left << std::setw(static_cast<int>(valueWidth)) << "value" << " |\n";
    os << border << '\n';
    for (const auto &[metric, value] : rows) {
        os << "| " << std::left << std::setw(static_cast<int>(metricWidth)) << metric << " | "
           << std::right << std::setw(static_cast<int>(valueWidth)) << value << " |\n";
    }
    os << border << '\n';
}

void printTimeitSummaryJson(
    std::ostream &os, const std::string &passName, unsigned int iterations,
    const TimeitSummary &summary) {
    nlohmann::json payload = {
        {"kind", "timeit"},
        {"pass", passName},
        {"iterations", iterations},
        {"samples_ms", summary.samplesMs},
        {"mean_ms", summary.meanMs},
        {"ci95_ms", summary.ci95Ms},
        {"min_ms", summary.minMs},
        {"max_ms", summary.maxMs},
        {"copy_text", formatTimeitCopyLine(summary)},
    };
    os << payload.dump(2) << '\n';
}

} // namespace

int runTimeitMode(std::ostream &os, bool useJsonFormat) {
    const auto timedIndexOpt = resolveTimedPassIndex(Run::resolvedPassList, Run::timeitPass, os);
    if (!timedIndexOpt.has_value()) {
        return 1;
    }

    const size_t timedIndex = *timedIndexOpt;
    const std::vector<std::string> prefixPasses(
        Run::resolvedPassList.begin(),
        Run::resolvedPassList.begin() + static_cast<std::ptrdiff_t>(timedIndex));
    const std::vector<std::string> suffixPasses(
        Run::resolvedPassList.begin() + static_cast<std::ptrdiff_t>(timedIndex + 1),
        Run::resolvedPassList.end());

    if (!useJsonFormat) {
        os << std::format(
            "timeit target={} iterations={}\n",
            Run::timeitPass,
            Run::timeitIterations);
    }

    std::vector<double> samplesMs;
    samplesMs.reserve(Run::timeitIterations);
    int lastRunExitCode         = 0;
    const std::string inputPath = Run::resolvedInputPath;

    for (unsigned int iteration = 0; iteration < Run::timeitIterations; ++iteration) {
        std::unique_ptr<RunInvocation> invocation;
        try {
            invocation = std::make_unique<RunInvocation>(makeRunInvocation(inputPath));
            invocation->parser->parse(*invocation->input);
            CAMEL_LOG_INFO_S("Main", "run | parse | done | {}", inputPath);

            invocation->mainModule->compile(CompileStage::Done);
            if (!invocation->mainModule->loaded()) {
                invocation->ctx->dumpAllModuleDiagnostics(os, useJsonFormat);
                return 1;
            }

            invocation->ctx->clearProcessExitCode();

            std::ostringstream execCapture;
            std::ostream &execOs = useJsonFormat ? static_cast<std::ostream &>(execCapture) : os;
            std::optional<ScopedStreamRedirect> coutRedirect;
            std::optional<ScopedStreamRedirect> cerrRedirect;
            std::optional<ScopedFdRedirect> stdoutFdRedirect;
            std::optional<ScopedFdRedirect> stderrFdRedirect;
            std::ostringstream redirectedStdout;
            std::ostringstream redirectedStderr;
            if (useJsonFormat) {
                coutRedirect.emplace(std::cout, redirectedStdout.rdbuf());
                cerrRedirect.emplace(std::cerr, redirectedStderr.rdbuf());
                stdoutFdRedirect.emplace(stdout);
                stderrFdRedirect.emplace(stderr);
            }
            auto *graph = invocation->ctx->runtimeRootGraph();
            try {
                if (!prefixPasses.empty()) {
                    auto prefixResult =
                        applyPassesDetailed(graph, prefixPasses, invocation->ctx, execOs);
                    graph = prefixResult.graph;
                    if (prefixResult.failed()) {
                        if (stdoutFdRedirect)
                            stdoutFdRedirect->restore();
                        if (stderrFdRedirect)
                            stderrFdRedirect->restore();
                        if (coutRedirect)
                            coutRedirect->restore();
                        if (cerrRedirect)
                            cerrRedirect->restore();
                        emitRuntimeFailure(
                            invocation->ctx,
                            useJsonFormat,
                            os,
                            "execution failed before the timed pass.");
                        return 1;
                    }
                    if (prefixResult.consumed()) {
                        os << "execution was consumed before timed pass '" << Run::timeitPass
                           << "' was reached." << endl;
                        return 1;
                    }
                }

                const auto startedAt = std::chrono::steady_clock::now();
                auto timedResult     = applyPassesDetailed(
                    graph,
                    std::vector<std::string>{Run::timeitPass},
                    invocation->ctx,
                    execOs);
                const auto endedAt = std::chrono::steady_clock::now();
                const double elapsedMs =
                    std::chrono::duration<double, std::milli>(endedAt - startedAt).count();

                if (timedResult.failed()) {
                    if (stdoutFdRedirect)
                        stdoutFdRedirect->restore();
                    if (stderrFdRedirect)
                        stderrFdRedirect->restore();
                    if (coutRedirect)
                        coutRedirect->restore();
                    if (cerrRedirect)
                        cerrRedirect->restore();
                    emitRuntimeFailure(
                        invocation->ctx,
                        useJsonFormat,
                        os,
                        "timed pass execution failed without diagnostics.");
                    return 1;
                }

                graph = timedResult.graph;
                if (!timedResult.consumed() && !suffixPasses.empty()) {
                    auto suffixResult =
                        applyPassesDetailed(graph, suffixPasses, invocation->ctx, execOs);
                    graph = suffixResult.graph;
                    if (suffixResult.failed()) {
                        if (stdoutFdRedirect)
                            stdoutFdRedirect->restore();
                        if (stderrFdRedirect)
                            stderrFdRedirect->restore();
                        if (coutRedirect)
                            coutRedirect->restore();
                        if (cerrRedirect)
                            cerrRedirect->restore();
                        emitRuntimeFailure(
                            invocation->ctx,
                            useJsonFormat,
                            os,
                            "execution failed after the timed pass.");
                        return 1;
                    }
                }

                if (graph != nullptr) {
                    auto fallbackResult =
                        applyPassesDetailed(graph, Run::fallbackPasses, invocation->ctx, execOs);
                    if (fallbackResult.failed()) {
                        if (stdoutFdRedirect)
                            stdoutFdRedirect->restore();
                        if (stderrFdRedirect)
                            stderrFdRedirect->restore();
                        if (coutRedirect)
                            coutRedirect->restore();
                        if (cerrRedirect)
                            cerrRedirect->restore();
                        emitRuntimeFailure(
                            invocation->ctx,
                            useJsonFormat,
                            os,
                            "fallback execution failed after the timed pass.");
                        return 1;
                    }
                }

                if (stdoutFdRedirect)
                    stdoutFdRedirect->restore();
                if (stderrFdRedirect)
                    stderrFdRedirect->restore();
                if (coutRedirect)
                    coutRedirect->restore();
                if (cerrRedirect)
                    cerrRedirect->restore();
                samplesMs.push_back(elapsedMs);
                if (!useJsonFormat) {
                    os << std::format(
                        "[timeit] {}/{} pass={} elapsed={:.4f} ms\n",
                        iteration + 1,
                        Run::timeitIterations,
                        Run::timeitPass,
                        elapsedMs);
                }
            } catch (Diagnostic &d) {
                if (stdoutFdRedirect)
                    stdoutFdRedirect->restore();
                if (stderrFdRedirect)
                    stderrFdRedirect->restore();
                if (coutRedirect)
                    coutRedirect->restore();
                if (cerrRedirect)
                    cerrRedirect->restore();
                if (!d.persisted) {
                    invocation->ctx->runtimeDiagSink()->add(std::move(d));
                }
                emitRuntimeFailure(
                    invocation->ctx,
                    useJsonFormat,
                    os,
                    "timed execution raised a diagnostic without details.");
                return 1;
            }

            lastRunExitCode = invocation->ctx->processExitCodeOr(0);
        } catch (DiagnosticsLimitExceededBaseException &) {
            if (invocation && invocation->ctx) {
                invocation->ctx->dumpAllModuleDiagnostics(os, useJsonFormat);
            }
            return 1;
        } catch (Diagnostic &d) {
            if (invocation && invocation->parser) {
                RangeConverter conv(invocation->parser->getTokens());
                d.fetchRange(conv);
            }
            os << "Uncaught diagnostic: " << (useJsonFormat ? d.toJson() : d.toText()) << endl;
            return 1;
        } catch (exception &e) {
            os << e.what() << endl;
            ASSERT(false, e.what());
            return 1;
        } catch (...) {
            os << "Unknown error occurred." << endl;
            ASSERT(false, "Unknown error occurred.");
            return 1;
        }
    }

    const auto summary = summarizeTimeit(samplesMs);
    if (useJsonFormat) {
        printTimeitSummaryJson(os, Run::timeitPass, Run::timeitIterations, summary);
    } else {
        printTimeitSummaryTable(os, Run::timeitPass, Run::timeitIterations, summary);
    }
    return lastRunExitCode;
}
