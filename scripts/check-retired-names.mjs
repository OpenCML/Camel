#!/usr/bin/env node
// Fails when a reader-facing page still teaches a name the language has retired:
//
//   sys -> env, os -> process, crypto -> hash (H114)
//   str::bytes / str::from_bytes -> bytes::encode / bytes::decode (H122)
//   the inner, outer, atomic and shared function modifiers (H107)
//   the body-carrying signature form: `func ... { return '<module:operator>' }` (H127)
//
//   node scripts/check-retired-names.mjs            check the reader-facing pages
//   node scripts/check-retired-names.mjs --default-roots  the same, ignoring a script path argument
//   node scripts/check-retired-names.mjs a.md b/    check the given files and directories
//
// A retired spelling is a use, not a mention. A line that says the spelling was retired
// (`旧写法`, `已删除`, `已改名`, `retired`, `no longer`, ...) is skipped: that is how the current
// documents explain the retirement. Everything else must use the current name. The two record
// directories (docs/handover, docs/reviews) are not pages; the few reader-facing pages whose
// body is a dated snapshot are listed in ALLOWED below, which is deliberately short.
//
// Scope: the Markdown pages under docs/, README.md, the example pages and programs, and the
// Markdown of editors/, tools/ and scripts/. Fenced code in a language other than Camel is
// skipped, so a Python `import os` in a doc is not a Camel import.
//
// Exit status 1 when a use is found.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

// The records of an earlier state, with the reason each one may keep the old spelling. The two
// record directories (docs/handover, docs/reviews) are not pages and are not scanned at all;
// these two files are reader-facing pages whose body is nonetheless a dated snapshot.
const ALLOWED = [
    ['docs/design/stdlib/inventory.cn.md', "the 2026-10-20 inventory: the names are that snapshot's evidence"],
    ['docs/design/training/bytes.cn.md', 'a dated implementation log: the entry is what was true then'],
]

// Directories that are records, not pages: they quote the old spelling to say what happened.
const RECORDS = ['docs/handover/', 'docs/reviews/']

// A page that explains the retirement is a mention, not a use.
const MENTION = /已(?:删除|退役|改名)|旧写法|旧名|retired|renamed|is gone|no longer/i

// `func` and not `type`: the English phrase "the inner type of `T?`" is not a declaration.
const RULES = [
    { what: 'retired module URI', re: /\b(?:sys|os|crypto):[A-Za-z_*]/ },
    { what: 'retired qualified name', re: /\b(?:sys|os|crypto)::[A-Za-z_]/ },
    { what: 'retired module import', re: /\bfrom\s+(?:sys|os|crypto)\b/ },
    { what: 'retired module import', re: /^\s*import\s+(?:sys|os|crypto)\b/ },
    { what: 'retired function', re: /\bstr:{1,2}(?:bytes|from_bytes)\b/ },
    { what: 'retired modifier', re: /\b(?:inner|outer|atomic|shared)\s+func\b/ },
    { what: 'body-carrying signature', re: /\breturn\s+'[A-Za-z_][A-Za-z0-9_]*:[A-Za-z0-9_*:/<>.+-]*'/ },
    { what: 'body-carrying signature', re: /\breturn\s+'<(?:uri|URI)>'/ },
    { what: 'retired module name', re: /`(?:sys|os|crypto)`|\b(?:sys|os|crypto)\s+module\b/ },
    {
        what: 'retired operator name',
        re: /\b(?:sys\.get_env|sys\.get_cwd|os\.(?:exit|sleep|whoami|cpu_count)|crypto\.sha256(?:_rounds)?)\b/,
    },
]

const EXTENSIONS = new Set(['.md', '.cml', '.asdl'])
const ROOTS = ['README.md', 'README.cn.md', 'docs', 'examples', 'editors', 'tools', 'scripts']
const SKIP_DIRS = new Set(['.git', 'node_modules', 'out', 'build', 'tmp', 'dist', '.venv'])
// A fence in one of these is not Camel code; its `import`/`from` lines are another language's.
const OTHER_LANGUAGES = new Set([
    'bash',
    'c',
    'cpp',
    'c++',
    'css',
    'go',
    'html',
    'java',
    'javascript',
    'js',
    'json',
    'powershell',
    'ps1',
    'py',
    'python',
    'rust',
    'sh',
    'sql',
    'toml',
    'ts',
    'typescript',
    'yaml',
    'yml',
])

function collect(entry, out) {
    const abs = path.resolve(ROOT, entry)
    if (!fs.existsSync(abs)) return
    const stat = fs.statSync(abs)
    const rel = relative(abs) + (stat.isDirectory() ? '/' : '')
    if (RECORDS.some((prefix) => rel === prefix || rel.startsWith(prefix))) return
    if (stat.isFile()) {
        if (EXTENSIONS.has(path.extname(abs))) out.push(abs)
        return
    }
    for (const name of fs.readdirSync(abs)) {
        if (SKIP_DIRS.has(name)) continue
        collect(path.join(abs, name), out)
    }
}

function relative(abs) {
    return path.relative(ROOT, abs).replace(/\\/g, '/')
}

function allowed(abs) {
    const rel = relative(abs)
    return ALLOWED.find(([entry]) => (entry.endsWith('/') ? rel.startsWith(entry) : rel === entry))
}

// The line is in a non-Camel fence: return true when the bare-import rule should not apply.
function otherLanguageBlock(lines) {
    const flags = new Array(lines.length).fill(false)
    let fence = null
    for (let i = 0; i < lines.length; i++) {
        const match = /^\s*(```+|~~~+)\s*([A-Za-z0-9_+#-]*)/.exec(lines[i])
        if (match) {
            if (fence === null) fence = match[2].toLowerCase()
            else fence = null
            flags[i] = fence !== null && OTHER_LANGUAGES.has(fence)
            continue
        }
        flags[i] = fence !== null && OTHER_LANGUAGES.has(fence)
    }
    return flags
}

function checkFile(abs) {
    const rel = relative(abs)
    const cml = path.extname(abs) === '.cml'
    const lines = fs.readFileSync(abs, 'utf8').split(/\r?\n/)
    const other = cml ? lines.map(() => false) : otherLanguageBlock(lines)
    const problems = []
    for (let i = 0; i < lines.length; i++) {
        const line = lines[i]
        if (other[i]) continue
        if (MENTION.test(line)) continue
        for (const rule of RULES) {
            if (rule.re.test(line)) {
                problems.push({
                    file: rel,
                    line: i + 1,
                    what: rule.what,
                    text: line.trim().replace(/\s+/g, ' ').slice(0, 160),
                })
                break
            }
        }
    }
    return problems
}

const args = process.argv.slice(2).filter((arg) => !arg.startsWith('--'))
const entries = process.argv.includes('--default-roots') || args.length === 0 ? ROOTS : args
const files = []
for (const entry of entries) collect(entry, files)
const unique = [...new Set(files)].sort()

const problems = []
const skipped = []
for (const abs of unique) {
    const history = allowed(abs)
    if (history) {
        skipped.push(abs)
        continue
    }
    problems.push(...checkFile(abs))
}

for (const problem of problems) {
    console.log(`${problem.file}:${problem.line}: ${problem.what}: ${problem.text}`)
}
const checked = unique.length - skipped.length
if (problems.length > 0) {
    console.log(`${problems.length} retired spelling(s) in ${checked} reader-facing file(s)`)
    process.exit(1)
}
console.log(
    `no retired spelling in ${checked} reader-facing file(s) ` +
        `(${skipped.length} dated snapshot(s) allowed; ` +
        `${RECORDS.length} record directories not scanned)`
)