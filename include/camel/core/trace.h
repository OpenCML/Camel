// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The run's tracing: which areas of the compiler print their step-by-step readings, and where they
 * print them.
 *
 * A program's stdout belongs to the program, and the readings a compiler writes about its own work
 * belong on the diagnostic side -- but only when someone asked. Until 2026-11-16 they were
 * unconditional: a program that differentiates a small model wrote 1988 `TRANSPOSE`/`REINTERP` lines
 * on a *default* run, one per operator per step, so debugging output had entered the product path and
 * the program's own four numbers were lost in it (author's ruling of 2026-11-16).
 *
 * The asking is the command line: `--trace transforms`, and several areas with commas
 * (`--trace transforms,plan`). It is not an environment variable -- a run's behaviour is decided by
 * the program, the command line and the build options (design principles 9.3), and
 * `test/tools/check-env-switches.mjs` keeps the environment from growing switches -- and it is not a
 * pass parameter either, measured: two entries that pin a reading write `args = ["${case}"]` and
 * name no pass at all (the `std::nvm` fallback), so a parameter has nowhere to hang.
 *
 * **The stream and the silence live here, once.** An area registers its name and nothing else
 * (`traceAreas`); every site asks `trace(area)` for a stream and writes the text it always wrote.
 * No prefix is added, because a reading that a plan pins has to stay byte-for-byte what it was: the
 * one thing this file owns is whether the bytes are written at all.
 *
 * The four areas and what they carry:
 *
 *   transforms   the derivative walk: `TRANSPOSE ...`, `REINTERP ...`, `SPLIT ...`, `JVP ...`
 *   plan         the plan's reference sites (`PLAN_REF_SITES`)
 *   effects      the effect-containment summary
 *   fold         the folding pass's debug counters
 *
 * A refusal is not a reading and does not go through here: `modules/transforms/readings.h#verdicts`
 * keeps the sentence for a refused transform on stderr always, because a reader who never heard of
 * `--trace` still has to be able to read why their derivative was refused.
 *
 * The definitions are in `src/core/trace.cpp` and not inline here on purpose: the switch is read by
 * code in `libcamel` and written by code in `camel.exe`, and a function-local `static` in a header
 * gives each module its own copy -- the first shape of this file did exactly that and the switch set
 * on the command line never reached a transform.
 */

#pragma once

#if defined(_WIN32) && !defined(CAMEL_DLL_EXPORTS)
#define CAMEL_TRACE_API __declspec(dllimport)
#elif defined(_WIN32)
#define CAMEL_TRACE_API __declspec(dllexport)
#else
#define CAMEL_TRACE_API
#endif

#include <cstddef>
#include <ostream>
#include <string>
#include <string_view>

namespace camel::core {

/// The areas a run can trace. Adding one is adding a row to the registry and a call site; the
/// enumeration, the table and the switch are the same thing.
enum class TraceArea : unsigned {
    Transforms = 0,
    Plan,
    Effects,
    Fold,
};

struct TraceAreaEntry {
    const char *name;
    TraceArea area;
};

/// The registry: one row per area, its name and its area. Nothing else registers a reading channel.
CAMEL_TRACE_API const TraceAreaEntry *traceAreas(std::size_t &count);

/// Whether this run asked for one area.
CAMEL_TRACE_API bool traceEnabled(TraceArea area);

/// The area names in the order the registry lists them, for a message that has to name them all.
CAMEL_TRACE_API std::string traceAreaList();

/// Reads one `--trace` value: area names separated by commas. Turns each on and answers the first
/// name that is not in the registry (empty when every name was taken), so the command line can
/// refuse the run instead of silently tracing nothing.
CAMEL_TRACE_API std::string enableTraceAreas(std::string_view spec);

/// The one place a reading's stream is decided. Never `std::cout`: the program's output is the
/// program's. `std::cerr` when the run asked for this area, a discarded stream otherwise, so every
/// call site stays one expression and no site has to know the switch exists.
CAMEL_TRACE_API std::ostream &trace(TraceArea area);

} // namespace camel::core