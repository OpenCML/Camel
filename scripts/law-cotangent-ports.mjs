#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// A law's cotangent port and the body written against it have to agree, and until this check nothing
// compared them.
//
// What the pair looks like. A transpose rule's right side is `(ct: <port type>) => <body>`: the port
// says what the cotangent of the left side's answer is, and the body consumes it. The two neighbours
// below are one word apart, and each is right only for its own law:
//
//     sum_stated(t, at)       => (ct: T) => spread_over(ct, at)          // one number, spread over
//     mean_axis_stated(t, at) => (ct: T) => mean_axis_cotangent(ct, at)  // one number, entered twice
//
// `spread_over` takes one number (`stdlib/rulesets/transpose.cml#spread_over`: `ct: T`, feeding
// `full(at, ct)` whose `value` is a `number`); `numel` reads an array (`stdlib/tensor.cml#numel`:
// `t: T[...]`). Write the port one way and the body the other and the law still parses, still
// instantiates, still builds a graph -- and the slot the body reads is not the value the port
// promised. Nothing compared the two sentences, so nothing said so.
//
// One disagreement is not one: design principles 5.4 makes a rank-zero array *be* its element -- the
// same value, the same printing, the same comparison -- and the language converts between the two
// spellings wherever a value enters an array position (`ArrayType::castSlotFrom` ->
// `Array::createRankZero`, inserted by `Builder::coerceTo`; the exporter writes it as
// `rt::Array<T>::element`). So an **element port** handed to a parameter declared as **that same
// element's open rank with no prefix** (`T` handed to `t: T[...]`) is one value read either way: the
// box is made at the argument position and `numel` of it answers 1. Allowed, and only that:
//
//   * the port has to be a bare element type (`T` or `float`), not `T[3]` and not `T[...]`;
//   * the parameter has to be an open rank with no prefix (`T[...]`), not `T[3]` and not fixed extents
//     in any other spelling -- a fixed extent says the body reads more than one element, and the box
//     is one element wide;
//   * the element types have to be the same letter (`T` to `T`, never `T` to `U`).
//
// Every other disagreement is still a finding, in both directions. The sentence a finding prints
// says which requirement it violated, so a port handed to a `T[3]` parameter still reads "reads an
// array" -- that one does.
//
// The failure this class produces was measured by `lane-rank0` on 2026-11-04: 8 of 8 runs died in
// `numelKernel`. That reading has two halves and this check owns the first of them:
//
//   * a port that disagrees with its own body. This check reports that -- except for the rank-zero
//     equivalence above, which is not a disagreement. On the tree of 2026-11-04 the only shape left
//     is a body that reads a length, which the allowance lets through; before the allowance the same
//     shape was reported, and the reported tree was the one whose 8 runs died in `numelKernel` -- the
//     check said nothing about the law that crashed and said something about the repair. That is the
//     honest history of this paragraph and the reason the allowance exists at all.
//   * a port that agrees with its body and disagrees with the value the transform hands over. That is
//     the half `mean_axis_stated_t` was on, and a law's source has no witness for it: `ruleCotangent`
//     (`modules/transforms/transpose.cpp`) decides the types at build time. It is recorded as a
//     question beside this change
//     (`docs/handover/questions/2026-11-04-narrow-the-declaration-or-teach-the-consumer.cn.md`).
//
// So this reads the laws as text and asks one question of each port: what does the body require of
// it? Three kinds of witness, and each names where its requirement comes from:
//
//   * an exact argument position of a call whose declaration is in the tree -- `numel(ct)` reads an
//     array because `stdlib/tensor.cml#numel` says `t: T[...]`, `spread_over(ct, at)` takes an element
//     because `stdlib/rulesets/transpose.cml#spread_over` says `ct: T`;
//   * an index or an arrow -- `ct[i]`, `ct->reduce<...>` read an array, whatever the callee says;
//   * a companion of the same shape -- `ct * exp(x)` and `ct - sum<0, true>(ct)` are one array wide
//     because `exp` and the three-operand `sum` answer arrays.
//
// And a fourth that is about the law rather than the body: the type the law's own left side answers
// (`mean_axis_stated` is declared `(t: T[...], at: int[]): T[...]`, so its cotangent is an array), the
// operands' own types for the forward family (a tangent is the tangent of the operand it sits at),
// and the law's own `when` guard (`is_float::<T>()` says `T` is one value, since an array is not a
// float).
//
// What this cannot do, said here rather than discovered later:
//
//   * it cannot see the type the transform actually hands a law -- the half above;
//   * it cannot look inside the body of the helper a port is handed to. `numel(ct)` inside
//     `mean_axis_cotangent` answers 1 for the rank-zero box and the lane count for an array
//     cotangent, and this check sees only the port and the parameter -- so a law whose helper reads a
//     length *and assumes more than one element* is not caught here. The probe
//     `element_port_many.cml` in the self-test is that law, and the self-test prints which of the two
//     readings it gets, so the gap is measured rather than assumed;
//   * a port the body only returns, only multiplies by a value of unknown shape (`mul_ok`'s `dx + dw`),
//     or never uses at all (`probe_rule_mul`'s `bw`) has no witness: this prints those as counted, not
//     as passed, and says how many;
//   * arithmetic alone is not a witness. `ct * w` says nothing about whether `ct` is an array: `w`
//     could be either;
//   * a `Lift<...>` port -- the `vmap` family's row batch -- is neither of the two kinds this compares,
//     and is counted apart rather than guessed at.
//
//   node scripts/law-cotangent-ports.mjs                    check stdlib/rulesets, print every finding
//   node scripts/law-cotangent-ports.mjs --explain          the table: law, port, first use, witness
//   node scripts/law-cotangent-ports.mjs --self-test        the reverse reading: a fixed-extent
//                                                           disagreement and an array port handed
//                                                           one number must be reported, the
//                                                           rank-zero equivalence and a clean pair
//                                                           must not, and the helper that assumes
//                                                           more than one element is measured
//   node scripts/law-cotangent-ports.mjs FILE...            check the files named instead
//
// Exit: 0 when nothing disagrees, 1 when a port and its body disagree, 2 when the run checked no law
// at all (a check of nothing is not a pass).

import fs from 'node:fs'
import path from 'node:path'
import os from 'node:os'

const root = path.resolve(import.meta.dirname, '..')

// The declarations a requirement is read from. The laws import from these modules, so a requirement
// this check states is the one the compiler will apply -- not a second table that could drift.
const DECLARATION_FILES = [
    'stdlib/tensor.cml',
    'stdlib/signatures/tensor.cml',
    'stdlib/array.cml',
    'stdlib/autodiff.cml',
    'stdlib/signatures/autodiff.cml'
]

const LAW_DIRECTORY = 'stdlib/rulesets'

// One family per module: `stdlib/rulesets/jvp.cml` is the forward family, whose ports are the
// tangents of the operands, and `stdlib/rulesets/transpose.cml` is the reverse one, whose ports are
// the cotangents of the answer. A module serves one family (design principles 9.9).
const FAMILY_OF_FILE = new Map([
    ['jvp.cml', 'forward'],
    ['transpose.cml', 'reverse'],
    ['vmap.cml', 'batch']
])

const NOUN = new Map([
    ['forward', 'tangent port'],
    ['reverse', 'cotangent port'],
    ['batch', 'batch port']
])

// ---- reading the text ----

function isIdentStart(c) {
    return c !== undefined && /[A-Za-z_]/.test(c)
}

function isIdentPart(c) {
    return c !== undefined && /[A-Za-z0-9_]/.test(c)
}

/// The file with every comment blanked and every newline kept: a law's left side is written with
/// `=>`, and a comment that mentions `=>` must not be read as one.
function blankComments(text) {
    let out = ''
    let i = 0
    while (i < text.length) {
        const c = text[i]
        if (c === '"' || c === "'") {
            const quote = c
            out += c
            i += 1
            while (i < text.length && text[i] !== quote) {
                if (text[i] === '\\' && i + 1 < text.length) {
                    out += text[i]
                    i += 1
                }
                out += text[i]
                i += 1
            }
            if (i < text.length) out += text[i]
            i += 1
            continue
        }
        if (c === '/' && text[i + 1] === '/') {
            while (i < text.length && text[i] !== '\n') i += 1
            continue
        }
        if (c === '/' && text[i + 1] === '*') {
            while (i < text.length && !(text[i] === '*' && text[i + 1] === '/')) {
                if (text[i] === '\n') out += '\n'
                i += 1
            }
            i += 2
            continue
        }
        out += c
        i += 1
    }
    return out
}

/// The index just past the bracket that closes the one at `start`, or -1.
function matchBracket(text, start, open, close) {
    let depth = 0
    for (let i = start; i < text.length; i += 1) {
        if (text[i] === open) depth += 1
        else if (text[i] === close) {
            depth -= 1
            if (depth === 0) return i + 1
        }
    }
    return -1
}

/// The index just past `open`'s matching `>`. `=>` is not a close: the arrow's `>` is skipped, which
/// is what lets `sum<0, true>(ct)` and a `map<(i: int): T => ...>` header both be read.
function matchAngles(text, start) {
    let depth = 0
    for (let i = start; i < text.length; i += 1) {
        const c = text[i]
        if (c === '<') depth += 1
        else if (c === '>') {
            let j = i - 1
            while (j >= 0 && /\s/.test(text[j])) j -= 1
            if (text[j] === '=') continue
            depth -= 1
            if (depth === 0) return i + 1
        }
    }
    return -1
}

/// Split at top-level commas: inside `(...)`, `[...]`, `{...}` or `<...>` a comma is part of a type
/// or of an argument list.
function splitTopLevel(text, separator = ',') {
    const parts = []
    let depth = 0
    let start = 0
    for (let i = 0; i < text.length; i += 1) {
        const c = text[i]
        if (c === '(' || c === '[' || c === '{' || c === '<') depth += 1
        else if (c === ')' || c === ']' || c === '}' || c === '>') depth -= 1
        else if (c === separator && depth === 0) {
            parts.push(text.slice(start, i))
            start = i + 1
        }
    }
    parts.push(text.slice(start))
    return parts
}

/// The index of the first `character` outside every bracket, or -1.
function topLevelIndexOf(text, character) {
    let depth = 0
    for (let i = 0; i < text.length; i += 1) {
        const c = text[i]
        if (c === '(' || c === '[' || c === '{' || c === '<') depth += 1
        else if (c === ')' || c === ']' || c === '}' || c === '>') depth -= 1
        else if (c === character && depth === 0) return i
    }
    return -1
}

/// The type of one `name: type` parameter, or '' when the parameter states none.
function parameterType(parameter) {
    const text = parameter.trim()
    const colon = topLevelIndexOf(text, ':')
    if (colon < 0) return ''
    return text.slice(colon + 1).trim().replace(/=$/, '')
}

/// The name of one `name: type` parameter.
function parameterName(parameter) {
    const text = parameter.trim()
    const colon = topLevelIndexOf(text, ':')
    if (colon < 0) return ''
    return text.slice(0, colon).trim().replace(/\?$/, '')
}

// ---- what a type is, in the two senses this check compares ----

const SCALAR_NAMES = new Set([
    'float',
    'f32',
    'f16',
    'bf16',
    'f64',
    'int',
    'i32',
    'i64',
    'int64',
    'number',
    'bool',
    'string',
    'void',
    'char'
])

/// `array` (an array of anything, of any rank), `element` (one number and not an array), `either`
/// (a union that covers both), `opaque` (a type application this check does not open, `Lift<T>`,
/// `Tangent<T>`), or `unknown`.
function kindOfType(text, declaration = null) {
    const type = text.trim()
    if (type === '') return 'unknown'
    const branches = splitTopLevel(type, '|')
    if (branches.length > 1) {
        const kinds = new Set(branches.map((branch) => kindOfType(branch, declaration)))
        if (kinds.size === 1) return kinds.values().next().value
        return 'either'
    }
    if (/\[\s*(?:\.\.\.|[\d?][\d,\s?]*)?\s*\]\s*$/.test(type)) return 'array'
    if (type.includes('<') || type.includes('>')) return 'opaque'
    if (/^[A-Za-z_][A-Za-z0-9_]*$/.test(type)) {
        if (SCALAR_NAMES.has(type)) return 'element'
        // A bare type variable is an *element* when the declaration it belongs to says so: it is
        // wrapped in an array somewhere (`spread_over::<T>(ct: T, ...): T[...]` is instantiated at `T`
        // = the element type, which is why its `ct` is one number), or the declaration is guarded by
        // `is_float::<T>()` / `is_numeric::<T>()`, and an array is neither a float nor a number
        // (`stdlib/types.cml#is_float`). Otherwise the variable is a shape donor --
        // `sum_to::<T, S>(t: T[...], like: S): S` -- and says nothing about being an element.
        if (declaration !== null && declarationBindsElement(declaration, type)) return 'element'
        return 'unknown'
    }
    return 'unknown'
}

function declarationBindsElement(declaration, variable) {
    const pattern = new RegExp(`\\b${variable}\\s*\\[`)
    if (declaration.result !== '' && pattern.test(declaration.result)) return true
    if (declaration.params.some((parameter) => pattern.test(parameter.type))) return true
    const guarded = new RegExp(
        `is_(?:float|numeric|integer|plain|same)::<\\s*${variable}\\b`
    )
    return guarded.test(declaration.when)
}

/// A law's own port type. A bare type variable here is the element type: the transform instantiates
/// a law at its element type first (`modules/transforms/transpose.cpp#ruleCotangent`: "`T` is the
/// law's *element* type"), so `(ct: T)` promises one number and `(ct: T[...])` promises an array.
function kindOfPortType(text) {
    const kind = kindOfType(text)
    if (kind === 'unknown' && /^[A-Za-z_][A-Za-z0-9_]*$/.test(text.trim())) return 'element'
    return kind
}

/// Whether one use of an element port is the rank-zero equivalence of design principles 5.4 rather
/// than a disagreement: the port is a bare element type, the parameter it enters is the *same*
/// element's open rank with no prefix, and nothing else. `T` handed to `t: T[...]` is one value read
/// two ways -- the box is `Array::createRankZero` at the argument position, `numel` of it answers 1 --
/// while `T` handed to `t: T[3]` is a body that reads three elements, and `T` handed to `t: U[...]` is
/// another element's array. Only the first is allowed, which is what keeps this a rule and not a
/// blanket.
function isRankZeroEquivalence(portType, parameterType) {
    if (parameterType === undefined) return false
    const port = portType.trim()
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(port)) return false
    const open = /^([A-Za-z_][A-Za-z0-9_]*)\s*\[\s*\.\.\.\s*\]$/.exec(parameterType.trim())
    if (open === null) return false
    return open[1] === port
}

// ---- the declarations the requirements come from ----

/// Every `func` declaration of one file: the parameter types and the result type, in the order the
/// file writes them. A declaration with several overloads contributes one entry each, and a caller
/// aggregates them (`requirementAt`): a requirement all overloads agree on is a requirement, and one
/// they disagree on is `either` -- `float` takes a number and an array, so it says nothing.
function parseDeclarations(file) {
    const absolute = path.isAbsolute(file) ? file : path.join(root, file)
    const label = relativeOrBase(absolute)
    const text = blankComments(fs.readFileSync(absolute, 'utf8'))
    const declarations = []
    const pattern = /(^|\n)[ \t]*(?:export[ \t]+)?func[ \t]+([A-Za-z_][A-Za-z0-9_]*)/g
    let match
    while ((match = pattern.exec(text)) !== null) {
        const name = match[2]
        let i = match.index + match[0].length
        while (i < text.length && /\s/.test(text[i])) i += 1
        if (text[i] === ':' && text[i + 1] === ':') {
            i += 2
            if (text[i] === '<') {
                const close = matchAngles(text, i)
                if (close < 0) continue
                i = close
                while (i < text.length && /\s/.test(text[i])) i += 1
            }
        }
        if (text[i] !== '(') continue
        const close = matchBracket(text, i, '(', ')')
        if (close < 0) continue
        const params = splitTopLevel(text.slice(i + 1, close - 1))
            .map((parameter) => parameter.trim())
            .filter((parameter) => parameter !== '')
            .map((parameter) => ({ name: parameterName(parameter), type: parameterType(parameter) }))
        let tail = ''
        for (let j = close; j < text.length; j += 1) {
            if (text[j] === '{' || text[j] === '\n') break
            tail += text[j]
        }
        const guard = /\bwhen\b([\s\S]*)$/.exec(tail)
        const when = guard === null ? '' : guard[1]
        const head = guard === null ? tail : tail.slice(0, guard.index)
        const colon = topLevelIndexOf(head, ':')
        const result = colon < 0 ? '' : head.slice(colon + 1).trim()
        const signature = `${name}(${params.map((parameter) => `${parameter.name}: ${parameter.type}`).join(', ')}): ${result === '' ? '?' : result}`
        declarations.push({ name, params, result, when, signature, file: label, where: `${label}#${name}` })
    }
    return declarations
}

// ---- the laws ----

/// Every `rule` of one file: its left side, its port list and its body.
function parseLaws(file, source) {
    const text = blankComments(source)
    const laws = []
    const pattern = /(^|\n)[ \t]*(?:export[ \t]+)?rule[ \t]+([A-Za-z_][A-Za-z0-9_]*)/g
    let match
    while ((match = pattern.exec(text)) !== null) {
        const name = match[2]
        let i = match.index + match[0].length
        while (i < text.length && /\s/.test(text[i])) i += 1
        if (text[i] === ':' && text[i + 1] === ':') {
            i += 2
            if (text[i] === '<') {
                const close = matchAngles(text, i)
                if (close < 0) continue
                i = close
            }
        }
        while (i < text.length && text[i] !== '{') i += 1
        if (i >= text.length) continue
        // The header states the law's own guard (`when is_float::<T>()`), which is a witness in its
        // own right: an array is not a float, so a guard that says `T` is one says the port is an
        // element.
        const guard = text.slice(match.index + match[0].length, i)
        const blockEnd = matchBracket(text, i, '{', '}')
        if (blockEnd < 0) continue
        const block = text.slice(i + 1, blockEnd - 1)
        const arrow = findTopLevelArrow(block)
        if (arrow < 0) continue
        const left = block.slice(0, arrow).trim()
        let p = arrow + 2
        while (p < block.length && /\s/.test(block[p])) p += 1
        if (block[p] !== '(') continue
        const portsEnd = matchBracket(block, p, '(', ')')
        if (portsEnd < 0) continue
        const ports = splitTopLevel(block.slice(p + 1, portsEnd - 1))
            .map((port) => port.trim())
            .filter((port) => port !== '')
            .map((port) => ({ name: parameterName(port), type: parameterType(port) }))
        let q = portsEnd
        while (q < block.length && /\s/.test(block[q])) q += 1
        if (!(block[q] === '=' && block[q + 1] === '>')) continue
        const body = block.slice(q + 2)
        // A module that names no other family is read as the reverse one: a law's port is the
        // cotangent of its left side's answer unless the module says its ports are tangents (`jvp`)
        // or row batches (`vmap`). The two fixture rulesets beside the library name no family and
        // are read this way; their left sides are operators, so the family decides only the wording.
        laws.push({
            name,
            file,
            left,
            ports,
            body,
            guard,
            family: FAMILY_OF_FILE.get(path.basename(file)) ?? 'reverse'
        })
    }
    return laws
}

function findTopLevelArrow(block) {
    let depth = 0
    for (let i = 0; i < block.length; i += 1) {
        const c = block[i]
        if (c === '(' || c === '[' || c === '{' || c === '<') depth += 1
        else if (c === ')' || c === ']' || c === '}' || c === '>') depth -= 1
        else if (c === '=' && block[i + 1] === '>' && depth === 0) return i
    }
    return -1
}

/// Every call in a body: the name, its arguments and where it starts. Nested calls are found too,
/// because the scan resumes at the callee's name and walks through the brackets.
function callSites(text) {
    const sites = []
    let i = 0
    while (i < text.length) {
        if (!isIdentStart(text[i])) {
            i += 1
            continue
        }
        let j = i
        while (j < text.length && isIdentPart(text[j])) j += 1
        const name = text.slice(i, j)
        let k = j
        while (k < text.length && /\s/.test(text[k])) k += 1
        // A spelled type argument comes in two forms and both may precede the call: `sum<0, true>(ct)`
        // and `idx_batch::<T>(bx, bi)`.
        if (text[k] === ':' && text[k + 1] === ':') {
            k += 2
            while (k < text.length && /\s/.test(text[k])) k += 1
        }
        if (text[k] === '<') {
            const close = matchAngles(text, k)
            if (close > 0) {
                k = close
                while (k < text.length && /\s/.test(text[k])) k += 1
            }
        }
        if (text[k] === '(') {
            const close = matchBracket(text, k, '(', ')')
            if (close > 0) {
                sites.push({ name, args: splitTopLevel(text.slice(k + 1, close - 1)), start: i })
            }
        }
        i = j
    }
    return sites
}

/// The name of the operand to the left of `before`, when that operand is a call or a parenthesised
/// group: `exp(x) * ` -> `exp`, `(1.0 - tanh(x)) * ` -> the first array-valued call inside the group.
function operandBefore(text, before, producesArray) {
    let i = before - 1
    while (i >= 0 && /\s/.test(text[i])) i -= 1
    if (i < 0) return null
    if (text[i] === ')') {
        let depth = 0
        let j = i
        for (; j >= 0; j -= 1) {
            if (text[j] === ')') depth += 1
            else if (text[j] === '(') {
                depth -= 1
                if (depth === 0) break
            }
        }
        if (j < 0) return null
        let k = j - 1
        while (k >= 0 && /\s/.test(text[k])) k -= 1
        let start = k
        while (start >= 0 && isIdentPart(text[start])) start -= 1
        const name = text.slice(start + 1, k + 1)
        if (name !== '' && producesArray(name)) return name
        // A group rather than a call: the first array-valued call inside it is the shape.
        return firstArrayCall(text.slice(j, i + 1), producesArray)
    }
    if (isIdentPart(text[i])) {
        let start = i
        while (start >= 0 && isIdentPart(text[start])) start -= 1
        const name = text.slice(start + 1, i + 1)
        if (producesArray(name)) return name
    }
    return null
}

/// The name of the operand to the right of `after`, on the same reading as `operandBefore`.
function operandAfter(text, after, producesArray) {
    let i = after
    while (i < text.length && /\s/.test(text[i])) i += 1
    if (i >= text.length) return null
    if (text[i] === '(') {
        const close = matchBracket(text, i, '(', ')')
        if (close < 0) return null
        return firstArrayCall(text.slice(i, close), producesArray)
    }
    if (isIdentStart(text[i])) {
        let j = i
        while (j < text.length && isIdentPart(text[j])) j += 1
        const name = text.slice(i, j)
        if (producesArray(name)) return name
    }
    return null
}

function firstArrayCall(text, producesArray) {
    for (const site of callSites(text)) {
        if (producesArray(site.name)) return site.name
    }
    return null
}

// ---- the check ----

function relativeOrBase(file) {
    const absolute = path.resolve(file)
    const inside = path.relative(root, absolute)
    if (inside === '' || inside.startsWith('..') || path.isAbsolute(inside)) {
        return path.basename(absolute)
    }
    return inside.split(path.sep).join('/')
}

function requirementAt(declarations, name, index) {
    const matching = declarations.filter((declaration) => declaration.name === name)
    if (matching.length === 0) return null
    const kinds = []
    for (const declaration of matching) {
        const parameter = declaration.params[index]
        if (parameter === undefined) return null
        kinds.push(kindOfType(parameter.type, declaration))
    }
    const unique = new Set(kinds)
    // The source is the declaration and the position in it, so a reader can check the requirement
    // instead of believing it: `stdlib/tensor.cml#numel, parameter 1 't: T[...]'`.
    const parameter = matching[0].params[index]
    const where = `${matching[0].where}, parameter ${index + 1} \`${parameter.name}: ${parameter.type}\``
    // The parameter's own type text travels with the requirement, because whether this use is the
    // rank-zero equivalence of 5.4 depends on the spelling and not on the kind alone: `T[...]` is an
    // array and so is `T[3]`, and only the first one can hold a value that is one element.
    if (unique.size !== 1) return { kind: 'either', where, parameterType: parameter.type }
    return { kind: kinds[0], where, parameterType: parameter.type }
}

function resultKind(declarations, name) {
    const matching = declarations.filter((declaration) => declaration.name === name)
    if (matching.length === 0) return null
    const kinds = new Set(matching.map((declaration) => kindOfType(declaration.result, declaration)))
    const where = `${matching[0].where}: \`${matching[0].signature}\``
    if (kinds.size !== 1) return { kind: 'either', where }
    return { kind: kinds.values().next().value, where }
}

/// The witnesses one port's uses give: what the body requires of it, and where that requirement is
/// written down.
function witnessesOfPort(law, port, declarations) {
    const producesArray = (name) => {
        const result = resultKind(declarations, name)
        return result !== null && result.kind === 'array'
    }
    const uses = []
    const body = law.body
    const sites = callSites(body)
    for (const site of sites) {
        site.args.forEach((argument, index) => {
            if (argument.trim() !== port.name) return
            const requirement = requirementAt(declarations, site.name, index)
            if (requirement === null) return
            if (requirement.kind !== 'array' && requirement.kind !== 'element') return
            uses.push({
                at: site.start,
                text: `${site.name}(${site.args.map((a) => a.trim()).join(', ')})`,
                kind: requirement.kind,
                where: requirement.where,
                parameterType: requirement.parameterType,
                callee: site.name
            })
        })
    }
    const indexPattern = new RegExp(`\\b${port.name}\\s*\\[`)
    if (indexPattern.test(body)) {
        uses.push({
            at: body.search(indexPattern),
            text: `${port.name}[...]`,
            kind: 'array',
            where: 'an element of it is read',
            callee: `${port.name}[...]`
        })
    }
    const arrowPattern = new RegExp(`\\b${port.name}\\s*->`)
    if (arrowPattern.test(body)) {
        uses.push({
            at: body.search(arrowPattern),
            text: `${port.name}->…`,
            kind: 'array',
            where: 'a method of it is called',
            callee: `${port.name}->`
        })
    }
    const occurrence = new RegExp(`\\b${port.name}\\b`, 'g')
    let occurrenceMatch
    while ((occurrenceMatch = occurrence.exec(body)) !== null) {
        const start = occurrenceMatch.index
        const end = start + port.name.length
        let before = start - 1
        while (before >= 0 && /\s/.test(body[before])) before -= 1
        let after = end
        while (after < body.length && /\s/.test(body[after])) after += 1
        const pairs = []
        if (before >= 0 && '*/+-'.includes(body[before])) pairs.push(['before', before])
        if (after < body.length && '*/+-'.includes(body[after])) pairs.push(['after', after])
        for (const [side, at] of pairs) {
            const name =
                side === 'before'
                    ? operandBefore(body, at, producesArray)
                    : operandAfter(body, at + 1, producesArray)
            if (name === null) continue
            const result = resultKind(declarations, name)
            const operator = body[at]
            uses.push({
                at: start,
                text:
                    side === 'before'
                        ? `${name}(…) ${operator} ${port.name}`
                        : `${port.name} ${operator} ${name}(…)`,
                kind: 'array',
                where: `${result.where} answers an array, so this is one array wide`,
                callee: name
            })
        }
    }
    return uses
}

/// The witness the law itself gives: the type its left side answers (reverse family) or the types of
/// the operands it is written over (forward family).
function witnessOfLaw(law, portIndex, declarations) {
    const callee = /^([A-Za-z_][A-Za-z0-9_]*)\s*(?:<[^>]*>)?\s*\(/.exec(law.left)
    if (callee === null) return null
    if (law.family === 'reverse') {
        if (portIndex !== 0) return null
        const result = resultKind(declarations, callee[1])
        if (result === null) return null
        if (result.kind !== 'array' && result.kind !== 'element') return null
        const answer = result.kind === 'array' ? 'an array' : 'one value'
        return {
            kind: result.kind,
            where: `${result.where} answers ${answer}`,
            sentence: `but its left side calls '${callee[1]}', which answers ${answer} (${result.where})`
        }
    }
    if (law.family === 'forward') {
        const matching = declarations.filter((declaration) => declaration.name === callee[1])
        if (matching.length === 0) return null
        const parameter = matching[0].params[portIndex]
        if (parameter === undefined) return null
        const kind = kindOfType(parameter.type, matching[0])
        if (kind !== 'array' && kind !== 'element') return null
        return {
            kind,
            where: `operand ${portIndex + 1} of '${callee[1]}' is declared \`${parameter.type}\` (${matching[0].where})`,
            sentence: `but the tangent is the tangent of operand ${portIndex + 1}, declared \`${parameter.type}\` (${matching[0].where})`
        }
    }
    return null
}

/// The witness the law's own guard gives. `when is_float::<T>()` says `T` is a float, and an array is
/// neither a float nor a number (`stdlib/types.cml#is_float`); `when is_same::<T, float>()` says the
/// same thing by name.
function guardWitness(law, port) {
    const type = port.type.trim()
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(type)) return null
    const element = new RegExp(`is_(?:float|numeric|integer|plain)::<\\s*${type}\\s*>`)
    const same = new RegExp(`is_same::<\\s*${type}\\s*,\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*>`)
    const named = same.exec(law.guard)
    const when = /\bwhen\b([\s\S]*)$/.exec(law.guard)
    const guardText = (when === null ? law.guard : `when${when[1]}`).replace(/\s+/g, ' ').trim()
    if (element.test(law.guard) || (named !== null && SCALAR_NAMES.has(named[1]))) {
        return {
            kind: 'element',
            where: `the law's own guard, \`${guardText}\``,
            sentence: `but the law's own guard says it is one value, \`${guardText}\``
        }
    }
    return null
}

function checkLaw(law, declarations) {
    const findings = []
    const rows = []
    for (let index = 0; index < law.ports.length; index += 1) {
        const port = law.ports[index]
        const noun = NOUN.get(law.family) ?? 'port'
        const kind = kindOfPortType(port.type)
        const uses = witnessesOfPort(law, port, declarations).sort((a, b) => a.at - b.at)
        const first = uses.length > 0 ? uses[0] : null
        const row = {
            file: relativeOrBase(law.file),
            law: law.name,
            port: `${port.name}: ${port.type}`,
            kind,
            firstUse: first === null ? (law.body.trim() === port.name ? 'returned' : '—') : first.text,
            witness: first === null ? '—' : `${first.kind} (${first.where})`,
            verdict: 'agrees'
        }
        if (kind !== 'array' && kind !== 'element') {
            row.verdict = 'not compared'
            rows.push(row)
            continue
        }
        const contradicting = uses.find(
            (use) => use.kind !== kind && !isRankZeroEquivalence(port.type, use.parameterType)
        )
        const equivalent = uses.filter(
            (use) => use.kind !== kind && isRankZeroEquivalence(port.type, use.parameterType)
        )
        if (contradicting !== undefined) {
            row.verdict = 'disagrees'
            row.witness = `${contradicting.kind} (${contradicting.where})`
            const reads = contradicting.kind === 'array' ? 'reads an array' : 'takes an element'
            findings.push(
                `${row.file}:${law.name}: ${noun} is '${port.type}' but the body hands it to '${contradicting.callee}', which ${reads} (${contradicting.where})`
            )
            rows.push(row)
            continue
        }
        if (equivalent.length > 0) {
            // 5.4: the value is one element and the parameter is that element's open rank, so the
            // language makes the rank-zero box where it enters and the body reads one element.
            row.verdict = 'agrees'
            row.witness = `${equivalent[0].where} -- the rank-zero array of 5.4, one value read either way`
            rows.push(row)
            continue
        }
        if (uses.length === 0) {
            const witness = witnessOfLaw(law, index, declarations) ?? guardWitness(law, port)
            if (witness !== null && witness.kind !== kind) {
                row.verdict = 'disagrees'
                findings.push(`${row.file}:${law.name}: ${noun} is '${port.type}' ${witness.sentence}`)
                rows.push(row)
                continue
            }
            row.verdict = witness === null ? 'no witness, counted' : 'agrees'
            row.witness = witness === null ? '—' : `${witness.kind} (${witness.where})`
        }
        rows.push(row)
    }
    return { findings, rows }
}

function lawFiles(targets) {
    if (targets.length > 0) return targets.map((target) => path.resolve(target))
    const directory = path.join(root, LAW_DIRECTORY)
    return fs
        .readdirSync(directory)
        .filter((name) => name.endsWith('.cml'))
        .sort()
        .map((name) => path.join(directory, name))
}

function run(targets, { explain }) {
    const files = lawFiles(targets)
    const stdlib = DECLARATION_FILES.flatMap((file) => parseDeclarations(file))
    const laws = []
    // The requirements, per law file: the module's own functions first (a law names the helpers
    // declared beside it -- `spread_over`, `mean_axis_cotangent` -- and those are the ones the
    // compiler will bind), then the standard library's.
    const declarationsOf = new Map()
    for (const file of files) {
        const source = fs.readFileSync(file, 'utf8')
        laws.push(...parseLaws(file, source))
        declarationsOf.set(file, [...parseDeclarations(file), ...stdlib])
    }
    if (laws.length === 0) {
        console.log(`${files.length} file(s) named, and not one law in them: nothing was checked`)
        return 2
    }
    const findings = []
    const rows = []
    for (const law of laws) {
        const result = checkLaw(law, declarationsOf.get(path.resolve(law.file)))
        findings.push(...result.findings)
        rows.push(...result.rows)
    }
    if (explain) {
        const width = rows.reduce(
            (widest, row) => ({
                law: Math.max(widest.law, row.law.length),
                port: Math.max(widest.port, row.port.length),
                firstUse: Math.max(widest.firstUse, row.firstUse.length)
            }),
            { law: 4, port: 4, firstUse: 9 }
        )
        console.log(
            `${'law'.padEnd(width.law)}  ${'port'.padEnd(width.port)}  ${'first use'.padEnd(width.firstUse)}  witness`
        )
        for (const row of rows) {
            console.log(
                `${row.law.padEnd(width.law)}  ${row.port.padEnd(width.port)}  ${row.firstUse.padEnd(width.firstUse)}  ${row.verdict === 'agrees' ? row.witness : `${row.verdict}: ${row.witness}`}`
            )
        }
        const counted = rows.filter((row) => row.verdict === 'no witness, counted').length
        const compared = rows.length - counted - rows.filter((r) => r.verdict === 'not compared').length
        console.log(
            `${rows.length} port(s) read: ${compared} compared with the body, ${counted} with no witness (counted, not passed), ${rows.filter((r) => r.verdict === 'not compared').length} of a type this check does not compare`
        )
    }
    if (findings.length > 0) {
        for (const finding of findings) console.log(finding)
        console.log(
            `${findings.length} finding(s) in ${laws.length} law(s) across ${files.length} file(s): the port a law promises is not the one its body reads`
        )
        return 1
    }
    const counted = rows.filter((row) => row.verdict === 'no witness, counted').length
    console.log(
        `no law's port disagrees with its body or its left side: ${laws.length} law(s) in ${files.length} file(s), ${rows.length} port(s), ${counted} with no witness on either side (counted, not passed)`
    )
    return 0
}

// ---- the reverse reading ----

const PROBES = {
    'element_port_fixed_extent.cml': [
        '// A law whose cotangent port is one number and whose body hands it to a parameter of three',
        '// numbers: a fixed extent is a body that reads more than one element, and the rank-zero box of',
        '// 5.4 is one element wide, so this stays a finding. It is the negative control of the',
        '// allowance: without it, allowing `T` -> `T[...]` would have allowed every extent.',
        'func three_lanes::<T>(ct: T[3]): T[...] when is_float::<T>() {',
        '    return full([3], ct[0])',
        '}',
        '',
        'export rule element_port_fixed_extent::<T>(t: T[...], at: int[]) when is_float::<T>() {',
        '    mean_axis_stated(t, at) => (ct: T) => three_lanes(ct)',
        '}',
        ''
    ].join('\n'),
    'array_port_spread.cml': [
        '// A law whose cotangent port is an array and whose body hands it to `spread_over`, which wants',
        '// one number: the same disagreement with the two spellings the other way round.',
        'func spread_over::<T>(ct: T, at: int[]): T[...] when is_float::<T>() {',
        '    return full(at, ct)',
        '}',
        '',
        'export rule array_port_spread::<T, X, A>(t: X, at: A) when is_float::<T>() {',
        '    sum_stated(t, at) => (ct: T[...]) => spread_over(ct, at)',
        '}',
        ''
    ].join('\n'),
    'element_port_open_rank.cml': [
        '// The shape the allowance exists for: a port of one number handed to a parameter declared as',
        '// that same number\'s open rank, with no prefix and no extents. Design principles 5.4 makes the',
        '// two the same value and the language makes the box at the argument position, so this is not a',
        '// finding -- `numel` of it answers 1, which is what `mean_axis_cotangent` computes with.',
        'func lanes_of::<T>(ct: T[...], at: int[]): T[...] when is_float::<T>() {',
        '    let extents = at[2:len(at)]',
        '    let lanes = float(extent_product(extents)) / float(numel(ct))',
        '    return full(extents, 1.0) * (ct / lanes)',
        '}',
        '',
        'func extent_product(extents: int[]): int {',
        '    return extents->reduce<(acc: int, d: int) => acc * d, 1>',
        '}',
        '',
        'export rule element_port_open_rank::<T, X, A>(t: X, at: A) when is_float::<T>() {',
        '    mean_axis_stated(t, at) => (ct: T) => lanes_of(ct, at)',
        '}',
        ''
    ].join('\n'),
    'element_port_many.cml': [
        '// The shape the allowance cannot see, and the reason it is bounded: the port promises one',
        '// number, the parameter is that number\'s open rank (so the allowance lets it through), and the',
        '// helper\'s body reads a length and assumes more than one element. The assumption is inside the',
        '// helper, which this check does not open, so it is measured here rather than caught: a port and',
        '// a parameter can agree while the arithmetic behind them still means a lane count.',
        'func scaled_by_lanes::<T>(ct: T[...], at: int[]): T[...] when is_float::<T>() {',
        '    let n = numel(ct)',
        '    let doubled = n > 1 ? float(n) * 2.0 : 1.0',
        '    return full(at, doubled * ct)',
        '}',
        '',
        'export rule element_port_many::<T, X, A>(t: X, at: A) when is_float::<T>() {',
        '    mean_axis_stated(t, at) => (ct: T) => scaled_by_lanes(ct, at)',
        '}',
        ''
    ].join('\n'),
    'clean_control.cml': [
        '// The two laws of the library as it writes them today: an element port spread over the extents',
        '// it states, and an element port handed to the helper that reads its length through the open',
        '// rank. Neither is a finding, which is the half of the reading that says the check is not',
        '// merely refusing everything.',
        'func spread_over::<T>(ct: T, at: int[]): T[...] when is_float::<T>() {',
        '    return full(at, ct)',
        '}',
        '',
        'func mean_axis_cotangent::<T>(ct: T[...], at: int[]): T[...] when is_float::<T>() {',
        '    let extents = at[2:len(at)]',
        '    let lanes = float(extent_product(extents)) / float(numel(ct))',
        '    return full(extents, 1.0) * (ct / lanes)',
        '}',
        '',
        'func extent_product(extents: int[]): int {',
        '    return extents->reduce<(acc: int, d: int) => acc * d, 1>',
        '}',
        '',
        'export rule sum_stated_t::<T, X, A>(t: X, at: A) when is_float::<T>() {',
        '    sum_stated(t, at) => (ct: T) => spread_over(ct, at)',
        '}',
        '',
        'export rule mean_axis_stated_t::<T, X, A>(t: X, at: A) when is_float::<T>() {',
        '    mean_axis_stated(t, at) => (ct: T) => mean_axis_cotangent(ct, at)',
        '}',
        ''
    ].join('\n')
}

function selfTest() {
    const directory = path.join(process.env.TEMP ?? os.tmpdir(), 'camel-law-cotangent-ports')
    fs.mkdirSync(directory, { recursive: true })
    for (const [name, text] of Object.entries(PROBES)) {
        fs.writeFileSync(path.join(directory, name), text, 'utf8')
    }
    const ran = (name) => {
        const captured = []
        const log = console.log
        console.log = (...args) => captured.push(args.join(' '))
        try {
            return { status: run([path.join(directory, name)], { explain: false }), lines: captured }
        } finally {
            console.log = log
        }
    }
    const fixed = ran('element_port_fixed_extent.cml')
    const array = ran('array_port_spread.cml')
    const openRank = ran('element_port_open_rank.cml')
    const many = ran('element_port_many.cml')
    const clean = ran('clean_control.cml')
    const caught = [...fixed.lines, ...array.lines].filter(
        (line) => line.includes('disagrees') || line.includes('cotangent port is')
    )
    for (const line of caught) console.log(line)
    console.log(
        `self-test: a port 'T' handed to a parameter 'T[3]' is ${fixed.status === 1 ? 'reported' : 'NOT reported'}, a port 'T[...]' handed to a parameter 'T' is ${array.status === 1 ? 'reported' : 'NOT reported'}`
    )
    console.log(
        `self-test: the rank-zero pair 'T' -> 'T[...]' is ${openRank.status === 0 ? 'not reported' : 'REPORTED'}, and the two laws of the library are ${clean.status === 0 ? 'not reported either' : 'REPORTED'}`
    )
    // The gap the allowance leaves is printed, not hidden: the law below is one number handed to an
    // open rank whose body reads a length and assumes more than one element, and no reading of the
    // port and the parameter can tell it from the one above.
    console.log(
        `self-test: the helper that reads a length and assumes more than one element is ${many.status === 1 ? 'reported' : 'NOT reported'} -- ${many.status === 1 ? 'caught' : 'the assumption is inside the helper, which this check does not open'}`
    )
    const ok =
        fixed.status === 1 && array.status === 1 && openRank.status === 0 && clean.status === 0 && caught.length === 2
    console.log(
        ok
            ? 'self-test: both disagreements are caught, the rank-zero equivalence and the clean pair are not, so the reading is a rule and not a blanket'
            : 'self-test: FAILED'
    )
    // The probe modules are the whole fixture and they live in the system temp directory, so a run of
    // the gate leaves nothing behind.
    fs.rmSync(directory, { recursive: true, force: true })
    return ok ? 0 : 1
}

// ---- the command line ----

const args = process.argv.slice(2)
const targets = args.filter((argument) => !argument.startsWith('--'))
if (args.includes('--self-test')) {
    process.exit(selfTest())
}
process.exit(run(targets, { explain: args.includes('--explain') }))