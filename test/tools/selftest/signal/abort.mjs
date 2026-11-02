// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Fixture of test/tools/selftest/run.mjs: a process that dies of SIGABRT, so that a plan can expect
// it. A real death of a real process, not a made-up exit code -- that is the case `expect_signal`
// exists to record, and the one `expect_exit` cannot: a killed process has no exit code.
process.abort()