// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Fixture of test/tools/selftest/run.mjs, for the `known_fail` gate. It fails on purpose, so the one
// plan in this directory that carries the mark together with its owner and its reason reports
// `[XFAIL]` instead of going green. The sentence it prints is the wall that plan's
// `known_fail_reason` quotes.
console.error('WALL fixture=knownfail reason=the fixture fails on purpose')
process.exit(1)