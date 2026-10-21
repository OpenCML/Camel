// Why a removal failed is told apart by the error value's kind: "the path is not there" is
// NotFound, "the file system refused" is PermissionDenied, and both carry the operator as
// their location. The permission case cannot be arranged from Camel itself, so this verifier
// denies the removal around the call (a Windows deny ACE; elsewhere the permission bits of the
// containing directory) and restores it afterwards.
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { spawnSync } from 'node:child_process'

const cases = path.dirname(fileURLToPath(import.meta.url))
const repo = path.resolve(cases, '../../../../..')
const runtime = process.env.CAMEL_HOME || path.join(repo, 'out/latest')
const camel = path.join(runtime, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const parent = fs.realpathSync(process.env.CAMEL_TEST_TMPDIR || os.tmpdir())
const root = fs.mkdtempSync(path.join(parent, 'camel-fs-remove-'))
const casePath = path.join(cases, 'remove.cml')
const privileged = process.platform !== 'win32' && typeof process.getuid === 'function' && process.getuid() === 0

function invoke(mode, target) {
    const result = spawnSync(
        camel,
        [casePath, 'std::nvm', '--arg', `mode=${mode}`, '--arg', `path=${target}`],
        { env: { ...process.env, CAMEL_TEST_TMPDIR: root }, encoding: 'utf8', timeout: 20000 }
    )
    assert.ifError(result.error)
    assert.equal(result.status, 0, result.stdout + result.stderr)
    return result.stdout.trim()
}

// The account this process runs as, named by its SID. `USERDOMAIN\USERNAME` is not reliable on a
// runner: the deny ACE is accepted for a name that resolves to an account the token does not
// carry, so the deny has no effect and `icacls /remove:d` then cannot find the entry it made.
function currentUserSid() {
    const who = spawnSync('whoami', ['/user', '/fo', 'csv', '/nh'], { encoding: 'utf8' })
    if (who.status !== 0) {
        return null
    }
    const fields = who.stdout.trim().split(',')
    const sid = fields[fields.length - 1].replace(/"/g, '').trim()
    return /^S-\d-\d+(-\d+)+$/.test(sid) ? sid : null
}

// Whether a removal of `target` is refused right now: a rename needs DELETE on the entry, the same
// right a remove needs, and renaming back leaves everything as it was.
function removalRefused(target) {
    const probe = `${target}.deny-probe`
    try {
        fs.renameSync(target, probe)
    } catch {
        return true
    }
    fs.renameSync(probe, target)
    return false
}

// Refuses the removal of `target` for this process. On Windows removing an entry needs DELETE
// on the entry itself, so a deny ACE on it is refused even to an administrator. On POSIX
// removing an entry needs write permission on its containing directory.
//
// A runner is not this workstation: `icacls` may refuse the SID, or accept an ACE that does not
// bind the process. Both are reported and the check is skipped with the reason, because the
// subject here is what Camel reports for a refused removal, not whether a runner grants a deny.
function denyRemoval(target) {
    if (process.platform === 'win32') {
        const sid = currentUserSid()
        const account = sid ? `*${sid}` : `${process.env.USERDOMAIN}\\${process.env.USERNAME}`
        const denied = spawnSync('icacls', [target, '/deny', `${account}:(D)`], { encoding: 'utf8' })
        if (denied.status !== 0) {
            return { skip: `icacls could not deny ${account}: ${(denied.stdout + denied.stderr).trim()}` }
        }
        if (!removalRefused(target)) {
            return { skip: `a deny ACE on ${account} did not refuse the removal` }
        }
        return {
            undo: () => {
                const undone = spawnSync('icacls', [target, '/remove:d', account], { encoding: 'utf8' })
                assert.equal(undone.status, 0, undone.stdout + undone.stderr)
            },
        }
    }
    const directory = path.dirname(target)
    const mode = fs.statSync(directory).mode
    fs.chmodSync(directory, 0o555)
    return { undo: () => fs.chmodSync(directory, mode) }
}

// The checks themselves, so that a precondition a runner will not grant ends them with `return`
// and a printed reason instead of a failure: what is under test is the error Camel reports.
function checkRemovalRefusal() {
    const fileParent = path.join(root, 'file-parent')
    fs.mkdirSync(fileParent)
    const file = path.join(fileParent, 'denied.txt')
    fs.writeFileSync(file, 'x')
    const first = denyRemoval(file)
    if (first.skip) {
        console.log(`fs.remove permission checks skipped (${first.skip})`)
        return
    }
    let denied
    try {
        denied = invoke(1, file)
    } finally {
        first.undo()
    }
    assert.equal(denied, 'PermissionDenied | <fs::remove> | denied', denied)
    assert(fs.existsSync(file), 'a refused remove deleted the file')
    assert.equal(fs.readFileSync(file, 'utf8'), 'x')

    const directoryParent = path.join(root, 'directory-parent')
    fs.mkdirSync(directoryParent)
    const directory = path.join(directoryParent, 'denied')
    fs.mkdirSync(directory)
    const second = denyRemoval(directory)
    if (second.skip) {
        console.log(`fs.remove permission checks skipped (${second.skip})`)
        return
    }
    let deniedDirectory
    try {
        deniedDirectory = invoke(2, directory)
    } finally {
        second.undo()
    }
    assert.equal(deniedDirectory, 'PermissionDenied | <fs::remove_dir> | denied', deniedDirectory)
    assert(fs.statSync(directory).isDirectory(), 'a refused remove_dir deleted the directory')

    // The same calls succeed once the refusal is gone.
    assert.equal(invoke(1, file), 'ok | -')
    assert(!fs.existsSync(file))
    assert.equal(invoke(2, directory), 'ok | -')
    assert(!fs.existsSync(directory))
    console.log('fs.remove permission checks passed')
}

try {
    if (privileged) {
        console.log('fs.remove permission checks skipped (privileged process)')
    } else {
        checkRemovalRefusal()
    }
} finally {
    assert(path.isAbsolute(root) && path.dirname(root) === parent && path.basename(root).startsWith('camel-fs-remove-'))
    fs.rmSync(root, { recursive: true, force: true })
}