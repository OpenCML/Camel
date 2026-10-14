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
 * Created: Oct. 13, 2026
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Phase timing of one compiler run: where the wall-clock time and the heap allocations of
 * `camel x.cml <passes>` go, phase by phase (start-up, parse, GCT, GIR, each pass, teardown).
 *
 * Off by default. It is switched on by `camel --time-phases` (standard error),
 * `camel --time-phases=<file>` or by the environment variable CAMEL_TIME_PHASES=<file>
 * (`-` is standard error), and the report is one JSON
 * object (see docs/technical/38_compile_pipeline_cost.md for the schema and for how
 * benchmarks/compile/ uses it).
 *
 * Cost when off: a Scope is one load of a global flag and one branch at each end, and scopes
 * are only placed around whole phases (a module, a pass), never inside a loop over nodes.
 *
 * A phase is a named interval; phases nest (the GIR phase of a module contains the phases of
 * the modules it imports) and repeat (one "gct" per module). Each one records its start, its
 * duration and, when the counting allocator shim (test/tools/allocshim) is preloaded, the
 * number and bytes of heap allocations made while it was open. The shim is found by symbol
 * at run time; without it the allocation fields are absent from the report.
 *
 * The recorder is process-wide and meant for the thread that drives the compilation: scopes
 * opened on other threads are ignored (their nesting could not be told apart).
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace camel::utils::phase {

namespace detail {
/// Whether phases are being recorded. Read on every Scope; written once, before the first.
extern bool gEnabled;
void begin(std::string_view name, std::string_view detail);
void end();
} // namespace detail

/// Whether phases are being recorded.
inline bool enabled() { return detail::gEnabled; }

/// Starts recording; the report goes to `path` (`-`: standard error) when report() is called.
/// The time origin of the report is this call. Calling it again only replaces the path.
void enable(const std::string &path);

/// Starts recording when the environment variable CAMEL_TIME_PHASES names a path.
void enableFromEnvironment();

/// One phase, open for the lifetime of the object. `name` is the kind of phase ("gct",
/// "pass"), `detail` what it was applied to (a module name, a pass name); the report keeps
/// them apart so that phases can be summed by kind.
class Scope {
  public:
    explicit Scope(std::string_view name, std::string_view detail = {}) : open_(enabled()) {
        if (open_) {
            detail::begin(name, detail);
        }
    }
    ~Scope() {
        if (open_) {
            detail::end();
        }
    }

    Scope(const Scope &)            = delete;
    Scope &operator=(const Scope &) = delete;

  private:
    bool open_;
};

/// Opens a phase that is closed by a later call of closeOpen() instead of by a scope: for an
/// interval that starts where a destructor runs (the teardown of a run). No-op when off.
void open(std::string_view name, std::string_view detail = {});
/// Closes the phase opened last, if any is open.
void closeOpen();

/// Adds `value` to the counter `name` of the report (object counts: "gct.nodes", ...). The
/// counters are for quantities that are free to obtain or are only computed under enabled().
void count(std::string_view name, uint64_t value);

/// Writes the report and stops recording. Phases still open are closed first. No-op when off.
void report();

/// The report as JSON, without stopping: what report() writes. For tests.
std::string toJson();

} // namespace camel::utils::phase
