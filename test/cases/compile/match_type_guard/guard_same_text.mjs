#!/usr/bin/env node
/**
 * A guard's identity is what it says, not where it is written down (design principles 5.5): three
 * type functions write the same guard text, `is_numeric::<T>()`, over the same subjects, and every
 * case has to get the answer its own function states -- a number takes each function's guarded
 * case, a string falls through to each function's own default, and the order the questions are
 * asked in changes nothing. A shape word still decides before any guard is read.
 *
 * The program is written to the run's scratch directory rather than kept as a case of its own: a
 * `.cml` file in the tree is recorded in the AST snapshot, and this reading is about the type
 * layer's identity, not about the tree.
 *
 * Usage: node guard_same_text.mjs --camel <camel executable>
 */

import fs from 'fs'
import os from 'os'
import path from 'path'
import { spawnSync } from 'child_process'

const PROGRAM = `/// A guard's identity is what it says: the same predicate written in three type functions is one
/// question, and each case still gets the answer of its own case.
import { is_numeric, is_same } from types

/// ① the first copy: a number takes \`int[1]\`, anything else \`int[2]\`.
type FirstCopy<T> = match type T {
    case _ when is_numeric::<T>() => int[1]
    case _ => int[2]
}

/// ② the same guard text, other results: a number takes \`int[3]\`, anything else \`int[4]\`.
type SecondCopy<T> = match type T {
    case _ when is_numeric::<T>() => int[3]
    case _ => int[4]
}

/// ③ the same guard text once more, after a shape word has already decided the arrays.
type ArrayFirst<T> = match type T {
    case array => int[5]
    case _ when is_numeric::<T>() => int[6]
    case _ => int[7]
}

func main(): int sync {
    // ① and ②: one question asked by two type functions, each case answered for itself.
    println(is_same::<FirstCopy<int>, int[1]>(), is_same::<SecondCopy<int>, int[3]>())
    // ③ the same two questions with a subject the guard refuses: each falls through to its own
    //    default and neither takes the other's.
    println(is_same::<FirstCopy<string>, int[2]>(), is_same::<SecondCopy<string>, int[4]>())
    // ④ the answers do not depend on the order the questions are asked in.
    println(is_same::<SecondCopy<string>, int[4]>(), is_same::<FirstCopy<int>, int[1]>())
    // ⑤ a shape word decides before any guard is read.
    println(is_same::<ArrayFirst<float[2]>, int[5]>(), is_same::<ArrayFirst<int>, int[6]>())
    println(is_same::<ArrayFirst<string>, int[7]>())
    return 0
}
`

const EXPECTED = 'true true\ntrue true\ntrue true\ntrue true\ntrue\n'

const args = process.argv.slice(2)
const at = args.indexOf('--camel')
const CAMEL = at >= 0 ? args[at + 1] : 'camel'
const scratch = fs.mkdtempSync(path.join(process.env.CAMEL_TEST_TMPDIR || os.tmpdir(), 'guard-same-text-'))
const program = path.join(scratch, 'same_text.cml')
fs.writeFileSync(program, PROGRAM)

/// One backend's answer: what it printed, and whether it is the answer the rules give.
function answerOf(backend) {
    const run = spawnSync(CAMEL, [program, backend], { env: { ...process.env }, encoding: 'utf8' })
    const stdout = (run.stdout || '').replace(/\r\n/g, '\n')
    return { stdout, ok: run.status === 0 && stdout === EXPECTED, status: run.status, stderr: run.stderr || '' }
}

const nvm = answerOf('std::nvm')
const fvm = answerOf('std::fvm')
for (const [backend, answer] of [['nvm', nvm], ['fvm', fvm]]) {
    if (!answer.ok) {
        console.log(`GUARD ${backend} exit=${answer.status}`)
        console.log(`GUARD ${backend} stdout:\n${answer.stdout}`)
        console.log(`GUARD ${backend} stderr:\n${answer.stderr}`)
    }
}
console.log(`GUARD same_text=${nvm.ok ? 'yes' : 'no'}`)
console.log(`GUARD backends_agree=${nvm.stdout === fvm.stdout ? 'yes' : 'no'}`)
fs.rmSync(scratch, { recursive: true, force: true })
process.exit(nvm.ok && fvm.ok && nvm.stdout === fvm.stdout ? 0 : 1)