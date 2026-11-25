// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Fixture of test/tools/selftest/run.mjs, for the strict half of a mark: it prints the line its plan
// says arrives when the wall moves, and it still exits 1. A mark whose ending has arrived has to be
// reported as a failure and told to be deleted -- excused for another day it would be a green nobody
// ever has to look at again, which is the debt the STALE snippet entries carried.
console.log('ARRIVED fixture=knownfail the numbers are 2, 4 and 6')
console.error('WALL fixture=knownfail reason=the fixture fails on purpose')
process.exit(1)