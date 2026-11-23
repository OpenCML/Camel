// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The clang syntax gate: compile this tree's translation units with CI's compiler and CI's exact
 * warning set, `-fsyntax-only`, and fail on anything the CI build would fail on.
 *
 * Why it exists: the local gate compiles with one toolchain on one platform. A diagnostic that only
 * another toolchain reports -- `-Wc++11-narrowing` is the one that cost a day (D-1123-83) -- stays
 * invisible until a CI run, and CI runs are the slowest reading in the project. This step takes that
 * reading locally, on the build host, in the time of a syntax pass.
 *
 * Where the flags come from: not from this file and not from the workflow. The warning set is
 * `cmake/Warnings.cmake`, and the per-target flags, includes and defines are the ones CMake wrote
 * into `build/compile_commands.json` for this very checkout. This script reads that database and
 * changes exactly one token of each command -- the compiler -- and drops `-c`/`-o` in favour of
 * `-fsyntax-only`. Everything else, down to the order of the `-Wno-` switches, is the build's own
 * line, which is the only way the gate cannot drift from CI by a flag someone forgot to copy.
 *
 * The compiler: CI's Linux leg is clang 19 (`.github/workflows/ci-leg.yml` installs `clang-19` on
 * ubuntu-24.04). The build host's own clang is a newer major, and it does not report every
 * diagnostic 19 does, so the gate runs 19 from a container image pinned for this purpose. The image
 * and the two toolchain arguments that point clang 19 at the headers this tree is built against are
 * parameters: no host path lives in this file.
 *
 * What is checked:
 *   all      every translation unit of the database (the default when the caller names no files)
 *   changed  the units the change touches, plus every unit that includes a changed header -- the
 *            header's dependents are read from the build's own dependency graph (`ninja -t deps`),
 *            because a header that breaks one unit breaks every unit that includes it. When the
 *            dependency graph cannot answer for a changed file, this widens to all instead of
 *            narrowing to "fast but leaky" (D-1123-84, prohibition 2).
 *
 * Exit codes: 0 no diagnostics that the CI build would fail on; 1 at least one unit failed;
 * 2 the gate could not take a reading (no database, no command for a named file, no image) -- a
 * check that could not run is not a green one.
 */

import { execFileSync, spawnSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

// The image CI's clang 19 comes from; --image overrides it.
const DEFAULT_IMAGE = 'silkeh/clang:19'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

function usage() {
    console.error(`usage: node scripts/check-clang-syntax.mjs [options]

  --jobs N              parallel compiles inside the container (default: min(16, cpus))
  --files a.cpp,b.cpp   check exactly these paths (relative to the repository root)
  --changed [BASE]      check the change against BASE (default origin/develop): changed units plus
                        every unit that includes a changed header
  --all                 check every translation unit in the build database
  --image NAME          the container image carrying clang 19
  --cxx PATH            the compiler inside that image (default /usr/lib/llvm-19/bin/clang++)
  --gcc-install-dir DIR the libstdc++ this tree is built against (passed to clang 19)
  --sysroot DIR         the sysroot of that libstdc++ (passed to clang 19)
  --database PATH       compile_commands.json (default build/compile_commands.json)
  --dry-run             print what would run and exit 0`)
    process.exit(2)
}

const argv = []
for (const a of process.argv.slice(2)) {
    // `--name=value` and `--name value` are the same option.
    const at = a.indexOf('=')
    if (a.startsWith('--') && at > 2) argv.push(a.slice(0, at), a.slice(at + 1))
    else argv.push(a)
}
const opt = {
    jobs: Math.min(16, os.cpus().length),
    files: null,
    mode: 'all',
    base: 'origin/develop',
    image: '',
    cxx: '/usr/lib/llvm-19/bin/clang++',
    gccInstallDir: '',
    sysroot: '',
    database: path.join(ROOT, 'build', 'compile_commands.json'),
    dryRun: false,
}
for (let i = 0; i < argv.length; i++) {
    const a = argv[i]
    const next = () => {
        if (i + 1 >= argv.length) usage()
        return argv[++i]
    }
    switch (a) {
        case '--jobs': opt.jobs = Number(next()); break
        case '--files': opt.files = next().split(',').map((s) => s.trim()).filter(Boolean); break
        case '--changed':
            opt.mode = 'changed'
            if (argv[i + 1] && !argv[i + 1].startsWith('--')) opt.base = argv[++i]
            break
        case '--all': opt.mode = 'all'; break
        case '--image': opt.image = next(); break
        case '--cxx': opt.cxx = next(); break
        case '--gcc-install-dir': opt.gccInstallDir = next(); break
        case '--sysroot': opt.sysroot = next(); break
        case '--database': opt.database = next(); break
        case '--dry-run': opt.dryRun = true; break
        default: usage()
    }
}
if (opt.files) opt.mode = 'files'

function die(why) {
    console.error(`clang-syntax: ${why}`)
    process.exit(2)
}

// --- the build's own command lines -----------------------------------------
if (!fs.existsSync(opt.database)) die(`no compile database at ${opt.database}: the checkout has not been configured, so there is no line to reproduce`)
const db = JSON.parse(fs.readFileSync(opt.database, 'utf8'))

/** One command per source file: a multi-config database carries one per configuration. */
const byFile = new Map()
for (const entry of db) {
    const file = path.resolve(entry.directory, entry.file)
    const previous = byFile.get(file)
    // Release is the configuration CI builds; its flags are the frontier of what can break.
    const release = String(entry.command || '').includes('CMAKE_INTDIR=\\"Release\\"')
    if (!previous || (release && !previous.release)) byFile.set(file, { entry, release })
}

function splitCommand(command) {
    // A shell command line. CMake escapes the quotes of a definition (`-DCMAKE_INTDIR=\"Release\"`)
    // and they are part of the token: `-DCMAKE_INTDIR=Release` would define an identifier instead
    // of a string literal, so the quotes survive into argv here exactly as the shell would leave
    // them.
    const out = []
    let cur = ''
    let quote = null
    for (let i = 0; i < command.length; i++) {
        const c = command[i]
        if (c === '\\' && command[i + 1] === '"') { cur += '"'; i++; continue }
        if (quote) { if (c === quote) quote = null; else cur += c; continue }
        if (c === '"' || c === "'") { quote = c; continue }
        if (/\s/.test(c)) { if (cur) { out.push(cur); cur = '' } continue }
        cur += c
    }
    if (cur) out.push(cur)
    return out
}

function syntaxArgs(file, extra) {
    const { entry } = byFile.get(file)
    const args = splitCommand(entry.command)
    const out = []
    for (let i = 0; i < args.length; i++) {
        const a = args[i]
        if (a === '-o') { i++; continue }
        if (a === '-c') continue
        if (i === 0) continue
        // The source token names the file this command was written for; the map's absolute path is
        // the same file, but it is also what a replay re-points at a scratch tree, so it is written
        // from the map rather than copied from the line.
        if (a === entry.file || a === file) continue
        out.push(a)
    }
    // The compiler's own defaults must be the ones the build used: the same libstdc++ headers.
    if (opt.gccInstallDir) out.unshift(`--gcc-install-dir=${opt.gccInstallDir}`)
    if (opt.sysroot) out.unshift(`--sysroot=${opt.sysroot}`)
    out.push('-fsyntax-only')
    out.push(file)
    return out.concat(extra || [])
}

// --- which translation units -------------------------------------------------
const MSVC = false
function git(args) {
    return execFileSync('git', ['-C', ROOT, ...args], { encoding: 'utf8' })
}

let selected = []
let widened = ''

if (opt.mode === 'files') {
    for (const rel of opt.files) {
        const abs = path.resolve(ROOT, rel)
        if (!byFile.has(abs)) die(`no compile command for ${rel}: the database does not describe this tree (a new source needs a configure first)`)
        selected.push(abs)
    }
} else if (opt.mode === 'changed') {
    let baseCommit
    // A merge commit is read against its first parent, which is the branch it is being merged into:
    // the question at a merge is "what does this merge bring", and the base ref is useless here --
    // the gate pushes the merged commit, so `origin/develop` IS this commit and the diff against it
    // is empty. That empty diff is not "nothing changed", it is "nothing was compared": it made the
    // first end-to-end run of this gate report `units=0 ... exit 0` on a tree with a planted error.
    const parents = (() => {
        try {
            return git(['rev-list', '--parents', '-n', '1', 'HEAD']).trim().split(/\s+/).slice(1)
        } catch {
            return []
        }
    })()
    if (parents.length > 1) {
        baseCommit = parents[0]
        console.log(`clang-syntax: HEAD is a merge; reading the change against its first parent (${baseCommit.slice(0, 10)})`)
    } else {
        try {
            baseCommit = git(['merge-base', opt.base, 'HEAD']).trim()
        } catch {
            // A checkout that never fetched the base still knows the commit it is built on.
            try {
                baseCommit = git(['rev-parse', 'HEAD^1']).trim()
                console.log(`clang-syntax: ${opt.base} does not resolve here; reading the change against HEAD^1 (${baseCommit.slice(0, 10)})`)
            } catch {
                opt.mode = 'all'
                widened = `neither ${opt.base} nor HEAD^1 resolves, so every unit is checked`
            }
        }
        // A base that IS this commit says nothing about what this tree brings: on the host the base
        // ref is the very commit the wrapper just pushed, so this is the ordinary case there and not
        // an empty change. The commit's own content is then the change to read (HEAD^1), with the
        // same line as the unresolvable base above, so the reading names what it compared. A tree
        // whose HEAD really is already contained in the base is refused by the caller (which is the
        // only place the two can be told apart: camel-remote.ps1 compares the checkout's own
        // merge-base before it pushes anything).
        if (baseCommit === git(['rev-parse', 'HEAD']).trim()) {
            try {
                const own = git(['rev-parse', 'HEAD^1']).trim()
                console.log(`clang-syntax: ${opt.base} is this commit; reading the change against HEAD^1 (${own.slice(0, 10)})`)
                baseCommit = own
            } catch {
                // A root commit has nothing to compare against, so there is no change to select.
                opt.mode = 'all'
                widened = 'this commit has no parent, so every unit is checked'
            }
        }
    }
    if (opt.mode === 'changed') {
        const changed = git(['diff', '--name-only', baseCommit, 'HEAD']).split('\n').filter(Boolean)
    // Two zeroes read the same on the summary line and mean opposite things: "the change compiles
    // nothing" (a plan, a document, a golden -- a real reading about a real change) and "there was
    // no change to compare" (base and HEAD are the same commit -- nothing was measured at all).
    // The second is not a green: a lane whose HEAD is already contained in develop has an empty
    // `--changed` diff forever, so this gate would hand it a blank pass on every run, and at the
    // merge position the base IS this commit. Same rule as the row channel: a zero that no
    // measurement produced is a refusal, not a pass.
    if (changed.length === 0) {
        die(`the base and this tree are the same commit (${baseCommit.slice(0, 10)}): there was no change to measure, so nothing was checked`)
    }
    const touched = new Set()
    const headers = []
    for (const rel of changed) {
        if (rel.endsWith('.cpp')) touched.add(path.resolve(ROOT, rel))
        else if (/\.(h|hpp|hxx|inc)$/.test(rel)) headers.push(path.resolve(ROOT, rel))
    }
    // The dependents of a changed header, from the build graph that compiled this tree.
    if (headers.length > 0) {
        const deps = ninjaDeps()
        if (!deps) {
            widened = 'no dependency graph in the build directory, so every unit is checked'
        } else {
            for (const header of headers) {
                const users = deps.get(header)
                if (!users) {
                    widened = `no dependency record for ${path.relative(ROOT, header)}, so every unit is checked`
                    break
                }
                for (const unit of users) touched.add(unit)
            }
        }
    }
    if (widened) selected = [...byFile.keys()]
    else {
        for (const unit of touched) if (byFile.has(unit)) selected.push(unit)
        // A changed unit with no command (a new file) means the database is older than the tree.
        for (const unit of touched) if (!byFile.has(unit)) die(`no compile command for ${path.relative(ROOT, unit)}: the database does not describe this tree (a new source needs a configure first)`)
        }
    }
} else {
    selected = [...byFile.keys()]
}

selected.sort()

/** `ninja -t deps` -> Map<absolute input path, Set<absolute object source path>>. */
/** The build files to ask for dependency records, in the order worth asking. */
function buildFiles(buildDir) {
    let names = []
    try {
        names = fs.readdirSync(buildDir).filter((n) => n.endsWith('.ninja'))
    } catch {
        return ['build.ninja']
    }
    const release = names.filter((n) => /Release/.test(n))
    const rest = names.filter((n) => !/Release/.test(n) && n !== 'build.ninja')
    return [...release, 'build.ninja', ...rest]
}

function ninjaDeps() {
    const buildDir = path.join(ROOT, 'build')
    if (!fs.existsSync(path.join(buildDir, '.ninja_deps'))) return null
    // `ninja` is not on a non-login PATH on the build host (it lives in the toolchain's bin, beside
    // the compiler the database names), and a missing ninja used to read as "no dependency graph",
    // which widened every change to the whole tree -- 160 s per merge instead of 2 s. Ask the
    // compiler's own directory before giving up, and say which binary answered.
    const candidates = [process.env.CAMEL_NINJA, 'ninja', 'ninja-build', buildCompiler && path.join(path.dirname(buildCompiler), 'ninja'), '/usr/bin/ninja', path.join(os.homedir(), '.local/bin/ninja'), path.join(os.homedir(), '.local/bin/ninja-build')].filter(Boolean)
    let r = null
    let used = ''
    for (const candidate of candidates) {
        // Ninja Multi-Config writes one build file per configuration and the targets -- and so their
        // dependency records -- live in `build-<Config>.ninja`; `build.ninja` answers `-t deps` with
        // nothing. Release first, because that is the configuration the database's commands name.
        for (const file of buildFiles(buildDir)) {
            r = spawnSync(candidate, ['-C', buildDir, '-f', file, '-t', 'deps'], { encoding: 'utf8', maxBuffer: 512 * 1024 * 1024 })
            if (r && r.status === 0 && r.stdout) break
            r = null
        }
        if (r && r.status === 0 && r.stdout) {
            used = candidate
            break
        }
        r = null
    }
    if (!r) return null
    if (used !== 'ninja') console.log(`clang-syntax: dependency graph read with ${used}`)
    if (r.status !== 0 || !r.stdout) return null
    const map = new Map()
    let unit = ''
    for (const line of r.stdout.split('\n')) {
        if (!line.startsWith('  ')) {
            // "obj/....o: #deps 12, deps mtime ... (VALID)"
            const m = line.match(/^(\S+\.o):/)
            unit = m ? path.resolve(buildDir, m[1]) : ''
            continue
        }
        if (!unit) continue
        const input = line.trim().split(/\s+/).slice(1).join(' ')
        if (!input) continue
        const abs = path.resolve(buildDir, input)
        if (!map.has(abs)) map.set(abs, new Set())
        map.get(abs).add(unit)
    }
    // The graph names objects, not sources: map each object back to the source that produced it.
    const objectSource = new Map()
    for (const [file, { entry }] of byFile) {
        const args = splitCommand(entry.command)
        const at = args.lastIndexOf('-o')
        if (at > 0 && args[at + 1]) objectSource.set(path.resolve(entry.directory, args[at + 1]), file)
    }
    const bySource = new Map()
    for (const [input, objects] of map) {
        const sources = new Set()
        for (const o of objects) {
            const src = objectSource.get(o)
            if (src) sources.add(src)
        }
        if (sources.size > 0) bySource.set(input, sources)
    }
    return bySource
}

// --- the toolchain clang 19 must be pointed at -------------------------------
// The database was produced by this tree's own compiler; its standard library is the one the build
// reads, so clang 19 is given that same one. Asking that compiler rather than naming a path here is
// what keeps this file free of host paths and correct when the toolchain moves.
var buildCompiler = ''
function toolchainOfTheBuild() {
    const onLine = splitCommand([...byFile.values()][0].entry.command)[0]
    buildCompiler = onLine
    if (!onLine) return {}
    const r = spawnSync(onLine, ['-v', '-E', '-x', 'c++', '/dev/null'], { encoding: 'utf8' })
    const text = (r.stderr || '') + (r.stdout || '')
    const gcc = text.match(/Selected GCC installation:\s*(\S+)/)
    const sysroot = text.match(/-isysroot\s+(\S+)/)
    const found = {}
    if (gcc) found.gccInstallDir = gcc[1]
    if (sysroot) found.sysroot = sysroot[1]
    return found
}

if (!opt.gccInstallDir || !opt.sysroot) {
    const derived = toolchainOfTheBuild()
    if (!opt.gccInstallDir) opt.gccInstallDir = derived.gccInstallDir || ''
    if (!opt.sysroot) opt.sysroot = derived.sysroot || ''
}

// --- the reading -------------------------------------------------------------
const summary = {
    units: selected.length,
    failed: 0,
    seconds: 0,
}

if (opt.dryRun) {
    console.log(`clang-syntax: ${selected.length} unit(s) would be checked${widened ? ` (${widened})` : ''}`)
    process.exit(0)
}

// CI's own major, from an image that carries it: `silkeh/clang:19` is clang 19.1.7 on Debian, the
// same upstream release Ubuntu 24.04's leg installs (19.1.x), and `--image` names a different one
// when a host holds it elsewhere. The image is a name, not a path, so it leaks no host into the tree.
if (!opt.image) {
    opt.image = DEFAULT_IMAGE
    console.log(`clang-syntax: no --image given; using ${DEFAULT_IMAGE} (this is CI's major, not the host's compiler)`)
}
if (!opt.gccInstallDir || !opt.sysroot) {
    die('cannot read the standard library this tree is built against (the database\'s own compiler ' +
        'named no "Selected GCC installation"): pass --gcc-install-dir and --sysroot, because ' +
        'clang 19\'s defaults are not this tree\'s and would report diagnostics the build never sees')
}

// The version, printed with every reading: a syntax gate whose compiler is not the CI one is a
// different gate, and the only way a reader can check that is to see it in the output.
const versionLine = (() => {
    const r = spawnSync('docker', ['run', '--rm', opt.image, opt.cxx, '--version'], { encoding: 'utf8' })
    return (r.stdout || '').split('\n')[0].trim() || '(version not readable)'
})()
console.log(`clang-syntax: compiler=${versionLine} image=${opt.image}`)
console.log(`clang-syntax: gcc-install-dir=${opt.gccInstallDir} sysroot=${opt.sysroot}`)

// The scratch lives outside the tree. A run that writes into `build/` leaves the checkout looking
// edited, and the gate resets a checkout it believes nobody touched: the next run would then check
// the tree it just left instead of the one it was given, and report the old reading as the new one.
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'clang-syntax-'))
process.on('exit', () => fs.rmSync(work, { recursive: true, force: true }))
fs.mkdirSync(path.join(work, 'out'), { recursive: true })

function q(s) {
    return `'${String(s).replace(/'/g, `'\\''`)}'`
}

const lines = []
selected.forEach((file, i) => {
    const args = syntaxArgs(file, ['-fno-color-diagnostics'])
    const directory = byFile.get(file).entry.directory
    const errFile = path.join(work, 'out', `${String(i).padStart(4, '0')}.err`)
    const rcFile = path.join(work, 'out', `${String(i).padStart(4, '0')}.rc`)
    lines.push(`cd ${q(directory)} && ${q(opt.cxx)} ${args.map(q).join(' ')} >${q(errFile)} 2>&1; echo $? >${q(rcFile)}`)
})
fs.writeFileSync(path.join(work, 'jobs.txt'), lines.join('\n') + '\n')
fs.writeFileSync(path.join(work, 'files.txt'), selected.map((f) => path.relative(ROOT, f)).join('\n') + '\n')

const script = `set -u
cd ${q(ROOT)}
xargs -d '\\n' -n 1 -P ${opt.jobs} sh -c < ${q(path.join(work, 'jobs.txt'))}
`
fs.writeFileSync(path.join(work, 'run.sh'), script)

const started = Date.now()
// The tree and the home directory are what a compile line reaches for (the tree's sources, the
// toolchain's installation); the scratch directory carries the script the container runs.
const mounts = ['-v', `${ROOT}:${ROOT}`, '-v', `${os.homedir()}:${os.homedir()}`, '-v', `${work}:${work}`]
const run = spawnSync(
    'docker',
    ['run', '--rm', '--network=none', ...mounts, '-w', ROOT, opt.image, 'sh', path.join(work, 'run.sh')],
    { stdio: 'inherit' },
)
const seconds = Math.round((Date.now() - started) / 100) / 10
summary.seconds = seconds

if (run.error || run.status !== 0) {
    die(`the container could not run the compiles (${run.error ? run.error.message : `exit ${run.status}`}); no reading was taken`)
}

const failed = []
const unable = []
// A unit can be unchecked for a reason that is about this container and not about the tree: the
// host's configuration compiles CUDA, Python bindings and BLAS (whose headers the host has and the
// image does not), and replaying those lines here fails with `'cuda.h' file not found`. Counting
// that as a red would refuse every merge on a tree nobody has touched, and calling it green would
// hide the unit entirely -- so it is its own reading: named, counted, and never a pass. The test is
// the tree, not the message: a header the tree itself holds and cannot find is a real defect.
for (let i = 0; i < selected.length; i++) {
    const rc = Number(fs.readFileSync(path.join(work, 'out', `${String(i).padStart(4, '0')}.rc`), 'utf8').trim())
    if (rc === 0) continue
    const err = fs.readFileSync(path.join(work, 'out', `${String(i).padStart(4, '0')}.err`), 'utf8')
    const missing = [...err.matchAll(/fatal error: '([^']+)' file not found/g)].map((m) => m[1])
    const outside = missing.length > 0 && missing.every((h) => !existsInTree(h))
    if (outside) unable.push({ file: selected[i], headers: missing })
    else failed.push({ file: selected[i], rc })
}

function existsInTree(name) {
    const stack = [ROOT]
    while (stack.length > 0) {
        const dir = stack.pop()
        let entries
        try {
            entries = fs.readdirSync(dir, { withFileTypes: true })
        } catch {
            continue
        }
        for (const e of entries) {
            if (e.isDirectory()) {
                if (e.name !== '.git' && e.name !== 'build' && e.name !== 'out') stack.push(path.join(dir, e.name))
            } else if (e.name === name) {
                return true
            }
        }
    }
    return false
}

const warnings = []
for (let i = 0; i < selected.length; i++) {
    const err = fs.readFileSync(path.join(work, 'out', `${String(i).padStart(4, '0')}.err`), 'utf8')
    for (const line of err.split('\n')) {
        if (/: warning: /.test(line)) warnings.push(line.replace(`${ROOT}/`, '').replaceAll(ROOT, ''))
    }
}

for (const bad of failed) {
    const i = selected.indexOf(bad.file)
    const err = fs.readFileSync(path.join(work, 'out', `${String(i).padStart(4, '0')}.err`), 'utf8')
    console.log(`--- ${path.relative(ROOT, bad.file)} (exit ${bad.rc})`)
    console.log(err.replaceAll(ROOT, '').trimEnd())
}

if (warnings.length > 0) {
    console.log(`clang-syntax: ${warnings.length} warning(s) the CI build also prints but does not fail on:`)
    for (const w of [...new Set(warnings)].slice(0, 20)) console.log(`  ${w}`)
}

if (unable.length > 0) {
    console.log(`clang-syntax: ${unable.length} unit(s) could not be checked here: a header they include is in no file of the tree, so it belongs to a package this image does not carry (${[...new Set(unable.flatMap((u) => u.headers))].join(', ')}). They are unchecked, not green:`)
    for (const u of unable) console.log(`  ${path.relative(ROOT, u.file)}`)
}

console.log(
    `clang-syntax: units=${summary.units} failed=${failed.length} unchecked=${unable.length} warnings=${new Set(warnings).size} ` +
        `seconds=${seconds}${widened ? ` widened=${JSON.stringify(widened)}` : ''} mode=${opt.mode}`,
)
// A zero here is a reading about a change that compiles nothing (a document, a plan, a golden), and
// it must say so: an unannounced `units=0` is indistinguishable from a selection that measured the
// wrong thing, which is how this gate first reported green on a tree with a planted error.
if (summary.units === 0 && opt.mode !== 'all') {
    console.log('clang-syntax: this change compiles no unit -- nothing was measured, and nothing needed to be')
}

process.exit(failed.length > 0 ? 1 : 0)