// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The record a change writes and the run that reads it must agree on what a case is.
//
// What went wrong, and why each check below is here. `scripts/selection-cases.mjs` writes one row
// per case, `{ name, cause }` -- the cause is the reading (it says whether the case was selected by
// its own input, by a plan-level rule, or as a companion). The reader,
// `test/tools/run-tests.mjs#readCaseSelection`, did `String(entry)` on each row, so every case of a
// change became the single string `[object Object]`, the set de-duplicated them into exactly one
// name, and the run refused with
//
//     [PLAN ERROR] --selection-cases names 1 case(s) this run would not start: [object Object].
//     The record is stale or was taken against another tree, so nothing is started.
//
// exit 2 and zero rows. Every merge that handed its record to the run read that, because `--changed`
// always hands the record over. A second shape of the same failure sat beside it: a selection wider
// than `CASES_IN_RECORD_MAX` stored a count and a digest instead of its rows, so the run was handed a
// record with no cases at all and refused it as "an empty selection". And behind both of them, a
// change that matched no rule widened to the whole fast layer and left a record that could not be
// executed or replayed as a list.
//
// The checks:
//   * `--changed` with a change that selects cases: the record it writes is the list the run starts,
//     by name, with the causes intact -- not one `[object Object]`;
//   * `--changed` with a change wide enough to exceed the record cap: the run list still carries its
//     names (a stored reading may keep a digest; a run may not);
//   * an entry that is not a name is refused **by its shape**, not coerced into one;
//   * a change that matches no rule is refused as an empty selection and does not widen to the fast
//     layer (`--changed --list` and the engine both);
//   * a record computed for a named target list (`--targets`) lists only the cases of those targets,
//     and the run over them accepts it -- narrowing at computation, not a refusal at the runner;
//   * a layer and the run of it are one number: `scripts/tier-report.mjs` and
//     `run-tests.mjs --list-cases` must count the same cases, because both call themselves the layer
//     (a record that narrowed the listing and a plan that wrote two cases with one name each made
//     them disagree before this check);
//   * a change record handed to a whole-layer run is refused, and the refusal's denominator is the
//     layer's own count -- a layer run may not sign for the layer while starting a change's cases;
//   * no gate surface (plans, `scripts`, `test/tools`, `internal/tools`, CI, `package.json`) names a
//     stored reading under `test/selection/`: the record a gate runs on is computed on the spot, and
//     a checked-in record would be a gate whose answer the next case-adding commit moves;
//   * one refusal that held before any of this was changed and still holds, so a green here is not
//     just "something ran": a name the selection would not start is a `[PLAN ERROR]` with exit 2.
//     The empty-list refusal's *sentence* held too, and its exit code did not -- it was 1 (a throw)
//     and is 2 now that a run that starts nothing is "not measured" rather than "red".
//
// Run it directly (`node test/tools/selftest/selection-cases.mjs`) or through the runner self-test,
// which folds these problems into its own list.
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const here = path.dirname(fileURLToPath(import.meta.url))
const defaultRoot = path.resolve(here, '..', '..', '..')

// The cap a stored reading keeps instead of its rows, mirrored here so check 2 cannot pass on a
// selection that is merely small: reading the constant out of the engine would let a lowered cap
// lower the check with it.
const CAP = 1000

// A `.cml` that a plan names as one of its case inputs: the change whose record is a narrowing.
const CASE_INPUT = 'test/cases/compile/enum/err_duplicate_field.cml'

// A change has to be found whose selection is wider than the cap. These are read in order and the
// first one that exceeds it is used; if none does, that is a problem rather than a silent pass,
// because a check that examined no wide selection would read exactly like a wide selection that
// works. A source directory is the honest wide change: a rule names its plans and keeps them whole.
const WIDE_CANDIDATES = ['src/stdlib/std/fvm/', 'src/kernel/graph.cpp', 'src/']

function sh(args, cwd) {
    // A whole-layer listing is ~9,900 JSON rows: the default 1 MiB buffer turns it into ENOBUFS,
    // which reads as "the loader printed no count" and would make the layer check report a
    // disagreement that is only a truncated pipe.
    const run = spawnSync(process.execPath, args, {
        cwd,
        encoding: 'utf8',
        windowsHide: true,
        maxBuffer: 128 * 1024 * 1024,
    })
    return {
        status: run.status,
        stdout: `${run.stdout || ''}`,
        stderr: `${run.stderr || ''}`,
        output: `${run.stdout || ''}${run.stderr || ''}`,
        error: run.error ? run.error.message : null,
    }
}

const head = (run, lines = 6) => run.output.trim().split(/\r?\n/).slice(0, lines).join(' / ')

/** The names the listing printed, in order, plus the count it claimed for itself. */
function listed(run) {
    const names = []
    let claimed = null
    for (const line of run.stdout.split(/\r?\n/)) {
        const text = line.trim()
        if (text.startsWith('{')) {
            try {
                names.push(JSON.parse(text).name)
            } catch {
                return { names, claimed, unreadable: text.slice(0, 80) }
            }
        } else if (text.startsWith('cases:')) {
            claimed = Number(text.slice('cases:'.length).trim())
        }
    }
    return { names, claimed, unreadable: null }
}

function readJson(file) {
    try {
        return JSON.parse(fs.readFileSync(file, 'utf8'))
    } catch {
        return null
    }
}

/**
 * The problems this check found. Empty means the record and the reader agree.
 *
 * `repoRoot` is the checkout whose plan tree is read, so the same code can run from the runner
 * self-test (which knows its own root) and from this file's command line.
 */
export function selectionCaseProblems(repoRoot = defaultRoot) {
    const problems = []
    const engine = path.join(repoRoot, 'scripts', 'selection-cases.mjs')
    const runner = path.join(repoRoot, 'test', 'tools', 'run-tests.mjs')
    const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-selection-selftest-'))
    const listFile = (name, files) => {
        const file = path.join(scratch, name)
        fs.writeFileSync(file, `${files.join('\n')}\n`, 'utf8')
        return file
    }
    const select = (filesFile, extra = []) => sh([engine, '--files', filesFile, ...extra], repoRoot)
    const listCases = (targets, extra = []) =>
        sh([runner, ...targets, '--tier', 'fast', '--list-cases', ...extra], repoRoot)

    try {
        // ---- 1. the record a change writes is the list the run starts, by name ------------------
        const caseList = listFile('case-input.txt', [CASE_INPUT])
        const caseRecord = path.join(scratch, 'case-record.json')
        const written = select(caseList, ['--write', caseRecord, '--quiet', '--names'])
        const record = written.status === 0 ? readJson(caseRecord) : null
        if (written.status !== 0) {
            problems.push(
                `selection: the engine refused a change that selects cases (exit ${written.status}): ${head(written)}`
            )
        } else if (record === null) {
            problems.push(`selection: the engine wrote no readable record at ${caseRecord}`)
        } else {
            const rows = Array.isArray(record.cases) ? record.cases : []
            if (record.case_count < 1) {
                problems.push(`selection: ${CASE_INPUT} selected ${record.case_count} case(s); the check needs a narrowing with cases in it`)
            }
            if (rows.length !== record.case_count) {
                problems.push(
                    `selection: the record says ${record.case_count} case(s) and holds ${rows.length} row(s); a run list must carry every name`
                )
            }
            const shapeless = rows.filter(
                (row) => !row || typeof row.name !== 'string' || row.name.length === 0 || typeof row.cause !== 'string'
            )
            if (shapeless.length > 0) {
                problems.push(
                    `selection: ${shapeless.length} recorded row(s) carry no {name, cause}: ${JSON.stringify(shapeless[0])}`
                )
            }
            const replay = listCases(record.plans || [], ['--selection-cases', caseRecord])
            const listing = listed(replay)
            if (replay.status !== 0) {
                problems.push(
                    `selection: the run refused the record this change wrote (exit ${replay.status}): ${head(replay)}`
                )
            } else if (listing.unreadable) {
                problems.push(`selection: the listing printed a row this check cannot read: ${listing.unreadable}`)
            } else {
                if (listing.claimed !== record.case_count) {
                    problems.push(
                        `selection: the run listed ${listing.claimed} case(s) for a record that names ${record.case_count}`
                    )
                }
                const recorded = new Set(rows.map((row) => row.name))
                const started = new Set(listing.names)
                const lost = [...recorded].filter((name) => !started.has(name))
                const added = [...started].filter((name) => !recorded.has(name))
                if (lost.length > 0) {
                    problems.push(`selection: the run did not start ${lost.length} recorded case(s): ${lost.slice(0, 3).join(', ')}`)
                }
                if (added.length > 0) {
                    problems.push(`selection: the run started ${added.length} case(s) the record does not name: ${added.slice(0, 3).join(', ')}`)
                }
                if (started.has('[object Object]')) {
                    problems.push('selection: a case was still read as the name "[object Object]"')
                }
            }
        }

        // ---- 2. a selection wider than the cap keeps its names when a run needs them -------------
        let wide = null
        for (const candidate of WIDE_CANDIDATES) {
            const files = listFile('wide.txt', [candidate])
            const probe = select(files, ['--write', path.join(scratch, 'wide-probe.json'), '--quiet', '--names'])
            const doc = probe.status === 0 ? readJson(path.join(scratch, 'wide-probe.json')) : null
            if (doc && doc.case_count > CAP) {
                wide = { candidate, files, doc }
                break
            }
        }
        if (wide === null) {
            problems.push(
                `selection: no candidate change selects more than ${CAP} case(s) (tried ${WIDE_CANDIDATES.join(', ')}), ` +
                    'so the record-too-wide path was not exercised'
            )
        } else {
            const cappedFile = path.join(scratch, 'wide-capped.json')
            const capped = select(wide.files, ['--write', cappedFile, '--quiet'])
            const cappedDoc = capped.status === 0 ? readJson(cappedFile) : null
            if (!cappedDoc) {
                problems.push(`selection: the engine wrote no capped record for ${wide.candidate} (exit ${capped.status})`)
            } else {
                // A stored reading keeps the digest; that is not the defect and stays.
                if (cappedDoc.cases_recorded !== false || (cappedDoc.cases || []).length !== 0) {
                    problems.push(
                        `selection: a ${cappedDoc.case_count}-case reading recorded its rows; the cap that keeps a stored reading small is gone`
                    )
                }
                // The same record, asked for as a run list, must carry them.
                if ((wide.doc.cases || []).length !== wide.doc.case_count) {
                    problems.push(
                        `selection: ${wide.candidate} keeps ${(wide.doc.cases || []).length} row(s) for ${wide.doc.case_count} case(s) even with --names`
                    )
                }
                const replay = listCases(wide.doc.plans || [], ['--selection-cases', path.join(scratch, 'wide-probe.json')])
                const listing = listed(replay)
                if (replay.status !== 0) {
                    problems.push(
                        `selection: the run refused a ${wide.doc.case_count}-case record written with --names (exit ${replay.status}): ${head(replay)}`
                    )
                } else if (listing.claimed !== wide.doc.case_count) {
                    problems.push(
                        `selection: the run listed ${listing.claimed} case(s) for a ${wide.doc.case_count}-case record`
                    )
                }
                // And a record that really does hold only a digest is refused as a digest, not as an
                // empty selection: the two are different answers.
                const refused = listCases(cappedDoc.plans || [], ['--selection-cases', cappedFile])
                if (refused.status === 0) {
                    problems.push(`selection: a ${cappedDoc.case_count}-case digest-only record was accepted as a run list`)
                } else if (/names no case; an empty selection/.test(refused.output)) {
                    problems.push('selection: a digest-only record was reported as an empty selection')
                }
            }
        }

        // ---- 3. an entry that is not a name is refused by its shape ------------------------------
        const shapelessFile = path.join(scratch, 'shapeless.json')
        fs.writeFileSync(
            shapelessFile,
            `${JSON.stringify({ version: 1, plans: [], cases: [{ cause: `case:${CASE_INPUT}` }] }, null, 2)}\n`,
            'utf8'
        )
        const shapelessRun = listCases(['test/plans/smoke'], ['--selection-cases', shapelessFile])
        if (shapelessRun.status === 0) {
            problems.push('selection: a record row with no name was accepted as a case')
        } else if (!/an object whose keys are \[cause\]/.test(shapelessRun.output)) {
            problems.push(`selection: a nameless row was not refused by its shape: ${head(shapelessRun)}`)
        }

        // ---- 4. an empty case list is refused, and the refusal is a plan error -------------------
        // The sentence held before any of this was touched: an empty selection must not become a
        // plan expansion. The exit code did not: it was 1, because the reader threw and `main`'s
        // catch exits 1 -- the code that says a test failed, for a run that started nothing. It is
        // 2 now ("not measured"), which is the code a missing install and an unreadable base get.
        // Measured on the tree before the rule (2b141ad836): exit 1 with a stack trace; after it:
        // exit 2 with one `[PLAN ERROR]` line.
        const emptyFile = path.join(scratch, 'empty.json')
        fs.writeFileSync(emptyFile, `${JSON.stringify({ version: 1, plans: [], cases: [] }, null, 2)}\n`, 'utf8')
        const emptyRun = listCases(['test/plans/smoke'], ['--selection-cases', emptyFile])
        if (emptyRun.status !== 2) {
            problems.push(`selection: an empty case list read exit ${emptyRun.status}, expected 2 (not measured)`)
        }
        if (!/names no case; an empty selection must not become a plan expansion/.test(emptyRun.output)) {
            problems.push(`selection: an empty case list was refused without the sentence that says why: ${head(emptyRun)}`)
        }
        if (!/\[PLAN ERROR\]/.test(emptyRun.output)) {
            problems.push(`selection: an empty case list was refused without a [PLAN ERROR] line: ${head(emptyRun)}`)
        }
        if (/\n\s+at .*run-tests\.mjs:/.test(emptyRun.output)) {
            problems.push(`selection: an empty case list printed a stack instead of a refusal: ${head(emptyRun)}`)
        }

        // ---- 5. held before and after: a name the selection would not start is a plan error ------
        const staleFile = path.join(scratch, 'stale.json')
        fs.writeFileSync(
            staleFile,
            `${JSON.stringify({ version: 1, plans: [], cases: [{ name: 'no.such/case', cause: `case:${CASE_INPUT}` }] }, null, 2)}\n`,
            'utf8'
        )
        const staleRun = listCases(['test/plans/smoke'], ['--selection-cases', staleFile])
        if (staleRun.status !== 2) {
            problems.push(`selection: a stale name read exit ${staleRun.status}, expected 2`)
        }
        if (!/\[PLAN ERROR\].*--selection-cases names 1 case\(s\).*would not start/.test(staleRun.output)) {
            problems.push(`selection: a stale name was not refused as a plan error: ${head(staleRun)}`)
        }

        // ---- 6. a change no rule matches selects nothing, and does not widen ---------------------
        // The candidate list holds rule-less *classes* whose files a plan does not read. It
        // deliberately does not hold the directory of stored records under `test/selection/`: check 10
        // below asserts that nothing a gate runs names it, and a probe that listed it here would be
        // the one reading the check would then have to except.
        //
        // The file is *found*, not assumed: a class here is a place a rule has not been written yet,
        // and a rule written for one of them tomorrow turns "the first file of this directory" into a
        // comparison of two selections -- which is what happened to `internal/reports/`, whose pages
        // the docs_links rule now reads. So each candidate is asked the question the check is about
        // (does any rule match this file?) and the first one that answers no is used. The probe is
        // the selection engine itself, so the class list cannot vouch for a file it does not read.
        // A run where every candidate is matched reports what matched, because that is a check that
        // could not be taken, not a pass over nothing.
        const RULELESS_CANDIDATES = ['internal/reports/', 'internal/archive/', 'benchmarks/']
        let unruled = null
        const matched = []
        for (const candidate of RULELESS_CANDIDATES) {
            const dir = path.join(repoRoot, candidate)
            if (!fs.existsSync(dir)) continue
            const files = fs.readdirSync(dir).sort().filter((entry) => fs.statSync(path.join(dir, entry)).isFile())
            for (const name of files.slice(0, 4)) {
                const file = `${candidate}${name}`
                const probe = sh(
                    ['-e', `import('./test/runner.mjs').then((m) => { const s = m.plansForChanges(${JSON.stringify([file])}); process.exit(s === null ? 3 : 0) })`],
                    repoRoot
                )
                if (probe.status === 3) {
                    unruled = file
                    break
                }
                matched.push(`${file}${probe.status === 0 ? '' : ` (probe exit ${probe.status})`}`)
            }
            if (unruled !== null) break
        }
        if (unruled === null) {
            problems.push(
                'selection: every candidate for a file no rule matches is matched by a rule (' +
                    `${matched.join(', ')}), so the empty-selection path was not exercised`
            )
        } else {
            const files = listFile('unruled.txt', [unruled])
            const engineRun = select(files, ['--print'])
            if (engineRun.status !== 2) {
                problems.push(
                    `selection: the engine read exit ${engineRun.status} for a change no rule matches, expected 2`
                )
            }
            if (!/an empty selection must not become a plan expansion/.test(engineRun.output)) {
                problems.push(`selection: the engine did not name the empty selection: ${head(engineRun)}`)
            }
            if (/layer-cases|whole fast layer is the run/.test(engineRun.output)) {
                problems.push('selection: the engine still widened a rule-less change to the fast layer')
            }
            const listedRun = sh(
                [path.join(repoRoot, 'scripts', 'test.js'), '--changed', '--changed-files', files, '--list'],
                repoRoot
            )
            if (listedRun.status === 0) {
                problems.push(
                    `selection: --changed --list read exit 0 for a change no rule matches: ${head(listedRun)}`
                )
            }
            if (!/no rule in test\/plan-selection\.json matches/.test(listedRun.output)) {
                problems.push(`selection: --changed --list did not name the files no rule matched: ${head(listedRun)}`)
            }
            if (/\(the whole fast layer/.test(listedRun.output)) {
                problems.push('selection: --changed --list still printed the whole fast layer for a rule-less change')
            }
        }
    // ---- 7. the record lists the cases of the run's targets, not of the change's plans ------
        // `--targets` is how a run tells the engine what it starts. The record must then name only
        // those cases: the narrowing belongs at the step that computes the record, so a run is never
        // handed a case it would not start and never has to refuse its own record. `CASE_INPUT`
        // selects two plans; the record is computed for one of them and replayed over that one.
        const fullFile = path.join(scratch, 'full-targets.json')
        const fullWritten = select(caseList, ['--write', fullFile, '--quiet', '--names'])
        const fullDoc = fullWritten.status === 0 ? readJson(fullFile) : null
        if (!fullDoc || (fullDoc.plans || []).length < 2) {
            problems.push(
                `selection: ${CASE_INPUT} no longer selects two plans, so the narrowing check has nothing to narrow`
            )
        } else {
            // The plain plan rather than the layered one, so the replay below is about the narrowing
            // and not about the layer.
            const layeredOf = new Set(fullDoc.layered || [])
            const narrowed = [fullDoc.plans.find((plan) => !layeredOf.has(plan)) || fullDoc.plans[0]]
            const targetsFile = path.join(scratch, 'targets.txt')
            fs.writeFileSync(targetsFile, `${narrowed.join('\n')}\n`, 'utf8')
            const narrowFile = path.join(scratch, 'narrowed.json')
            const narrowWritten = select(caseList, ['--write', narrowFile, '--quiet', '--names', '--targets', targetsFile])
            const narrowDoc = narrowWritten.status === 0 ? readJson(narrowFile) : null
            if (narrowWritten.status !== 0) {
                problems.push(`selection: the engine refused --targets (exit ${narrowWritten.status}): ${head(narrowWritten)}`)
            } else if (!narrowDoc) {
                problems.push('selection: the engine wrote no record for a named target list')
            } else {
                const named = (narrowDoc.plans || []).join(', ')
                if (named !== narrowed.join(',')) {
                    problems.push(`selection: the record lists ${named} for a run that starts ${narrowed.join(',')}`)
                }
                if (narrowDoc.case_count < 1) {
                    problems.push(`selection: the record lists no case for ${narrowed[0]}`)
                }
                if (narrowDoc.case_count > fullDoc.case_count) {
                    problems.push(
                        `selection: narrowing to ${narrowed[0]} produced ${narrowDoc.case_count} case(s), ` +
                            `more than the ${fullDoc.case_count} of the whole change`
                    )
                }
                // Narrowed, not invented: every case it names is one the change selects.
                const whole = new Set((fullDoc.cases || []).map((row) => row.name))
                const invented = (narrowDoc.cases || []).map((row) => row.name).filter((name) => !whole.has(name))
                if (invented.length > 0) {
                    problems.push(
                        `selection: the narrowed record invented ${invented.length} case(s): ${invented.slice(0, 3).join(', ')}`
                    )
                }
                // And the run over exactly those targets accepts it: no refusal for a case it would
                // not start, which is the failure mode this narrowing exists to remove.
                const layers = (narrowDoc.layered || []).length > 0 ? ['--selection-layer', narrowDoc.layered.join(',')] : []
                const replay = listCases(narrowed, ['--selection-cases', narrowFile, ...layers])
                if (replay.status !== 0) {
                    problems.push(
                        `selection: the run refused the record computed for its own targets (exit ${replay.status}): ${head(replay)}`
                    )
                } else {
                    const listing = listed(replay)
                    if (listing.claimed !== narrowDoc.case_count) {
                        problems.push(
                            `selection: the run for ${narrowed[0]} listed ${listing.claimed} case(s) for a record naming ${narrowDoc.case_count}`
                        )
                    }
                }
            }
        }

        // ---- 8. a layer's own count and the run of it are one number ------------------------------
        // Both tools call themselves the layer: `scripts/tier-report.mjs` counts the plan files by
        // their declared and derived entries, and `run-tests.mjs --list-cases` prints the cases a
        // run of them would start. A record that narrowed the listing is what made them disagree on
        // the tree this check was written for, and a plan that writes two cases with one name makes
        // the listing keep one of the two. Either way one of the numbers is not the layer, and a
        // gate that quotes the smaller one signs for a layer it did not run.
        const report = sh(
            [path.join(repoRoot, 'scripts', 'tier-report.mjs'), '--tier', 'fast', '--quiet'],
            repoRoot
        )
        const reportLine =
            `${report.stdout}`.split(/\r?\n/).find((line) => /^\[tier-report\] fast: /.test(line)) || ''
        const reportTotal = Number((reportLine.match(/= (\d+) total/) || [])[1])
        const layerListRun = listCases(['test/plans'])
        const layerList = listed(layerListRun)
        if (report.status !== 0) {
            problems.push(`layer: tier-report read exit ${report.status} for the fast layer: ${head(report)}`)
        } else if (!Number.isInteger(reportTotal)) {
            problems.push(`layer: tier-report printed no fast total: ${head(report)}`)
        } else if (reportTotal < 2) {
            problems.push(
                `layer: tier-report counted ${reportTotal} case(s) in the fast layer, too few to be a reading`
            )
        } else if (layerList.claimed === null) {
            problems.push(`layer: --list-cases printed no 'cases:' line: ${head(layerListRun)}`)
        } else if (layerList.claimed !== reportTotal) {
            problems.push(
                `layer: tier-report says ${reportTotal} case(s) in the fast layer and --list-cases printed ` +
                    `${layerList.claimed}; one of the two is not the layer`
            )
        }

        // ---- 9. a change record cannot narrow a layer run -----------------------------------------
        // The record computed in check 1 names two plans; the whole fast layer is not those plans.
        // Handing it to the layer run must refuse -- exit 2, one `[PLAN ERROR]` -- and the numbers
        // in the refusal are the layer's own: what the record covers, and the count tier-report
        // gives for this tree. A run that started the record's cases and printed `tier: fast` would
        // be the reading this check exists to make impossible.
        if (record !== null && Array.isArray(record.cases)) {
            const layerRun = listCases(['test/plans'], ['--selection-cases', caseRecord])
            const refusal = `${layerRun.output}`.match(
                /covers (\d+) of (\d+) cases in this layer; a layer run cannot sign/
            )
            if (layerRun.status !== 2) {
                problems.push(
                    `layer: a change record handed to the whole fast layer read exit ` +
                        `${layerRun.status}, expected 2: ${head(layerRun)}`
                )
            } else if (refusal === null) {
                problems.push(`layer: the refusal did not name the cases the record covers: ${head(layerRun)}`)
            } else {
                if (!/\[PLAN ERROR\]/.test(layerRun.output)) {
                    problems.push(`layer: the narrowing was refused without a [PLAN ERROR] line: ${head(layerRun)}`)
                }
                if (Number(refusal[1]) !== record.case_count) {
                    problems.push(
                        `layer: the refusal says the record covers ${refusal[1]} case(s) and the record names ` +
                            `${record.case_count}; the two must be one count`
                    )
                }
                if (!(Number(refusal[1]) < Number(refusal[2]))) {
                    problems.push(
                        `layer: the refusal covers ${refusal[1]} of ${refusal[2]}, which is not a narrowing; ` +
                            'a record that keeps the whole layer is not the defect this check is about'
                    )
                }
                if (Number.isInteger(reportTotal) && Number(refusal[2]) !== reportTotal) {
                    problems.push(
                        `layer: the refusal's denominator is ${refusal[2]}, tier-report says ${reportTotal}; ` +
                            'a layer run must count the layer'
                    )
                }
            }
        }

        // ---- 10. no gate step reads a stored selection record -------------------------------------
        // The record a gate runs on is computed on the spot, always. The records kept in the tree under
        // `test/selection/` are readings of past changes, kept so a replay can be compared with them;
        // a gate that read one would answer with the last commit that wrote it, and any commit adding
        // a case would move a gate's answer. This walks the surfaces a gate can run -- plans,
        // `scripts`, the tooling under `test/tools`, the host helpers under `internal/tools`, CI,
        // `package.json` -- and refuses a file of them that names that directory. The needle is
        // assembled from two halves so that this file, which is scanned like any other, cannot match
        // itself; prose that has to name the directory can write `test/selection/` and the segment
        // after it in words.
        const needle = ['selection', 'readings'].join('/')
        const gateRoots = ['test/plans', 'internal/tools', 'scripts', 'test/tools', '.github']
        const scanned = []
        const hits = []
        const walk = (dir) => {
            let entries = []
            try {
                entries = fs.readdirSync(dir, { withFileTypes: true })
            } catch {
                return
            }
            for (const entry of entries) {
                const full = path.join(dir, entry.name)
                if (entry.isDirectory()) {
                    if (entry.name === 'node_modules' || entry.name === '.git') continue
                    walk(full)
                } else if (entry.isFile()) {
                    scanned.push(full)
                }
            }
        }
        for (const root of gateRoots) walk(path.join(repoRoot, root))
        scanned.push(path.join(repoRoot, 'package.json'))
        // The rules file the gate reads is a surface too: a comment in it is read by whoever changes
        // the rules, so a name of the stored records there would be a step towards reading one.
        scanned.push(path.join(repoRoot, 'test', 'plan-selection.json'))
        for (const file of scanned) {
            let text = ''
            try {
                text = fs.readFileSync(file, 'utf8')
            } catch {
                continue
            }
            if (!text.includes(needle)) continue
            const line = text.split(/\r?\n/).findIndex((row) => row.includes(needle)) + 1
            hits.push(`${path.relative(repoRoot, file).split(path.sep).join('/')}:${line}`)
        }
        if (hits.length > 0) {
            problems.push(
                `selection: a gate step names the stored readings (${hits.slice(0, 4).join(', ')}); ` +
                    'the record a gate runs on is computed on the spot'
            )
        }
        if (scanned.length < 50) {
            problems.push(`selection: the stored-record scan read ${scanned.length} file(s), too few to be a reading`)
        }

        // ---- 11. a run that starts nothing exits 2, and says so ----------------------------------
        // The code, not the wording: `--selection-empty` is a real run of the run machinery that
        // starts no test, and it needs no install, so this is the one zero-row run that can be
        // measured anywhere. It exited 0 before this ruling (measured on 2b141ad836: exit 0 with
        // `[SUMMARY] total: 0, pass: 0, fail: 0`), which is the same three numbers a green run
        // prints. The `[SUMMARY]` line below is asserted too, because the fix must not have bought
        // its exit code by not printing the summary a green run also prints.
        const zeroRowRun = sh([runner, '--selection-empty', '--jobs', '1'], repoRoot)
        if (zeroRowRun.status !== 2) {
            problems.push(`selection: a run that started nothing read exit ${zeroRowRun.status}, expected 2 (not measured)`)
        }
        if (!/nothing was started, so nothing was measured: exit 2/.test(zeroRowRun.output)) {
            problems.push(`selection: a zero-row run did not say why its exit code is 2: ${head(zeroRowRun, 4)}`)
        }
        // The `.*` is the colour reset between `[SUMMARY]` and its numbers, not slack.
        if (!/\[SUMMARY\].*total: 0, pass: 0, fail: 0/.test(zeroRowRun.output)) {
            problems.push(`selection: a zero-row run lost its summary line: ${head(zeroRowRun, 4)}`)
        }
        if (!/discovered plans: 0/.test(zeroRowRun.output)) {
            problems.push(`selection: a zero-row run read an empty target list as the plan tree: ${head(zeroRowRun, 4)}`)
        }
    } finally {
        fs.rmSync(scratch, { recursive: true, force: true })
    }
    return problems
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    const problems = selectionCaseProblems(defaultRoot)
    if (problems.length > 0) {
        console.error(problems.join('\n'))
        process.exit(1)
    }
    console.log(
        'selection-cases self-test ok: a recorded case is read by its name, a record lists the run\'s ' +
            'own targets, a layer and the run of it count the same cases, a change record cannot ' +
            'narrow a layer run, a zero-row run exits 2, and a rule-less change selects nothing'
    )
}