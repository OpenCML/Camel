// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// One rule, in one place: a script that reads the operator registry of a built `camel` measures
// that install, and an install that is not **this** tree's build is refused before the reading
//  (the rule in the `stdlib-declaration-check-measures-a-
// foreign-install` section of `docs/handover/reviews/questions-2026-10-24-evening.md`, which
// answered the question file of that name and removed it).
//
// The rule is the one `scripts/release-gate.mjs` applies before it runs anything: an install lives
// at `out/<tag>` under this tree, and `node scripts/build-info.js check --tag <tag>` compares the
// content fingerprint the build stamped there with the sources of this tree. Exit 0 is the only
// pass; the stamp's own refusal code is 4. `--camel <path>` is a parameter naming which install a
// run measures, never an exemption from the check: an install outside this tree's `out/` carries no
//  stamp this rule can compare, and the same refusal applies -- what a run measured must be
// provable, whichever way the binary was named.
//
// The refusal is exit 2. It used to be exit 1 with one problem per line of the child's output
// (`144 difference(s) between the declarations and the registered operators`, plus a summary line,
// plus a lone `\r`), which read like a defect of the tree; the tree was fine and the binary was
// four days older. A run that measured the wrong install has nothing to report about this tree, so
// it exits 2 and says which install it was and what that install's stamp says.

import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'

/** The install a run measures when `--camel <path>` names none: this tree's `out/latest`. */
export function defaultCamel(repoRoot) {
    return path.join(repoRoot, 'out', 'latest', 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
}

// What a stamp says, in one line. Only the refusal for an install outside this tree's `out/` needs
// this: there `build-info.js check` has nothing to compare, and the message still has to say what
// the install carries.
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
 * stamp says) and exit 2. Returns normally only when `build-info.js check` proved the install
 * belongs to this tree. `script` names the caller in the first line.
 */
export function refuseInstallNotOfThisTree(binary, repoRoot, script) {
    const install = path.dirname(path.dirname(binary))
    const outDir = path.join(repoRoot, 'out')
    const relative = path.relative(outDir, install)
    const lines = []
    const outside = relative === '..' || relative.startsWith(`..${path.sep}`) || path.isAbsolute(relative)
    if (relative === '' || outside) {
        lines.push(
            `it is not out/<tag> under this tree (${outDir}), so build-info.js check has no stamp of this`,
            `tree to compare it with`,
        )
        const stamp = stampSummary(install)
        lines.push(stamp ? `the stamp it carries: ${stamp}` : 'it carries no build-info.json at all')
    } else {
        const tag = relative.split(path.sep)[0]
        const check = spawnSync(process.execPath, ['scripts/build-info.js', 'check', '--tag', tag], {
            cwd: repoRoot,
            encoding: 'utf8',
            windowsHide: true,
        })
        if (check.status === 0) return
        const detail = `${check.stdout || ''}${check.stderr || ''}`.trim()
        lines.push(
            `out/${tag} did not pass the build stamp check:`,
            `build-info.js check --tag ${tag} exited ${check.status}${
                check.error ? ` (${check.error.message})` : ''
            }`,
        )
        // The stamp and both fingerprints are what the message has to carry, and they come from the
        // rule itself. Its closing advice names `-NoBuild` / `-AllowStale`, switches of
        // `camel-remote.ps1` that a direct run of this script does not have; the remedy this refusal
        // prints is the one that applies here.
        const advice = new RegExp(
            '^\\s*(-AllowStale was given|Re-run without -NoBuild|Run once without -NoBuild)',
        )
        const shown = detail
            ? detail.split(/\r?\n/).filter((each) => !advice.test(each))
            : ['build-info printed nothing']
        for (const line of shown) lines.push(`  ${line}`)
    }
    console.log(
        `${script}: refusing to measure an install that is not this tree's build ` +
            '(H149; the rule scripts/release-gate.mjs applies)',
    )
    console.log(`  the install measured: ${install}`)
    console.log(`  the binary:           ${binary}`)
    for (const line of lines) console.log(`  ${line}`)
    console.log(
        "  build this tree once (`npm run build`) and re-run: a reading taken from another tree's " +
            'install describes that tree, not this one',
    )
    process.exit(2)
}