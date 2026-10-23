// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Fixture of test/tools/selftest/load-class/run.mjs: a test whose bound is today's wall time.
// It sleeps 400 ms and declares a 1000 ms timeout, so it uses more than a quarter of its bound
// on any machine and the runner must fail it for the missing margin, not for being slow.
const sleep = Number(process.argv[2] ?? 400)
setTimeout(() => process.exit(0), sleep)