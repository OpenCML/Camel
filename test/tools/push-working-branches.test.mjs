// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The pre-push hook refuses to send a working branch (lane/, b/, seat/, meta/) or an archive tag
// to the project's remote, allows taking one back with a delete, and leaves pushes to any other
// remote alone. The real hook is run by sh with the refs on stdin, as git runs it.

import { spawnSync } from 'node:child_process'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const hook = path.join(root, 'scripts', 'hooks', 'pre-push')
const zero = '0'.repeat(40)
const sha = 'a'.repeat(40)
const project = 'git@github.com:OpenCML/Camel.git'

let failed = 0
let checks = 0
function check(name, line, remote, url, expected) {
    checks += 1
    const run = spawnSync('sh', [hook, remote, url], { input: `${line}\n`, encoding: 'utf8', cwd: root, timeout: 30000 })
    const ok = run.status === expected
    if (!ok) failed += 1
    process.stdout.write(`${ok ? 'ok  ' : 'FAIL'} ${name} (exit ${run.status}, expected ${expected})\n`)
}

check('a lane branch to the project remote is refused', `refs/heads/lane/x ${sha} refs/heads/lane/x ${zero}`, 'origin', project, 1)
check('a batch branch to the project remote is refused', `refs/heads/b/x ${sha} refs/heads/b/x ${zero}`, 'origin', project, 1)
check('an archive tag to the project remote is refused', `refs/tags/archive/x ${sha} refs/tags/archive/x ${zero}`, 'origin', project, 1)
check('the project remote named by its https URL is the same remote', `refs/heads/lane/x ${sha} refs/heads/lane/x ${zero}`, 'https://github.com/OpenCML/Camel', 'https://github.com/OpenCML/Camel', 1)
check('deleting a lane branch from the project remote is allowed', `(delete) ${zero} refs/heads/lane/x ${sha}`, 'origin', project, 0)
check('a staging push of a lane to the build host is allowed', `refs/heads/lane/x ${sha} refs/heads/lane-x ${zero}`, 'host', 'ssh://host/srv/camel.git', 0)

process.stdout.write(`push-working-branches: ${checks} checks, ${failed} failed\n`)
process.exit(failed ? 1 : 0)
