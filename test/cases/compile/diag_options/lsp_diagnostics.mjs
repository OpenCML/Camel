#!/usr/bin/env node
/**
 * Opens two documents in the language server, asks for their diagnostics
 * (textDocument/diagnostic: the server answers pulls, it does not push) and checks them:
 * the catalog code in `code`, the name and the suggestion in `data`, the secondary location
 * of a generic instance in `relatedInformation`, and several errors per document without the
 * consequences of the earlier ones.
 *
 * Usage: node lsp_diagnostics.mjs
 */

import fs from 'fs'
import path from 'path'
import url from 'url'
import { spawnSync } from 'child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const SERVER = path.join(REPO_ROOT, 'out', 'latest', 'bin', process.platform === 'win32' ? 'camel-ls.exe' : 'camel-ls')
const RENDER = path.resolve(HERE, '..', 'diag_render')
const env = { ...process.env, CAMEL_HOME: process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest') }

function frame(message) {
    const body = JSON.stringify(message)
    return `Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`
}

function uriOf(file) {
    return url.pathToFileURL(file).href
}

function open(file) {
    return frame({
        jsonrpc: '2.0',
        method: 'textDocument/didOpen',
        params: { textDocument: { uri: uriOf(file), languageId: 'camel', version: 1, text: fs.readFileSync(file, 'utf8') } },
    })
}

const generic = path.join(RENDER, 'sem_generic_instance.cml')
const cascade = path.join(RENDER, 'sem_cascade.cml')
const input =
    frame({ jsonrpc: '2.0', id: 1, method: 'initialize', params: { capabilities: {} } }) +
    frame({ jsonrpc: '2.0', method: 'initialized', params: {} }) +
    open(generic) +
    open(cascade) +
    frame({ jsonrpc: '2.0', id: 10, method: 'textDocument/diagnostic', params: { textDocument: { uri: uriOf(generic) } } }) +
    frame({ jsonrpc: '2.0', id: 11, method: 'textDocument/diagnostic', params: { textDocument: { uri: uriOf(cascade) } } }) +
    frame({ jsonrpc: '2.0', id: 2, method: 'shutdown' }) +
    frame({ jsonrpc: '2.0', method: 'exit' })

const proc = spawnSync(SERVER, ['--stdio'], { env, input, encoding: 'utf8', timeout: 20000 })

const published = new Map()
let rest = proc.stdout || ''
for (;;) {
    const m = rest.match(/Content-Length: (\d+)\r\n\r\n/)
    if (!m) break
    const start = m.index + m[0].length
    const body = Buffer.from(rest, 'utf8').subarray(Buffer.byteLength(rest.slice(0, start)), Buffer.byteLength(rest.slice(0, start)) + Number(m[1])).toString('utf8')
    rest = rest.slice(start + body.length)
    const message = JSON.parse(body)
    if (message.id === 10 && message.result) published.set(uriOf(generic), message.result.items || [])
    if (message.id === 11 && message.result) published.set(uriOf(cascade), message.result.items || [])
}

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

const g = published.get(uriOf(generic)) || []
const instance = g.find((d) => d.message.includes("operator '+' is not defined"))
check('generic: the instance error is reported', Boolean(instance), JSON.stringify(g))
if (instance) {
    check('code is the catalog code', instance.code === '0x0300000F', String(instance.code))
    check('data.name is the diagnostic name', instance.data && instance.data.name === 'SemanticDiag::NoMatchingFunction', JSON.stringify(instance.data))
    check('data.suggestion carries the suggestion', instance.data && typeof instance.data.suggestion === 'string' && instance.data.suggestion.includes('Available overloads'), JSON.stringify(instance.data))
    const related = instance.relatedInformation || []
    check('one related location', related.length === 1, JSON.stringify(related))
    if (related.length === 1) {
        check('related location is the call', related[0].location.uri === uriOf(generic) && related[0].location.range.start.line === 9, JSON.stringify(related[0].location))
        check('related message names the instance', related[0].message.includes("in the instance 'twice<i64[]>'"), related[0].message)
    }
    check('the message is free of the instance note', !instance.message.includes('instantiated'), instance.message)
}

const c = (published.get(uriOf(cascade)) || []).filter((d) => d.severity === 1)
check('cascade: two errors, not the consequences', c.length === 2, JSON.stringify(c.map((d) => d.message)))
check('cascade: both carry the code', c.every((d) => d.code === '0x0300000B'), JSON.stringify(c.map((d) => d.code)))

console.log(failed ? 'FAILED' : 'PASS')
process.exit(failed ? 1 : 0)
