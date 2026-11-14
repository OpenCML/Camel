// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Fixture of test/tools/selftest/run.mjs: the `skip_unless` probe of signal.plan.toml, asked from
// both sides so that one script answers both entries.
//
// It answers a fact about the host, not about the code: does this host report a death by signal at
// all? A POSIX host ends a child with `signal = 'SIGABRT'` and no exit code, and Windows has no
// signal to report -- the same abort ends the child with exit status 134 (128 + the POSIX number
// for SIGABRT) and `signal = null`. The plan therefore states the abort twice, once in each
// vocabulary, and this probe says which of the two a run on this host reads and which it skips.
//
// `reports-signals` (the default) exits 0 where a signal death is reported; `no-signals` exits 0
// where it is not. The line printed is the skip reason the runner shows, so it names the host's
// answer rather than only the command that asked.
const word = process.argv[2] || 'reports-signals'
const wantsSignals = word === 'reports-signals'
const hostReportsSignals = process.platform !== 'win32'
const answer = hostReportsSignals
    ? 'this host reports deaths by signal (the abort is SIGABRT, with no exit status)'
    : 'this host has no signal deaths to report (the abort is exit status 134, with no signal)'
if (wantsSignals === hostReportsSignals) {
    process.exit(0)
}
console.log(answer)
process.exit(1)