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
function invoke(source, pass, mode, directory, extraEnv = {}, exit = 0) {
    const result = spawnSync(camel, [path.join(cases, source), pass, '--arg', `mode=${mode}`, '--arg', `dir=${directory}`], {
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
        assert.equal(lines[8], '[0, 0, 0, 128, 0, 0, 0, 128]')
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
        // The checkpoint's names are exactly the paths of tree_paths (design principles 2.17):
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
    }
    const stress = {
        CAMEL_GC: 'generational', CAMEL_GC_ENABLE_YOUNG_COPYING: '1', CAMEL_GC_VERIFY: 'both',
        CAMEL_GC_STRESS_SAFEPOINT: '1', CAMEL_GC_STRESS_MODE: 'minor'
    }
    for (const pass of ['std::nvm', 'std::fvm', 'std::jit']) {
        assert.equal(invoke('checkpoint_roundtrip.cml', pass, 1, path.join(root, 'nvm'), stress).stdout, original)
    }
    const tensor = path.join(root, 'tensor')
    const bits = invoke('checkpoint_tensor.cml', 'std::jit', 0, tensor).stdout.trim()
    assert.equal(bits, '[1, 0, 128, 127, 1, 0, 192, 127, 0, 0, 128, 127, 1, 0, 0, 0]')
    const immutable = fs.readFileSync(path.join(tensor, '_root.npy'))
    assert.match(invoke('checkpoint_tensor.cml', 'std::fvm', 1, tensor).stdout, /CheckpointError.*tensor dtype differs/)
    assert.match(invoke('checkpoint_tensor.cml', 'std::nvm', 2, tensor).stdout, /CheckpointError.*extent differs at axis 0/)
    const collision = invoke('checkpoint_tensor.cml', 'std::jit', 4, tensor, {}, 1)
    assert.match(collision.stdout + collision.stderr, /checkpoint already exists/)
    assert.deepEqual(fs.readFileSync(path.join(tensor, '_root.npy')), immutable)
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
    assert.match(invoke('checkpoint_tensor.cml', 'std::nvm', 3, path.join(root, 'missing')).stdout, /CheckpointError.*manifest.json/)
    console.log('native checkpoints: schedulers, restart equality, GC, exact bits and rejection checks passed')
} finally {
    // This is the single directory reserved by mkdtemp, never an input checkpoint directory.
    assert(path.isAbsolute(root) && path.dirname(root) === parent && path.basename(root).startsWith('camel-checkpoint-'))
    fs.rmSync(root, { recursive: true, force: true })
}
