// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * What a run did, per operator, per step and per device (`std::profile`, design
 * docs/design/gpu/README.cn.md 5.6).
 *
 * The session is off unless the pipeline names `std::profile`: every hook below is one call
 * that reads a flag and returns, and the back ends pay it at the places where the answer is
 * counted anyway (one per loop round, one per device kernel, one per transfer). Nothing here
 * is compiled out in a release build: a pass that is not on the command line costs nothing,
 * and one that is works in every build.
 *
 * What is recorded, and where from:
 *
 *   - operators: the CPU time inside an operator call, per operator, through the timing thunks
 *     of profile/operators.h (a scheduler hands out a thunk in place of the operator it resolved
 *     while the session is on). One entry per URI, with its calls and its total time. The same
 *     row says how much of that time was inside the Camel allocators, how many allocations the
 *     call made, how many were data blocks and how many payload bytes they asked for
 *     (profile/allocation.h): an operator boundary alone cannot separate a kernel's arithmetic
 *     from the allocation and packing it does, and this column does the allocation part.
 *   - steps: a step is one round of a loop of the program (`loopRound`), which every back end
 *     reports from camel/execute/loop_frame.h: nvm, fvm, jit and the macro evaluator share
 *     `loopEnd`. The report reads the numbers per round of every loop that ran, and picks the
 *     loop that carries the most operators per round as the step loop of the program (the
 *     training loop of a training step; a loop nested in it has fewer operators per round). The
 *     step also says how many allocations one round made and what they cost.
 *   - fusion groups: the one pass a back end ran for a group of nodes (camel/execute/
 *     fusion_group.h) is not an operator call, so it gets a row of its own: the attribute that
 *     names the group, its members' operators in execution order, how many times it ran and what
 *     its kernel cost. Its members ran no operator, so they have no operator row; the row says
 *     how many operator executions each run stood for. The group row's executions plus the
 *     operator rows' calls are the program's operator executions: each one is counted once.
 *   - devices: one kernel launch, one host/device transfer and one synchronisation, recorded
 *     by the module that does them (tiles today: measures/kernels, uploads/downloads, waits).
 *   - compilation: the phase recorder of camel/utils/phase_timer.h (parse, GCT, GIR, every pass,
 *     the run), which the driver turns on when the pipeline names `std::profile`; a module that
 *     compiles on the device at run time (NVRTC) reports its own compile time with
 *     `deviceCompile`.
 *
 * The counters are atomics: a parallel scheduler records from its workers too. Step boundaries
 * are only taken on the thread that enabled the session (the driver's), which is where the
 * rounds of the program's own loops run.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <iosfwd>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

namespace camel::profile {

#ifdef CAMEL_DLL_EXPORTS
namespace detail {
/// The flag enabled() reads. Only the core's own translation units see it: libcamel exports
/// functions, not variables, so a module (and the tool) keeps calling the function.
extern std::atomic<bool> enabledFlag;
} // namespace detail
#endif

/// Whether the session is recording. Out of line on purpose: libcamel is a DLL on Windows,
/// which exports functions but not variables. One call, no branch in the caller's hot path.
bool enabled() noexcept;

/// `enabled()` for the core's own hot paths, where the call is measurable: an interpreter asks
/// once a round of a loop (camel/execute/loop_frame.h). Inside libcamel it is the flag itself;
/// anywhere else it is the exported call.
inline bool enabledInline() noexcept {
#ifdef CAMEL_DLL_EXPORTS
    return detail::enabledFlag.load(std::memory_order_relaxed);
#else
    return enabled();
#endif
}

/// Starts recording (the driver's thread). Idempotent; `reset` empties what was recorded.
void enable();
void reset();

/// Records one round of the loop written at `origin` (one step of a program whose step loop is
/// that loop). Called by camel/execute/loop_frame.h from every back end.
void loopRound(uint64_t origin) noexcept;

/// Records one execution of a fusion group (camel/execute/fusion_group.h): `key` is the
/// attribute that names it and `name` its value (the name the module registered), `members` the
/// operators of the nodes it computed in execution order (the tail last), and `ns` what its
/// kernel cost. Called once per run of a group by the registry that ran it.
///
/// A group's name identifies one kernel and one shape: the first execution fixes the member list
/// a row shows, and later executions pass the same list. Its members ran no operator, so they get
/// no operator row; `runs * members.size()` is how many operator executions the group stood for,
/// and the report counts them exactly once.
void fusionGroup(
    std::string_view key, std::string_view name,
    std::span<const std::string_view> members, uint64_t ns);

/// The marks of one execution plan, when it is built: how many update nodes it decided reuse the
/// storage of the value they are given (layer 1), how many it left to the array's own count
/// (layer 2), and how many it decided copy (layer 3) (docs/design/arrays-memory/README.cn.md
/// 4.3.1). Every plan of the run reports its own marks; a plan built and never run counts too.
void planUpdateMarks(size_t reuseInput, size_t byArrayCount, size_t copy) noexcept;
/// An update operator asked the array itself whether its storage may be written where it is
/// (Array::canWriteInPlace) and this is the answer: true when the only reference to a block the
/// array owns is its own, so the update changed it there, false when the update copied. Asked at
/// every update an in-place form runs.
void arrayCountAnswer(bool inPlace) noexcept;

/// One device kernel ran: `label` names it (the module's own label, "launch"), `ns` is what the
/// device spent on it when the caller measured that (0 otherwise).
void kernelLaunch(std::string_view label, uint64_t ns = 0);
/// `bytes` were moved between the host and a device; `toDevice` says which way.
void transferToDevice(uint64_t bytes);
void transferToHost(uint64_t bytes);
/// The host waited for a device (`what`: "stream", "event", "download", ...).
void synchronise(std::string_view what);
/// A module compiled code on the device at run time (NVRTC): `what` is the unit, `ns` the time.
void deviceCompile(std::string_view what, uint64_t ns);

/// The back end that ran the program, as the report shows it (`std::fvm`).
void setBackend(std::string backend);
/// The device the back end ran on ("cpu", "cuda:0"); `cpu` when nothing said otherwise.
void setDevice(std::string device);

/// Why a loop over a tensor in the program stays on the host: the pass writes one of these per
/// LOOP node it finds (the place is the origin of the node, the reason names the operators and
/// what kept them off the device).
struct HostLoopHint {
    uint64_t origin = 0;
    std::string graph;
    /// The place in the source ("test/cases/x.cml:12:9"), when the origin resolves to one.
    std::string position;
    /// The operators of the body, in source order, comma separated ("tensor:add, tensor:mul").
    std::string operators;
    /// True when the body is a pure element-wise chain: one kernel could do the whole round.
    bool elementwise = false;
    /// The rounds make a tensor element by element (the sequence is a tensor's elements).
    bool tensor = false;
    /// Why it did not go to a device, as a sentence.
    std::string reason;
};
void addHostLoopHint(HostLoopHint hint);

/// Where the loop written at `origin` is in the source: the pass notes one of these per LOOP
/// node, so the report of a step says the file and the line rather than an origin number.
void noteLoopPosition(uint64_t origin, std::string position);

/// What the report writes.
struct ReportOptions {
    /// Rows of the operator table; 0 writes every operator that ran.
    size_t top = 24;
    /// Report the element-wise loops that ran on the host.
    bool hints = true;
    /// Print the text report when the run ends. False for a pass that only turns on a diagnostic
    /// (an effect trace, the allocator's statistics) and wants no report on the program's output.
    bool report = true;
};

/// What the pass was written with (`std::profile top=... hints=... json="..."`): the driver
/// reads it when the run ends, which is when the report is written.
void setReportOptions(const ReportOptions &options);
ReportOptions reportOptions();
/// A file the JSON report is also written to, empty for none.
void setJsonPath(std::string path);
std::string jsonPath();

/// Writes the text report (what `std::profile` prints) to `os`, and stops recording.
void reportText(std::ostream &os, const ReportOptions &options);
/// The same report as one JSON object, without stopping the recording (for tests and tools).
std::string reportJson(const ReportOptions &options);

/// The measured cost of the timing itself: nanoseconds per recorded operator call. Filled once,
/// at the first report, by timing the timestamp pair the thunks use.
double measurementOverheadNs();

} // namespace camel::profile