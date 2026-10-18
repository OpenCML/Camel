// Opt-in local editor deployment after a commit. Never build or retry.
// Prepare and verify a complete sibling directory before replacing the fixed runtime.
import fs from 'node:fs'
import path from 'node:path'
import crypto from 'node:crypto'
import { execFileSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const required = ['bin/camel.exe', 'bin/camel-db.exe', 'bin/camel-ls.exe', 'libs/libcamel.dll']
const inside = (parent, child) => { const relative = path.relative(parent, child); return relative === '' || (!relative.startsWith('..') && !path.isAbsolute(relative)) }

function report(target, message, level = 'log') {
    const line = `[runtime-sync] ${message}`
    console[level](line)
    try {
        const logFile = path.join(path.dirname(path.resolve(target)), 'runtime-sync.log')
        fs.appendFileSync(logFile, `${new Date().toISOString()} ${line}\n`, 'utf8')
    } catch (error) {
        console.warn(`[runtime-sync] Could not write the local sync log: ${error.message}`)
    }
}

function manifest(root) {
    const result = new Map()
    const visit = relative => {
        const full = path.join(root, relative)
        const stat = fs.lstatSync(full)
        if (stat.isSymbolicLink()) throw new Error(`Runtime contains a link: ${full}`)
        if (stat.isDirectory()) for (const name of fs.readdirSync(full)) visit(path.join(relative, name))
        else if (stat.isFile()) result.set(relative, crypto.createHash('sha256').update(fs.readFileSync(full)).digest('hex'))
        else throw new Error(`Unsupported runtime entry: ${full}`)
    }
    visit('')
    return result
}

function stopRuntime(target) {
    if (process.platform !== 'win32') throw new Error('This local runtime hook currently supports Windows only')
    const raw = execFileSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command',
        "$ErrorActionPreference='Stop'; $root=$env:CAMEL_EDITOR_RUNTIME_TARGET; " +
        "$active=@(Get-Process -Name 'camel*' -ErrorAction SilentlyContinue | Where-Object { $_.Path -and $_.Path.StartsWith($root+'\\',[StringComparison]::OrdinalIgnoreCase) }); " +
        "foreach ($item in $active) { $item.Kill(); $item.WaitForExit() }; Write-Output $active.Count"],
        { encoding: 'utf8', windowsHide: true, env: { ...process.env, CAMEL_EDITOR_RUNTIME_TARGET: target } }).trim()
    if (Number(raw) > 0) report(target, `Stopped ${Number(raw)} Camel process(es) using the fixed runtime`)
}

export function syncRuntime(source, target) {
    const startedAt = Date.now()
    if (!path.isAbsolute(target)) throw new Error('The runtime destination must be absolute')
    source = fs.realpathSync(source)
    if (fs.lstatSync(target).isSymbolicLink()) throw new Error('The fixed runtime must be an independent directory')
    target = fs.realpathSync(target)
    if (inside(source, target) || inside(target, source) || inside(target, repoRoot)) throw new Error('Unsafe runtime destination')
    for (const relative of required) {
        if (!fs.statSync(path.join(source, relative)).isFile() || !fs.statSync(path.join(target, relative)).isFile())
            throw new Error(`Incomplete runtime: ${relative}`)
    }
    const expected = manifest(source)
    const parent = path.dirname(target)
    const suffix = crypto.randomUUID()
    const staged = path.join(parent, `.runtime-sync-${suffix}`)
    const backup = path.join(parent, `.runtime-old-${suffix}`)
    const remove = directory => {
        if (![staged, backup].includes(directory) || path.dirname(directory) !== parent) throw new Error('Unsafe cleanup path')
        fs.rmSync(directory, { recursive: true, force: true, maxRetries: 0 })
    }
    try {
        fs.cpSync(source, staged, { recursive: true, preserveTimestamps: true, errorOnExist: true, force: false })
        for (const directory of [staged, source]) {
            const actual = manifest(directory)
            if (actual.size !== expected.size || [...expected].some(([name, hash]) => actual.get(name) !== hash))
                throw new Error('Runtime changed during copying or verification failed')
        }
        // Stop only the fixed copy's Camel processes, once, after the new copy is ready.
        // VS Code and other checkouts' build/debug processes are outside this path.
        stopRuntime(target)
        fs.renameSync(target, backup)
        try { fs.renameSync(staged, target) }
        catch (error) { fs.renameSync(backup, target); throw error } // rollback, not a second sync attempt
        try { remove(backup) }
        catch (error) { report(target, `New runtime installed; old copy remains at ${backup}: ${error.message}. No retry.`, 'warn') }
        report(target, `Synchronized ${expected.size} files in ${Date.now() - startedAt} ms to ${target}`)
        return expected.size
    } finally {
        if (fs.existsSync(staged)) remove(staged)
    }
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
    let target = ''
    try {
        target = execFileSync('git', ['config', '--local', '--get', 'camel.runtimeSyncTarget'],
            { cwd: repoRoot, encoding: 'utf8', windowsHide: true }).trim()
        if (target) syncRuntime(path.join(repoRoot, 'out/latest'), target)
    } catch (error) {
        // An unset opt-in is normal. Every other failure is visible but does not affect the commit.
        if (!(error.status === 1 && error.stdout?.toString().trim() === ''))
            report(target || path.join(repoRoot, 'out/latest'), `Not synchronized: ${error.message}. No retry.`, 'warn')
    }
}
