#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Put the pushed `camel-oversight` onto the machine where DSH loads it from, without a worktree and
 * without touching the main checkout.
 *
 * DSH loads the plugin from a directory of its own -- `D:\Projects\camel-oversight-live\` -- and not
 * from `D:\Projects\Camel\internal\tools\camel-oversight\`, because the main checkout holds merges in
 * flight and lane half-work: a reloader that watches it would load code that has not passed the gate.
 * This script is the one way content reaches that directory, and it is called from two places that
 * need the same guarantees: the plugin's own `POST /oversee/reload` (the primary path, a button on
 * the band) and, by hand, from a shell.
 *
 * The merge seat's tool deliberately does not call it: the owner chose the button, so the merge position
 * stays out of this (`internal/plans/2026-11-24-oversight-live-reload.cn.md`, "按钮怎么做").
 *
 * What it does, in this order, and why the order matters:
 *
 *   1. read the plugin tree straight out of a commit -- `git ls-tree` for the file list and the blob
 *      ids, `git cat-file blob` for the bytes. No worktree, no checkout, no `stash`: the tree is
 *      taken from the object database, so a merge running in the main checkout cannot be read
 *      half-applied;
 *   2. compare content, never timestamps: the live files are hashed into git blob ids and compared
 *      against the commit's blob ids. Equal means nothing happens at all, and equals no reload --
 *      pushing a merge that does not touch the plugin must not hiccup the running one;
 *   3. write the whole tree into a temporary directory beside the live one, so the live directory is
 *      never half old and half new;
 *   4. gate every `.js` file with `node --check`, and then one step further: import the staged
 *      `index.js` in a child process and import the staged `client.js` against a stub loader. A
 *      syntax check alone cannot see a module that parses and throws on its first line, and a
 *      DSH plugin that fails to import does not fall back to the code it replaced -- it stops, and
 *      the button that would report the failure stops with it. The import is the cheapest check
 *      that catches that class before the live directory moves. One file failing stops everything:
 *      the temporary directory is removed, the live directory is left exactly as it was, and the
 *      failure names the file and the line;
 *   5. only then replace: the live directory is renamed aside (`.previous`, kept for a manual
 *      rollback) and the temporary directory is renamed into its place, both renames on one volume;
 *   6. write a record beside the live directory -- `<target>.sync.json`, deliberately OUTSIDE the
 *      directory DSH watches and imports -- holding the commit sha, so the band can show which
 *      version is live.
 *
 * Usage:
 *   node internal/tools/sync-oversight-live.mjs [options]
 *
 *   --repo <dir>      the checkout whose object database is read (default: $CAMEL_OVERSIGHT_REPO,
 *                     then D:\Projects\Camel). Never written to.
 *   --ref <ref>       the commit to publish (default: origin/develop)
 *   --dir <path>      the plugin directory inside the repository
 *                     (default: internal/tools/camel-oversight)
 *   --target <dir>    where the plugin is loaded from (default: $CAMEL_OVERSIGHT_LIVE, then
 *                     D:\Projects\camel-oversight-live)
 *   --json            one JSON object on stdout, for a caller that parses it
 *   --force           replace even when the content is unchanged
 *   --self-test       run the fixture test (a bad index.js must be refused and must leave the live
 *                     directory alone; a good one must replace it) in a temporary directory and exit
 *
 * Exit codes: 0 nothing to do or replaced; 2 the staged tree failed a check (`node --check` or the
 * import preflight) and the live directory is unchanged; 3 git or file-system failure; 1 a
 * self-test check failed.
 */

import { execFileSync, spawnSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'

const EXIT_OK = 0
const EXIT_SELFTEST = 1
const EXIT_CHECK = 2
const EXIT_GIT = 3

const DEFAULT_REPO = process.env.CAMEL_OVERSIGHT_REPO || 'D:\\Projects\\Camel'
const DEFAULT_REF = 'origin/develop'
const DEFAULT_DIR = 'internal/tools/camel-oversight'
const DEFAULT_TARGET = process.env.CAMEL_OVERSIGHT_LIVE || 'D:\\Projects\\camel-oversight-live'

// ---- git, read-only ------------------------------------------------------------------------------

/** Run git and return stdout as text. `-C` rather than a cwd: the repo path is never entered. */
function gitText(repo, args) {
    return execFileSync('git', ['-C', repo, ...args], {
        encoding: 'utf8',
        maxBuffer: 64 * 1024 * 1024,
        windowsHide: true,
    })
}

/** Run git and return stdout as bytes, for a blob that may not be text. */
function gitBytes(repo, args) {
    return execFileSync('git', ['-C', repo, ...args], {
        maxBuffer: 64 * 1024 * 1024,
        windowsHide: true,
    })
}

/**
 * The files of one directory in one commit, as `{ relativePath: blobId }`.
 *
 * `ls-tree -r` lists blobs only; `--long` is not needed because the blob id is all this compares
 * and all `cat-file` is asked for. A path is reduced to its part below `dir`, so `index.js` here is
 * `<target>/index.js` in the live directory.
 */
function commitTree(repo, ref, dir) {
    const out = gitText(repo, ['ls-tree', '-r', '-z', ref, '--', dir])
    const files = new Map()
    for (const record of out.split('\0')) {
        if (record === '') continue
        const tab = record.indexOf('\t')
        const [, type, object] = record.slice(0, tab).split(' ')
        const full = record.slice(tab + 1)
        if (type !== 'blob') continue
        const relative = path.relative(dir.replace(/\//g, path.sep), full.replace(/\//g, path.sep))
        files.set(relative === '' ? path.basename(full) : relative, object)
    }
    return files
}

/** The git blob id of a buffer: what `git hash-object` would print, computed without git. */
function blobId(bytes) {
    return createHash('sha1').update(`blob ${bytes.length}\0`, 'latin1').update(bytes).digest('hex')
}

/** The live directory as `{ relativePath: blobId }`; missing or unreadable reads as empty. */
function liveTree(target) {
    const files = new Map()
    let entries
    try {
        entries = fs.readdirSync(target, { withFileTypes: true })
    } catch {
        return files
    }
    for (const entry of entries) {
        if (!entry.isFile()) continue
        const bytes = fs.readFileSync(path.join(target, entry.name))
        files.set(entry.name, blobId(bytes))
    }
    return files
}

function sameContent(a, b) {
    if (a.size !== b.size) return false
    for (const [name, id] of a) if (b.get(name) !== id) return false
    return true
}

// ---- the gates -----------------------------------------------------------------------------------

/**
 * `node --check` on one staged file.
 * @returns null when it parses, or `{ file, line, message }` from what node printed.
 */
function checkSyntax(file) {
    const run = spawnSync(process.execPath, ['--check', file], { encoding: 'utf8', windowsHide: true })
    if (run.status === 0) return null
    const stderr = String(run.stderr || '')
    // node prints `  <absolute path>:<line>` on the first line, then the code frame and the error.
    const first = stderr.split(/\r?\n/)[0] || ''
    const match = /^(.*):(\d+)$/.exec(first.trim())
    const message = stderr
        .split(/\r?\n/)
        .map((line) => line.trim())
        .filter((line) => /^[A-Za-z]*Error\b/.test(line))
        .pop()
    return {
        file,
        line: match ? Number(match[2]) : null,
        message: message || first.trim() || `node --check exited ${run.status}`,
    }
}

/**
 * Import a staged module in a child process, before it is allowed to replace running code.
 *
 * A syntax check cannot see a module that parses and throws while it is evaluated, and that is the
 * failure this plugin cannot report: DSH stops a plugin whose replacement fails to import, and the
 * route that would have said so is inside the plugin that stopped. `index.js` is imported as the
 * host half; `client.js` is imported against a stub of the browser module loader, which is enough to
 * prove it evaluates and registers a module with a factory.
 *
 * @param kind 'host' for index.js, 'client' for client.js.
 * @returns null when it loads, or `{ file, line, message }`.
 */
function checkImport(file, kind) {
    const harness = path.join(os.tmpdir(), `oversight-preflight-${process.pid}-${kind}.mjs`)
    const source =
        `const url = process.env.OVERSIGHT_PREFLIGHT_URL\n` +
        `const kind = process.env.OVERSIGHT_PREFLIGHT_KIND\n` +
        `let registered = null\n` +
        `if (kind === 'client') {\n` +
        `    globalThis.window = { __ModuleLoader__: { load(definition) { registered = definition } } }\n` +
        `}\n` +
        `const module = await import(url)\n` +
        `if (kind === 'client') {\n` +
        `    if (!registered || typeof registered.id !== 'string' || typeof registered.factory !== 'function') {\n` +
        `        throw new Error('client.js did not register { id, factory } with window.__ModuleLoader__.load')\n` +
        `    }\n` +
        `} else {\n` +
        `    const missing = ['name', 'apply'].filter((key) => typeof module[key] !== (key === 'apply' ? 'function' : 'string'))\n` +
        `    if (missing.length) throw new Error('index.js does not export ' + missing.join(', '))\n` +
        `    if (module.apply.length < 1) throw new Error('index.js apply() takes no context')\n` +
        `}\n`
    fs.writeFileSync(harness, source)
    try {
        const run = spawnSync(process.execPath, ['--no-warnings', harness], {
            encoding: 'utf8',
            windowsHide: true,
            timeout: 60_000,
            env: { ...process.env, OVERSIGHT_PREFLIGHT_URL: pathToUrl(file), OVERSIGHT_PREFLIGHT_KIND: kind },
        })
        if (run.status === 0) return null
        const stderr = String(run.stderr || '')
        // The throwing frame is the first stack line naming this file, as `<basename>:<line>:<column>`.
        const at = new RegExp(`${escapeRegExp(path.basename(file))}:(\\d+):\\d+`).exec(stderr)
        const message = stderr
            .split(/\r?\n/)
            .map((line) => line.trim())
            .filter((line) => /^[A-Za-z]*Error\b/.test(line))
            .pop()
        return {
            file,
            line: at ? Number(at[1]) : null,
            message: message || stderr.split(/\r?\n/).map((l) => l.trim()).filter(Boolean).pop() || `preflight exited ${run.status}`,
        }
    } finally {
        fs.rmSync(harness, { force: true })
    }
}

function pathToUrl(file) {
    let resolved = path.resolve(file).replace(/\\/g, '/')
    if (!resolved.startsWith('/')) resolved = '/' + resolved
    return 'file://' + encodeURI(resolved)
}

function escapeRegExp(text) {
    return text.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
}

// ---- the one operation -----------------------------------------------------------------------------

/**
 * Publish `ref:dir` to `target`.
 * @returns An outcome object; `ok` says whether the live directory holds the commit afterwards.
 */
export function sync({ repo, ref, dir, target, force = false, log = () => {} }) {
    const started = Date.now()
    let sha
    let tree
    try {
        sha = gitText(repo, ['rev-parse', '--verify', `${ref}^{commit}`]).trim()
        tree = gitText(repo, ['rev-parse', '--verify', `${ref}:${dir}`]).trim()
    } catch (error) {
        return {
            ok: false,
            stage: 'git',
            sha: null,
            repo,
            ref,
            dir,
            target,
            message: `cannot read ${ref} in ${repo}: ${firstLine(error)}`,
        }
    }

    let staged
    try {
        staged = commitTree(repo, ref, dir)
    } catch (error) {
        return { ok: false, stage: 'git', sha, repo, ref, dir, target, message: `cannot list ${ref}:${dir}: ${firstLine(error)}` }
    }
    if (staged.size === 0) {
        return { ok: false, stage: 'git', sha, repo, ref, dir, target, message: `${ref}:${dir} holds no files` }
    }

    const live = liveTree(target)
    const unchanged = sameContent(staged, live)
    if (unchanged && !force) {
        writeRecord(target, { sha, tree, repo, ref, dir, files: Object.fromEntries(staged), unchanged: true })
        log(`unchanged: ${target} already holds ${sha.slice(0, 9)} (content compared, not timestamps)`)
        return {
            ok: true,
            changed: false,
            sha,
            tree,
            ref,
            repo,
            dir,
            target,
            files: [...staged.keys()].sort(),
            ms: Date.now() - started,
        }
    }

    // A temporary directory on the same volume as the target, so the last two renames are cheap.
    const temp = path.join(path.dirname(target), `.${path.basename(target)}.tmp-${process.pid}-${Date.now()}`)
    fs.rmSync(temp, { recursive: true, force: true })
    fs.mkdirSync(temp, { recursive: true })
    try {
        for (const [relative, object] of staged) {
            const bytes = gitBytes(repo, ['cat-file', 'blob', object])
            const place = path.join(temp, relative)
            fs.mkdirSync(path.dirname(place), { recursive: true })
            fs.writeFileSync(place, bytes)
        }
    } catch (error) {
        fs.rmSync(temp, { recursive: true, force: true })
        return { ok: false, stage: 'git', sha, repo, ref, dir, target, message: `cannot read a blob of ${ref}:${dir}: ${firstLine(error)}` }
    }

    // Gate 1: every .js file parses.
    const jsFiles = [...staged.keys()].filter((name) => name.endsWith('.js')).sort()
    for (const relative of jsFiles) {
        const failure = checkSyntax(path.join(temp, relative))
        if (failure !== null) {
            fs.rmSync(temp, { recursive: true, force: true })
            return {
                ok: false,
                stage: 'check',
                sha,
                repo,
                ref,
                dir,
                target,
                file: relative,
                line: failure.line,
                liveUnchanged: true,
                message: `${relative}${failure.line ? `:${failure.line}` : ''}: ${failure.message}`,
                ms: Date.now() - started,
            }
        }
    }

    // Gate 2: the two modules DSH will import actually evaluate.
    const imports = [
        ['index.js', 'host'],
        ['client.js', 'client'],
    ]
    for (const [relative, kind] of imports) {
        if (!staged.has(relative)) continue
        const failure = checkImport(path.join(temp, relative), kind)
        if (failure !== null) {
            fs.rmSync(temp, { recursive: true, force: true })
            return {
                ok: false,
                stage: 'import',
                sha,
                repo,
                ref,
                dir,
                target,
                file: relative,
                line: failure.line,
                liveUnchanged: true,
                message: `${relative}${failure.line ? `:${failure.line}` : ''}: ${failure.message}`,
                ms: Date.now() - started,
            }
        }
    }

    // Replace: aside, then in. The previous tree is kept beside the live one so a human can put it
    // back by hand; nothing else in this machine reads it.
    const previous = `${target}.previous`
    try {
        fs.rmSync(previous, { recursive: true, force: true })
        if (fs.existsSync(target)) fs.renameSync(target, previous)
        fs.renameSync(temp, target)
    } catch (error) {
        // Put the old tree back rather than leaving no plugin directory at all.
        if (!fs.existsSync(target) && fs.existsSync(previous)) {
            try {
                fs.renameSync(previous, target)
            } catch {
                // The message below is what a reader acts on; the rename is best effort.
            }
        }
        fs.rmSync(temp, { recursive: true, force: true })
        return {
            ok: false,
            stage: 'replace',
            sha,
            repo,
            ref,
            dir,
            target,
            message: `cannot replace ${target}: ${firstLine(error)}`,
            ms: Date.now() - started,
        }
    }

    writeRecord(target, { sha, tree, repo, ref, dir, files: Object.fromEntries(staged), previous })
    log(`replaced: ${target} now holds ${sha.slice(0, 9)} (${staged.size} files)`)
    return {
        ok: true,
        changed: true,
        sha,
        tree,
        ref,
        repo,
        dir,
        target,
        previous,
        files: [...staged.keys()].sort(),
        ms: Date.now() - started,
    }
}

/** The record the band reads for its version: commit sha plus what was published. */
function writeRecord(target, record) {
    const file = `${target}.sync.json`
    const document = { ...record, at: new Date().toISOString() }
    try {
        fs.mkdirSync(path.dirname(file), { recursive: true })
        fs.writeFileSync(file, JSON.stringify(document, null, 2) + '\n')
    } catch {
        // A record that cannot be written says nothing about the plugin directory, which is already
        // in place; the band shows no version rather than the sync failing after it succeeded.
    }
}

function firstLine(error) {
    const text = String((error && (error.stderr || error.message)) || error)
    return text.split(/\r?\n/).map((line) => line.trim()).filter(Boolean)[0] || 'unknown error'
}

// ---- command line -----------------------------------------------------------------------------------

function parseArgs(argv) {
    const options = { repo: DEFAULT_REPO, ref: DEFAULT_REF, dir: DEFAULT_DIR, target: DEFAULT_TARGET, json: false, force: false, selfTest: false }
    for (let index = 0; index < argv.length; index++) {
        const word = argv[index]
        switch (word) {
            case '--repo': options.repo = argv[++index]; break
            case '--ref': options.ref = argv[++index]; break
            case '--dir': options.dir = argv[++index]; break
            case '--target': options.target = argv[++index]; break
            case '--json': options.json = true; break
            case '--force': options.force = true; break
            case '--self-test': options.selfTest = true; break
            case '--help': options.help = true; break
            default:
                throw new Error(`unknown argument: ${word} (try --help)`)
        }
    }
    return options
}

function main(argv) {
    let options
    try {
        options = parseArgs(argv)
    } catch (error) {
        process.stderr.write(String(error.message) + '\n')
        return EXIT_GIT
    }
    if (options.help) {
        process.stdout.write('usage: node internal/tools/sync-oversight-live.mjs [--repo <dir>] [--ref <ref>] [--dir <path>] [--target <dir>] [--json] [--force] [--self-test]\n')
        return EXIT_OK
    }
    if (options.selfTest) return selfTest()

    const say = options.json ? () => {} : (line) => process.stdout.write(line + '\n')
    const outcome = sync({ ...options, log: say })
    if (options.json) {
        process.stdout.write(JSON.stringify(outcome) + '\n')
    } else if (outcome.ok) {
        process.stdout.write(`ok: ${outcome.target} holds ${outcome.sha}${outcome.changed ? '' : ' (unchanged)'}\n`)
    } else {
        process.stdout.write(`FAILED (${outcome.stage}): ${outcome.message}\n`)
        if (outcome.liveUnchanged) process.stdout.write(`the live directory was not touched: ${outcome.target}\n`)
    }
    if (outcome.ok) return EXIT_OK
    return outcome.stage === 'git' || outcome.stage === 'replace' ? EXIT_GIT : EXIT_CHECK
}

// ---- self-test --------------------------------------------------------------------------------------

/**
 * The plan's acceptance, run on a throwaway repository:
 *
 *   a bad `index.js` is refused and the live directory does not move;
 *   a good one replaces it;
 *   the same commit a second time changes nothing (content compared, not timestamps);
 *   a tree that parses but throws on import is refused at the preflight, not at the import DSH runs.
 */
function selfTest() {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'oversight-sync-selftest-'))
    const repo = path.join(root, 'repo')
    const target = path.join(root, 'live')
    const dir = 'internal/tools/camel-oversight'
    const plugin = path.join(repo, dir)
    let checks = 0
    let failures = 0

    const check = (name, condition, detail = '') => {
        checks += 1
        if (condition) {
            process.stdout.write(`PASS ${name}\n`)
        } else {
            failures += 1
            process.stdout.write(`FAIL ${name}${detail ? `: ${detail}` : ''}\n`)
        }
    }
    const git = (...args) =>
        execFileSync('git', ['-C', repo, '-c', 'user.name=selftest', '-c', 'user.email=selftest@example.com', ...args], {
            encoding: 'utf8',
            windowsHide: true,
        })
    const write = (relative, text) => {
        const place = path.join(plugin, relative)
        fs.mkdirSync(path.dirname(place), { recursive: true })
        fs.writeFileSync(place, text)
    }
    const good = (extra = '') =>
        `import { readFile } from 'node:fs/promises'\n` +
        `import { measure } from './measure.js'\n` +
        `import { RULES } from './rules.js'\n` +
        `export const name = 'camel-oversight'\n` +
        `export const inject = ['tools']\n` +
        `${extra}` +
        `export function apply(ctx) { void readFile; void measure; void RULES; void ctx }\n`
    const commit = (message) => {
        git('add', '-A')
        git('commit', '-q', '-m', message)
        return git('rev-parse', 'HEAD').trim()
    }
    const hashes = (where) => {
        const files = {}
        for (const entry of fs.readdirSync(where, { withFileTypes: true })) {
            if (entry.isFile()) files[entry.name] = blobId(fs.readFileSync(path.join(where, entry.name)))
        }
        return JSON.stringify(files)
    }

    try {
        fs.mkdirSync(plugin, { recursive: true })
        git('init', '-q')
        fs.writeFileSync(path.join(plugin, 'package.json'), JSON.stringify({ name: '@local/camel-oversight', type: 'module', private: true }, null, 2) + '\n')
        write('index.js', good())
        write('measure.js', 'export const measure = () => ({})\n')
        write('rules.js', 'export const RULES = []\n')
        write('client.js', "window.__ModuleLoader__.load({ id: '@local/camel-oversight', factory() { return {} } })\n")
        write('README.md', '# fixture\n')
        write('cordis.patch.yml', '- insert: []\n')
        const first = commit('fixture: a good plugin tree')
        write('index.js', `${good()}// one more line, so the good tree moves\n`)
        const second = commit('fixture: a second good tree')
        write('index.js', 'export const name = "camel-oversight"\nexport function apply( {\n')
        const broken = commit('fixture: index.js does not parse')
        write('index.js', 'throw new Error("this parses and dies on import")\n')
        const throwing = commit('fixture: index.js parses and throws')

        // 1. A good tree is published.
        const one = sync({ repo, ref: first, dir, target })
        check('a good tree is published', one.ok && one.changed === true, JSON.stringify(one))
        check('the live directory holds every file', fs.existsSync(path.join(target, 'index.js')) && fs.existsSync(path.join(target, 'client.js')))
        const afterFirst = hashes(target)

        // 2. The same commit again changes nothing.
        const twice = sync({ repo, ref: first, dir, target })
        check('the same commit again is unchanged', twice.ok === true && twice.changed === false, JSON.stringify(twice))

        // 3. A broken index.js is refused, and the live directory does not move.
        const bad = sync({ repo, ref: broken, dir, target })
        check('a syntax error is refused', bad.ok === false && bad.stage === 'check', JSON.stringify(bad))
        check('the refusal names the file', bad.file === 'index.js', JSON.stringify(bad.file))
        check('the refusal names a line', typeof bad.line === 'number' && bad.line > 0, JSON.stringify(bad.line))
        check('the live directory is unchanged after a refusal', hashes(target) === afterFirst)
        process.stdout.write(`     refusal read: ${bad.message}\n`)

        // 4. A tree that parses but throws on import is refused at the preflight.
        const boom = sync({ repo, ref: throwing, dir, target })
        check('an import that throws is refused', boom.ok === false && boom.stage === 'import', JSON.stringify(boom))
        check('the live directory is unchanged after an import refusal', hashes(target) === afterFirst)
        process.stdout.write(`     refusal read: ${boom.message}\n`)

        // 5. A newer good tree still replaces it, and the record carries the commit.
        const next = sync({ repo, ref: second, dir, target })
        check('a newer good tree replaces it', next.ok === true && next.changed === true, JSON.stringify(next))
        check('the record carries the commit sha', fs.readFileSync(`${target}.sync.json`, 'utf8').includes(next.sha))
        check('the previous tree is kept beside the live one', fs.existsSync(`${target}.previous`))
    } finally {
        fs.rmSync(root, { recursive: true, force: true })
    }

    process.stdout.write(`${failures === 0 ? 'ok' : 'FAILED'}: ${checks - failures}/${checks} checks\n`)
    return failures === 0 ? EXIT_OK : EXIT_SELFTEST
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'))) {
    process.exitCode = main(process.argv.slice(2))
}