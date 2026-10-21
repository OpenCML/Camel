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
 * Updated: Oct. 19, 2026
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

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/core/module/userdef.h"
#include "camel/core/type.h"
#include "camel/execute/entry_args.h"
#include "camel/execute/pass/base.h"
#include "camel/init.h"
#include "camel/parse/ast/builder.h"
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
#include <functional>
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
    return camel::utils::pathToUtf8(
        fs::absolute(camel::utils::pathFromUtf8(inputPath)).parent_path());
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
                camel::utils::pathToUtf8(installRoot),
                entryDir,
                stdlibNote);
        },
        searchPaths);
    return searchPaths;
}

RunInvocation makeRunInvocation(const std::string &inputPath) {
    RunInvocation invocation;
    auto file = std::make_unique<std::ifstream>(camel::utils::pathFromUtf8(inputPath));
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
            .entryDir          = invocation.entryDir,
            .entryFile         = inputPath,
            .searchPaths       = std::move(searchPaths),
            .maxRecursionDepth = Run::maxRecursionDepth,
            .maxMacroDepth     = Run::maxMacroDepth,
            .maxMacroRounds    = Run::maxMacroRounds,
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

/// Writes the diagnostics of a failed iteration; returns whether it wrote any.
using TimeitDiagnosticsEmitter = std::function<bool(std::ostream &, bool)>;

/// The report of one failed `--timeit` iteration. Text mode writes the `[timeit] i/N` line the
/// completed iterations write, with `failed` in place of the time, then the diagnostics; the
/// number is the one the failures are counted by. JSON mode writes one timeit object that names
/// the 1-based `iteration` that failed (before H16 item 6 a failed run said nothing about it) and
/// keeps the samples of the iterations that did complete, so a reader can tell how far the run got
/// and what ended it. `emitDiagnostics` returns false when there is nothing to write; `fallback`
/// is the sentence used then.
void reportTimeitIterationFailure(
    std::ostream &os, bool useJsonFormat, unsigned int iteration,
    const std::vector<double> &samplesMs, std::string_view fallback,
    const TimeitDiagnosticsEmitter &emitDiagnostics) {
    if (!useJsonFormat) {
        os << std::format(
            "[timeit] {}/{} pass={} failed\n",
            iteration,
            Run::timeitIterations,
            Run::timeitPass);
        if (!emitDiagnostics(os, false) && !fallback.empty()) {
            os << fallback << '\n';
        }
        return;
    }

    os << "{\n"
       << "  \"kind\": \"timeit\",\n"
       << "  \"failed\": true,\n"
       << "  \"iteration\": " << iteration << ",\n"
       << "  \"iterations\": " << Run::timeitIterations << ",\n"
       << "  \"pass\": " << nlohmann::json(Run::timeitPass).dump() << ",\n"
       << "  \"samples_ms\": " << nlohmann::json(samplesMs).dump();
    if (!fallback.empty()) {
        os << ",\n  \"failure\": " << nlohmann::json(std::string(fallback)).dump();
    }
    os << ",\n  \"diagnostics\": [";
    emitDiagnostics(os, true);
    os << "]\n}\n";
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
                reportTimeitIterationFailure(
                    os,
                    useJsonFormat,
                    iteration + 1,
                    samplesMs,
                    "",
                    [ctx = invocation->ctx](std::ostream &out, bool json) {
                        ctx->dumpAllModuleDiagnostics(out, json, !json);
                        return true;
                    });
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
            auto restoreOutput = [&]() {
                if (stdoutFdRedirect)
                    stdoutFdRedirect->restore();
                if (stderrFdRedirect)
                    stderrFdRedirect->restore();
                if (coutRedirect)
                    coutRedirect->restore();
                if (cerrRedirect)
                    cerrRedirect->restore();
            };
            // The runtime diagnostics of this iteration, for the failure report. A failed pass
            // always has at least one; the report's fallback sentence covers the paths that do
            // not (the timed pass consumed no graph, an exception without a diagnostic, ...).
            const TimeitDiagnosticsEmitter runtimeDiagnostics =
                [diags = invocation->ctx->runtimeDiagSink()](std::ostream &out, bool json) {
                    if (!diags->hasErrors()) {
                        return false;
                    }
                    diags->dump(out, json, !json);
                    return true;
                };
            auto failIteration = [&](std::string_view fallback) {
                restoreOutput();
                reportTimeitIterationFailure(
                    os,
                    useJsonFormat,
                    iteration + 1,
                    samplesMs,
                    fallback,
                    runtimeDiagnostics);
                return 1;
            };
            const camel::runtime::Program *program = invocation->ctx->runtimeProgram();
            try {
                // The list is run in three parts; all names are checked before the first.
                requireKnownPasses(Run::resolvedPassList, invocation->ctx);
                // The values given for the parameters of `main` (--arg), as in a plain run.
                program = camel::execute::bindEntryArgumentSpecs(
                    invocation->ctx,
                    program,
                    Run::entryArgs);
                if (program == nullptr) {
                    return failIteration("the arguments of main could not be bound.");
                }
                if (!prefixPasses.empty()) {
                    auto prefixResult =
                        applyPassesDetailed(program, prefixPasses, invocation->ctx, execOs);
                    program = prefixResult.program;
                    if (prefixResult.failed()) {
                        return failIteration("execution failed before the timed pass.");
                    }
                    if (prefixResult.consumed()) {
                        return failIteration(std::format(
                            "execution was consumed before timed pass '{}' was reached.",
                            Run::timeitPass));
                    }
                }

                const auto startedAt = std::chrono::steady_clock::now();
                auto timedResult     = applyPassesDetailed(
                    program,
                    std::vector<std::string>{Run::timeitPass},
                    invocation->ctx,
                    execOs);
                const auto endedAt = std::chrono::steady_clock::now();
                const double elapsedMs =
                    std::chrono::duration<double, std::milli>(endedAt - startedAt).count();

                if (timedResult.failed()) {
                    return failIteration("timed pass execution failed without diagnostics.");
                }

                program = timedResult.program;
                if (!timedResult.consumed() && !suffixPasses.empty()) {
                    auto suffixResult =
                        applyPassesDetailed(program, suffixPasses, invocation->ctx, execOs);
                    program = suffixResult.program;
                    if (suffixResult.failed()) {
                        return failIteration("execution failed after the timed pass.");
                    }
                }

                if (program != nullptr) {
                    auto fallbackResult =
                        applyPassesDetailed(program, Run::fallbackPasses, invocation->ctx, execOs);
                    if (fallbackResult.failed()) {
                        return failIteration("fallback execution failed after the timed pass.");
                    }
                }

                restoreOutput();
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
                restoreOutput();
                if (!d.persisted) {
                    invocation->ctx->runtimeDiagSink()->add(std::move(d));
                }
                return failIteration("timed execution raised a diagnostic without details.");
            }

            lastRunExitCode = invocation->ctx->processExitCodeOr(0);
        } catch (DiagnosticsLimitExceededBaseException &) {
            // The diagnostic that ended the run is a runtime one when a pass or the binding of
            // main's arguments reported it; the module diagnostics otherwise.
            const TimeitDiagnosticsEmitter diagnostics =
                invocation && invocation->ctx
                    ? TimeitDiagnosticsEmitter([ctx = invocation->ctx](
                                                   std::ostream &out, bool json) {
                          if (const auto &diags = ctx->runtimeDiagSink(); diags->hasErrors()) {
                              diags->dump(out, json, !json);
                          } else {
                              ctx->dumpAllModuleDiagnostics(out, json, !json);
                          }
                          return true;
                      })
                    : TimeitDiagnosticsEmitter([](std::ostream &, bool) { return false; });
            reportTimeitIterationFailure(
                os, useJsonFormat, iteration + 1, samplesMs, "", diagnostics);
            return 1;
        } catch (Diagnostic &d) {
            if (invocation && invocation->parser) {
                RangeConverter conv(invocation->parser->tokenPositions());
                d.fetchRange(conv);
            }
            reportTimeitIterationFailure(
                os,
                useJsonFormat,
                iteration + 1,
                samplesMs,
                "uncaught diagnostic",
                [&d](std::ostream &out, bool json) {
                    out << (json ? d.toJson() : d.toText());
                    if (!json) {
                        out << '\n';
                    }
                    return true;
                });
            return 1;
        } catch (exception &e) {
            reportTimeitIterationFailure(
                os,
                useJsonFormat,
                iteration + 1,
                samplesMs,
                e.what(),
                [](std::ostream &, bool) { return false; });
            ASSERT(false, e.what());
            return 1;
        } catch (...) {
            reportTimeitIterationFailure(
                os,
                useJsonFormat,
                iteration + 1,
                samplesMs,
                "Unknown error occurred.",
                [](std::ostream &, bool) { return false; });
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
