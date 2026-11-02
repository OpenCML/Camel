/**
 * The entry file written as a bare file name (`camel edits.cml probes::count_adds std::nvm`) finds
 * the module beside it, exactly as `./edits.cml` and an absolute path do.
 *
 * A pass is resolved against the entry program's own directory: `probes::count_adds` names the
 * module `probes`, which is the file next to the program, and a program has no import of its own
 * to bring it in. The directory the entry names is read from the entry path, and `edits.cml` names
 * none: its parent is the empty path. That empty path used to be the whole answer, so nothing
 * beside the program was searched and the pass was unknown -- while `./edits.cml` gave `.` and
 * worked. The three spellings below are the same file, so they must give the same output.
 *
 * This is a harness rather than a plain case because the test runner starts every process in the
 * repository root and this defect is about the shape of the path word: the compiler has to be run
 * from the case's own directory with the bare name in its argument list.
 */
import { spawnSync } from 'node:child_process'
import path from 'node:path'
import process from 'node:process'
import { fileURLToPath } from 'node:url'

const caseDir = path.dirname(fileURLToPath(import.meta.url))
const entry = 'edits.cml'
const passes = ['probes::count_adds', 'std::nvm']

function run(camelEntry) {
    const result = spawnSync('camel', [camelEntry, ...passes], {
        cwd: caseDir,
        encoding: 'utf8',
        timeout: 60000,
    })
    return {
        entry: camelEntry,
        status: result.status,
        stdout: (result.stdout || '').replace(/\r\n/g, '\n'),
        stderr: (result.stderr || '').replace(/\r\n/g, '\n'),
    }
}

const runs = [run(entry), run('./' + entry), run(path.join(caseDir, entry))]

let ok = true
const failures = []
for (const result of runs) {
    if (result.status !== 0) {
        ok = false
        failures.push(`${result.entry}: exit ${result.status}: ${(result.stderr || result.stdout).trim()}`)
    }
}
const first = runs[0]
for (const result of runs.slice(1)) {
    if (result.stdout !== first.stdout) {
        ok = false
        failures.push(`${result.entry}: reads ${JSON.stringify(result.stdout)} where ${first.entry} reads ${JSON.stringify(first.stdout)}`)
    }
}
if (first.stdout !== '1\n126\ntext\n') {
    ok = false
    failures.push(`${first.entry}: the pass printed neither the count nor the program's output: ${JSON.stringify(first.stdout)}`)
}

for (const result of runs) {
    console.log(`${result.entry}: exit ${result.status}, ${JSON.stringify(result.stdout)}`)
}
for (const failure of failures) {
    console.log(`FAILED ${failure}`)
}
process.exit(ok ? 0 : 1)