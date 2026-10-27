// Integration checks follow manually observed CLI behavior, including separate processes.
import assert from 'node:assert/strict'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { spawnSync } from 'node:child_process'
import { createHash } from 'node:crypto'

const cases = path.dirname(fileURLToPath(import.meta.url))
const repo = path.resolve(cases, '../../../..')
const runtime = process.env.CAMEL_HOME || path.join(repo, 'out/latest')
const camel = path.join(runtime, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const parent = fs.realpathSync(process.env.CAMEL_TEST_TMPDIR || os.tmpdir())
const root = fs.mkdtempSync(path.join(parent, 'camel-checkpoint-'))
const sha256 = bytes => createHash('sha256').update(bytes).digest('hex')
// The previous checkpoint is a hidden sibling of the checkpoint, not the checkpoint itself:
// `out/ckpt` keeps it at `out/.ckpt.ckpt-previous`.
const previousOf = directory => path.join(path.dirname(directory), `.${path.basename(directory)}.ckpt-previous`)
// Every file under a directory and its hash, to prove a failed save changed nothing.
function snapshotTree(directory) {
    const entries = []
    const walk = (current, prefix) => {
        for (const name of fs.readdirSync(current).sort()) {
            const full = path.join(current, name)
            if (fs.statSync(full).isDirectory()) walk(full, `${prefix}${name}/`)
            else entries.push(`${prefix}${name}:${sha256(fs.readFileSync(full))}`)
        }
    }
    walk(directory, '')
    return entries
}
// The account this process runs as, named by its SID: `USERDOMAIN\USERNAME` is not reliable on a
// runner, where the deny ACE is accepted for a name the token does not carry, so the write is not
// refused at all and `icacls /remove:d` cannot find the entry it made.
function currentUserSid() {
    const who = spawnSync('whoami', ['/user', '/fo', 'csv', '/nh'], { encoding: 'utf8' })
    if (who.status !== 0) {
        return null
    }
    const fields = who.stdout.trim().split(',')
    const sid = fields[fields.length - 1].replace(/"/g, '').trim()
    return /^S-\d-\d+(-\d+)+$/.test(sid) ? sid : null
}
// Writes into a directory are refused for this process: on Windows with a deny ACE on the
// directory (deny ACEs bind administrators too), elsewhere with the permission bits. Returns the
// undo, or a `skip` reason when a runner will not grant the refusal: what is under test is the
// fault Camel reports for a save it cannot make, not whether a runner honours a deny ACE.
function denyWrites(directory) {
    if (process.platform === 'win32') {
        const sid = currentUserSid()
        const account = sid ? `*${sid}` : `${process.env.USERDOMAIN}\\${process.env.USERNAME}`
        const denied = spawnSync('icacls', [directory, '/deny', `${account}:(W)`], { encoding: 'utf8' })
        if (denied.status !== 0) {
            return { skip: `icacls could not deny ${account}: ${(denied.stdout + denied.stderr).trim()}` }
        }
        const probe = path.join(directory, '.deny-probe')
        let refused = false
        try {
            fs.writeFileSync(probe, 'x')
            fs.rmSync(probe, { force: true })
        } catch {
            refused = true
        }
        if (!refused) {
            return { skip: `a deny ACE on ${account} did not refuse a write` }
        }
        return {
            undo: () => {
                const undone = spawnSync('icacls', [directory, '/remove:d', account], { encoding: 'utf8' })
                assert.equal(undone.status, 0, undone.stdout + undone.stderr)
            },
        }
    }
    const mode = fs.statSync(directory).mode
    fs.chmodSync(directory, 0o555)
    return { undo: () => fs.chmodSync(directory, mode) }
}
const privileged = process.platform !== 'win32' && typeof process.getuid === 'function' && process.getuid() === 0
function invoke(source, pass, mode, directory, extraEnv = {}, exit = 0, options = []) {
    const result = spawnSync(camel, [...options, path.join(cases, source), pass, '--arg', `mode=${mode}`, '--arg', `dir=${directory}`], {
        env: { ...process.env, ...extraEnv }, encoding: 'utf8', timeout: 30000
    })
    assert.ifError(result.error)
    assert.equal(result.status, exit, result.stdout + result.stderr)
    assert.equal(result.signal, null)
    return result
}
try {
    let original
    for (const pass of ['std::nvm', 'std::fvm', 'std::jit', 'std::prl']) {
        const directory = path.join(root, pass.slice(5))
        const saved = invoke('checkpoint_roundtrip.cml', pass, 0, directory).stdout
        const lines = saved.trim().split(/\r?\n/)
        assert.equal(lines.length, 9)
        assert.equal(lines[0], '3')
        // new([float(s.gain), s.scale]) is a f64[..] — float(...) and a float literal make the
        // array float64 — and the bytes are its two doubles, exactly as written (P4: a tensor
        // keeps the element type it is written with; it used to be normalized to f32 here).
        assert.equal(lines[8], '[0, 0, 0, 0, 0, 0, 0, 128, 0, 0, 0, 0, 0, 0, 0, 128]')
        assert.equal(fs.readFileSync(path.join(directory, 'gain.txt'), 'utf8'), '80000000')
        assert.equal(fs.readFileSync(path.join(directory, 'scale.txt'), 'utf8'), '8000000000000000')
        if (original) assert.equal(saved, original)
        original = saved
        assert.equal(invoke('checkpoint_roundtrip.cml', pass, 1, directory).stdout, saved, 'expected-type inference changed the state')
        const resumed = invoke('checkpoint_roundtrip.cml', pass, 2, directory).stdout
        const continuous = invoke('checkpoint_roundtrip.cml', pass, 3, directory).stdout
        assert.equal(resumed, continuous, `${pass}: restarting changed params/Adam/EMA/step/cursor`)
        assert.equal(continuous.split(/\r?\n/)[0], '10')
        const manifest = JSON.parse(fs.readFileSync(path.join(directory, 'manifest.json')))
        assert.equal(manifest.schema, 1)
        assert.equal(manifest.dataset_manifest_sha256, 'dataset-v1')
        const checkLeaf = node => {
            if (node.members) for (const child of node.members) checkLeaf(child.type)
            else {
                const bytes = fs.readFileSync(path.join(directory, node.file))
                assert.equal(node.bytes, bytes.length)
                assert.equal(node.sha256, sha256(bytes))
            }
        }
        checkLeaf(manifest.tree)
        // The checkpoint's names are exactly the paths of tree_paths (design principles 13.5):
        // one file per leaf, named by the path, and the manifest records the same path.
        const pathsText = invoke('checkpoint_roundtrip.cml', pass, 6, directory).stdout.trim()
        const treePaths = pathsText.slice(1, -1).split(', ')
        const leafPaths = []
        const collect = node => {
            if (node.members) for (const child of node.members) collect(child.type)
            else leafPaths.push(node.path)
        }
        collect(manifest.tree)
        assert.deepEqual(leafPaths, treePaths, 'manifest paths differ from tree_paths')
        for (const leaf of treePaths) {
            const files = fs.readdirSync(directory).filter(name => name.slice(0, name.lastIndexOf('.')) === leaf)
            assert.equal(files.length, 1, `no file named by the path ${leaf}`)
        }
        // Replacing an existing checkpoint as a whole: mode 7 saves a different state over a
        // checkpoint of the same tree and reads the new content back, on every scheduler. It
        // must equal a fresh checkpoint of that state (not a mix of old and new leaves), and
        // leave no temporary or previous directory behind. A separate directory is used so the
        // restart checks below still see the checkpoint they were given.
        const replaceDir = path.join(root, `replace-${pass.slice(5)}`)
        assert.equal(invoke('checkpoint_roundtrip.cml', pass, 0, replaceDir).stdout, saved)
        const fresh = path.join(root, `fresh-${pass.slice(5)}`)
        const expectedReplacement = invoke('checkpoint_roundtrip.cml', pass, 7, fresh).stdout
        assert.equal(invoke('checkpoint_roundtrip.cml', pass, 8, fresh).stdout, expectedReplacement, `${pass}: the replacement round trip is not the state`)
        const replacedOver = invoke('checkpoint_roundtrip.cml', pass, 7, replaceDir).stdout
        assert.notEqual(replacedOver, saved, `${pass}: saving over a checkpoint did not change it`)
        assert.equal(replacedOver, expectedReplacement, `${pass}: replacing a checkpoint mixed the old and new states`)
        assert.deepEqual(
            fs.readdirSync(root).filter(name => name.includes('.pending-') || name.endsWith('.ckpt-previous')),
            [],
            `${pass}: a temporary or previous directory was left behind`
        )
        // A tree of a different shape replaces every leaf: the old leaf files are gone, not
        // merged under the new manifest.
        assert.equal(invoke('checkpoint_roundtrip.cml', pass, 9, replaceDir).stdout.trim(), '1 false true')
        assert.deepEqual(fs.readdirSync(replaceDir).sort(), ['a.txt', 'manifest.json'], `${pass}: old leaves survived the replacement`)
    }
    const stress = {
        CAMEL_GC_ENABLE_YOUNG_COPYING: '1', CAMEL_GC_VERIFY: 'both',
        CAMEL_GC_STRESS_SAFEPOINT: '1', CAMEL_GC_STRESS_MODE: 'minor'
    }
    for (const pass of ['std::nvm', 'std::fvm', 'std::jit']) {
        assert.equal(invoke('checkpoint_roundtrip.cml', pass, 1, path.join(root, 'nvm'), stress, 0, ['--gc', 'generational']).stdout, original)
    }
    const tensor = path.join(root, 'tensor')
    const bits = invoke('checkpoint_tensor.cml', 'std::jit', 0, tensor).stdout.trim()
    assert.equal(bits, '[1, 0, 128, 127, 1, 0, 192, 127, 0, 0, 128, 127, 1, 0, 0, 0]')
    const immutable = fs.readFileSync(path.join(tensor, '_root.npy'))
    assert.match(invoke('checkpoint_tensor.cml', 'std::fvm', 1, tensor).stdout, /CheckpointError.*tensor dtype differs/)
    assert.match(invoke('checkpoint_tensor.cml', 'std::nvm', 2, tensor).stdout, /CheckpointError.*extent differs at axis 0/)
    const corrupt = path.join(root, 'corrupt')
    fs.cpSync(tensor, corrupt, { recursive: true })
    fs.appendFileSync(path.join(corrupt, '_root.npy'), 'x')
    assert.match(invoke('checkpoint_tensor.cml', 'std::fvm', 3, corrupt).stdout, /CheckpointError.*length or SHA-256 mismatch/)
    const malformed = path.join(root, 'malformed')
    fs.cpSync(tensor, malformed, { recursive: true })
    const malformedNpy = Buffer.from(immutable)
    const descr = malformedNpy.indexOf(Buffer.from('<f4'))
    assert(descr >= 0)
    malformedNpy[descr + 2] = '8'.charCodeAt(0)
    fs.writeFileSync(path.join(malformed, '_root.npy'), malformedNpy)
    const manifestFile = path.join(malformed, 'manifest.json')
    const manifest = JSON.parse(fs.readFileSync(manifestFile))
    manifest.tree.sha256 = sha256(malformedNpy)
    fs.writeFileSync(manifestFile, JSON.stringify(manifest))
    assert.match(invoke('checkpoint_tensor.cml', 'std::jit', 3, malformed).stdout, /CheckpointError.*exact f32\/int64\/bool storage dtype/)
    assert.match(invoke('checkpoint_tensor.cml', 'std::nvm', 3, path.join(root, 'missing')).stdout, /NotFound.*no checkpoint at this path/)
    // A tensor checkpoint is replaced too: mode 4 writes one element over the four of the
    // original, and the old leaf is gone rather than left beside the new manifest.
    const overwritten = invoke('checkpoint_tensor.cml', 'std::jit', 4, tensor, {}, 0)
    assert.equal(overwritten.status, 0)
    assert.notDeepEqual(fs.readFileSync(path.join(tensor, '_root.npy')), immutable)
    assert.deepEqual(fs.readdirSync(tensor).sort(), ['_root.npy', 'manifest.json'])
    assert.equal(invoke('checkpoint_tensor.cml', 'std::nvm', 5, tensor).stdout.trim(), '[0, 0, 16, 65]')

    // The same directory named with a trailing separator is the same checkpoint.
    const trailing = path.join(root, 'trailing')
    const trailingSaved = invoke('checkpoint_roundtrip.cml', 'std::nvm', 0, `${trailing}${path.sep}`).stdout
    assert.equal(invoke('checkpoint_roundtrip.cml', 'std::nvm', 1, trailing).stdout, trailingSaved)

    // An interrupted save between its two renames: the destination is missing and only the
    // hidden `.<name>.ckpt-previous` sibling holds the complete old checkpoint. A load reads it
    // (with a warning); the next save restores it, replaces it, and leaves neither the previous
    // nor a temporary directory behind.
    const recovery = path.join(root, 'recovery')
    const recoverySaved = invoke('checkpoint_roundtrip.cml', 'std::nvm', 0, recovery).stdout
    const previous = previousOf(recovery)
    // A user directory that happens to be named like the old scheme is not this checkpoint's
    // backup: only the hidden fixed-prefix sibling is read or removed.
    const lookalike = `${recovery}.previous`
    fs.mkdirSync(lookalike)
    fs.writeFileSync(path.join(lookalike, 'user.txt'), 'user')
    fs.renameSync(recovery, previous)
    const recoveredRead = invoke('checkpoint_roundtrip.cml', 'std::nvm', 1, recovery)
    assert.equal(recoveredRead.stdout, recoverySaved, 'reading the previous checkpoint changed the state')
    assert.match(recoveredRead.stderr, /previous checkpoint an interrupted save_tree left/)
    const recoveredSave = invoke('checkpoint_roundtrip.cml', 'std::nvm', 7, recovery).stdout
    assert.equal(recoveredSave, invoke('checkpoint_roundtrip.cml', 'std::nvm', 8, recovery).stdout)
    assert(!fs.existsSync(previous))
    assert.equal(fs.readFileSync(path.join(lookalike, 'user.txt'), 'utf8'), 'user')
    assert.equal(invoke('checkpoint_roundtrip.cml', 'std::nvm', 1, recovery).stdout, recoveredSave)
    // A previous directory left by a save whose cleanup failed is removed by the next save.
    fs.mkdirSync(previous)
    fs.writeFileSync(path.join(previous, 'stale.txt'), 'stale')
    assert.equal(invoke('checkpoint_roundtrip.cml', 'std::nvm', 7, recovery).stdout, recoveredSave)
    assert(!fs.existsSync(previous))
    assert.deepEqual(
        fs.readdirSync(root).filter(name => name.includes('.pending-') || name.endsWith('.ckpt-previous')),
        []
    )

    // A failure while writing the temporary directory leaves the old checkpoint exactly as it
    // was: the new checkpoint is never written into the destination. On a privileged POSIX
    // process the permission bits do not refuse the write, so the check is skipped there.
    if (privileged) {
        console.log('native checkpoints: the failure path is skipped (privileged process)')
    } else {
        const failingParent = path.join(root, 'failure-parent')
        fs.mkdirSync(failingParent)
        const failing = path.join(failingParent, 'ckpt')
        const failingSaved = invoke('checkpoint_roundtrip.cml', 'std::nvm', 0, failing).stdout
        const failingBefore = snapshotTree(failing)
        const grant = denyWrites(failingParent)
        if (grant.skip) {
            console.log(`native checkpoints: the failure path is skipped (${grant.skip})`)
        } else {
            let failed
            try {
                failed = invoke('checkpoint_roundtrip.cml', 'std::nvm', 7, failing, {}, 1)
            } finally {
                grant.undo()
            }
            assert.match(
                failed.stdout + failed.stderr,
                /FileSystemError|cannot create the checkpoint temporary directory/,
                'a save that cannot create its temporary directory did not report a fault'
            )
            assert.deepEqual(snapshotTree(failing), failingBefore, 'a failed save changed the old checkpoint')
            assert.equal(invoke('checkpoint_roundtrip.cml', 'std::nvm', 1, failing).stdout, failingSaved)
            assert.deepEqual(
                fs.readdirSync(failingParent).filter(name => name.includes('.pending-') || name.endsWith('.ckpt-previous')),
                [],
                'a failed save left a temporary or previous directory behind'
            )
        }
    }
    console.log('native checkpoints: schedulers, restart equality, GC, exact bits and rejection checks passed')
} finally {
    // This is the single directory reserved by mkdtemp, never an input checkpoint directory.
    assert(path.isAbsolute(root) && path.dirname(root) === parent && path.basename(root).startsWith('camel-checkpoint-'))
    fs.rmSync(root, { recursive: true, force: true })
}
