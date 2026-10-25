#!/usr/bin/env node
/**
 * `camel --version` names the build the binary was compiled in, and that name is the footprint
 * the build stamped. This case holds both, without hardcoding a stamp: the value is read from
 * `out/<tag>/build-info.json`, so a rebuild with a new footprint stays green while a binary whose
 * stamp and install disagree goes red.
 *
 * The shape is the pinned footprint's: a UTC timestamp `YYMMDD_HHMMSS` and, when git is available,
 * the `git describe` suffix with `-` replaced by `_`. The configure fallback (`%y%m%d_%H%M%S`)
 * fails the shape, which is how an unconfigured or stale `build_config.h` shows itself.
 *
 * Red when: build-info.json's footprint is edited, `camel --version` stops printing it, or the
 *  binary is built from a tree other than the install it sits in (the stale-binary case was
 * about). Run: node version_stamp.mjs
 */

import fs from 'fs'
import path from 'path'
import url from 'url'
import { spawnSync } from 'child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const HOME = process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest')
const CAMEL = path.join(HOME, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const STAMP_FILE = path.join(HOME, 'build-info.json')

// YYMMDD_HHMMSS, then optionally _<git describe with '-' written as '_'>.
const SHAPE = /^(\d{2})(\d{2})(\d{2})_(\d{2})(\d{2})(\d{2})(_[^\s]+)?$/

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

const run = spawnSync(CAMEL, ['--version'], {
    cwd: REPO_ROOT,
    encoding: 'utf8',
    timeout: 30000,
    env: { ...process.env, CAMEL_HOME: HOME },
})
const output = `${run.stdout || ''}${run.stderr || ''}`
check('--version exits 0', run.status === 0, `${run.status}: ${output.trim()}`)

const line = output.split(/\r?\n/).find((candidate) => /^Camel v\S+ \((Build|Debug) \S+\)$/.test(candidate))
check('--version prints one "Camel v<version> (<Build|Debug> <stamp>)" line', Boolean(line), output.trim())
if (line) {
    const [, version, label, stamp] = line.match(/^Camel v(\S+) \((Build|Debug) (\S+)\)$/)
    check('the version is non-empty', version.length > 0, version)

    const shape = stamp.match(SHAPE)
    check('the stamp has the pinned footprint shape', Boolean(shape), stamp)
    if (shape) {
        const [, , month, day, hour, minute, second] = shape
        check('the stamp timestamp is a real time', Number(month) >= 1 && Number(month) <= 12 &&
            Number(day) >= 1 && Number(day) <= 31 && Number(hour) < 24 &&
            Number(minute) < 60 && Number(second) < 60, stamp)
    }

    let install = null
    try {
        install = JSON.parse(fs.readFileSync(STAMP_FILE, 'utf-8'))
    } catch (error) {
        check(`the install records its stamp (${path.relative(REPO_ROOT, STAMP_FILE)})`, false, error.message)
    }
    if (install) {
        // Debug builds print "(Debug ...)", every other configuration prints "(Build ...)".
        check('the build type agrees with the install', label === (install.config === 'Debug' ? 'Debug' : 'Build'),
            `${label} vs config ${install.config}`)
        check('the install records the footprint', typeof install.footprint === 'string' && install.footprint.length > 0,
            JSON.stringify(install.footprint))
        check('--version prints the footprint the build recorded', stamp === install.footprint,
            `${stamp} vs ${install.footprint}`)
    }
}

console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)