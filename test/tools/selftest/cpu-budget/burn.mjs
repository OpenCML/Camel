// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Fixture of test/tools/selftest/load-class/run.mjs: a test that works instead of waiting. It
// burns the number of milliseconds of **its own CPU time** given as its first argument, so a test
// that declares a `cpu_budget_ms` below that figure must be failed by the runner's CPU judgement
// -- a budget that no amount of work can cross would be examining nothing.
//
// The amount is consumed CPU time (user + system), not wall-clock time, and deliberately so: a
// wall-clock loop accumulates less and less CPU the busier the host is, so the fixture would let
// the machine's load decide whether it crosses its budget -- exactly the flakiness a CPU budget
// exists to remove (measured: 600 ms of wall clock on a loaded host charged the process 200 ms of
// CPU, which is under the 300 ms budget this fixture must cross). Consumed CPU time does not
// depend on the load.
const burnMs = Number(process.argv[2] ?? 600)
const consumedMs = () => {
    const usage = process.cpuUsage()
    return (usage.user + usage.system) / 1000
}
const started = consumedMs()
let sink = 0
while (consumedMs() - started < burnMs) {
    for (let index = 0; index < 100000; index++) sink += Math.sqrt(index)
}
// Keep the loop's result observable, so no engine can fold the work away.
if (!Number.isFinite(sink)) process.exit(3)