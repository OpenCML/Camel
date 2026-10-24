#!/usr/bin/env node
/**
 * The value `main` returns goes to the output (`-o` / `--output <file>`, standard output by
 * default). To a file it is written in the layout `--arg` reads:
 *   - struct_param.cml takes its weights from a directory (fc.w.npy, fc.b.npy) and returns a
 *     tuple of two tensors: the result is a directory with 0.npy and 1.npy, under every
 *     scheduler, and 1.npy is 0.npy times the scalar argument;
 *   - scalars.cml returns an int: a text file holding the literal;
 *   - a `main` that returns nothing: RuntimeDiag::EntryResultNotWritten at main, exit 1.
 * On standard output (no `-o`): the tuple is printed as `println` prints it, after the
 * program's own output; an int result is the exit code and is not printed.
 *
 * Usage: node result.mjs
 */

import fs from 'fs'
import os from 'os'
import path from 'path'
import url from 'url'
import { spawnSync } from 'child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..')
const CAMEL = path.join(REPO_ROOT, 'out', 'latest', 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const env = { ...process.env, CAMEL_HOME: process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest') }
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'camel-entry-result-'))
const normalizeNewlines = (text) => text.replace(/\r\n/g, '\n')

/// The float32 payload of a little-endian .npy file (version 1 header).
function npyFloats(file) {
    const buf = fs.readFileSync(file)
    const headerLen = buf.readUInt16LE(8)
    const header = buf.subarray(10, 10 + headerLen).toString('latin1')
    if (!header.includes("'<f4'")) throw new Error(`${file}: expected float32, header ${header}`)
    const data = buf.subarray(10 + headerLen)
    return Array.from({ length: data.length / 4 }, (_, i) => data.readFloatLE(4 * i))
}

let failed = false
const check = (ok, pass, fail) => {
    console.log(ok ? pass : `FAIL ${fail}`)
    failed ||= !ok
}

for (const pass of [null, 'std::fvm', 'std::nvm', 'std::jit', 'std::tf']) {
    const out = path.join(tmp, `tuple-${(pass || 'default').replace(/:/g, '_')}`)
    const args = [
        path.join(HERE, 'struct_param.cml'),
        '--arg', `x=${path.join(HERE, 'x.npy')}`,
        '--arg', `p=${path.join(HERE, 'model')}`,
        '--arg', 'scale=2',
        '-o', out,
    ]
    if (pass) args.push(pass)
    const proc = spawnSync(CAMEL, args, { env, encoding: 'utf8' })
    let ok = proc.status === 0 && fs.existsSync(out)
    let detail = `exit ${proc.status}: ${proc.stdout} ${proc.stderr}`
    if (ok) {
        const files = fs.readdirSync(out).sort().join(' ')
        const h = npyFloats(path.join(out, '0.npy'))
        const h2 = npyFloats(path.join(out, '1.npy'))
        // tanh(x @ w + b) of the fixtures, first and last element (NumPy: 0.6877902, 0.635149).
        ok =
            files === '0.npy 1.npy' &&
            h.length === 8 &&
            Math.abs(h[0] - 0.6877902) < 1e-5 &&
            Math.abs(h[7] - 0.635149) < 1e-5 &&
            h2.every((v, i) => Math.abs(v - 2 * h[i]) < 1e-6)
        detail = `files ${files}, values ${h} / ${h2}`
    }
    check(ok, `${pass || 'default'}: tuple result: 0.npy 1.npy`, `${pass || 'default'}: ${detail}`)
}

{
    const out = path.join(tmp, 'n.txt')
    const proc = spawnSync(
        CAMEL,
        [path.join(HERE, 'scalars.cml'), '--arg', 'n=3', '--arg', 'rate=0.5', '--arg', 'name=hi', '--arg', 'loud=false', '--output', out],
        { env, encoding: 'utf8' },
    )
    const text = fs.existsSync(out) ? fs.readFileSync(out, 'utf8').trim() : '(no file)'
    check(proc.status === 4 && text === '4', `scalar result: ${text}`, `scalar result: exit ${proc.status}, file ${text}`)
}

{
    const out = path.join(tmp, 'void.txt')
    const program = path.join(tmp, 'void_main.cml')
    fs.writeFileSync(program, "module void_main\n\nfunc main(n: int): void sync {\n    println(n)\n}\n")
    const proc = spawnSync(CAMEL, ['--output-format', 'json', program, '--arg', 'n=3', '-o', out], { env, encoding: 'utf8' })
    let name = '(none)'
    let line = -1
    try {
        const text = normalizeNewlines(proc.stdout)
        const start = text.lastIndexOf('[\n{')
        const d = JSON.parse(text.slice(start))[0]
        name = d.data.name
        line = d.range.start.line
    } catch (e) {
        name = `unparsed: ${proc.stdout}`
    }
    check(
        proc.status === 1 && name === 'RuntimeDiag::EntryResultNotWritten' && line === 2 && !fs.existsSync(out),
        'void result: EntryResultNotWritten at main, no file',
        `void result: exit ${proc.status}, ${name} line ${line}`,
    )
}

{
    // No -o: the tuple goes to standard output in its text form, nothing is written to a file.
    const proc = spawnSync(
        CAMEL,
        [path.join(HERE, 'struct_param.cml'), '--arg', `x=${path.join(HERE, 'x.npy')}`, '--arg', `p=${path.join(HERE, 'model')}`, '--arg', 'scale=2'],
        { env, encoding: 'utf8' },
    )
    const out = proc.stdout
    // The ruled printed form is the array's (questions-2026-10-24-evening.md): `tensor(..., dtype=...)`
    // is gone with `Tensor`, so each member is counted by its own nested brackets.
    const arrays = out.split('[[').length - 1
    check(
        proc.status === 0 && arrays === 2 && out.includes('0.68779016') && out.includes('1.3755803') && out.endsWith('\n'),
        'tuple result on stdout: two tensors printed',
        `tuple result on stdout: exit ${proc.status}: ${out} ${proc.stderr}`,
    )
}

{
    // No -o and an int result: it is the exit code and is not printed.
    const proc = spawnSync(
        CAMEL,
        [path.join(HERE, 'scalars.cml'), '--arg', 'n=3', '--arg', 'rate=0.5', '--arg', 'name=hi', '--arg', 'loud=false'],
        { env, encoding: 'utf8' },
    )
    check(
        proc.status === 4 && normalizeNewlines(proc.stdout) === 'hi 3 0.5 false\n',
        'int result on stdout: not printed, exit code 4',
        `int result on stdout: exit ${proc.status}: ${JSON.stringify(proc.stdout)}`,
    )
}

{
    // No -o and no result: nothing is printed and nothing is refused.
    const program = path.join(tmp, 'void_main2.cml')
    fs.writeFileSync(program, "module void_main2\n\nfunc main(n: int): void sync {\n    println(n)\n}\n")
    const proc = spawnSync(CAMEL, [program, '--arg', 'n=3'], { env, encoding: 'utf8' })
    check(
        proc.status === 0 && normalizeNewlines(proc.stdout) === '3\n',
        'void result on stdout: nothing printed',
        `void result on stdout: exit ${proc.status}: ${JSON.stringify(proc.stdout)} ${proc.stderr}`,
    )
}

fs.rmSync(tmp, { recursive: true, force: true })
console.log(failed ? 'FAIL' : 'PASS')
process.exit(failed ? 1 : 0)
