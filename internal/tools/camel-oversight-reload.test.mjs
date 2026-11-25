// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * What `POST /oversee/reload` does, and in which order.
 *
 * The route can only be reached from a running DSH, but what it decides does not need one: `apply()`
 * takes a context, and every decision the route makes -- refuse a broken tree with the file and the
 * line, do nothing when the content has not moved, refuse to "reload" when the process did not load
 * from the live directory, and answer BEFORE the restart -- is made from `ctx` and from the sync
 * script's outcome. So the plugin is imported here with a stub context, and the route is called with
 * the request and response a web server would hand it.
 *
 * Four properties are the ones worth pinning, because each of them is a way to lose the plugin:
 *
 *   1. a sync that fails leaves the live directory alone, and says `file:line`;
 *   2. a sync that changes nothing does not reload -- a push that leaves the plugin untouched must not
 *      hiccup a coordinator mid-turn;
 *   3. a plugin that is not loaded from the live directory refuses rather than re-importing the old
 *      files and reporting success;
 *   4. the HTTP answer is written before `ctx.hmr.reconcileProfile` is called, because that call stops
 *      this very plugin: a response written after it would come from a module the process let go of.
 *
 * The home directory is redirected first, so the plugin's machine-local config (log path, repo) and
 * the log it appends to are the test's own; nothing here reads or writes `%USERPROFILE%\.camel-oversight`.
 */
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { execFileSync } from 'node:child_process'

const sandbox = fs.mkdtempSync(path.join(os.tmpdir(), 'oversight-reload-'))
process.env.USERPROFILE = path.join(sandbox, 'home')
process.env.HOME = process.env.USERPROFILE
fs.mkdirSync(path.join(process.env.USERPROFILE, '.camel-oversight'), { recursive: true })

const here = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, '$1'))
const pluginDir = path.join(here, 'camel-oversight')
const syncScript = path.join(here, 'sync-oversight-live.mjs')
const repo = 'D:\\Projects\\Camel'
const logFile = path.join(sandbox, 'log.jsonl')

let checks = 0
let failures = 0
const check = (name, condition, detail = '') => {
    checks += 1
    if (condition) process.stdout.write(`PASS ${name}\n`)
    else {
        failures += 1
        process.stdout.write(`FAIL ${name}${detail ? `: ${detail}` : ''}\n`)
    }
}

const toUrl = (file) => {
    let resolved = path.resolve(file).replace(/\\/g, '/')
    if (!resolved.startsWith('/')) resolved = '/' + resolved
    return 'file://' + encodeURI(resolved)
}

/** One plugin instance, with a context that records what it would have registered. */
async function instance({ loadFrom, liveDir, repoPath = repo, ref = 'origin/develop', hmr = null }) {
    const routes = new Map()
    const ctx = {
        logger: { warn() {}, info() {}, error() {}, debug() {} },
        effect: (fn) => {
            const disposer = fn()
            return () => {
                if (typeof disposer === 'function') disposer()
            }
        },
        get: (name) => (name === 'hmr' ? hmr : undefined),
        agents: { list: () => [], get: () => null, currentInitiator: () => null },
        tools: { register: () => () => {} },
        on: () => () => {},
        inject: () => () => {},
        systemPrompt: { context: () => () => {} },
        timer: { interval: () => () => {}, timeout: () => () => {} },
        webServer: {
            register: ({ path: routePath, handler }) => {
                routes.set(routePath, handler)
                return () => {}
            },
        },
    }
    const module = await import(toUrl(path.join(loadFrom, 'index.js')))
    module.apply(ctx, { repo: repoPath, syncScript, syncRef: ref, liveDir, logFile })
    return routes
}

/** A response the route can write to, remembering when the body was finished. */
function response() {
    return {
        status: 0,
        body: null,
        headers: null,
        endedAt: 0,
        writeHead(status, headers) {
            this.status = status
            this.headers = headers
        },
        end(body) {
            this.body = body === undefined ? null : JSON.parse(body)
            this.endedAt = Date.now()
        },
    }
}

async function call(routes, routePath, method = 'POST') {
    const handler = routes.get(routePath)
    if (!handler) throw new Error(`route ${routePath} was not registered`)
    const res = response()
    await handler({ method, url: routePath, headers: {}, on() {} }, res)
    return res
}

// ---- a fixture repository with three trees: good, unparsable, and parses-then-throws -------------

function fixture() {
    const root = path.join(sandbox, 'fixture')
    const dir = path.join(root, 'internal', 'tools', 'camel-oversight')
    fs.mkdirSync(dir, { recursive: true })
    const git = (...args) =>
        execFileSync('git', ['-C', root, '-c', 'user.name=selftest', '-c', 'user.email=selftest@example.com', ...args], {
            encoding: 'utf8',
        })
    const write = (name, text) => fs.writeFileSync(path.join(dir, name), text)
    const commit = (message) => {
        git('add', '-A')
        git('commit', '-q', '-m', message)
        return git('rev-parse', 'HEAD').trim()
    }
    const good = `export const name = 'camel-oversight'\nexport function apply(ctx) { void ctx }\n`
    git('init', '-q')
    fs.writeFileSync(path.join(dir, 'package.json'), JSON.stringify({ name: '@local/camel-oversight', type: 'module' }) + '\n')
    write('index.js', good)
    write('measure.js', 'export const measure = () => ({})\n')
    write('rules.js', 'export const RULES = []\n')
    write('client.js', "window.__ModuleLoader__.load({ id: '@local/camel-oversight', factory() { return {} } })\n")
    const first = commit('fixture: good')
    write('index.js', 'export const name = "camel-oversight"\nexport function apply( {\n')
    const unparsable = commit('fixture: does not parse')
    write('index.js', 'throw new Error("parses, then dies")\n')
    const throwing = commit('fixture: parses and throws')
    return { root, first, unparsable, throwing }
}

async function run() {
    const fix = fixture()

    // ---- 1. a good sync, from a process that is NOT loaded from the live directory ----------------
    const liveA = path.join(sandbox, 'live-a')
    const routesA = await instance({ loadFrom: pluginDir, liveDir: liveA })
    const first = await call(routesA, '/oversee/reload')
    check('the sync puts the committed tree in the live directory', fs.existsSync(path.join(liveA, 'index.js')) && fs.existsSync(path.join(liveA, 'package.json')))
    check('the record beside the live directory names the commit', fs.readFileSync(`${liveA}.sync.json`, 'utf8').includes(first.body.sha))
    check('a plugin loaded from elsewhere refuses to claim a reload', first.status === 409 && first.body.reload === 'refused', JSON.stringify(first.body))
    check(
        'the refusal names both directories',
        typeof first.body.reason === 'string' && first.body.reason.includes(pluginDir) && first.body.reason.includes(liveA),
        String(first.body.reason),
    )

    // ---- 2. the same commit again is not a reload -------------------------------------------------
    const second = await call(routesA, '/oversee/reload')
    check('an unchanged tree answers 200 and does not reload', second.status === 200 && second.body.changed === false && second.body.reload === 'not-needed', JSON.stringify(second.body))

    // ---- 3. the band's version field ----------------------------------------------------------------
    const state = await call(routesA, '/oversee/state', 'GET')
    const plugin = state.body.plugin || {}
    check('state carries the version the last sync published', plugin.synced === second.body.sha, JSON.stringify(plugin))
    check('a process that started before the sync reports no loaded version', plugin.loaded === null, JSON.stringify(plugin.loaded))
    check('state says the plugin is not loaded from the live directory', plugin.fromLive === false, JSON.stringify(plugin.fromLive))
    check('state names the loading path', plugin.loading === pluginDir, JSON.stringify(plugin.loading))

    // ---- 4. a tree that does not parse is refused, with the file and the line -----------------------
    const liveB = path.join(sandbox, 'live-b')
    const routesB = await instance({ loadFrom: pluginDir, liveDir: liveB, repoPath: fix.root, ref: fix.unparsable })
    const bad = await call(routesB, '/oversee/reload')
    check('an unparsable index.js is refused', bad.status === 409 && bad.body.ok === false && bad.body.stage === 'check', JSON.stringify(bad.body))
    check('the refusal names the file and the line', bad.body.file === 'index.js' && Number(bad.body.line) > 0, JSON.stringify(bad.body))
    check('the refusal says the live directory was not touched', bad.body.liveUnchanged === true, JSON.stringify(bad.body.liveUnchanged))
    check('the live directory does not exist after a refusal', !fs.existsSync(liveB))
    process.stdout.write(`     refusal read: ${bad.body.message}\n`)

    // ---- 5. a tree that parses and throws is refused at the import gate ----------------------------
    const liveC = path.join(sandbox, 'live-c')
    const routesC = await instance({ loadFrom: pluginDir, liveDir: liveC, repoPath: fix.root, ref: fix.throwing })
    const boom = await call(routesC, '/oversee/reload')
    check('an index.js that throws on import is refused before the live directory moves', boom.status === 409 && boom.body.stage === 'import' && !fs.existsSync(liveC), JSON.stringify(boom.body))
    check('the import refusal names a line too', Number(boom.body.line) >= 1, JSON.stringify(boom.body.line))
    process.stdout.write(`     refusal read: ${boom.body.message}\n`)

    // ---- 6. loaded from the live directory: the reload path, including the order of the two --------
    const liveD = path.join(sandbox, 'live-d')
    fs.mkdirSync(liveD, { recursive: true })
    for (const name of ['index.js', 'measure.js', 'rules.js', 'client.js', 'package.json']) {
        fs.copyFileSync(path.join(pluginDir, name), path.join(liveD, name))
    }
    const calls = []
    const hmr = {
        runExclusive: (operation) => operation(),
        reconcileProfile: async (ids) => {
            calls.push({ ids, at: Date.now() })
            return { warnings: [], pending: [] }
        },
    }
    const routesD = await instance({ loadFrom: liveD, liveDir: liveD, hmr })
    const reloaded = await call(routesD, '/oversee/reload')
    check('a plugin loaded from the live directory schedules the reload', reloaded.status === 200 && reloaded.body.reload === 'scheduled', JSON.stringify(reloaded.body))
    check('the scheduled reload names this plugin alone', reloaded.body.loadedFrom === liveD, JSON.stringify(reloaded.body.loadedFrom))
    await new Promise((resolve) => setTimeout(resolve, 700))
    check('reconciliation runs once, for camel-oversight', calls.length === 1 && calls[0].ids.length === 1 && calls[0].ids[0] === 'camel-oversight', JSON.stringify(calls))
    check('the answer is written before reconciliation starts', calls.length === 1 && calls[0].at > reloaded.endedAt, `${calls[0] && calls[0].at} vs ${reloaded.endedAt}`)

    // ---- 7. a process running an older live directory than the last sync reads as "pending" --------
    // This is the band's 待重载: `loaded` is read once, when the instance starts; `synced` follows the
    // record. A sync that lands between the two is exactly the state the button has to resolve, and a
    // process with no hmr service must say so instead of pretending it reloaded.
    const routesE = await instance({ loadFrom: liveD, liveDir: liveD, repoPath: fix.root, ref: fix.first, hmr: null })
    const before = (await call(routesE, '/oversee/state', 'GET')).body.plugin || {}
    check('a process loaded from the live directory reports the version it loaded', typeof before.loaded === 'string' && before.loaded === before.synced, JSON.stringify(before))
    const unavailable = await call(routesE, '/oversee/reload')
    check('without an hmr service the route refuses instead of claiming a reload', unavailable.status === 409 && unavailable.body.reload === 'unavailable', JSON.stringify(unavailable.body))
    const after = (await call(routesE, '/oversee/state', 'GET')).body.plugin || {}
    check('state then reads the synced version as newer than the loaded one', after.synced === fix.first && after.loaded !== after.synced, JSON.stringify(after))

    // ---- 7. nothing above wrote to the machine's own home or log ----------------------------------
    check('the test used its own log', fs.readFileSync(logFile, 'utf8').length > 0)
}

run().then(
    () => {
        fs.rmSync(sandbox, { recursive: true, force: true })
        process.stdout.write(`${failures === 0 ? 'ok' : 'FAILED'}: ${checks - failures}/${checks} checks\n`)
        process.exitCode = failures === 0 ? 0 : 1
    },
    (error) => {
        fs.rmSync(sandbox, { recursive: true, force: true })
        process.stdout.write(`FAILED: ${error && error.stack ? error.stack : error}\n`)
        process.exitCode = 1
    },
)