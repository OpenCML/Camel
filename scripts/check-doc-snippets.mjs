#!/usr/bin/env node
// Fails when a reader-facing page shows a Camel snippet that the current language does not
// accept, or whose printed output is not what the page says it is.
//
//   node scripts/check-doc-snippets.mjs              check the reader-facing pages
//   node scripts/check-doc-snippets.mjs --default-roots  the same, ignoring a script path argument
//   node scripts/check-doc-snippets.mjs FILE DIR  check the given files and directories
//
// A Camel fence is a fenced block tagged `camel` or `cml` (both tags are used in the tree).
// The page itself is the source of truth for what the snippet means:
//
//   * A `text` fence directly after the Camel fence, or after a `bash`/`sh` command fence that
//     runs it, is the output the page declares (the convention of `docs/language/README.md`:
//     "a `text` code block is its output"). The snippet is run and its stdout must match. A
//     command fence chooses the passes and `--arg` values, exactly as the page does.
//   * Otherwise the snippet only has to compile: parse, GCT and GIR, asserted with
//     `camel <file> std::gir`. A fence with no entry point is compiled as a module with a
//     generated `func main(): int sync { return 0 }`; the fence itself is not changed.
//   * Adjacent Camel fences whose first lines name files (`// shapes.cml`, then `// main.cml`)
//     are one example: the files are written into one directory and the entry is the fence
//     with `func main`.
//
// A fence that is deliberately not a program is marked, never guessed at. The marker is an
// HTML comment on the line before the opening fence, so it does not appear on the page:
//
//   <!-- camel-snippet: fragment: continues the function above -->
//   <!-- camel-snippet: counter-example: the two returns do not leave the function -->
//   <!-- camel-snippet: graph-dump: the text block is std::rgir's DOT, not stdout -->
//   <!-- camel-snippet: compile-only: the text block is a grammar, not output -->
//
// `fragment` and `counter-example` are not compiled. A `counter-example` must also say so on
// the page: the fence or the paragraph before it has to contain `counter-example`, `wrong`,
// `反例`, `错误`, `does not compile` or `is an error`. A marker without a reason is an error.
//
// On a `docs/design/` or `docs/technical/` page a fence without an entry point is an excerpt,
// not a program: those pages quote fragments, proposed syntax and measurements. Only their
// fences that contain `func main` are compiled. The rule is in EXCERPTS below, with the reason.
// The record directories (docs/handover, docs/reviews) are not pages and are not scanned; three
// generated or dated references are allowlisted because their `camel` fences are signatures,
// diagnostics or excerpts of an earlier state, not programs.
//
// The run is parallel (one camel process per snippet, up to JOBS at a time) because every
// snippet is its own entry file and its own temp directory; batching them into one process
// would mix module names and entries. The summary prints the wall time.
//
// Exit status 1 when a snippet is stale, 2 when the scope holds no document or no Camel fence: a
// check of nothing is refused, never reported as a pass.

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawn } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const JOBS = Math.max(1, Number(process.env.CAMEL_SNIPPET_JOBS || 8))
const TIMEOUT_MS = Number(process.env.CAMEL_SNIPPET_TIMEOUT_MS || 15000)

const CAMEL =
    process.env.CAMEL_EXE ||
    path.join(
        process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest'),
        'bin',
        process.platform === 'win32' ? 'camel.exe' : 'camel',
    )

// Directories that are records, not pages: they quote the old spelling to say what happened.
const RECORDS = ['docs/handover/', 'docs/reviews/']

// A generated reference is not a teaching page: its `camel` fences are signatures (one per
// overload) and the triggering examples of diagnostics, deliberately wrong.
const ALLOWED_FILES = [
    ['docs/stdlib-reference.md', 'generated: signature lines, one per overload, not programs'],
    ['docs/diagnostics.md', 'generated: each example is a program that raises the diagnostic on purpose'],
    ['docs/design-principles-log.cn.md', 'a dated implementation log: the fences are excerpts of the state then'],
]

// Design and technical notes are working records: they quote fragments, proposed syntax and
// measurements. Their fences with an entry point are programs and are checked; the others
// are excerpts, with this reason.
const EXCERPTS = [
    ['docs/design/', 'design note: excerpts and proposed syntax; only a fence with func main is a program'],
    ['docs/technical/', 'technical record: excerpts and measurements; only a fence with func main is a program'],
]

const OTHER_LANGUAGES = new Set([
    '', 'bash', 'c', 'cpp', 'c++', 'css', 'go', 'html', 'java', 'javascript', 'js', 'json',
    'lua', 'markdown', 'powershell', 'ps1', 'py', 'python', 'rust', 'sh', 'sql', 'toml', 'ts',
    'typescript', 'text', 'yaml', 'yml', 'diff', 'cmake', 'asm', 'elisp',
])

const CAMEL_LANGUAGES = new Set(['camel', 'cml'])
const MARKER = /<!--\s*camel-snippet:\s*([a-z-]+)\s*(?::\s*(.*?))?\s*-->/
const MARKED_KINDS = new Set(['fragment', 'counter-example', 'graph-dump', 'compile-only'])
const COUNTER_LABEL = /counter-?example|wrong\b|反例|错误|does not compile|is an error/i

const FENCE = /^(\s*)(```+|~~~+)\s*([A-Za-z0-9_+#-]*)(.*)$/
const FILE_HEADER = /^\s*\/\/\s*([A-Za-z0-9_.-]+\.cml)\b/
const HAS_ENTRY = /^\s*(?:sync\s+)?func\s+main\s*\(/m
const DIAGNOSTIC = /^(error|warning)\[([^\]]+)\]/gm

function relative(abs) {
    return path.relative(ROOT, abs).replace(/\\/g, '/')
}

function collect(entry, out) {
    const abs = path.resolve(ROOT, entry)
    if (!fs.existsSync(abs)) return
    const stat = fs.statSync(abs)
    if (stat.isFile()) {
        if (path.extname(abs) === '.md') out.push(abs)
        return
    }
    for (const name of fs.readdirSync(abs)) {
        if (name === '.git' || name === 'node_modules' || name === 'out' || name === 'build') continue
        const child = path.join(abs, name)
        const rel = relative(child)
        if (RECORDS.some((prefix) => rel === prefix.slice(0, -1) || rel.startsWith(prefix))) continue
        collect(rel, out)
    }
}

function allowedFile(abs) {
    const rel = relative(abs)
    return ALLOWED_FILES.find(([entry]) => rel === entry)
}

function excerptPage(abs) {
    const rel = relative(abs)
    return EXCERPTS.find(([prefix]) => rel === prefix.slice(0, -1) || rel.startsWith(prefix))
}

// ---- Markdown fences -----------------------------------------------------------------------

function parseFences(lines) {
    const fences = []
    let current = null
    for (let i = 0; i < lines.length; i++) {
        const match = FENCE.exec(lines[i])
        if (match) {
            if (current === null) {
                const before = lines
                    .slice(0, i)
                    .reverse()
                    .find((line) => line.trim() !== '')
                const marker = before ? MARKER.exec(before) : null
                current = {
                    open: i,
                    close: -1,
                    lang: match[3].toLowerCase(),
                    info: match[4].trim(),
                    body: [],
                    marker: marker ? { kind: marker[1].toLowerCase(), reason: (marker[2] || '').trim() } : null,
                }
            } else {
                current.close = i
                fences.push(current)
                current = null
            }
            continue
        }
        if (current !== null) current.body.push(lines[i])
    }
    if (current !== null) {
        current.close = lines.length - 1
        fences.push(current)
    }
    return fences
}

function isCamel(fence) {
    return CAMEL_LANGUAGES.has(fence.lang)
}

// True when only blank lines separate two fences.
function adjacent(lines, first, second) {
    for (let i = first.close + 1; i < second.open; i++) {
        if (lines[i].trim() !== '') return false
    }
    return true
}

function firstLine(fence) {
    return fence.body.find((line) => line.trim() !== '') || ''
}

function entryOf(fence) {
    return HAS_ENTRY.test(fence.body.join('\n'))
}

// ---- Snippet construction ------------------------------------------------------------------

function fileNameOf(fence, fallback) {
    const match = FILE_HEADER.exec(firstLine(fence))
    return match ? match[1] : fallback
}

// Groups adjacent Camel fences that are the files of one example. A fence whose first line is
// `// name.cml` starts a group; the next fence belongs to it when it is also a file (`// x.cml`)
// or when it is the entry (`func main`) of the file just named.
//
// `fragment` and `counter-example` fences are not snippets. `graph-dump` and `compile-only`
// fences are: they are compiled, but the `text` block after them is not compared with stdout.
function buildSnippets(lines, fences) {
    const snippets = []
    for (let i = 0; i < fences.length; i++) {
        const fence = fences[i]
        if (!isCamel(fence)) continue
        if (fence.marker && ['fragment', 'counter-example'].includes(fence.marker.kind)) continue
        const group = [fence]
        let last = i
        while (last + 1 < fences.length) {
            const next = fences[last + 1]
            if (!isCamel(next)) break
            if (next.marker && ['fragment', 'counter-example'].includes(next.marker.kind)) break
            if (!adjacent(lines, fences[last], next)) break
            const named = FILE_HEADER.test(firstLine(fence))
            const nextNamed = FILE_HEADER.test(firstLine(next))
            if (nextNamed || (named && entryOf(next))) {
                group.push(next)
                last++
                continue
            }
            break
        }
        i = last
        snippets.push({ fences: group, open: fence.open })
    }
    if (process.env.CAMEL_SNIPPET_DEBUG) {
        for (const snippet of snippets) {
            console.log(
                `DEBUG snippet ${snippet.open + 1}: ` +
                    snippet.fences
                        .map((fence) => `${fence.open + 1}:${fileNameOf(fence, '?')}${entryOf(fence) ? ':entry' : ''}`)
                        .join(', '),
            )
        }
    }
    return snippets
}

function outputAfter(lines, fences, snippet) {
    const last = snippet.fences[snippet.fences.length - 1]
    const index = fences.indexOf(last)
    if (index < 0) return null
    let cursor = index + 1
    if (cursor >= fences.length) return null
    if (!adjacent(lines, last, fences[cursor])) return null
    let command = null
    if (['bash', 'sh', 'powershell'].includes(fences[cursor].lang)) {
        command = fences[cursor]
        cursor++
        if (cursor >= fences.length || !adjacent(lines, command, fences[cursor])) return null
    }
    if (fences[cursor].lang !== 'text') return null
    return { command, output: fences[cursor] }
}

// ---- Command lines -------------------------------------------------------------------------

function shellSplit(text) {
    const tokens = []
    let current = ''
    let quote = null
    for (let i = 0; i < text.length; i++) {
        const ch = text[i]
        if (quote) {
            if (ch === quote) quote = null
            else current += ch
            continue
        }
        if (ch === "'" || ch === '"') {
            quote = ch
            continue
        }
        if (/\s/.test(ch)) {
            if (current !== '') {
                tokens.push(current)
                current = ''
            }
            continue
        }
        current += ch
    }
    if (current !== '') tokens.push(current)
    return tokens
}

function parseCommand(fence) {
    if (!fence) return null
    for (const line of fence.body) {
        const text = line
            .replace(/^\s*\/\/\s*/, '')
            .replace(/\s+#.*$/, '')
            .trim()
        if (!/^camel\b/.test(text)) continue
        const tokens = shellSplit(text)
        const args = []
        const passes = []
        let file = null
        let i = 1
        while (i < tokens.length) {
            const token = tokens[i]
            if (token === '--arg' || token === '-I' || token === '--include') {
                if (i + 1 < tokens.length) args.push(token, tokens[++i])
                i++
                continue
            }
            if (token.startsWith('--arg=')) {
                args.push('--arg', token.slice('--arg='.length))
                i++
                continue
            }
            if (token.startsWith('-I=')) {
                args.push('-I', token.slice(3))
                i++
                continue
            }
            if (token.startsWith('--include=')) {
                args.push('--include', token.slice('--include='.length))
                i++
                continue
            }
            if (token === '--input') {
                if (i + 1 < tokens.length) file = tokens[++i]
                i++
                continue
            }
            if (token.startsWith('--input=')) {
                file = token.slice('--input='.length)
                i++
                continue
            }
            if (token.startsWith('-')) {
                i++
                if (i < tokens.length && !tokens[i].startsWith('-') && !tokens[i].includes('::')) i++
                continue
            }
            if (file === null && /\.cml$/.test(token)) {
                file = token
                i++
                continue
            }
            if (token.includes('::')) {
                passes.push(token)
                i++
                continue
            }
            if (file === null) {
                file = token
                i++
                continue
            }
            i++
        }
        return { file, passes, args }
    }
    return null
}

// The transliteration passes print the graph instead of running the program: their `text`
// block is a dump, not stdout, so it is compiled but not compared.
function isGraphDump(passes) {
    return passes.some((pass) =>
        /^(std::(?:rgir|gir|jgir|cpp|agir|argir)|onnx::export|.*::(?:rgir|gir|jgir))/.test(pass),
    )
}

// ---- Running camel -------------------------------------------------------------------------

function runCamel(args, cwd) {
    return new Promise((resolve) => {
        const started = Date.now()
        const env = { ...process.env }
        // The test runner sets the short form for its own assertions; a snippet's text block is
        // written in the default text form, and warnings are part of what a page shows.
        delete env.CAMEL_ERROR_FORMAT
        env.CAMEL_HOME = process.env.CAMEL_HOME || path.join(ROOT, 'out', 'latest')
        const child = spawn(CAMEL, args, {
            cwd,
            env,
            stdio: ['ignore', 'pipe', 'pipe'],
        })
        let stdout = ''
        let stderr = ''
        let timedOut = false
        const timer = setTimeout(() => {
            timedOut = true
            child.kill('SIGKILL')
        }, TIMEOUT_MS)
        child.stdout.on('data', (chunk) => (stdout += chunk))
        child.stderr.on('data', (chunk) => (stderr += chunk))
        child.on('error', (error) => {
            clearTimeout(timer)
            resolve({ code: -1, stdout, stderr: stderr + error.message, timedOut, ms: Date.now() - started })
        })
        child.on('close', (code) => {
            clearTimeout(timer)
            resolve({ code, stdout, stderr, timedOut, ms: Date.now() - started })
        })
    })
}

function firstDiagnostic(result) {
    const text = `${result.stdout}\n${result.stderr}`
    const line = text
        .split(/\r?\n/)
        .map((entry) => entry.trim())
        .find((entry) => /\[Error\]|error\[|RuntimeDiag|SemanticDiag|SyntaxDiag|LexicalDiag/.test(entry))
    return (line || text.trim().split(/\r?\n/)[0] || 'no diagnostic').slice(0, 240)
}

// A `text` block that shows a diagnostic: the program prints the lines before it, then fails
// (an error) or warns (a warning). The name is asserted; the path is not, because the snippet
// runs from a temporary file.
function parseExpected(text) {
    const lines = text.replace(/\r\n/g, '\n').split('\n')
    const kept = []
    const diagnostics = []
    let inDiagnostic = false
    for (const line of lines) {
        if (/^(error|warning)\[/.test(line)) {
            inDiagnostic = true
            const match = /^(error|warning)\[([^\]]+)\]/.exec(line)
            diagnostics.push({ severity: match[1], name: match[2] })
            continue
        }
        if (inDiagnostic) {
            if (line.trim() === '') inDiagnostic = false
            continue
        }
        kept.push(line)
    }
    const expected = kept.join('\n').replace(/\n+$/, '')
    return { expected, diagnostics }
}

function beforeDiagnostic(text) {
    const lines = text.replace(/\r\n/g, '\n').split('\n')
    const index = lines.findIndex((line) => /^(error|warning)\[/.test(line))
    return (index < 0 ? lines : lines.slice(0, index)).join('\n').replace(/\n+$/, '')
}

function compareText(actual, expected) {
    const normalize = (text) =>
        text
            .replace(/\r\n/g, '\n')
            .replace(/(?:[^\s:()]*[\\/])?[A-Za-z0-9_.-]+\.cml:(\d+):(\d+)/g, '<file>:$1:$2')
            .replace(/\n+$/, '')
    const left = normalize(actual)
    const right = normalize(expected)
    if (left === right) return null
    const a = left.split('\n')
    const b = right.split('\n')
    for (let i = 0; i < Math.max(a.length, b.length); i++) {
        if (a[i] !== b[i]) {
            return `line ${i + 1}: page says ${JSON.stringify(b[i] ?? '<end>')}, program printed ${JSON.stringify(a[i] ?? '<end>')}`
        }
    }
    return 'output differs'
}

// ---- One snippet ---------------------------------------------------------------------------

function writeSnippet(snippet, tempDir) {
    const used = new Set()
    const files = []
    for (let i = 0; i < snippet.fences.length; i++) {
        const fence = snippet.fences[i]
        const fallback = snippet.fences.length === 1 ? 'snippet.cml' : i === 0 ? 'lib.cml' : 'main.cml'
        let name = fileNameOf(fence, fallback)
        if (used.has(name)) name = `${i}_${name}`
        used.add(name)
        const file = path.join(tempDir, name)
        fs.writeFileSync(file, fence.body.join('\n') + '\n')
        files.push({ name, file, body: fence.body.join('\n'), entry: entryOf(fence) })
    }
    let entry = files.find((file) => file.entry) || files[0]
    if (!entry.entry) {
        // A fence with no entry point is checked as a module: parse, GCT and GIR only need an
        // entry graph to build, and a generated main does not change the fence.
        fs.appendFileSync(entry.file, '\nfunc main(): int sync { return 0 }\n')
    }
    return { entry, files }
}

async function checkSnippet(documentAbs, snippet, lines, fences) {
    const group = snippet.fences
    const marked = group.map((fence) => fence.marker).find(Boolean)
    if (marked && ['fragment', 'counter-example'].includes(marked.kind)) {
        return { status: 'marked', kind: marked.kind, reason: marked.reason }
    }
    const compileOnly = marked && ['graph-dump', 'compile-only'].includes(marked.kind)

    const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-doc-snippet-'))
    const problems = []
    try {
        // A tutorial page keeps its example programs next to it; a snippet that imports one of
        // them (`import shapes`) finds it, as the page says it should.
        const examples = path.join(path.dirname(documentAbs), 'examples')
        if (fs.existsSync(examples) && fs.statSync(examples).isDirectory()) {
            fs.cpSync(examples, tempDir, { recursive: true })
        }
        const written = writeSnippet(snippet, tempDir)
        const after = outputAfter(lines, fences, snippet)
        const inline = written.entry.body
            .split(/\r?\n/)
            .find((line) => /^\s*\/\/\s*camel\b/.test(line))
        const command = parseCommand(after && after.command) || (inline ? parseCommand({ body: [inline] }) : null)
        const passes = command ? command.passes : []
        const args = command ? command.args : []

        // Compile first: parse + GCT + GIR, whatever the snippet is.
        const compilePasses = passes.length > 0 ? passes : ['std::gir']
        const compiled = await runCamel([written.entry.file, ...compilePasses], tempDir)
        if (compiled.timedOut) {
            problems.push(`compiling timed out after ${TIMEOUT_MS} ms`)
        } else if (compiled.code !== 0) {
            problems.push(`does not compile: ${firstDiagnostic(compiled)}`)
        }

        if (problems.length > 0) {
            return { status: 'failed', problems, ms: compiled.ms }
        }

        if (compileOnly || !after || isGraphDump(passes)) {
            return { status: 'compiled', ms: compiled.ms, graphDump: Boolean(after) && !compileOnly && isGraphDump(passes) }
        }

        if (!written.entry.entry) {
            return {
                status: 'failed',
                problems: ['declares an output but has no func main to print it'],
                ms: compiled.ms,
            }
        }

        const expected = parseExpected(after.output.body.join('\n'))
        const options = []
        for (let i = 0; i < args.length; i++) {
            const arg = args[i]
            if (arg === '--arg') {
                const pair = args[++i] ?? ''
                const eq = pair.indexOf('=')
                if (eq > 0) {
                    const name = pair.slice(0, eq)
                    const value = pair.slice(eq + 1)
                    // A path on the page is relative to the repository root; the snippet runs in
                    // its own temp directory.
                    const resolved = path.isAbsolute(value) ? value : path.resolve(ROOT, value)
                    options.push('--arg', `${name}=${fs.existsSync(resolved) ? resolved : value}`)
                } else {
                    options.push('--arg', pair)
                }
                continue
            }
            if (arg === '-I' || arg === '--include') {
                const value = args[++i] ?? ''
                options.push(arg, path.isAbsolute(value) ? value : path.resolve(ROOT, value))
                continue
            }
            options.push(arg)
        }
        const runArgs = [...options, written.entry.file, ...passes]
        const ran = await runCamel(runArgs, tempDir)
        if (ran.timedOut) {
            problems.push(`running timed out after ${TIMEOUT_MS} ms`)
        } else {
            const errorDiagnostic = expected.diagnostics.some((entry) => entry.severity === 'error')
            const want = errorDiagnostic ? 1 : 0
            if (ran.code !== want) {
                problems.push(`expected exit code ${want}, got ${ran.code}: ${firstDiagnostic(ran)}`)
            }
            const pageText = after.output.body.join('\n')
            const hasLocation = /-->\s/.test(pageText)
            let difference
            if (errorDiagnostic && hasLocation) {
                // The text block is the whole run: the lines printed before the fault, then the
                // located diagnostic. Compare all of it (the path is normalised).
                difference = compareText(ran.stdout, pageText)
            } else if (errorDiagnostic) {
                // A truncated diagnostic: the page keeps the lines printed before it and names
                // the diagnostic, but leaves out the location and the help.
                difference = compareText(beforeDiagnostic(ran.stdout), expected.expected)
            } else {
                // A warning goes to standard error; stdout is the part after the block.
                difference = compareText(ran.stdout, expected.expected)
            }
            if (difference) problems.push(`output differs from the text block: ${difference}`)
            for (const entry of expected.diagnostics) {
                if (!new RegExp(`${entry.severity}\\[${entry.name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}\\]`).test(`${ran.stdout}\n${ran.stderr}`)) {
                    problems.push(`the text block promises ${entry.severity}[${entry.name}], which was not reported`)
                }
            }
        }
        return { status: problems.length > 0 ? 'failed' : 'ran', problems, ms: compiled.ms + ran.ms }
    } finally {
        fs.rmSync(tempDir, { recursive: true, force: true })
    }
}

// ---- Main ----------------------------------------------------------------------------------

const args = process.argv.slice(2).filter((arg) => !arg.startsWith('--'))
const entries =
    process.argv.includes('--default-roots') || args.length === 0 ? ['README.md', 'docs'] : args
// The H171 defect: the plan passed the script's own path, the scope came out empty, and the guard
// reported a pass. A scope entry that does not exist and a scan of no document are now refusals.
const missing = entries.filter((entry) => !fs.existsSync(path.resolve(ROOT, entry)))
if (missing.length > 0) {
    for (const entry of missing) console.log(`${entry}: no such file or directory`)
    console.log(`${missing.length} scope entr${missing.length === 1 ? 'y does' : 'ies do'} not exist; refusing to check a smaller tree`)
    process.exit(2)
}
const files = []
for (const entry of entries) collect(entry, files)
const unique = [...new Set(files)].sort()
if (unique.length === 0) {
    console.log('no reader-facing document in the given scope: nothing was checked')
    process.exit(2)
}

const started = Date.now()
const problems = []
const marked = []
const excerpts = []
const snippets = []
const scanned = []
const skippedFiles = []

for (const abs of unique) {
    const rel = relative(abs)
    const skip = allowedFile(abs)
    if (skip) {
        skippedFiles.push({ rel, reason: skip[1] })
        continue
    }
    scanned.push(rel)
    const text = fs.readFileSync(abs, 'utf8')
    const lines = text.split(/\r?\n/)
    const fences = parseFences(lines)
    if (!fences.some(isCamel)) continue
    for (const snippet of buildSnippets(lines, fences)) snippets.push({ rel, abs, snippet, lines, fences })
    for (const fence of fences) {
        if (!isCamel(fence)) continue
        if (fence.marker) {
            const { kind, reason } = fence.marker
            if (!MARKED_KINDS.has(kind)) {
                problems.push(`${rel}:${fence.open + 1}: unknown snippet marker '${kind}' (fragment, counter-example, graph-dump or compile-only)`)
                continue
            }
            if (!reason) {
                problems.push(`${rel}:${fence.open + 1}: the '${kind}' marker needs a reason`)
                continue
            }
            if (kind === 'counter-example') {
                const before = lines.slice(Math.max(0, fence.open - 8), fence.open).join('\n')
                if (!COUNTER_LABEL.test(`${before}\n${fence.body.join('\n')}\n${reason}`)) {
                    problems.push(`${rel}:${fence.open + 1}: a counter-example must be labelled as one on the page`)
                }
            }
            marked.push({ rel, line: fence.open + 1, kind, reason })
        }
    }
}

const excerptSnippets = snippets.filter(
    (entry) => excerptPage(entry.abs) && !entry.snippet.fences.some(entryOf),
)
for (const entry of excerptSnippets) {
    for (const fence of entry.snippet.fences) {
        excerpts.push({
            rel: entry.rel,
            line: fence.open + 1,
            reason: excerptPage(entry.abs)[1],
        })
    }
}

// Every fence is either a snippet, marked, or an excerpt; a fence inside a multi-file group is
// counted once. A fence with no entry point on a design/technical page is an excerpt (its
// reason is in EXCERPTS); everywhere else a fence with no entry point is compiled as a module
// with a generated main, so that declarations alone are still checked.
const checkedSnippets = snippets.filter((entry) => {
    if (entry.snippet.fences.some((fence) => fence.marker && ['fragment', 'counter-example'].includes(fence.marker.kind))) {
        return false
    }
    return !(excerptPage(entry.abs) && !entry.snippet.fences.some(entryOf))
})
// A page with no Camel fence is not a checked snippet: if the scope holds none at all, the guard
// examined no code and must say so instead of printing "no stale snippet".
if (checkedSnippets.length === 0) {
    console.log(`no Camel fence in ${scanned.length} reader-facing document(s): nothing was compiled`)
    process.exit(2)
}

let cursor = 0
const results = []
async function worker() {
    while (cursor < checkedSnippets.length) {
        const index = cursor++
        const entry = checkedSnippets[index]
        const result = await checkSnippet(entry.abs, entry.snippet, entry.lines, entry.fences)
        results[index] = { ...entry, result }
    }
}
await Promise.all(Array.from({ length: Math.min(JOBS, checkedSnippets.length || 1) }, worker))

let compiled = 0
let ran = 0
let graphDumps = 0
for (const entry of results) {
    if (!entry) continue
    const result = entry.result
    const line = entry.snippet.open + 1
    if (result.status === 'failed') {
        for (const problem of result.problems) problems.push(`${entry.rel}:${line}: ${problem}`)
    } else if (result.status === 'ran') {
        ran++
    } else if (result.status === 'compiled') {
        compiled++
        if (result.graphDump) graphDumps++
    }
}
compiled += ran

for (const problem of problems) console.log(problem)

const seconds = ((Date.now() - started) / 1000).toFixed(1)
if (problems.length > 0) {
    console.log(
        `${problems.length} stale snippet problem(s) in ${scanned.length} reader-facing document(s) ` +
            `(${checkedSnippets.length} snippet(s) checked, ${seconds} s)`,
    )
    process.exit(1)
}
console.log(
    `no stale snippet in ${scanned.length} reader-facing document(s): ` +
        `${checkedSnippets.length} snippet(s), ${compiled} compiled, ${ran} ran their text block, ` +
        `${graphDumps} graph dump(s) compiled only, ${marked.length} marked incomplete, ` +
        `${excerpts.length} excerpt(s) on design/technical pages ` +
        `(${skippedFiles.length} generated/dated reference(s) allowlisted; ` +
        `${RECORDS.length} record directories not scanned; ${seconds} s)`,
)