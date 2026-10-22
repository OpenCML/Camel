// What `import m` does today, for every module of the standard library (handover H222).
//
// The language author ruled on 2026-10-22 that `import` executes no code (design principles
// 3.1, and the row "模块顶层的 `let`": a module-level `let` is a node of the public root graph,
// evaluated when something depends on it). `import pyplot` initialising CPython was the
// measurement that produced the ruling; this tool is the audit the ruling implies, module by
// module, with a probe per row.
//
// For each module it answers three questions and prints one line:
//
//   class=c   import declares names and does nothing else that can be observed: the operators,
//             the executor factory, the pass, the tag, the node role or the entry-argument
//             loader are registered, and a registration table is declaration (the ruling's own
//             strictness note). A module-level `let` in a Camel module is a root-graph node and
//             is not evaluated either.
//   class=b   import does work that must stay there until the author rules otherwise: today
//             `python` and `pyplot` initialise CPython in `load()`, and docs/technical/50 says
//             why moving it changes error codes and which module owns the interpreter's setup.
//   class=a   import does work that is cheap to move to first use. The table is empty: the scan
//             below refuses to keep it empty silently.
//
// `probe=clean` means the program that imports the module and does nothing else (a) exited 0,
// (b) wrote nothing to stdout or stderr at the default log level, and (c) created no file. A row
// whose import prints, exits non-zero or writes a file is a finding whatever its class.
//
// The `scan` half is the regression guard. It reads the module's own `module.cpp` and looks for
// the shapes that make import execute code: an environment read, a file open, a directory walk,
// CPython initialisation, thread creation, a process-exit hook. Every hit must be listed in the
// table below with its class and its reason; a hit that is not listed fails the tool, naming the
// module and the line, so work that returns to import time cannot pass as the table's quiet
// neighbour. File-scope objects that are constructed at `.cmo` load are reported as `static=`
// (they are the one construction the class-c rows still pay; none of them is observable).
//
// Extra lines, for the (b) decisions the author has to price:
//   import.audit baseline ...             a program with no import
//   import.audit marginal <module> ...    the same program with `import m` added
//   import.audit resolution ...           what `CAMEL_PACKAGES` / `-I` cost, and when
//   import.audit declaration ...          the declaration module compiled at import, priced
//                                         per declaration
//
//   node test/tools/import-audit.mjs                 probe, scan and the (b) prices
//   node test/tools/import-audit.mjs --json          the same, as JSON
//   node test/tools/import-audit.mjs --reps 11       more wall-clock samples per module
//   node test/tools/import-audit.mjs --no-scan       probes only
//   node test/tools/import-audit.mjs --scan-only     the source scan, no `camel` and no build

import { spawnSync } from 'child_process'
import fs from 'fs'
import os from 'os'
import path from 'path'
import { fileURLToPath } from 'url'

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const REGISTRY_ALL = path.join(REPO_ROOT, 'test', 'cases', 'modules', 'stdlib', 'registry_all.cml')
const IMPORTS_NOTHING = path.join(REPO_ROOT, 'test', 'cases', 'perf', 'startup', 'imports_nothing.cml')

// The modules of the table, in the order the standard-library registry imports them: the 27
// `.cmo` modules this tree builds plus `array`, which is a declaration module with no C++
// half. `registry_all.cml` imports all of them, so its list is the table's list (28 rows);
// the two debug-only builtin modules (`test_attrs`, `profiler`) are not `.cmo` modules and are
// not imported by name here.
function moduleNames() {
    const text = fs.readFileSync(REGISTRY_ALL, 'utf8')
    const names = []
    for (const line of text.split(/\r?\n/)) {
        const match = /^import\s+([A-Za-z_][\w]*)\s*$/.exec(line.trim())
        if (match) {
            names.push(match[1])
        }
    }
    if (names.length === 0) {
        throw new Error('registry_all.cml imports nothing: the audit has no rows')
    }
    return names
}

const MODULES = moduleNames()

// The rows whose import does work that is not name declaration, and the reason. Everything else
// must be declaration only; the scan below is what keeps that claim true.
const IMPORT_TIME_WORK = {
    python: {
        class: 'b',
        reason:
            'load() initialises CPython and checks the venv version (docs/technical/50_pyplot_cpython_init.md)',
    },
    pyplot: {
        class: 'b',
        reason:
            'load() initialises CPython and adds site-packages (docs/technical/50, another task owns the fix)',
    },
}

// The shapes that make import execute code rather than declare names.
const WORK_SHAPES = [
    { name: 'env-read', pattern: /\b(getenv|getEnv|::getEnv)\s*\(/ },
    { name: 'file-read', pattern: /\b(ifstream|fopen|filesystem::exists|filesystem::is_directory|directory_iterator|readFile|loadNpy)\b/ },
    { name: 'cpython-init', pattern: /\b(Py_Initialize|Py_IsInitialized|initialize_interpreter)\b/ },
    { name: 'thread', pattern: /\b(std::thread|pthread_create|CreateThread)\b/ },
    { name: 'exit-hook', pattern: /\b(std::atexit|atexit)\s*\(/ },
]

// The body of `load()` (the override in the module class) or of the constructor: the text
// between the first `::load()`/`load() override` and its matching brace. A module that does its
// work in the constructor instead is scanned the same way, from `BuiltinModule("<name>"`.
function bracedBody(text, from) {
    const open = text.indexOf('{', from)
    if (open < 0) {
        return { body: '', start: 0, source: text }
    }
    let depth = 0
    for (let i = open; i < text.length; i++) {
        if (text[i] === '{') {
            depth++
        } else if (text[i] === '}') {
            depth--
            if (depth === 0) {
                return { body: text.slice(open, i + 1), start: open, source: text }
            }
        }
    }
    return { body: text.slice(open), start: open, source: text }
}

// `ckpt` is built from `modules/tree` (its CMake target is named ckpt); every other `.cmo`
// module has a directory of its own, and `array` is Camel only.
const MODULE_SOURCE_DIR = { ckpt: 'tree' }

function moduleSource(module) {
    const dir = MODULE_SOURCE_DIR[module] || module
    const candidates = [
        path.join(REPO_ROOT, 'modules', dir, 'module.cpp'),
        path.join(REPO_ROOT, 'modules', dir, 'src', 'module.cpp'),
    ]
    for (const candidate of candidates) {
        if (fs.existsSync(candidate)) {
            return candidate
        }
    }
    return null
}

// The text a module executes when it is imported: the body of `load()` and the body of its
// constructor (which `camel_module_create` runs at `.cmo` load, before `load()` is asked).
function importTimeText(module) {
    const file = moduleSource(module)
    if (file === null) {
        return { file: null, parts: [] }
    }
    const text = fs.readFileSync(file, 'utf8')
    const parts = []
    const loadAt = text.search(/(?:::load\(\)|load\(\)\s+override)/)
    if (loadAt >= 0) {
        parts.push(bracedBody(text, loadAt))
    }
    const ctorAt = text.indexOf(`BuiltinModule("${module}"`)
    if (ctorAt >= 0) {
        parts.push(bracedBody(text, ctorAt))
    }
    return { file, parts }
}

// Whether the `{` at `open` starts a lambda body: a capture list `[...]` (an optional parameter
// list and return type) is what precedes it. A lambda is a callback a module registers for
// later; its body runs then, not at import, so the work shapes inside one are not import-time
// work. `tensor`'s entry-argument loader is the shape this rule exists for: `load()` registers
// it, and it reads an `.npy` only when the entry program is invoked with one.
function opensLambda(text, open) {
    let end = open
    while (end > 0 && /\s/.test(text[end - 1])) {
        end--
    }
    if (end === 0) {
        return false
    }
    if (text[end - 1] !== ']') {
        // `[](ArgsView &...) -> slot_t {` and the same with a trailing qualifier.
        const head = text.slice(Math.max(0, end - 160), end)
        return /\]\s*\([^()]*\)\s*(?:->\s*[\w:<>,\s*&*]+)?$/.test(head)
    }
    return true
}

// The lines of `text` that are executed at import, with the line number in the file: a line
// whose first non-space character is inside a lambda body is a callback registered for later,
// not work done at import, and is left out.
function executedLines(source, body, bodyStart) {
    const lineOf = (offset) => source.slice(0, offset).split('\n').length
    const baseLine = bodyStart === 0 ? 1 : lineOf(bodyStart)
    // `inside[i]` is 1 when the character at `i` is inside a lambda body: a `{` is judged by the
    // depth before it, so the line that opens the lambda (the registration line) still counts.
    const inside = new Uint8Array(body.length)
    const stack = []
    let lambdaDepth = 0
    for (let i = 0; i < body.length; i++) {
        inside[i] = lambdaDepth > 0 ? 1 : 0
        const ch = body[i]
        if (ch === '{') {
            const isLambda = opensLambda(body, i)
            stack.push(isLambda)
            if (isLambda) {
                lambdaDepth++
            }
        } else if (ch === '}') {
            if (stack.length > 0 && stack.pop()) {
                lambdaDepth--
            }
        }
    }
    const out = []
    let offset = 0
    let line = baseLine
    for (const raw of body.split('\n')) {
        const firstNonSpace = raw.length - raw.trimStart().length
        const at = Math.min(offset + firstNonSpace, Math.max(0, body.length - 1))
        if (raw.trim().length > 0 && inside[at] === 0) {
            out.push({ line, code: raw })
        }
        offset += raw.length + 1
        line++
    }
    return out
}

function scanModule(module) {
    const { file, parts } = importTimeText(module)
    if (file === null) {
        return { file: null, hits: [] }
    }
    const path_ = path.relative(REPO_ROOT, file).split(path.sep).join('/')
    const hits = []
    for (const part of parts) {
        for (const entry of executedLines(part.source, part.body, part.start)) {
            // Comments explain the work; they are not the work.
            const code = entry.code.replace(/\/\/.*$/, '')
            for (const shape of WORK_SHAPES) {
                if (shape.pattern.test(code)) {
                    hits.push({ shape: shape.name, where: `${path_}:${entry.line}`, code: entry.code.trim() })
                }
            }
        }
    }
    return { file, hits }
}

function camelExe() {
    if (process.env.CAMEL_EXE) {
        return process.env.CAMEL_EXE
    }
    const home = process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest')
    return path.join(home, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
}

function caseDir() {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-import-audit-'))
    return dir
}

// A program that imports `module` and does nothing else. The module name is unique per case so
// that no stale `.cmc`/`.cmo` of a previous run can stand in for the source.
function importOnlyProgram(dir, module) {
    const file = path.join(dir, `import_only_${module}.cml`)
    fs.writeFileSync(
        file,
        `// H222 probe: import ${module} and do nothing else.\n` +
            `module import_only_${module}\n\n` +
            `import ${module}\n\n` +
            `func main(): int {\n    return 0\n}\n`,
    )
    return file
}

function runOnce(argv, options = {}) {
    const run = spawnSync(argv[0], argv.slice(1), {
        cwd: options.cwd || REPO_ROOT,
        env: { ...process.env, ...(options.env || {}) },
        stdio: ['ignore', 'pipe', 'pipe'],
        encoding: 'utf8',
        maxBuffer: 1 << 26,
        timeout: 60000,
    })
    return run
}

function phaseReport(stderr) {
    const at = stderr.lastIndexOf('{"schema"')
    const end = stderr.lastIndexOf('}')
    if (at < 0 || end < at) {
        return null
    }
    try {
        return JSON.parse(stderr.slice(at, end + 1))
    } catch {
        return null
    }
}

// Self time (wall_ns) of the named phases, summed over every detail.
function phaseNanos(report, names) {
    let nanos = 0
    for (const phase of report.phases) {
        if (names.includes(phase.name)) {
            nanos += phase.wall_ns
        }
    }
    return nanos
}

function probeModule(module, reps) {
    const dir = caseDir()
    try {
        const file = importOnlyProgram(dir, module)
        const before = new Set(fs.readdirSync(dir))
        const timing = []
        let first = null
        for (let i = 0; i < reps + 1; i++) {
            const started = process.hrtime.bigint()
            const run = runOnce([camelExe(), file, 'std::nvm'], { cwd: dir })
            timing.push(Number(process.hrtime.bigint() - started) / 1e6)
            if (i === 0) {
                first = run
            }
        }
        const after = fs.readdirSync(dir)
        const created = after.filter((name) => !before.has(name))
        const createdExcept = created.filter((name) => !name.startsWith('import_only_'))
        const timed = timing.slice(1).sort((a, b) => a - b)
        const median = timed[Math.floor(timed.length / 2)]
        const phaseRun = runOnce([camelExe(), file, 'std::nvm'], {
            cwd: dir,
            env: { CAMEL_TIME_PHASES: '-' },
        })
        const report = phaseReport(phaseRun.stderr)
        const problems = []
        if (first.status !== 0) {
            problems.push(`exited ${first.status}`)
        }
        if (first.stdout !== '') {
            problems.push(`wrote ${JSON.stringify(first.stdout.slice(0, 120))} to stdout`)
        }
        if (first.stderr !== '') {
            const lines = first.stderr.trim().split(/\r?\n/).length
            problems.push(`wrote ${lines} line(s) to stderr`)
        }
        if (createdExcept.length > 0) {
            problems.push(`created ${createdExcept.join(', ')}`)
        }
        return {
            module,
            problems,
            wall_ms: median,
            cmo_load_ms: report ? phaseNanos(report, ['cmo_load']) / 1e6 : null,
            resolve_ms: report ? phaseNanos(report, ['resolve']) / 1e6 : null,
            import_ms: report ? phaseNanos(report, ['import']) / 1e6 : null,
            compile_ms: report ? phaseNanos(report, ['parse', 'gct', 'declare', 'gir']) / 1e6 : null,
            has_cmo: moduleSource(module) !== null,
        }
    } finally {
        fs.rmSync(dir, { recursive: true, force: true })
    }
}

function baselineWall(reps) {
    const dir = caseDir()
    try {
        const before = new Set(fs.readdirSync(dir))
        const timing = []
        let first = null
        for (let i = 0; i < reps + 1; i++) {
            const started = process.hrtime.bigint()
            const run = runOnce([camelExe(), IMPORTS_NOTHING, 'std::nvm'], { cwd: dir })
            timing.push(Number(process.hrtime.bigint() - started) / 1e6)
            if (i === 0) {
                first = run
            }
        }
        const timed = timing.slice(1).sort((a, b) => a - b)
        return { wall_ms: timed[Math.floor(timed.length / 2)], status: first.status }
    } finally {
        fs.rmSync(dir, { recursive: true, force: true })
    }
}

// `CAMEL_PACKAGES` and `-I` add search roots. Each root is a directory listing the first time
// the context looks a module up, cached afterwards (docs/technical/53_import_cost_audit.md,
// 53.3.2). The price is the
// `resolve` phase of the first import, with and without the roots.
function resolutionCost(reps) {
    const dir = caseDir()
    const extraDirs = []
    try {
        for (let i = 0; i < 10; i++) {
            const extra = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-import-root-'))
            extraDirs.push(extra)
        }
        const file = importOnlyProgram(dir, 'math')
        const sample = (env) => {
            const started = process.hrtime.bigint()
            const run = runOnce([camelExe(), file, 'std::nvm'], {
                cwd: dir,
                env: { ...env, CAMEL_TIME_PHASES: '-' },
            })
            const elapsed = Number(process.hrtime.bigint() - started) / 1e6
            const report = phaseReport(run.stderr)
            return {
                wall_ms: elapsed,
                resolve_ms: report ? phaseNanos(report, ['resolve']) / 1e6 : null,
                status: run.status,
            }
        }
        const medianOf = (values) => {
            const sorted = [...values].sort((a, b) => a - b)
            return sorted[Math.floor(sorted.length / 2)]
        }
        const plain = []
        const packages = []
        const includes = []
        for (let i = 0; i < reps; i++) {
            plain.push(sample({}))
            packages.push(sample({ CAMEL_PACKAGES: extraDirs.join(path.delimiter) }))
            includes.push(sample({}))
        }
        // `-I` is a command-line search root: the same lookup, one root per flag.
        const includeArgv = [camelExe(), file, 'std::nvm']
        for (const extra of extraDirs.slice(0, 3)) {
            includeArgv.push('-I', extra)
        }
        const started = process.hrtime.bigint()
        const includeRun = runOnce(includeArgv, { cwd: dir })
        const includeWall = Number(process.hrtime.bigint() - started) / 1e6
        return {
            plain_resolve_ms: medianOf(plain.map((entry) => entry.resolve_ms)),
            plain_wall_ms: medianOf(plain.map((entry) => entry.wall_ms)),
            packages_resolve_ms: medianOf(packages.map((entry) => entry.resolve_ms)),
            packages_wall_ms: medianOf(packages.map((entry) => entry.wall_ms)),
            roots: extraDirs.length,
            include_wall_ms: includeWall,
            include_status: includeRun.status,
        }
    } finally {
        fs.rmSync(dir, { recursive: true, force: true })
        for (const extra of extraDirs) {
            fs.rmSync(extra, { recursive: true, force: true })
        }
    }
}

// A declaration module is compiled whole at import: parse, GCT, declare and GIR, for every
// declaration it writes, whether or not the importing program uses one. Priced on `array` (a
// Camel module with no `.cmo`, so everything it costs is declaration) and on `graph` (a `.cmo`
// beside its `.cml`, which is the common shape).
function declarationCost(reps) {
    const out = []
    for (const module of ['array', 'graph']) {
        const dir = caseDir()
        try {
            const file = importOnlyProgram(dir, module)
            const samples = []
            for (let i = 0; i < reps; i++) {
                const run = runOnce([camelExe(), file, 'std::nvm'], {
                    cwd: dir,
                    env: { CAMEL_TIME_PHASES: '-' },
                })
                const report = phaseReport(run.stderr)
                if (run.status !== 0 || report === null) {
                    throw new Error(`import ${module}: exit ${run.status}, no phase report`)
                }
                let nanos = 0
                for (const phase of report.phases) {
                    if (phase.detail !== module) {
                        continue
                    }
                    if (!['parse', 'gct', 'declare', 'gir'].includes(phase.name)) {
                        continue
                    }
                    nanos += phase.wall_ns
                }
                samples.push(nanos)
            }
            samples.sort((a, b) => a - b)
            const median = samples[Math.floor(samples.length / 2)]
            const text = fs.readFileSync(path.join(REPO_ROOT, 'stdlib', `${module}.cml`), 'utf8')
            out.push({
                module,
                lines: text.split(/\r?\n/).filter((line) => line.trim() !== '').length,
                exports: (text.match(/^\s*export\s+/gm) || []).length,
                compile_ms: median / 1e6,
            })
        } finally {
            fs.rmSync(dir, { recursive: true, force: true })
        }
    }
    return out
}

function main(argv) {
    const asJson = argv.includes('--json')
    const skipScan = argv.includes('--no-scan')
    const scanOnly = argv.includes('--scan-only')
    const repsAt = argv.indexOf('--reps')
    const reps = repsAt >= 0 ? Number(argv[repsAt + 1]) : 5
    if (!Number.isFinite(reps) || reps < 1) {
        throw new Error('--reps needs a positive number')
    }
    if (scanOnly) {
        const failures = []
        for (const module of MODULES) {
            const scan = scanModule(module)
            const declared = IMPORT_TIME_WORK[module]
            if (!declared) {
                for (const hit of scan.hits) {
                    failures.push(
                        `import ${module}: ${hit.shape} at ${hit.where} is import-time work and the table calls the row class=c; classify it (docs/technical/53_import_cost_audit.md) or move it to first use — ${hit.code}`,
                    )
                }
            }
            console.log(
                `import.audit.scan ${module.padEnd(11)} class=${declared ? declared.class : 'c'} hits=${scan.hits.length}${scan.hits.length > 0 ? ' ' + scan.hits.map((hit) => hit.shape + '@' + hit.where).join(',') : ''}`,
            )
        }
        for (const failure of failures) {
            console.error(`import-audit: ${failure}`)
        }
        return failures.length === 0 ? 0 : 1
    }
    if (!fs.existsSync(camelExe())) {
        throw new Error(`no camel binary at ${camelExe()}: set CAMEL_HOME or CAMEL_EXE`)
    }

    const rows = []
    const failures = []
    const baseline = baselineWall(reps)
    for (const module of MODULES) {
        const probe = probeModule(module, reps)
        const declared = IMPORT_TIME_WORK[module]
        const scan = skipScan ? { hits: [] } : scanModule(module)
        const class_ = declared ? declared.class : 'c'
        const row = {
            module,
            class: class_,
            reason: declared ? declared.reason : 'declaration only',
            probe: probe.problems.length === 0 ? 'clean' : probe.problems.join('; '),
            wall_ms: Number(probe.wall_ms.toFixed(2)),
            marginal_ms: Number((probe.wall_ms - baseline.wall_ms).toFixed(2)),
            cmo_load_ms: probe.cmo_load_ms === null ? null : Number(probe.cmo_load_ms.toFixed(3)),
            resolve_ms: probe.resolve_ms === null ? null : Number(probe.resolve_ms.toFixed(3)),
            import_ms: probe.import_ms === null ? null : Number(probe.import_ms.toFixed(3)),
            compile_ms: probe.compile_ms === null ? null : Number(probe.compile_ms.toFixed(3)),
            cpp: probe.has_cmo,
            import_time_hits: scan.hits.map((hit) => `${hit.shape}@${hit.where}`),
        }
        rows.push(row)
        if (probe.problems.length > 0) {
            failures.push(`import ${module}: ${row.probe}`)
        }
        // A hit is allowed only where the table says import does that work. `python` and
        // `pyplot` read the venv, `ifstream` its `pyvenv.cfg`, and initialise CPython.
        if (class_ === 'c') {
            for (const hit of scan.hits) {
                failures.push(
                    `import ${module}: ${hit.shape} at ${hit.where} is import-time work and the table calls the row class=c; classify it (docs/technical/53_import_cost_audit.md) or move it to first use — ${hit.code}`,
                )
            }
        }
    }

    const resolution = resolutionCost(Math.min(reps, 3))
    const declaration = declarationCost(Math.min(reps, 3))

    if (asJson) {
        console.log(
            JSON.stringify(
                {
                    rows,
                    baseline_wall_ms: Number(baseline.wall_ms.toFixed(2)),
                    resolution,
                    declaration,
                    failures,
                },
                null,
                2,
            ),
        )
    } else {
        console.log(
            `import.audit modules=${MODULES.length} a=${rows.filter((row) => row.class === 'a').length} b=${rows.filter((row) => row.class === 'b').length} c=${rows.filter((row) => row.class === 'c').length} baseline_ms=${baseline.wall_ms.toFixed(2)}`,
        )
        for (const row of rows) {
            const times = [
                `wall_ms=${row.wall_ms}`,
                `marginal_ms=${row.marginal_ms}`,
                `cmo_load_ms=${row.cmo_load_ms}`,
                `resolve_ms=${row.resolve_ms}`,
                `compile_ms=${row.compile_ms}`,
            ].join(' ')
            const extra = []
            if (row.class !== 'c') {
                extra.push(`reason=${JSON.stringify(row.reason)}`)
            }
            if (row.import_time_hits.length > 0) {
                extra.push(`work=${row.import_time_hits.join(',')}`)
            }
            console.log(
                `import.audit ${row.module.padEnd(11)} class=${row.class} probe=${row.probe} cpp=${row.cpp ? 'yes' : 'no'} ${times}${extra.length > 0 ? ' ' + extra.join(' ') : ''}`,
            )
        }
        console.log(
            `import.audit resolution roots=${resolution.roots} plain_resolve_ms=${resolution.plain_resolve_ms} packages_resolve_ms=${resolution.packages_resolve_ms} plain_wall_ms=${resolution.plain_wall_ms.toFixed(2)} packages_wall_ms=${resolution.packages_wall_ms.toFixed(2)} include3_wall_ms=${resolution.include_wall_ms.toFixed(2)} include_status=${resolution.include_status}`,
        )
        for (const entry of declaration) {
            console.log(
                `import.audit declaration module=${entry.module} exports=${entry.exports} lines=${entry.lines} compile_ms=${entry.compile_ms.toFixed(2)}`,
            )
        }
        for (const failure of failures) {
            console.error(`import-audit: ${failure}`)
        }
    }
    return failures.length === 0 ? 0 : 1
}

process.exit(main(process.argv.slice(2)))