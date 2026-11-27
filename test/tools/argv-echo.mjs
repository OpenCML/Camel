#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Prints the arguments it was started with, one JSON line, so a plan entry can assert the exact
// command line the runner built for a `tool = "node"` entry. An argument equal to the compiler of
// the run (`${camel}` in the plan) is printed as `<camel>`, because the path differs per install.
import path from 'node:path'

const home = process.env.CAMEL_HOME
const camel = home ? path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel') : null
const shown = process.argv.slice(2).map((word) => (camel !== null && path.resolve(word) === path.resolve(camel) ? '<camel>' : word))
console.log(`argv ${JSON.stringify(shown)}`)
