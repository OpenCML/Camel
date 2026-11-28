// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The build report reads one build's steps and ccache counts: only the generator's log lines after
 * the build started, link steps apart from compile steps, and the hits and misses of one run's
 * statistics log. Run with `node test/tools/build-report.test.mjs`.
 */

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { buildSteps, ccacheCounts, summarize } from '../../scripts/build-report.mjs'

const checks = []
function check(name, holds, detail) {
    checks.push({ name, holds: Boolean(holds), detail: detail ?? '' })
}

const ninja = [
    '# ninja log v5',
    '0\t1000\t0\tsrc/old.o\tabc',
    '100\t2100\t0\tsrc/a.cpp.o\tdef',
    '200\t5200\t0\tsrc/b.cpp.o\t123',
    '5300\t9300\t0\tlibs/libcamel.so\t456',
    '9400\t9600\t0\tbin/camel\t789',
].join('\n')

const steps = buildSteps(ninja, 2)
check('only the lines after the build started count', steps.length === 4, JSON.stringify(steps))
const line = summarize(steps, 2)
check(
    'link steps are summed apart from compile steps, and the longest steps are named',
    line === 'BUILD_STEPS steps=4 compile_ms=7000 link_ms=4200 longest=src/b.cpp.o:5000;libs/libcamel.so:4000',
    line,
)

const counts = ccacheCounts(['# src/a.cpp', 'direct_cache_hit', '# src/b.cpp', 'cache_miss', '# src/c.cpp', 'preprocessed_cache_hit'].join('\n'))
check('a run counts its own hits and misses', counts.hits === 2 && counts.misses === 1, JSON.stringify(counts))

const here = path.dirname(fileURLToPath(import.meta.url))
const tool = path.resolve(here, '..', '..', 'scripts', 'build-report.mjs')
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'build-report-'))
const missing = spawnSync(process.execPath, [tool, '--ninja-log', path.join(dir, 'none'), '--ccache-log', path.join(dir, 'none')], { encoding: 'utf8' })
check(
    'missing logs are reported and do not fail the run',
    missing.status === 0 && missing.stdout.includes('BUILD_STEPS unread') && missing.stdout.includes('BUILD_CCACHE unread'),
    `${missing.status} ${missing.stdout}${missing.stderr}`,
)
fs.writeFileSync(path.join(dir, 'ninja'), ninja)
const read = spawnSync(process.execPath, [tool, '--ninja-log', path.join(dir, 'ninja'), '--from-line', '2', '--top', '1'], { encoding: 'utf8' })
check('the command line prints the steps of the build', read.status === 0 && read.stdout.includes('BUILD_STEPS steps=4 compile_ms=7000 link_ms=4200 longest=src/b.cpp.o:5000'), read.stdout + read.stderr)
fs.rmSync(dir, { recursive: true, force: true })

let failed = 0
for (const one of checks) {
    if (!one.holds) failed += 1
    process.stdout.write(`${one.holds ? 'ok  ' : 'FAIL'}  ${one.name}${one.holds ? '' : `\n      ${one.detail}`}\n`)
}
process.stdout.write(`build-report: ${checks.length} checks, ${failed} failed\n`)
process.exit(failed === 0 ? 0 : 1)
