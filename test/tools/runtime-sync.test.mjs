import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawn, spawnSync } from 'node:child_process'
import { syncRuntime } from '../../scripts/sync-editor-runtime.js'

const root = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-runtime-sync-test-'))
const source = path.join(root, 'source')
const target = path.join(root, 'runtime')
const binaries = ['bin/camel.exe', 'bin/camel-db.exe', 'bin/camel-ls.exe', 'libs/libcamel.dll']
let managed, unrelated
try {
    for (const directory of [source, target]) for (const file of binaries) {
        fs.mkdirSync(path.dirname(path.join(directory, file)), { recursive: true })
        fs.writeFileSync(path.join(directory, file), directory === source ? 'new' : 'old')
    }
    fs.writeFileSync(path.join(target, 'obsolete.txt'), 'stale')
    assert.equal(syncRuntime(source, target), 4)
    assert.equal(fs.readFileSync(path.join(target, binaries[0]), 'utf8'), 'new')
    assert.equal(fs.existsSync(path.join(target, 'obsolete.txt')), false)
    assert.deepEqual(fs.readdirSync(root).sort(), ['runtime', 'runtime-sync.log', 'source'])
    assert.match(fs.readFileSync(path.join(root, 'runtime-sync.log'), 'utf8'), /Synchronized 4 files/)
    // Use isolated ping executables as long-running Camel-named processes. The hook
    // must stop only the one inside the destination, never a different checkout.
    const ping = path.join(process.env.SystemRoot, 'System32/ping.exe')
    fs.copyFileSync(ping, path.join(target, 'bin/camel.exe'))
    const outside = path.join(root, 'camel-unrelated.exe')
    fs.copyFileSync(ping, outside)
    managed = spawn(path.join(target, 'bin/camel.exe'), ['-t', '127.0.0.1'], { stdio: 'ignore', windowsHide: true })
    unrelated = spawn(outside, ['-t', '127.0.0.1'], { stdio: 'ignore', windowsHide: true })
    const ended = new Promise(resolve => managed.once('exit', resolve))
    await new Promise(resolve => setTimeout(resolve, 150))
    assert.equal(managed.exitCode, null, 'managed fixture must still be running')
    assert.equal(unrelated.exitCode, null, 'unrelated fixture must still be running')
    assert.equal(syncRuntime(source, target), 4)
    await Promise.race([ended, new Promise((_, reject) => setTimeout(() => reject(new Error('managed fixture was not stopped')), 1500))])
    assert.equal(unrelated.exitCode, null, 'another checkout must keep running')
    fs.unlinkSync(path.join(source, binaries[0]))
    assert.throws(() => syncRuntime(source, target))
    assert.equal(fs.readFileSync(path.join(target, binaries[0]), 'utf8'), 'new', 'incomplete source must leave destination intact')
    assert.throws(() => syncRuntime(target, target), /Unsafe/)
    assert.throws(() => syncRuntime(target, 'relative-runtime'), /absolute/)
    // Exercise the CLI catch path without touching the real editor runtime.
    const fixtureGit = path.join(root, 'git-fixture')
    assert.equal(spawnSync('git', ['init', '--quiet', fixtureGit]).status, 0)
    assert.equal(spawnSync('git', ['-C', fixtureGit, 'config', 'camel.runtimeSyncTarget', 'relative-runtime']).status, 0)
    const failedSync = spawnSync(process.execPath, [path.resolve('scripts/sync-editor-runtime.js')], {
        cwd: root,
        env: { ...process.env, GIT_DIR: path.join(fixtureGit, '.git') },
        encoding: 'utf8',
    })
    assert.equal(failedSync.status, 0, failedSync.stderr)
    assert.match(failedSync.stderr, /Not synchronized: The runtime destination must be absolute/)
    assert.doesNotMatch(failedSync.stderr, /ReferenceError/)
    console.log('RUNTIME_SYNC_OK: replacement, process-path isolation, failure leaves runtime intact, unsafe paths rejected')
} finally {
    for (const child of [managed, unrelated]) if (child && child.exitCode === null) {
        const ended = new Promise(resolve => child.once('exit', resolve))
        child.kill()
        await ended
    }
    if (path.dirname(root) !== fs.realpathSync(os.tmpdir()) && path.dirname(root) !== path.resolve(os.tmpdir()))
        throw new Error('Unexpected test cleanup directory')
    fs.rmSync(root, { recursive: true, force: true, maxRetries: 0 })
}
