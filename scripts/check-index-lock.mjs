// camel-index-lock: a git process holding the checkout's index lock for longer than a merge takes.
//
// The lock is normal for the few seconds a merge or a commit holds it. It is not normal for a reader:
// the git panel reads the working tree every time it draws, and a reader that refreshes the index takes
// the lock, so a merge can be refused by something that is only looking. This reports the lock when it
// has been held long enough that no single git command explains it.

import { statSync } from 'node:fs'
import { join } from 'node:path'
import { execFileSync } from 'node:child_process'

const LIMIT_SECONDS = 120

const args = process.argv.slice(2)
const rootArg = args.indexOf('--root')
const root = rootArg >= 0 ? args[rootArg + 1] : process.cwd()
const lock = join(root, '.git', 'index.lock')

let ageSeconds = null
try {
    ageSeconds = (Date.now() - statSync(lock).mtimeMs) / 1000
} catch {
    console.log(`index-lock: no lock in ${root}`)
    process.exit(0)
}

if (ageSeconds <= LIMIT_SECONDS) {
    console.log(`index-lock: held ${ageSeconds.toFixed(0)} s, within the ${LIMIT_SECONDS} s a single command takes`)
    process.exit(0)
}

let holders = ''
try {
    holders = execFileSync('wmic', ['process', 'where', "name='git.exe'", 'get', 'ProcessId,CommandLine'], { encoding: 'utf8' })
        .split(/\r?\n/).map((line) => line.trim()).filter((line) => line && line !== 'CommandLine ProcessId').join(' | ')
} catch {
    holders = '(could not read the process list)'
}

console.log(`index-lock: held ${ageSeconds.toFixed(0)} s in ${root}, longer than any single git command takes`)
console.log(`index-lock: holders ${holders || '(none named git)'}`)
console.log('index-lock: a reader that refreshes the index takes this lock, so look for one before blaming a merge')
process.exit(1)