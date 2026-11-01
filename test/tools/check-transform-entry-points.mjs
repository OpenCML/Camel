// Every transform entry point decides what it does with a function argument, and the decision is checked.
//
// A transform builds a graph out of the graph of the function value it is handed, which is why the
// argument has to be a function the compiler can name before the program runs. Three operators declare
// that rule today (`transforms:transpose`, `transforms:jvp`, `transforms:split`), and a rule that lives
// in three places is a rule someone forgets in the fourth -- so the set of entry points is *read out of
// the sources*, not written down in this file:
//
//   - an entry point is an operator of `transforms` or `autodiff` whose traits say `readsGraphs = true`;
//   - every entry point must appear in `modules/transforms/entry-points.tsv`, and the file must say for
//     it either `declared` (then the sources must really declare `CallSpec::staticFunctionArg` with a
//     real index) or `undeclared` (then the note says what it does instead -- a decision held open on
//     purpose, printed on every run);
//   - a `transforms`/`autodiff` operator that reads graphs and is in neither is red, naming the operator.
//
// The other modules' graph-reading operators (`tiles`, `onnx`) are out of scope by construction: they
// read a graph to compile or export it, they are handed no function value whose name could be missing,
// and the scan below does not look at them.
//
//   node test/tools/check-transform-entry-points.mjs
//
// Exits 0 when every decision agrees with the sources, 1 with the operator named when one does not.

import fs from 'fs'
import path from 'path'
import { fileURLToPath } from 'url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const REGISTRY = 'modules/transforms/entry-points.tsv'
const SCANNED_MODULES = ['transforms', 'autodiff']
// The sources an operator's traits and call-site facts are declared in. The transform operators are
// declared in the module that owns them; `autodiff` declares its four in one loop over a name list.
const SOURCE_DIRS = ['modules', 'src']

function sourceFiles() {
    const out = []
    const walk = (dir) => {
        for (const entry of fs.readdirSync(path.join(ROOT, dir), { withFileTypes: true })) {
            const rel = path.join(dir, entry.name)
            if (entry.isDirectory()) {
                if (entry.name === 'build' || entry.name === 'out' || entry.name === 'node_modules') continue
                walk(rel)
            } else if (/\.(cpp|h)$/.test(entry.name)) {
                out.push(rel)
            }
        }
    }
    for (const dir of SOURCE_DIRS) walk(dir)
    return out
}

// An operator's traits are written in one statement, and the uri is either in that statement or, for the
// loop autodiff declares its four in, in the `for (... : { names })` header above it. So the scan reads
// the statement that carries `readsGraphs = true` first and the loop header only when the statement has
// no uri of its own -- a window of lines would also catch the neighbouring operator's name, which is
// exactly the false positive that made this file's first version report `autodiff:stop_gradient`.
const URI = /"([a-z_]+):([a-z_]+)"/g

function urisIn(text) {
    return [...text.matchAll(URI)].map((match) => ({
        uri: `${match[1]}:${match[2]}`,
        module: match[1],
    }))
}

function entriesFromSources() {
    const found = new Map()
    for (const file of sourceFiles()) {
        const text = fs.readFileSync(path.join(ROOT, file), 'utf8')
        for (const match of text.matchAll(/readsGraphs\s*=\s*true/g)) {
            const before = text.slice(0, match.index)
            const callStart = Math.max(before.lastIndexOf('set('), before.lastIndexOf('declare('))
            const callEnd = text.indexOf(';', match.index)
            const statement = callStart >= 0 && callEnd > callStart ? text.slice(callStart, callEnd) : ''
            let uris = urisIn(statement)
            if (uris.length === 0) {
                // The loop form: the names are in the `for` header, the statement sets `name`.
                const forStart = before.lastIndexOf('for (')
                const header = forStart >= 0 ? text.slice(forStart, callStart) : ''
                uris = urisIn(header)
            }
            for (const entry of uris) {
                if (!SCANNED_MODULES.includes(entry.module)) continue
                if (!found.has(entry.uri)) {
                    const line = text.slice(0, match.index).split('\n').length
                    found.set(entry.uri, { file, line })
                }
            }
        }
    }
    return found
}

function readRegistry() {
    const rows = new Map()
    for (const line of fs.readFileSync(path.join(ROOT, REGISTRY), 'utf8').split('\n')) {
        if (line.startsWith('#') || line.trim() === '') continue
        const [uri, decision, note] = line.split('\t')
        rows.set(uri, { uri, decision, note: note ?? '' })
    }
    return rows
}

// The declaration has to be a real one: `declareCallSpec("<uri>", {.staticFunctionArg = <n>})` with
// `n >= 0`. A commented-out line, or the same uri with `-1` (the A/B), is not a declaration.
function declaredInSources(uri) {
    const text = sourceFiles()
        .map((file) => fs.readFileSync(path.join(ROOT, file), 'utf8'))
        .join('\n')
    const pattern = new RegExp(
        `^[^/\\n]*declareCallSpec\\(\\s*"${uri.replace(':', '\\:')}"\\s*,\\s*\\{[^}]*staticFunctionArg\\s*=\\s*(\\d+)`,
        'm',
    )
    const match = text.match(pattern)
    return match !== null && Number(match[1]) >= 0
}

function main() {
    const sources = entriesFromSources()
    const registry = readRegistry()
    const problems = []

    for (const [uri, where] of sources) {
        const row = registry.get(uri)
        if (row === undefined) {
            problems.push(
                `${uri} reads a graph (${where.file}:${where.line}) and ${REGISTRY} says nothing about it: decide whether it declares staticFunctionArg, and write the decision there`,
            )
            continue
        }
        if (row.decision === 'declared' && !declaredInSources(uri)) {
            problems.push(
                `${uri} is recorded as 'declared' and the sources declare no CallSpec::staticFunctionArg for it -- this is the line that keeps the call from being refused at the call site`,
            )
        }
    }
    for (const uri of registry.keys()) {
        if (!sources.has(uri)) {
            problems.push(`${REGISTRY} records ${uri}, and no operator of that name reads a graph any more`)
        }
    }

    const declared = [...registry.values()].filter((r) => r.decision === 'declared').length
    const undeclared = [...registry.values()].filter((r) => r.decision === 'undeclared')
    console.log(`[entry-points] total: ${sources.size}, declared: ${declared}, undeclared: ${undeclared.length}`)
    for (const row of undeclared) {
        console.log(`[entry-points] undeclared: ${row.uri} -- ${row.note}`)
    }
    if (problems.length > 0) {
        for (const problem of problems) console.log(`[entry-points] FAIL: ${problem}`)
        console.log(`[entry-points] ${problems.length} problem(s)`)
        process.exit(1)
    }
    console.log(
        `[entry-points] ok: ${sources.size} transform entry points, ${declared} declared and verified, ${undeclared.length} recorded as not declared`,
    )
}

main()