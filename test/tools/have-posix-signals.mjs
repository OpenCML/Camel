#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Precondition of the tests that assert how a run reports a process the kernel ended with a signal
// (`skip_unless = ["node", "test/tools/have-posix-signals.mjs"]`).
//
// A signal is a POSIX notion: `process.kill(pid, 'SIGSEGV')` raises one on Linux and macOS, and the
// runner reads the end as `got signal SIGSEGV (rc=139, ...)` with the shell's 128 + 11. On Windows
// there is no SIGSEGV to raise -- `process.kill` throws `kill ENOSYS` -- so the same crash helper
// ends as an ordinary process (a nonzero exit code), and both the signal line and the summary's
// `killed by a signal` count are not readings of anything on that host. An entry whose subject is
// that reporting has nothing to assert there; it declares the precondition and is skipped.
//
// Exit 0 where a process can be ended by a signal, 1 with the reason elsewhere.

if (process.platform === 'win32') {
    console.log(
        `Windows has no POSIX signals: process.kill(pid, 'SIGSEGV') throws kill ENOSYS, and ` +
            `128 + 11 is not a Windows exit code`
    )
    process.exit(1)
}
process.exit(0)