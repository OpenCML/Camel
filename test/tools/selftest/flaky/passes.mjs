// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// A fixture case of the runner's self-test: it passes every time. It is the control for an entry
// that carries `flaky` -- the field must not turn a case that was green when it ran into anything
// but a plain `[PASS]`, which is the fault `known_fail` has (a mark that passes is reported as a
// mark that is out of date).
//
// `process.exitCode` rather than `process.exit`: the runner reads this child's stdout through a
// pipe, and a process that exits immediately after `console.log` can leave the write queued and
// arrive with nothing (measured here: exit 0 and an empty capture from this same fixture).
console.log('ok')