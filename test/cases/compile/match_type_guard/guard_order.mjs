#!/usr/bin/env node
/**
 * The reading a type gets does not depend on which module was declared first.
 *
 * `plain` declares a `match type` with a concrete subject and no guard, and uses it as a parameter
 * type; `guarded` declares a guarded `match type` and nothing else. The two programs below import
 * them in the two possible orders and are otherwise the same program, and what each says about
 * `plain::wants` has to be the same text. A reading that depends on the order is what a piece of
 * process-wide state in the type layer would produce, so this is the case that says there is none.
 *
 * The three modules are written to the run's scratch directory rather than kept as `.cml` cases:
 * every `.cml` of the tree is recorded in the AST snapshot, and this reading is about the type
 * layer, not about the tree.
 *
 * Usage: node guard_order.mjs --camel <camel executable>
 */

import fs from 'fs'
import os from 'os'
import path from 'path'
import { spawnSync } from 'child_process'

const PLAIN = `/// A guard-free \`match type\` with a concrete subject, used as a parameter type.
export type Zero = match type int {
    case _ => int[0]
}

export func wants(x: Zero): int sync {
    return 0
}
`

const GUARDED = `import { is_numeric } from types

/// A guarded \`match type\`, declared and never used.
export type Unused<T> = match type T {
    case _ when is_numeric::<T>() => int[1]
    case _ => int[0]
}
`

/// The entry, with the guarded module declared before the plain one or after it: the two imports
/// are the same two, written in the two possible orders.
const entry = (imports) => `${imports}

func main(): int sync {
    return wants("not an int")
}
`

const args = process.argv.slice(2)
const at = args.indexOf('--camel')
const CAMEL = at >= 0 ? args[at + 1] : 'camel'
const scratch = fs.mkdtempSync(path.join(process.env.CAMEL_TEST_TMPDIR || os.tmpdir(), 'guard-order-'))
fs.writeFileSync(path.join(scratch, 'plain.cml'), PLAIN)
fs.writeFileSync(path.join(scratch, 'guarded.cml'), GUARDED)
fs.writeFileSync(
    path.join(scratch, 'order_ab.cml'),
    entry('import guarded\nimport { wants } from plain'),
)
fs.writeFileSync(
    path.join(scratch, 'order_ba.cml'),
    entry('import { wants } from plain\nimport guarded'),
)

/// The line that shows the type `wants` declares, or a reading that says it is missing, with what
/// the program itself said when the line is not there.
function declaredTypeOf(file) {
    const run = spawnSync(CAMEL, [path.join(scratch, file)], { env: { ...process.env }, encoding: 'utf8' })
    const stdout = (run.stdout || '').replace(/\r\n/g, '\n')
    const line = stdout
        .split('\n')
        .map((text) => text.trim())
        .find((text) => text.startsWith('plain::wants:'))
    if (line !== undefined) return { file, line, status: run.status }
    return {
        file,
        line: `<nothing for plain::wants, exit ${run.status}>`,
        status: run.status,
        stdout,
        stderr: (run.stderr || '').replace(/\r\n/g, '\n'),
    }
}

const ab = declaredTypeOf('order_ab.cml')
const ba = declaredTypeOf('order_ba.cml')
console.log(`ORDER ab ${ab.line}`)
console.log(`ORDER ba ${ba.line}`)
for (const answer of [ab, ba]) {
    if (answer.stderr === undefined) continue
    console.log(`ORDER ${answer.file} exit=${answer.status}`)
    console.log(`ORDER ${answer.file} stdout:\n${answer.stdout}`)
    console.log(`ORDER ${answer.file} stderr:\n${answer.stderr}`)
}
const same = ab.line === ba.line && ab.line.startsWith('plain::wants:')
console.log(`ORDER same=${same ? 'yes' : 'no'}`)
fs.rmSync(scratch, { recursive: true, force: true })
process.exit(same ? 0 : 1)