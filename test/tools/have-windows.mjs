#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Precondition of the self-checks of the Windows-only tools
// (`skip_unless = ["node", "test/tools/have-windows.mjs"]`).
//
// Three of `internal/tools/*.test.ps1` measure things only Windows has, and each one has its own
// dependency. This probe answers for the host being Windows, which covers all three, and that is an
// approximation worth stating rather than a proof:
//
//   `git-bash.test.ps1`        needs `$env:SystemRoot`: it looks for Git for Windows' bash under the
//                              system directory and for the WSL launcher it must never pick, and
//                              `Join-Path` against an unset `SystemRoot` fails before any check runs.
//   `camel-remote-fetch.test.ps1`  names drive-letter paths (`C:\...`) among the spellings it accepts
//                              and refuses; a host without drive letters has nothing to answer.
//   `run-registry.test.ps1`    makes "a reader holds the file" with `FileShare::None`, which is the
//                              exclusive-open of Windows; POSIX has no such sharing mode, so the four
//                              checks of that state cannot hold there.
//
// A PowerShell exists on the Linux and macOS runners too, which is why the question used to be asked
// as "is there a pwsh" and those three entries ran on hosts they cannot measure. Where the host is
// Windows the answer is yes; elsewhere it is no, with the platform on stderr as the skip's reason.

const windows = process.platform === 'win32'
if (windows) {
    console.error(`this host is ${process.platform}: the Windows-only self-checks may run`)
    process.exit(0)
}
console.error(
    `this host is ${process.platform}: the tools these self-checks measure are Windows-only ` +
        '(SystemRoot, drive-letter paths, FileShare::None)',
)
process.exit(1)