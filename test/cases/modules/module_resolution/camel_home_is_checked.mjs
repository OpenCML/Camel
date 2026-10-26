#!/usr/bin/env node
/**
 * `CAMEL_HOME` names the installation a run reads its standard library and its runtime from. It is
 * checked now: a value that does not name an installation is an error about the variable, reported
 * before anything is loaded.
 *
 * The plan runner sets `CAMEL_HOME` for every child it starts (to the install the run measures), so
 * this is a checker of its own: it spawns `camel` twice, once with a `CAMEL_HOME` that names
 * nothing, once with the one the run measures. The first must be refused by name, the second must
 * run the program (3) -- a check that rejected every value would be no check at all.
 *
 * Red direction: without the check the first run ignores the variable, resolves nothing from an
 * installation that is not there, and either runs the program or fails with something that does not
 * mention `CAMEL_HOME`; both are reported as a failure here.
 *
 * Usage: node camel_home_is_checked.mjs <camel> (the plan passes ${camel}).
 */
import { spawnSync } from 'node:child_process'
import path from 'node:path'
import url from 'node:url'

const [, , camel] = process.argv
if (!camel) {
    console.log('FAIL the camel executable was not given')
    process.exit(1)
}

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
// A program that runs at all prints 3: `thing.cml` beside it is the module `thing`.
const program = path.join(HERE, 'only_cml_and_cmo', 'md_and_cml', 'main.cml')
const missing = path.join(HERE, 'no_such_camel_home_directory')

const run = (home) =>
    spawnSync(camel, ['-E', 'short', program], {
        encoding: 'utf8',
        env: { ...process.env, CAMEL_HOME: home },
    })

const refused = run(missing)
const refusedText = `${refused.stdout}${refused.stderr}`.trim()
if (refused.status !== 1 || !refusedText.includes('CAMEL_HOME') || !refusedText.includes('no_such_camel_home_directory')) {
    console.log(
        `FAIL CAMEL_HOME naming nothing was not refused by name: exit ${refused.status}\n` +
            `      output: ${JSON.stringify(refusedText)}`
    )
    process.exit(1)
}
if (refused.stdout.includes('3')) {
    console.log('FAIL the program ran although its installation root does not exist')
    process.exit(1)
}
console.log(`PASS a CAMEL_HOME that names nothing is refused: ${refusedText.split('\n')[0]}`)

const valid = process.env.CAMEL_HOME
if (!valid) {
    console.log('FAIL the runner did not name an installation to compare with')
    process.exit(1)
}
const ran = run(valid)
if (ran.status !== 0 || ran.stdout.trim() !== '3') {
    console.log(
        `FAIL the installation root the run measures was refused: exit ${ran.status}\n` +
            `      output: ${JSON.stringify(`${ran.stdout}${ran.stderr}`.trim())}`
    )
    process.exit(1)
}
console.log(`PASS the installation root the run measures still runs the program: ${valid}`)