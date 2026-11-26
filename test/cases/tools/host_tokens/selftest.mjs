// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Runs the host token pool's own self-test and passes its output and exit code through. The limit
// is a liveness bound: it says the self-test hung, it is not part of any verdict.
import { spawnSync } from 'node:child_process'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..')
const r = spawnSync('bash', ['internal/tools/host-tokens.sh', 'self-test'], {
    cwd: ROOT,
    encoding: 'utf8',
    timeout: 170000,
})
process.stdout.write(r.stdout || '')
process.stderr.write(r.stderr || '')
if (r.status === null) process.stderr.write(`host-tokens self-test did not finish (${r.signal || r.error})\n`)
process.exit(r.status === null ? 1 : r.status)
