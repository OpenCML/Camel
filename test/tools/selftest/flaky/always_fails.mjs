// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// A fixture case of the runner's self-test: it fails every time, on both attempts. It is what makes
// the `flaky` field's other half observable -- a case that is re-run and fails again is a red like
// any other, and a mechanism that excused it would be a mechanism that accepts a real regression.
//
// `process.exitCode` rather than `process.exit`: the runner reads this child's stdout through a
// pipe, and exiting right after `console.log` can drop the write (and, measured here, the exit code
// with it -- the same fixture arrived as exit 0 with nothing on stdout).
console.log('this case fails every time')
process.exitCode = 1