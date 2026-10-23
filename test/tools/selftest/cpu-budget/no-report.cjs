// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Fixture of test/tools/selftest/load-class/run.mjs: a `cpu_budget_ms` test whose command is green
// in every other way and whose CPU reading never arrives.
//
// This file is loaded twice by the plan that names it: as the test's command (node runs it as the
// main module) and, through `NODE_OPTIONS=--require ${case}`, inside test/tools/child-cpu.mjs --
// the wrapper that takes the test's own CPU time from /proc/self/stat and writes it on file
// descriptor 3. Inside that wrapper only, one read of /proc/self/stat is made to fail, which is
// exactly what a host without procfs leaves behind: the wrapper reports no figure and names the
// reason, the command itself runs normally, and its exit code and stdout are what the plan expects.
// The runner must fail the test for the missing reading: a reading it cannot make is not a bound.
const fs = require('node:fs')
const path = require('node:path')

// Loaded as the wrapper's preload (or as this command's own preload), or run as the command itself:
// `require.main` names the main module, which is this file only in the second case.
const asCommand = require.main === module

if (!asCommand) {
    if (path.basename(process.argv[1] || '') === 'child-cpu.mjs') {
        const readFileSync = fs.readFileSync
        fs.readFileSync = function (file, ...rest) {
            if (file === '/proc/self/stat') {
                const missing = new Error('no such file or directory')
                missing.code = 'ENOENT'
                throw missing
            }
            return readFileSync.call(this, file, ...rest)
        }
    }
    // A preload leaves this file in the module cache, so the main-module load below would be a cache
    // hit that runs nothing and prints nothing. Dropping the entry lets it run as the command.
    delete require.cache[__filename]
} else {
    console.log('no-report ok')
}