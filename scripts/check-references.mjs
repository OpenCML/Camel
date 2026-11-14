#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A path written in a comment or in a document is a claim that the file is there.
 *
 * The hygiene sweeps of section 11 delete things: draft copies of the standard library modules, a
 * documentation snapshot, the raw files of a measurement run. What they leave behind is sentences
 * that still name them, and a sentence that names a file which is gone is a reading nobody can take
 * again -- it is not a broken link in a browser, it is a page that points a worker at nothing.
 * `scripts/check-doc-links.mjs --citations` catches one half of that: a `*.md` path anywhere, and a
 * `file#Symbol` or `file:line` citation, in the files it reads. It deliberately does not read a
 * `.cml`, a `.json` or a `.toml` path written as a path, and it does not read the record
 * directories at all. This check reads the other half: a path of any extension, wherever it is
 * written, in the trees that are not records.
 *
 *   node scripts/check-references.mjs                 report, exit 0 (this is the mode CI runs)
 *   node scripts/check-references.mjs --enforce        exit 1 when a reference does not resolve
 *   node scripts/check-references.mjs --path <p>       scan one file or one directory (a sample)
 *
 * Why report-only first: the list this check finds is a work item, not a wall, and a lane that
 * deletes a directory cannot also prove, in the same commit, that every page which ever named it
 * has a replacement. `--enforce` is the mode the gate runs once the list is empty; until then the
 * reading is published and nobody is blocked by it. The gate entry is
 * `internal/tools/camel-remote.ps1`'s `$checkers`, which another lane is editing: this file exists
 * to be wired in at the merge, and it changes nothing about that script.
 *
 * What resolves, and what does not:
 *
 *  - a path taken from the repository root (`stdlib/array.cml`), or from the citing file
 *    (`../stdlib/array.cml`), or a suffix of one (`stdlib/array.cml` for a file under `funcs/`);
 *    the suffix form must name exactly one file, because a suffix two files answer is no evidence;
 *  - a token whose first segment is not a directory of this tree is a sentence's shorthand, not a
 *    claim about this tree: it is read when exactly one file ends that way and ignored otherwise;
 *  - a path a `.gitignore` covers is not reported: it names a file that lives with its run, by
 *    design, and the sweep that untracked those files left such references behind on purpose
 *    (`examples/models/naive-in-expert-out/results/`); each one is counted and printed as local;
 *  - a path with a placeholder (`<module>.cml`, `*.json`, `{a,b}.cml`) is a pattern, not a claim;
 *  - a URL is not a path of this tree.
 *
 * The record directories are not read by default (`RECORD_DIRS`, the same rule the citation check
 * keeps): `docs/handover` and `docs/reviews` are the history of decisions, a page there quotes a
 * path as it stood when the decision was taken, and rewriting it would be rewriting the record.
 * `--path docs/handover` reads them anyway, which is how a sweep checks its own deletions.
 *
 * A `.md` fence is an example, not a claim: the path inside it is a path a command will write. So
 * are a plan's strings (`expect_output_contains`); only a comment line in code is read, and in a
 * document every line outside a fence is.
 */

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')

/** The trees a build, a Python environment or a dependency download writes; never read. */
const SKIP_DIRS = new Set([
    '.git',
    '.github',
    '.venv',
    'venv',
    'node_modules',
    'build',
    'out',
    'dist',
    'vendor',
    'third_party',
    '.cache',
    '__pycache__',
])

/** The records: history, not the tree as it stands. Read only when named by `--path`. */
const RECORD_DIRS = new Set(['docs/handover', 'docs/reviews'])

/**
 * A synthetic checkout another check builds and runs against: the `fixture/` trees of the plans.
 * A path in one of their pages is relative to *that* tree -- a `src/<name>.c` there is the
 * fixture's own file beside its `notes.md`, and a fixture whose whole point is a file that is not
 * there would be reported forever. The same reason the record directories are not read, one step
 * further out.
 */
function isFixtureTree(full) {
    const rel = path.relative(ROOT, full).split(path.sep).join('/')
    return rel.startsWith('test/plans/') && path.basename(full) === 'fixture'
}

/** A runner writes these at run time; a page that names one names an output. */
const RUN_OUTPUTS = new Set([
    'CLASSIFIER-RULES.md',
    'HARNESS-CHANGES.md',
    'HARNESS-CHANGES-2.md',
    'RESULTS.md',
    'perf-baseline.md',
    'latency.md',
    'memory.md',
    'export.md',
])

/**
 * Files of other repositories, cited by name on purpose: the paper's repository (TSE2027 and the
 * name its rules file lives under now) and a page of OpenCML-Ext. The citation check keeps the same
 * three for the same reason; a path here is not a promise that this checkout holds it.
 */
const OTHER_REPOSITORY = ['TSE2027/', 'ECOOP2027/', 'docs/debugger-migration.md']

const C_LIKE = new Set(['.c', '.cc', '.cpp', '.cxx', '.h', '.hpp', '.hxx', '.cml', '.js', '.mjs', '.ts'])
const HASH_COMMENT = new Set(['.py', '.sh', '.ps1', '.toml', '.tsv', '.yml', '.yaml'])

/** The files that can carry a sentence: a document, a comment, a description. */
const SCANNED = new Set(['.md', ...C_LIKE, ...HASH_COMMENT])

/** A path with at least one `/`, and an extension on its last component. */
const PATH_TOKEN = /(?<![\w./@-])((?:\.{1,2}\/)?(?:[\w@.-]+\/)+[\w@.-]+\.[A-Za-z][A-Za-z0-9]{0,5})(?![\w-])/g

function walk(directory, skipRecordDirs) {
    const files = []
    const ignores = []
    const entered = (full) => {
        const rel = path.relative(ROOT, full).split(path.sep).join('/')
        if (skipRecordDirs && RECORD_DIRS.has(rel)) return false
        if (isFixtureTree(full)) return false
        return true
    }
    const visit = (dir) => {
        for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            if (SKIP_DIRS.has(entry.name) || entry.name.startsWith('out_lr')) continue
            const full = path.join(dir, entry.name)
            if (entry.isDirectory()) {
                if (entered(full)) visit(full)
                continue
            }
            if (entry.name === '.gitignore') ignores.push(full)
            const extension = path.extname(entry.name).toLowerCase()
            if (SCANNED.has(extension) && entry.name !== 'changes.log') files.push(full)
        }
    }
    visit(directory)
    return { files, ignores }
}

/** Every path a `--path` target stands for; a file stands for itself. */
function targetFiles(target, skipRecordDirs) {
    const stat = fs.statSync(target)
    if (stat.isFile()) return { files: [target], ignores: allIgnores() }
    const { files } = walk(target, skipRecordDirs)
    return { files, ignores: allIgnores() }
}

let ignoreCache = null
function allIgnores() {
    if (ignoreCache === null) ignoreCache = walk(ROOT, false).ignores
    return ignoreCache
}

/**
 * Whether a `.gitignore` covers a repository-relative path. The forms a `.gitignore` of this tree
 * uses: `name`, `name/`, `dir/name`, `*.ext`, `**\/name`. A pattern with no `/` matches at any
 * depth; anything else is anchored at the file that holds it.
 */
function ignoredBy(relPath, ignores) {
    for (const file of ignores) {
        const base = path.relative(ROOT, path.dirname(file)).split(path.sep).join('/')
        const local = base === '' || base === '.' ? relPath : relPath.startsWith(`${base}/`) ? relPath.slice(base.length + 1) : null
        if (local === null) continue
        let lines
        try {
            lines = fs.readFileSync(file, 'utf8').split('\n')
        } catch {
            continue
        }
        for (const raw of lines) {
            const pattern = raw.trim()
            if (!pattern || pattern.startsWith('#')) continue
            const directoryOnly = pattern.endsWith('/')
            const body = pattern.replace(/^\/+/, '').replace(/\/+$/, '')
            if (!body) continue
            const anchored = body.includes('/')
            const regex = new RegExp(
                (anchored ? '^' : '(?:^|/)') +
                    body
                        .split('**')
                        .map((part) =>
                            part
                                .split('*')
                                .map((piece) => piece.replace(/[.+?^${}()|[\]\\]/g, '\\$&'))
                                .join('[^/]*'),
                        )
                        .join('.*') +
                    (directoryOnly ? '(?:/|$)' : '$'),
            )
            if (regex.test(local)) return true
        }
    }
    return false
}

function isComment(file, text, index) {
    const extension = path.extname(file).toLowerCase()
    if (C_LIKE.has(extension)) {
        const lineStart = text.lastIndexOf('\n', index - 1) + 1
        const prefix = text.slice(lineStart, index)
        return prefix.includes('//') || /^\s*\*/.test(prefix) || prefix.includes('/*')
    }
    if (HASH_COMMENT.has(extension)) {
        const lineStart = text.lastIndexOf('\n', index - 1) + 1
        return text.slice(lineStart, index).includes('#')
    }
    return true
}

/** A fence shows an example: the command to run, not a citation of a file. */
function inFence(text, index) {
    const fences = (text.slice(0, index).match(/^\s*(?:```|~~~)/gm) || []).length
    return fences % 2 === 1
}

function insideUrl(text, index) {
    let start = index
    while (start > 0 && /[\w:/?=&%#+.-]/.test(text[start - 1])) start -= 1
    return /^[a-z][a-z0-9+.-]*:\/\//i.test(text.slice(start, index))
}

/** Every file of the tree, by the path under ROOT, for the suffix rule. */
let treeCache = null
function treePaths() {
    if (treeCache !== null) return treeCache
    const all = []
    const visit = (dir) => {
        for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
            if (SKIP_DIRS.has(entry.name)) continue
            const full = path.join(dir, entry.name)
            if (entry.isDirectory()) visit(full)
            else all.push(path.relative(ROOT, full).split(path.sep).join('/'))
        }
    }
    visit(ROOT)
    treeCache = all
    return all
}

/**
 * The file a cited path names, or the reason it names none. `candidates` is what a suffix match
 * found: two files is not a resolution, it is a question.
 */
function resolve(file, cited) {
    const roots = [path.resolve(path.dirname(file), cited), path.resolve(ROOT, cited)]
    for (const candidate of roots) {
        try {
            if (fs.statSync(candidate).isFile()) return { resolved: true }
        } catch {
            // not there; the suffix rule below may still answer
        }
    }
    // Every leading `./` and `../` goes, not just one: a comment quotes a path from the directory
    // the sentence is about, not from the file that holds it -- a test file's import example is
    // written from where that test file will sit, and a transcript quotes the path the tool
    // printed. Those segments are not evidence either way; the rule below still demands that
    // exactly one file of the tree answer the rest.
    const trimmed = cited.replace(/^(?:\.{1,2}\/)+/, '')
    const suffix = `/${trimmed}`
    const matches = treePaths().filter((entry) => entry.endsWith(suffix) || entry === cited || entry === trimmed)
    if (matches.length === 1) return { resolved: true }
    if (matches.length > 1) return { resolved: false, ambiguous: matches.length }
    return { resolved: false }
}

const argv = process.argv.slice(2)
const enforce = argv.includes('--enforce')
const targets = []
for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index]
    if (arg === '--path') {
        const value = argv[index + 1]
        if (!value) {
            console.error('check-references: --path needs a file or a directory')
            process.exit(2)
        }
        targets.push(path.resolve(value))
        index += 1
    } else if (arg !== '--enforce' && !arg.startsWith('--')) {
        console.error(`check-references: unknown argument '${arg}'`)
        process.exit(2)
    }
}

const named = targets.length > 0
const rootTop = new Set(fs.readdirSync(ROOT).map((name) => name))
const readDirectories = named ? targets.map((target) => path.relative(ROOT, target).split(path.sep).join('/')) : []
const readingRecords = readDirectories.some((rel) => rel === 'docs/handover' || rel.startsWith('docs/handover/') || rel.startsWith('docs/reviews'))
const { files, ignores } = named
    ? { files: targets.flatMap((target) => targetFiles(target, !readingRecords).files), ignores: allIgnores() }
    : { files: walk(ROOT, true).files, ignores: allIgnores() }

if (files.length === 0) {
    console.error(
        `check-references: ${targets.join(', ') || 'the tree'} holds no file this check reads, so it ` +
            'matched nothing and proves nothing',
    )
    process.exit(2)
}

const findings = []
let read = 0
let local = 0
for (const file of files) {
    const text = fs.readFileSync(file, 'utf8')
    const markdown = path.extname(file).toLowerCase() === '.md'
    const relative = path.relative(ROOT, file).split(path.sep).join('/')
    for (const match of text.matchAll(PATH_TOKEN)) {
        const cited = match[1]
        if (OTHER_REPOSITORY.some((prefix) => cited.startsWith(prefix))) continue
        if (insideUrl(text, match.index)) continue
        if (markdown && inFence(text, match.index)) continue
        if (!markdown && !isComment(file, text, match.index)) continue
        const first = cited.split('/')[0]
        const fromHere = first === '.' || first === '..'
        // `./name.ext` names no directory: it is the spelling a command line gives a bare name
        // (`./counts.cml` and `counts.cml` take the same pass-name prefix), and a bare name is a
        // sentence's shorthand which this check does not read. There is no place in it to be wrong
        // about; two files that bear the name are not a question the sentence asked.
        if (/^\.\/[^/]+$/.test(cited)) continue
        if (!fromHere && !rootTop.has(first)) {
            // Not a path this tree could hold from its root: a sentence's shorthand. The suffix
            // rule is the only thing that can still answer it, and only when it answers once.
            const bySuffix = treePaths().filter(
                (entry) => entry.endsWith(`/${cited}`) || entry === cited,
            )
            if (bySuffix.length === 1) read += 1
            continue
        }
        if (RUN_OUTPUTS.has(path.basename(cited))) continue
        const { resolved, ambiguous } = resolve(file, cited)
        if (resolved) {
            read += 1
            continue
        }
        if (ignoredBy(path.posix.normalize(cited.replace(/^\.{1,2}\//, '')), ignores)) {
            local += 1
            continue
        }
        const line = text.slice(0, match.index).split('\n').length
        findings.push(
            `${relative}:${line}: '${cited}': ${
                ambiguous ? `${ambiguous} files end this way and the sentence names none of them` : 'no such file'
            }`,
        )
    }
}

for (const finding of findings) console.log(finding)
const scanned = files.length
console.log(
    `check-references: ${read} reference(s) resolved, ${findings.length} did not, ${local} name an ` +
        `ignored (local) file, across ${scanned} file(s)${
            named ? ` of ${readDirectories.join(', ')}` : ''
        }`,
)
if (findings.length > 0) {
    console.error(
        'check-references: a comment or a page names a path of this tree that is not there. A ' +
            'sweep that deletes a file leaves these behind, and a reader who follows one finds ' +
            'nothing: name the file that holds the content now, or say what replaced it.',
    )
}
if (enforce && findings.length > 0) process.exit(1)
process.exit(0)