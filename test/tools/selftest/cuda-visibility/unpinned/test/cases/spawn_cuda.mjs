// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The node harness of the unpinned fixture: a node case that starts the PyTorch program itself,
// so `tool = "node"` means the runner's Python default never sees the child that matters. Nothing
// runs this file either: the guard reads it, finds the Python file it names, and refuses the test.
import { spawnSync } from 'node:child_process'

const result = spawnSync('python3', ['cuda_case.py'], { encoding: 'utf8' })
process.stdout.write(result.stdout || '')
process.exit(result.status ?? 1)