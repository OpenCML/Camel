// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Fixture of test/tools/selftest/load-class/run.mjs: a test that works instead of waiting. It
// burns CPU for the milliseconds given as its first argument, so a test that declares a
// `cpu_budget_ms` below that figure must be failed by the runner's CPU judgement -- a budget that
// no amount of work can cross would be examining nothing.
const burnMs = Number(process.argv[2] ?? 600)
const started = performance.now()
let sink = 0
while (performance.now() - started < burnMs) {
    for (let index = 0; index < 100000; index++) sink += Math.sqrt(index)
}
// Keep the loop's result observable, so no engine can fold the work away.
if (!Number.isFinite(sink)) process.exit(3)