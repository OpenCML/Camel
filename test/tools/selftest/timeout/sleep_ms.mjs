// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// A fixture case of the runner's self-test: it waits for the number of milliseconds its first
// argument names and then prints what it did. The bound belongs to the plan, not to this file, so
// one case produces different verdicts at different bounds -- which is what makes the second
// attempt of a timed-out case observable.
//
// A sleep spends no CPU, so an attempt that finishes leaves the timeout-margin rule (a passing
// case may use at most a quarter of its bound in its own CPU time) far below its line.
const ms = Number(process.argv[2] || 0)
setTimeout(() => {
    console.log(`waited ${ms} ms`)
    process.exit(0)
}, ms)