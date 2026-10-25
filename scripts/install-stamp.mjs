// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// One rule, in one place: a script that reads the operator registry of a built `camel` measures
// that install, and an install that is not **this** tree's build is refused before the reading.
//
// The rule is the one `scripts/release-gate.mjs` applies before it runs anything: `node
// scripts/build-info.js check` compares the content fingerprint the build stamped inside the
// install with the sources of this tree. Exit 0 is the only pass; the stamp's own refusal code is
// 4. `--camel <path>` is a parameter naming which install a run measures, never an exemption from
// the check: a stamp travels inside the install it describes, so an install outside this tree's
// `out/` is checked the same way and refused when it was built from other sources -- what a run
// measured must be provable, whichever way the binary was named.
//
// The refusal is exit 2. It used to be exit 1 with one problem per line of the child's output
// (`144 difference(s) between the declarations and the registered operators`, plus a summary line,
// plus a lone `\r`), which read like a defect of the tree; the tree was fine and the binary was
// four days older. A run that measured the wrong install has nothing to report about this tree, so
// it exits 2 and says which install it was and what that install's stamp says.

import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'

// The install a checker measures with no `--camel <path>` (this tree's `out/latest`) is
// `test/tools/camel-arg.mjs: defaultCamel`, the one place that default lives for both languages.

// What a stamp says, in one line: the install carries it, so the message can name it even when the
// stamp is missing or unreadable.
function stampSummary(install) {
    const file = path.join(install, 'build-info.json')
    if (!fs.existsSync(file)) return null
    let stamp
    try {
        stamp = JSON.parse(fs.readFileSync(file, 'utf-8'))
    } catch (error) {
        return `unreadable (${error.message})`
    }
    const revision = stamp.revision ? String(stamp.revision).slice(0, 10) : 'no revision recorded'
    const dirty =
        stamp.dirty === true ? ` (dirty: ${stamp.dirtyCount} file(s))` : stamp.dirty === false ? ' (clean)' : ''
    const fingerprint =
        stamp.fingerprint && typeof stamp.fingerprint.hash === 'string'
            ? `${stamp.fingerprint.hash.slice(0, 12)} over ${stamp.fingerprint.files} source file(s)`
            : 'none'
    return (
        `${revision}${dirty}, built ${stamp.builtAt || 'at an unknown time'}, ` +
        `config ${stamp.config || 'unknown'}, fingerprint ${fingerprint}`
    )
}

/**
 * Refuse to measure an install that is not this tree's build: print why (which install, what its
 * stamp says) and exit 2. Returns normally only when `build-info.js check` proved the install's
 * stamp matches this tree's sources. `script` names the caller in the first line.
 */
export function refuseInstallNotOfThisTree(binary, repoRoot, script) {
    const install = path.dirname(path.dirname(binary))
    const check = spawnSync(
        process.execPath,
        ['scripts/build-info.js', 'check', '--install', install, '--root', repoRoot],
        { cwd: repoRoot, encoding: 'utf8', windowsHide: true },
    )
    if (check.status === 0) return
    const lines = []
    const detail = `${check.stdout || ''}${check.stderr || ''}`.trim()
    if (detail) {
        // The stamp and both fingerprints are what the message has to carry, and they come from the
        // rule itself. Its closing advice names `-NoBuild` / `-AllowStale`, switches of
        // `camel-remote.ps1` that a direct run of this script does not have; the remedy this refusal
        // prints is the one that applies here.
        const advice = new RegExp(
            '^\\s*(-AllowStale was given|Re-run without -NoBuild|Run once without -NoBuild)',
        )
        for (const line of detail.split(/\r?\n/).filter((each) => !advice.test(each))) {
            lines.push(`  ${line}`)
        }
    } else {
        const stamp = stampSummary(install)
        lines.push(
            `build-info.js check --install ${install} exited ${check.status}` +
                `${check.error ? ` (${check.error.message})` : ''} and printed nothing`,
        )
        lines.push(stamp ? `the stamp it carries: ${stamp}` : 'it carries no build-info.json at all')
    }
    console.log(
        `${script}: refusing to measure an install that is not this tree's build ` +
            '(a reading taken from another tree\'s install describes that tree)',
    )
    console.log(`  the install measured: ${install}`)
    console.log(`  the binary:           ${binary}`)
    for (const line of lines) console.log(line)
    console.log(
        '  build this tree once (`npm run build`) and re-run, or name this tree\'s install with ' +
            '`--camel <path>`: it is the install a run measures that has to be this tree\'s build',
    )
    process.exit(2)
}