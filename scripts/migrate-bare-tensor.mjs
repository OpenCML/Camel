#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The migration engine for the bare `Tensor` spelling of a signature.
//
// `Tensor` with no argument is the transitional type the deletion step removes (the arrays and
// memory plan, docs/design/arrays-memory/plan.cn.md): a numeric array
// whose element type nobody fixed. P4 deletes it, and a signature that still writes it becomes a
// compile error whose help text says "write `Tensor<T>` and declare `T` at the head, or write the
// concrete element type". This tool reports every occurrence of the bare spelling, sorts each one
// by the position it stands in, and rewrites the ones a mechanical rule can justify. Everything
// else it refuses and lists: a migration lane decides those, one file at a time.
//
// What the target writing is, measured on the flip line (h465) rather than assumed:
//
//   * `Tensor<T>` with `T` a type variable is REFUSED today -- "the element type of a Tensor is a
//     numeric scalar" -- so no rule writes it (stdlib/rules.cml records the same measurement);
//   * a parameter whose element type is the call site's is written
//     `func f::<T>(x: T[..]) when is_numeric::<T>()` -- the spelling stdlib/tensor.cml already
//     uses for `shape`, `numel`, `dim` and `to_bytes`;
//   * a parameter that must take what the tensor operators still return (the transitional
//     `Tensor`) cannot be written that way yet, and cannot be written `f32[..]` either: measured,
//     "cannot be called with argument types (Tensor)" and "needs an explicit conversion". Those
//     call sites are reported as blockers, never rewritten.
//
// So the only rule that writes is
//
//   R1  a function whose bare `Tensor` occurrences are all parameters, whose head declares no
//       type parameter, whose body only reads from those parameters in an element-agnostic way,
//       and whose name is never used as a value or with explicit type arguments:
//       `func f(t: Tensor): int`  ->  `func f::<T>(t: T[..]): int when is_numeric::<T>()`
//
//   R2  R1's conditions, plus one bare `Tensor` as the result type and a body whose answer is
//       built from those parameters with the element-preserving operators (`return a @ b`):
//       `func g(a: Tensor, b: Tensor): Tensor`  ->  `func g::<T>(a: T[..], b: T[..]): T[..] ...`
//
// The call sites decide too: a caller that passes a value whose WRITTEN type is `Tensor` (a
// parameter or a `let` annotation) cannot bind a free `T` -- measured, "the generic function
// 'count' cannot be called with argument types (Tensor)" -- so the function waits for that
// caller, and the report names it.
//
// The body test is the rule's evidence, not a guess: today's compiler instantiates a bare
// `Tensor` parameter from the argument's element type at each call
// (src/compile/gir/builder_operators.cpp#instantiatedTensorSignature), which is exactly one
// generic parameter. The rewrite writes that generic parameter down. A body that reads the
// element type (`abs(t)`, `sum(t)`, `t * 3.0`) says the element type is not free, and the tool
// leaves it to a person with a concrete element type.
//
//   node scripts/migrate-bare-tensor.mjs                        report the whole tree, write nothing
//   node scripts/migrate-bare-tensor.mjs --path test/cases/loops
//   node scripts/migrate-bare-tensor.mjs --path <dir> --write   apply rules R1 and R2 there
//   node scripts/migrate-bare-tensor.mjs --json                 machine-readable report
//   --path DIR|FILE        scope, repeatable (comma-separated too); default the whole tree
//   --write                apply the rewrites; without it the run only reports
//   --json                 print the report as JSON instead of text
//   --quiet-sites          text report without the per-site lines
//   --spelling array|tensor  what `--write` writes: `T[..]` (default, compiles today) or
//                          `Tensor<T>` (report only: today's compiler refuses that argument)
//   --root DIR             read under DIR instead of this checkout (the fixture of a test)
//
// It never touches a `Tensor` in a comment or in a string (the scanner blanks both before it
// looks), never the lowercase module name `tensor`, and never an occurrence that is an argument
// of `Tensor<...>` or of any other type constructor. A second run over a tree the first one wrote
// reports the same tree it wrote -- the rewritten signature no longer holds a bare `Tensor`, and
// the head it now has stops R1 -- so a diff of two runs is empty.
//
// Exit status: 0 when the run reported (with or without rewrites), 2 when a flag is wrong or the
// scope holds no `.cml` file. A report over nothing is refused, never reported as a clean tree.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const SCRIPT_DIR = path.dirname(fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(SCRIPT_DIR, '..')

// The position categories of the reconnaissance (the report's eight buckets). `id` is what the
// code below assigns; the label is what the report prints.
const CATEGORIES = [
    ['param', '① parameter type'],
    ['return', '② result type'],
    ['field', '③ struct or tuple field'],
    ['closure', '④ closure or function-value type'],
    ['varParam', '⑤ var parameter'],
    ['alias', '⑥ type alias or type declaration'],
    ['nested', '⑦ inside a type argument list'],
    ['other', '⑧ other (binding annotation, cast)'],
    ['import', '— import list (not a type position)'],
]
const CATEGORY_LABEL = new Map(CATEGORIES)

// The helpers whose argument the element type cannot be read from: they answer a rank, a length
// or a count, and work for every numeric element type. A parameter used only here is free.
const ELEMENT_AGNOSTIC_HELPERS = new Set(['len', 'shape', 'numel', 'dim', 'size', 'rank', 'count'])

// The operators that carry their operands' element type through: with both sides arrays of the
// same element type the result is that type. `a @ b` is the matmul of two tensors of one element
// type; the elementwise forms are the rest.
const ELEMENT_PRESERVING_OPERATORS = ['@', '*', '+', '-', '/']

// The predicate the target spelling guards with: `Tensor` today means a NUMERIC array, so the
// explicit type variable has to say so, exactly as stdlib/tensor.cml writes it.
const PREDICATE = 'is_numeric'
const PREDICATE_MODULE = 'types'

// ---------------------------------------------------------------------------------------------
// Reading a file: comments and strings are blanked, so an offset in the masked text is the same
// offset in the source and a `Tensor` inside either is never seen.

function maskSource(text) {
    const out = Array.from(text)
    const n = text.length
    let i = 0
    const blank = (from, to) => {
        for (let k = from; k < to && k < n; k++) if (out[k] !== '\n') out[k] = ' '
    }
    while (i < n) {
        const c = text[i]
        if (c === '/' && text[i + 1] === '/') {
            let j = text.indexOf('\n', i)
            if (j === -1) j = n
            blank(i, j)
            i = j
        } else if (c === '/' && text[i + 1] === '*') {
            let j = text.indexOf('*/', i + 2)
            j = j === -1 ? n : j + 2
            blank(i, j)
            i = j
        } else if (text.startsWith("'''", i) || text.startsWith('"""', i)) {
            const quote = text.slice(i, i + 3)
            let j = text.indexOf(quote, i + 3)
            j = j === -1 ? n : j + 3
            blank(i, j)
            i = j
        } else if (c === "'" || c === '"') {
            let j = i + 1
            while (j < n) {
                if (text[j] === '\\') {
                    j += 2
                    continue
                }
                if (text[j] === c) break
                if (text[j] === '\n') break
                j++
            }
            blank(i, Math.min(j + 1, n))
            i = Math.min(j + 1, n)
        } else {
            i++
        }
    }
    return out.join('')
}

const IDENT = /[A-Za-z_0-9]/

/// Whether the character before `at` (skipping blanks) can end a name: the opener of a type
/// argument list stands right after `Map`, after `Tensor`, after `>`, but never after `::`.
function nameBefore(text, at) {
    let i = at - 1
    while (i >= 0 && /\s/.test(text[i])) i--
    if (i < 0 || !/[\w>)\]]/.test(text[i])) return null
    let j = i
    while (j >= 0 && IDENT.test(text[j])) j--
    const word = text.slice(j + 1, i + 1)
    if (text[j] === ':' && text[j - 1] === ':') return null
    return word
}

function matchBracket(text, from) {
    const open = text[from]
    const close = open === '(' ? ')' : open === '[' ? ']' : open === '{' ? '}' : '>'
    let depth = 0
    for (let i = from; i < text.length; i++) {
        const c = text[i]
        if (c === open) depth++
        else if (c === close) {
            depth--
            if (depth === 0) return i
        }
    }
    return -1
}

/// The depth of type-argument lists at `index`, counted inside the type expression that starts at
/// `from`. A `<` whose name is a type constructor opens a level; type expressions hold no
/// comparison, so no other reading is possible here.
function angleDepthIn(text, from, index) {
    let depth = 0
    for (let i = from; i < index; i++) {
        if (text[i] === '<' && nameBefore(text, i) !== null) depth++
        else if (text[i] === '>' && depth > 0) depth--
    }
    return depth
}

/// Whether `index` stands inside a parenthesized group of the type expression that starts at
/// `from` (a tuple type element, or the parameter list of a function type).
function parenDepthIn(text, from, index) {
    let depth = 0
    for (let i = from; i < index; i++) {
        if (text[i] === '(') depth++
        else if (text[i] === ')' && depth > 0) depth--
    }
    return depth
}

// ---------------------------------------------------------------------------------------------
// The structural scan: every span of the file that can hold a written type, with the kind the
// report sorts by.

function scanFile(text) {
    const masked = maskSource(text)
    const spans = []
    const funcs = []
    const add = (kind, start, end, extra) => {
        if (start < 0 || end > masked.length || start >= end) return null
        const span = { kind, start, end, text: masked.slice(start, end), textStart: start, ...extra }
        spans.push(span)
        return span
    }

    // import lines: `Tensor` here is the name of the alias constructor, never a type. A list may
// wrap across lines, so the span runs to the closing brace of the first `{` on the line.
    for (const m of masked.matchAll(/^[ \t]*import\b[^\n]*/gm)) {
        const open = masked.indexOf('{', m.index)
        const newline = m.index + m[0].length
        if (open !== -1 && open < newline) {
            const close = matchBracket(masked, open)
            add('import', m.index, close === -1 ? newline : close + 1)
        } else add('import', m.index, newline)
    }

    // `type Name = { fields }`, `type Name = enum { variants }` and `type Name = <target>`
    for (const m of masked.matchAll(/\btype\s+([A-Za-z_]\w*)\s*=/g)) {
        let i = m.index + m[0].length
        while (i < masked.length && /\s/.test(masked[i])) i++
        // `enum` before the brace: its variants' payloads are fields too (`Leaf(Tensor)`)
        const enumAt = masked.startsWith('enum', i) ? i : -1
        if (enumAt !== -1) {
            i = enumAt + 4
            while (i < masked.length && /\s/.test(masked[i])) i++
        }
        if (masked[i] === '{') {
            const end = matchBracket(masked, i)
            if (end === -1) continue
            const body = masked.slice(i + 1, end)
            // fields, split at the commas that are not inside brackets
            let depth = 0
            let fieldStart = 0
            const pushField = (from, to) => {
                const colon = topLevelColon(body, from, to)
                if (colon !== -1) add('field', i + 1 + colon + 1, i + 1 + to)
                // `Leaf(Tensor)`: an enum variant's payload is a field list without a colon
                else if (enumAt !== -1) {
                    const open = body.indexOf('(', from)
                    if (open !== -1 && open < to) {
                        const close = matchBracket(body, open)
                        add('field', i + 2 + open, i + 1 + (close === -1 || close > to ? to : close))
                    }
                }
            }
            for (let k = 0; k <= body.length; k++) {
                const c = body[k]
                if (k === body.length || (depth === 0 && c === ',')) {
                    pushField(fieldStart, k)
                    fieldStart = k + 1
                    continue
                }
                if (c === '(' || c === '[' || c === '{') depth++
                else if (c === ')' || c === ']' || c === '}') depth--
            }
        } else {
            let end = masked.indexOf('\n', m.index)
            if (end === -1) end = masked.length
            add('alias', i, end)
        }
    }

    // `with <params>`: the parameter list of the declaration that follows it.
    const withClauses = []
    for (const m of masked.matchAll(/(?<![\w.])with\s*</g)) {
        const open = masked.indexOf('<', m.index)
        const end = matchBracket(masked, open)
        if (end === -1) continue
        const clause = { start: m.index, end: end + 1 }
        withClauses.push(clause)
        splitParams(masked.slice(open + 1, end), open + 1).forEach((p) => {
            if (p.typeStart >= 0) add('param', p.typeStart, p.typeEnd, { with: true, name: p.name })
        })
    }

    // functions
    for (const m of masked.matchAll(/\bfunc\s+([A-Za-z_]\w*)/g)) {
        const name = m[1]
        const nameStart = m.index + m[0].length - name.length
        const nameEnd = nameStart + name.length
        let i = nameEnd
        while (i < masked.length && /\s/.test(masked[i])) i++
        let head = null
        if (masked.startsWith('::', i)) {
            let j = i + 2
            while (j < masked.length && /\s/.test(masked[j])) j++
            if (masked[j] === '<') {
                const end = matchBracket(masked, j)
                if (end !== -1) {
                    head = { text: masked.slice(j + 1, end), start: j, end: end + 1 }
                    i = end + 1
                }
            }
        }
        while (i < masked.length && /\s/.test(masked[i])) i++
        if (masked[i] !== '(') continue
        const parenEnd = matchBracket(masked, i)
        if (parenEnd === -1) continue
        const params = splitParams(masked.slice(i + 1, parenEnd), i + 1)
        for (const p of params) {
            if (p.typeStart >= 0) {
                add(p.isVar ? 'varParam' : 'param', p.typeStart, p.typeEnd, { name: p.name, func: name })
            }
        }
        // the result type: from the colon after `)` to the body, the `when` clause or the line end
        let j = parenEnd + 1
        while (j < masked.length && /[ \t]/.test(masked[j])) j++
        let returnSpan = null
        let whenSpan = null
        let bodyStart = -1
        let declEnd = masked.indexOf('\n', parenEnd)
        if (declEnd === -1) declEnd = masked.length
        if (masked[j] === ':') {
            j++
            while (j < masked.length && /[ \t]/.test(masked[j])) j++
            const typeStart = j
            let k = j
            let depth = 0
            while (k < masked.length) {
                const c = masked[k]
                if (c === '(' || c === '[' || c === '<') depth++
                else if (c === ')' || c === ']' || c === '>') {
                    if (depth === 0) break
                    depth--
                } else if (depth === 0 && (c === '{' || c === '\n' || c === ';')) break
                else if (depth === 0 && /[ \t]/.test(c)) break
                k++
            }
            if (k > typeStart) returnSpan = add('return', typeStart, k, { func: name })
            j = k
            while (j < masked.length && /[ \t]/.test(masked[j])) j++
        }
        if (masked.startsWith('when', j) && !IDENT.test(masked[j + 4] || '')) {
            let k = j
            let depth = 0
            while (k < masked.length && masked[k] !== '\n' && masked[k] !== '{') {
                if (masked[k] === '(') depth++
                else if (masked[k] === ')') depth--
                k++
            }
            whenSpan = { start: j, end: k }
            j = k
        }
        // the effect markers (`sync`, `uses`, `effects`) stand between the signature and the body:
// walk to the first `{` on the line, which is the body when the declaration has one
        let m2 = j
        while (m2 < masked.length && masked[m2] !== '\n' && masked[m2] !== '{') m2++
        if (masked[m2] === '{') {
            const end = matchBracket(masked, m2)
            bodyStart = m2
            funcs.push({
                name,
                nameStart,
                nameEnd,
                head,
                params,
                returnSpan,
                whenSpan,
                bodyStart,
                bodyEnd: end === -1 ? masked.length : end,
                sigEnd: returnSpan ? returnSpan.end : parenEnd + 1,
                insertWhenAt: returnSpan ? returnSpan.end : parenEnd + 1,
                withClauseSpans: [],
            })
        } else {
            funcs.push({
                name,
                nameStart,
                nameEnd,
                head,
                params,
                returnSpan,
                whenSpan,
                bodyStart: -1,
                bodyEnd: -1,
                sigEnd: declEnd,
                insertWhenAt: returnSpan ? returnSpan.end : parenEnd + 1,
                withClauseSpans: [],
            })
        }
    }

    // function types: `(params) => R`, `(params): R => R`, `<with>() => R`
    for (let i = 0; i < masked.length; i++) {
        if (masked[i] !== '(') continue
        const end = matchBracket(masked, i)
        if (end === -1) continue
        let j = end + 1
        while (j < masked.length && /\s/.test(masked[j])) j++
        if (masked[j] === '=' && masked[j + 1] === '>') {
            const params = splitParams(masked.slice(i + 1, end), i + 1)
            for (const p of params) if (p.typeStart >= 0) add('closure', p.typeStart, p.typeEnd, { name: p.name })
            continue
        }
        if (masked[j] === ':') {
            let k = j + 1
            while (k < masked.length && /[ \t]/.test(masked[k])) k++
            const typeStart = k
            let depth = 0
            while (k < masked.length) {
                const c = masked[k]
                if (c === '(' || c === '[' || c === '<') depth++
                else if (c === ')' || c === ']' || c === '>') {
                    if (depth === 0) break
                    depth--
                } else if (depth === 0 && /[ \t]/.test(c)) break
                k++
            }
            let l = k
            while (l < masked.length && /\s/.test(masked[l])) l++
            if (masked[l] === '=' && masked[l + 1] === '>') {
                const params = splitParams(masked.slice(i + 1, end), i + 1)
                for (const p of params) if (p.typeStart >= 0) add('closure', p.typeStart, p.typeEnd, { name: p.name })
                if (k > typeStart) add('closure', typeStart, k, { result: true })
            }
        }
    }

    // bindings: `let x: T = ...`, `var x: T`
    const bindings = []
    for (const m of masked.matchAll(/\b(?:let|var)\s+/g)) {
        let i = m.index + m[0].length
        let depth = 0
        let colon = -1
        let k = i
        while (k < masked.length && masked[k] !== '\n' && masked[k] !== ';') {
            const c = masked[k]
            if (c === '(' || c === '[' || c === '{') depth++
            else if (c === ')' || c === ']' || c === '}') {
                if (depth === 0) break
                depth--
            } else if (depth === 0 && c === ':' && masked[k + 1] !== ':') {
                colon = k
                break
            } else if (depth === 0 && c === '=') break
            k++
        }
        if (colon === -1) continue
        let typeStart = colon + 1
        while (typeStart < masked.length && /[ \t]/.test(masked[typeStart])) typeStart++
        let e = typeStart
        depth = 0
        while (e < masked.length && masked[e] !== '\n') {
            const c = masked[e]
            if (c === '(' || c === '[' || c === '<') depth++
            else if (c === ')' || c === ']' || c === '>') {
                if (depth === 0) break
                depth--
            } else if (depth === 0 && (c === '=' || c === ';')) break
            e++
        }
        if (e > typeStart) {
            const span = add('binding', typeStart, e)
            const nameMatch = masked.slice(i, colon).trim().match(/^([A-Za-z_]\w*)/)
            bindings.push({ span, name: nameMatch ? nameMatch[1] : null, typeText: masked.slice(typeStart, e) })
        }
    }

    // a `with <...>` clause belongs to the declaration that follows it, never to the one before
    let previousEnd = -1
    for (const func of funcs) {
        func.withClauseSpans = withClauses.filter((c) => c.end <= func.nameStart && c.start > previousEnd)
        func.withClause = func.withClauseSpans[0] || null
        previousEnd = func.bodyEnd > 0 ? func.bodyEnd : func.sigEnd
        // the names the body binds: a use of such a name inside the body is the local, never the
        // function of the same name (grad_library.cml binds `let exact = grad(exact_loss)`)
        func.locals = new Set()
        if (func.bodyStart >= 0) {
            const body = masked.slice(func.bodyStart, func.bodyEnd)
            for (const m of body.matchAll(/\b(?:let|var)\s+([A-Za-z_]\w*)/g)) func.locals.add(m[1])
        }
    }

    return { text, masked, spans, funcs, bindings }
}

/// The colon that separates a field or parameter name from its type, ignoring `::`.
function topLevelColon(text, from, to) {
    let depth = 0
    for (let i = from; i < to; i++) {
        const c = text[i]
        if (c === '(' || c === '[' || c === '{' || c === '<') depth++
        else if (c === ')' || c === ']' || c === '}' || c === '>') depth--
        else if (c === ':' && depth === 0 && text[i + 1] !== ':' && text[i - 1] !== ':') return i
    }
    return -1
}

/// The parameters of a `( ... )` or `< ... >` list, each with the span of its written type.
function splitParams(inner, base) {
    const out = []
    let depth = 0
    let from = 0
    const push = (a, b) => {
        const colon = topLevelColon(inner, a, b)
        if (colon === -1) return
        let typeStart = colon + 1
        while (typeStart < b && /[ \t]/.test(inner[typeStart])) typeStart++
        let typeEnd = b
        while (typeEnd > typeStart && /[ \t]/.test(inner[typeEnd - 1])) typeEnd--
        const head = inner.slice(a, colon).trim()
        const nameMatch = head.match(/([A-Za-z_]\w*)\s*$/)
        out.push({
            name: nameMatch ? nameMatch[1] : null,
            isVar: /(^|\s)var\s/.test(' ' + head),
            isConstas: /(^|\s)constas\s/.test(' ' + head),
            typeStart: base + typeStart,
            typeEnd: base + typeEnd,
            head,
            typeText: inner.slice(typeStart, typeEnd),
        })
    }
    for (let i = 0; i <= inner.length; i++) {
        const c = inner[i]
        if (i === inner.length || (depth === 0 && c === ',')) {
            push(from, i)
            from = i + 1
            continue
        }
        if (c === '(' || c === '[' || c === '{' || c === '<') depth++
        else if (c === ')' || c === ']' || c === '}' || c === '>') depth--
    }
    return out
}

// ---------------------------------------------------------------------------------------------
// Sites: one bare `Tensor` each, with the position it stands in.

const BARE_TENSOR = /(?<![\w$.])Tensor(?!\s*<)/g

/// Whether this occurrence is a qualified name (`tensor::Tensor::ones`, `Tensor::shape`): the
/// module's or the alias constructor's namespace, not a type position. The deletion step does not
/// touch those.
function isQualifiedName(masked, index) {
    return masked.slice(Math.max(0, index - 2), index) === '::' || masked.slice(index + 6, index + 8) === '::'
}

function collectSites(scanned, relFile) {
    const { masked, spans } = scanned
    const sites = []
    for (const m of masked.matchAll(BARE_TENSOR)) {
        const index = m.index
        if (isQualifiedName(masked, index)) continue
        const inner = spans
            .filter((s) => s.start <= index && index < s.end)
            .sort((a, b) => a.end - a.start - (b.end - b.start))[0]
        let category
        let sub = ''
        if (inner && inner.kind === 'import') category = 'import'
        else if (inner && (inner.kind === 'param' || inner.kind === 'varParam')) {
            category = inner.kind === 'varParam' ? 'varParam' : 'param'
            sub = inner.with ? 'with' : ''
        } else if (inner && inner.kind === 'return') category = 'return'
        else if (inner && inner.kind === 'field') category = 'field'
        else if (inner && inner.kind === 'closure') {
            category = 'closure'
            sub = inner.result ? 'result' : 'parameter'
        } else if (inner && inner.kind === 'alias') category = 'alias'
        else if (inner && inner.kind === 'binding') {
            category = 'other'
            sub = 'binding'
        }
        else category = 'other'
        if (inner && inner.kind !== 'import') {
            if (angleDepthIn(masked, inner.start, index) > 0) category = 'nested'
            else if (
                parenDepthIn(masked, inner.start, index) > 0 &&
                category !== 'closure' &&
                !(inner.kind === 'param' && inner.with)
            ) {
                category = category === 'field' ? 'field' : 'field'
                sub = sub || 'tuple'
            }
        }
        const line = masked.slice(0, index).split('\n').length
        const lineStart = masked.lastIndexOf('\n', index - 1) + 1
        sites.push({
            file: relFile,
            index,
            line,
            category,
            sub,
            context: masked.slice(lineStart, masked.indexOf('\n', index) === -1 ? masked.length : masked.indexOf('\n', index)).trim(),
            owner: inner && inner.func ? inner.func : null,
            inner,
        })
    }
    return sites
}

// ---------------------------------------------------------------------------------------------
// The indices the rules read: where a name is used, and what a function declares.

function buildIndex(files) {
    const funcsByName = new Map()
    const usesByName = new Map()
    for (const f of files) {
        for (const func of f.scanned.funcs) {
            if (!funcsByName.has(func.name)) funcsByName.set(func.name, [])
            funcsByName.get(func.name).push({ file: f.rel, func, scanned: f.scanned })
        }
        for (const m of f.scanned.masked.matchAll(/(?<![\w$.])([A-Za-z_]\w*)/g)) {
            const name = m[1]
            const index = m.index
            let j = index + name.length
            while (j < f.scanned.masked.length && /[ \t]/.test(f.scanned.masked[j])) j++
            const before = f.scanned.masked.slice(Math.max(0, index - 2), index)
            let kind = 'value'
            const isDeclaration = /func\s+$/.test(f.scanned.masked.slice(Math.max(0, index - 6), index))
            // a `let`/`var` of the same name introduces a NEW name: the local shadows the
            // function and is not a use of it (grad_library.cml binds `let exact = grad(...)`)
            const isBinding = /\b(?:let|var)\s+$/.test(f.scanned.masked.slice(Math.max(0, index - 6), index))
            if (f.scanned.masked.startsWith('::<', j) || f.scanned.masked.startsWith(':: <', j)) kind = 'explicit'
            else if (f.scanned.masked[j] === '(') kind = 'call'
            else if (f.scanned.masked[j] === ':' || /[.]/.test(before)) kind = 'field'
            else if (isDeclaration || isBinding) kind = 'declaration'
            if (kind === 'value') {
                const owner = f.scanned.funcs.find((fn) => fn.bodyStart >= 0 && index > fn.bodyStart && index < fn.bodyEnd)
                if (owner && owner.locals.has(name) && !/func\s+$/.test(f.scanned.masked.slice(Math.max(0, index - 6), index))) {
                    kind = 'local'
                }
            }
            if (!usesByName.has(name)) usesByName.set(name, [])
            usesByName.get(name).push({ file: f.rel, index, line: f.scanned.masked.slice(0, index).split('\n').length, kind })
        }
    }
    return { funcsByName, usesByName }
}

/// Whether a function's name is only ever called: a value use (`grad(f)`) or an explicit type
/// argument (`f::<int>`) would change meaning -- or stop compiling -- when the head gains a
/// parameter, so the rule refuses both.
function nameIsSafe(index, name) {
    const uses = index.usesByName.get(name) || []
    const bad = uses.filter((u) => u.kind === 'value')
    const explicit = uses.filter((u) => u.kind === 'explicit')
    return { ok: bad.length === 0 && explicit.length === 0, bad, explicit }
}

// ---------------------------------------------------------------------------------------------
// The body test: does the body only read the rank of its tensor parameters, or does it compute
// with their values? Only the first can become a free type variable.
//
// The test is a small taint analysis over the statements of the body. A bare `Tensor` parameter
// starts tainted -- its values are of the element type. A statement that mentions a tainted name
// somewhere a value can be read from taints what it binds (`let residuals = y_pred - y_true`)
// and refuses the rewrite; a mention the element type cannot be read from -- the argument of
// `len`/`shape`/`numel`, the iterated side of `for row in t`, or a result built only from tainted
// names with the element-preserving operators -- leaves it free. The last form is what a function
// that returns a tensor of the same element type looks like (`return a @ b`), and it is the only
// form R2 accepts for a bare result type.

/// The statements of a body, split where a newline or a semicolon sits outside every bracket.
function bodyStatements(text) {
    const out = []
    let depth = 0
    let from = 0
    for (let i = 0; i <= text.length; i++) {
        const c = text[i]
        if (c === '{' || c === '(' || c === '[') depth++
        else if (c === '}' || c === ')' || c === ']') depth--
        if (i === text.length || (depth === 0 && (c === '\n' || c === ';'))) {
            const s = text.slice(from, i).trim()
            if (s) out.push(s)
            from = i + 1
        }
    }
    return out
}

/// Whether `expr` is built only from the bare parameters and the element-preserving operators:
/// `a @ b`, `x - y`, `(a * b) + c`. Its element type is the parameters' own, so a result type may
/// name it (`T[..]`). Anything else -- a call, an index, a literal -- is not this form.
function elementPreservingExpr(expr, bareNames) {
    let e = expr.trim()
    while (e.startsWith('(') && matchBracket(e, 0) === e.length - 1) e = e.slice(1, -1).trim()
    if (new RegExp(`^[A-Za-z_]\\w*$`).test(e)) return bareNames.has(e)
    let depth = 0
    for (let i = 0; i < e.length; i++) {
        const c = e[i]
        if (c === '(' || c === '[' || c === '{') depth++
        else if (c === ')' || c === ']' || c === '}') depth--
        else if (depth === 0 && ELEMENT_PRESERVING_OPERATORS.includes(c)) {
            // `**` and `^` are not in the set; a unary sign at the start is not a binary operator
            if (i === 0) continue
            const left = e.slice(0, i)
            const right = e.slice(i + 1)
            return elementPreservingExpr(left, bareNames) && elementPreservingExpr(right, bareNames)
        }
    }
    return false
}

/// The call whose argument list contains `at`, as its callee name, or null when the position
/// stands outside every call.
function enclosingCallee(text, at) {
    let depth = 0
    for (let i = at - 1; i >= 0; i--) {
        const c = text[i]
        if (c === ')' || c === ']') depth++
        else if (c === '(') {
            if (depth === 0) {
                const m = text.slice(0, i).match(/([A-Za-z_]\w*)\s*$/)
                return m ? m[1] : null
            }
            depth--
        } else if (c === ']') depth++
    }
    return null
}

/// Whether every mention of a tainted name in `stmt` is a position the element type cannot be
/// read from. `bareNames` is the set of the function's bare parameters; `allowResult` permits the
/// last form (used by R2 and by a tensor-returning body).
function statementIsElementFree(stmt, tainted, bareNames, allowResult) {
    const names = [...tainted].filter((n) => new RegExp(`(?<![\\w$.])${n}\\b`).test(stmt))
    for (const name of names) {
        const m = stmt.match(new RegExp(`(?<![\\w$.])${name}\\b`))
        const at = m.index
        const call = enclosingCallee(stmt, at)
        if (call && ELEMENT_AGNOSTIC_HELPERS.has(call)) continue
        const before = stmt.slice(0, at)
        if (/(?:^|[^\w])for\s+[A-Za-z_]\w*\s+in\s*$/.test(before)) continue
        if (allowResult) {
            const r = stmt.match(/^\s*return\s+([\s\S]*)$/)
            if (r && elementPreservingExpr(r[1], bareNames)) continue
        }
        return { ok: false, at, name }
    }
    return { ok: true }
}

function elementFreeBody(scanned, func) {
    const { masked } = scanned
    const bareNames = new Set(func.params.filter((p) => isBareTensorText(p.typeText) && p.name).map((p) => p.name))
    const withNames = scanned.spans
        .filter(
            (s) =>
                s.kind === 'param' &&
                s.with &&
                s.name &&
                isBareTensorText(s.text) &&
                func.withClauseSpans.some((c) => s.start > c.start && s.start < c.end)
        )
        .map((s) => s.name)
    for (const n of withNames) bareNames.add(n)
    if (func.bodyStart < 0) return { ok: false, why: 'the declaration has no body to read', bareNames }
    const body = masked.slice(func.bodyStart + 1, func.bodyEnd)
    const tainted = new Set(bareNames)
    const statements = bodyStatements(body)
    const returnsTensor = func.returnSpan ? isArrayLikeType(func.returnSpan.text) : false
    for (const stmt of statements) {
        const isReturn = /^\s*return\b/.test(stmt)
        const check = statementIsElementFree(stmt, tainted, bareNames, isReturn && returnsTensor)
        if (!check.ok) {
            return {
                ok: false,
                bareNames,
                why: `the body reads the element type at \`${snippet(body, Math.max(0, body.indexOf(stmt) + check.at), check.name.length)}\``,
            }
        }
        // what a statement binds from a tainted value stays tainted, unless the value is a count
        const decl = stmt.match(/^\s*(?:let|var)\s+([A-Za-z_]\w*)(?:\s*:[^=]*)?=/)
        if (decl) {
            const rhs = stmt.slice(stmt.indexOf('=') + 1)
            const mentions = [...tainted].some((n) => new RegExp(`(?<![\\w$.])${n}\\b`).test(rhs))
            if (mentions) tainted.add(decl[1])
        }
    }
    return { ok: true, bareNames, statements, tainted }
}

/// A type that is an array of some kind, including the bare spelling and the module's other
/// arrays: a result of this kind can name an element type, a `float` or `int` cannot.
function isArrayLikeType(text) {
    const t = text.trim()
    if (t === 'Tensor' || /^Tensor\s*</.test(t)) return true
    if (/\[\s*[^\]]*\]/.test(t)) return true
    return false
}

function snippet(text, at, len) {
    const from = Math.max(0, text.lastIndexOf('\n', at) + 1)
    let to = text.indexOf('\n', at)
    if (to === -1) to = text.length
    const line = text.slice(from, to).trim().replace(/\s+/g, ' ')
    return line.length > 70 ? line.slice(0, 67) + '...' : line
}

function isBareTensorText(typeText) {
    return /^\s*Tensor\s*$/.test(typeText)
}

// ---------------------------------------------------------------------------------------------
// The call sites of a name, and whether an argument can bind an element type today.

function callSites(files, name) {
    const out = []
    for (const f of files) {
        const masked = f.scanned.masked
        const pattern = new RegExp(`(?<![\\w$.])${name}\\s*\\(`, 'g')
        for (const m of masked.matchAll(pattern)) {
            // the declaration itself is not a call site
            if (/func\s+$/.test(masked.slice(Math.max(0, m.index - 8), m.index))) continue
            const open = masked.indexOf('(', m.index)
            const end = matchBracket(masked, open)
            if (end === -1) continue
            const args = splitArguments(masked.slice(open + 1, end))
            out.push({
                file: f.rel,
                index: m.index,
                line: masked.slice(0, m.index).split('\n').length,
                args,
                context: snippet(masked, m.index, name.length),
                func: f.scanned.funcs.find((fn) => fn.bodyStart <= m.index && m.index < fn.bodyEnd) || null,
                scanned: f.scanned,
            })
        }
    }
    return out
}

function splitArguments(inner) {
    const out = []
    let depth = 0
    let from = 0
    for (let i = 0; i <= inner.length; i++) {
        const c = inner[i]
        if (i === inner.length || (depth === 0 && c === ',')) {
            const t = inner.slice(from, i).trim()
            if (t) out.push(t)
            from = i + 1
            continue
        }
        if (c === '(' || c === '[' || c === '{' || c === '<') depth++
        else if (c === ')' || c === ']' || c === '}' || c === '>') depth--
    }
    return out
}

/// Whether an argument expression's WRITTEN type is the transitional `Tensor`, which is what
/// stops a free `T` from binding. Measured on the flip line (test/cases/compile/_h551probe):
/// `count(m)` with `let m: Tensor = ones([2, 3])` is refused ("cannot be called with argument
/// types (Tensor)"), while an untyped `let m = ones([2, 3])` binds, an array literal binds, and an
/// elementwise operator's result binds -- so the annotation, not the producer, is the blocker.
function argumentTypeText(site, expr) {
    const bare = expr.replace(/^\(([\s\S]*)\)$/, '$1').trim()
    if (/^\[[\s\S]*\]$/.test(bare)) return { ok: true, why: 'array literal' }
    const name = bare.match(/^([A-Za-z_]\w*)$/)
    if (!name) return { ok: true, why: `\`${bare}\` is an expression: its type is computed` }
    const ident = name[1]
    const func = site.func
    if (func) {
        const param = func.params.find((p) => p.name === ident)
        if (param && isBareTensorText(param.typeText)) {
            return { ok: false, why: `\`${ident}\` is a parameter written \`Tensor\`` }
        }
        if (param) return { ok: true, why: `parameter \`${ident}: ${param.typeText}\`` }
    }
    const binding = site.scanned.bindings.find((b) => b.name === ident)
    if (binding && isBareTensorText(binding.typeText)) {
        return { ok: false, why: `\`${ident}\` is bound as \`Tensor\`` }
    }
    if (binding) return { ok: true, why: `binding \`${ident}: ${binding.typeText}\`` }
    return { ok: true, why: `\`${ident}\` carries no annotation, so its type is what produced it` }
}

// ---------------------------------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------
// The rule. Everything it does not write, it reports with the reason it refused.

/// Why a position no rule covers stays for a person: the element type it must name is decided
/// somewhere else in the file, not by the position itself.
function defaultReason(site) {
    switch (site.category) {
        case 'field':
            return site.sub === 'tuple'
                ? 'a tuple element: the element type is the tuple\'s own, read what is stored in it'
                : 'a struct field: the element type is the field\'s own, read what is stored in it'
        case 'closure':
            return 'a function value\'s type: the element type is the caller\'s, read the call sites'
        case 'alias':
            return 'a type declaration\'s target: the element type is what the alias stands for'
        case 'nested':
            return 'inside a type argument list: the element type belongs to the outer type'
        case 'varParam':
            return 'a var parameter: the takeover of the variable at the call site is the transitional dependency (measured: a generic var parameter breaks today)'
        case 'other':
            return site.sub === 'binding'
                ? 'a binding annotation: read what the binding is given'
                : 'a position outside a signature'
        default:
            return 'no rule covers this position'
    }
}

function analyse(files, options) {
    const index = buildIndex(files)
    const sites = []
    for (const f of files) sites.push(...collectSites(f.scanned, f.rel))

    const blockers = []
    const funcs = []
    for (const f of files) {
        for (const func of f.scanned.funcs) {
            const own = sites.filter(
                (s) =>
                    s.file === f.rel &&
                    s.index >= (func.withClauseSpans.length ? func.withClauseSpans[0].start : func.nameStart) &&
                    s.index < func.sigEnd
            )
            if (!own.length) continue
            const record = { file: f.rel, func, sites: own, verdict: 'judge', why: '', resultSite: null }
            funcs.push(record)
            const nonParam = own.filter((s) => s.category !== 'param')
            // R2: a bare result type may follow the parameters when the body's answer is built
            // from them with the element-preserving operators -- `return a @ b` -- and declares
            // one bare `Tensor` of its own. Anything else (a tuple, a `float`, an operator that
            // reads the element type) is a person's decision.
            const bareResults = nonParam.filter((s) => s.category === 'return' && isBareTensorText(s.inner.text))
            const otherSites = nonParam.filter((s) => !bareResults.includes(s))
            if (otherSites.length) {
                record.why =
                    otherSites[0].category === 'return'
                        ? 'the result type needs an element type the arguments do not give'
                        : `a bare \`Tensor\` stands in ${CATEGORY_LABEL.get(otherSites[0].category)}`
                continue
            }
            if (bareResults.length > 1) {
                record.why = 'the result type writes more than one bare `Tensor`'
                continue
            }
            if (func.head) {
                record.why = 'the head already declares a type parameter'
                continue
            }
            const safe = nameIsSafe(index, func.name)
            if (!safe.ok) {
                record.why = safe.bad.length
                    ? `\`${func.name}\` is used as a value at ${safe.bad[0].file} (adding a head parameter would require an explicit instance there)`
                    : `\`${func.name}\` is called with explicit type arguments at ${safe.explicit[0].file}`
                continue
            }
            const body = elementFreeBody(f.scanned, func)
            if (!body.ok) {
                record.why = body.why
                continue
            }
            // every site the rule would write must be the type of a declared parameter that is
            // written exactly `Tensor`: a bare `Tensor` inside a parameter's closure type
            // (`da: <x: f32> () => Tensor` in grad.cml) is not this parameter's element type, and
            // a signature whose bare `Tensor` is only that has no free type variable to declare.
            if (!body.bareNames.size) {
                record.why = 'no parameter is written `Tensor` itself, so there is no element type to free'
                continue
            }
            const paramSites = own.filter((s) => s.category === 'param')
            const notAParameter = paramSites.filter((s) => !s.inner.name || !body.bareNames.has(s.inner.name))
            if (notAParameter.length) {
                record.why =
                    'the bare `Tensor` stands inside a parameter\'s type, not as the parameter\'s own element type (read that type)'
                continue
            }
            // a `var` parameter beside a bare tensor waits: taking a variable over inside an
            // instantiated graph is not measured, and it breaks today (see the probe p6)
            if (func.params.some((p) => p.isVar)) {
                record.why = 'a `var` parameter stands beside the bare `Tensor` (the takeover of a variable in an instantiated graph is not measured)'
                continue
            }
            if (bareResults.length === 1) {
                const returned = elementFreeBody(f.scanned, func)
                if (!returned.ok) {
                    record.why = returned.why
                    continue
                }
                const stmts = body.statements.filter((s) => /^\s*return\b/.test(s))
                const r = stmts.length === 1 ? stmts[0].match(/^\s*return\s+([\s\S]*)$/) : null
                if (!r || !elementPreservingExpr(r[1], body.bareNames)) {
                    record.why = 'the result type needs an element type the body does not name'
                    continue
                }
                record.resultSite = bareResults[0]
            }
            record.verdict = 'rewrite'
            const read = [...body.bareNames].some((n) => new RegExp(`(?<![\\w$.])${n}\\b`).test(f.scanned.masked.slice(func.bodyStart + 1, func.bodyEnd)))
            record.why = bareResults.length
                ? `one graph per element type: \`${func.name}\` takes ${[...body.bareNames].join(', ')} and answers with a value of their element type`
                : read
                  ? `one graph per element type: \`${func.name}\` takes ${[...body.bareNames].join(', ')} and computes no element type of its own`
                  : `one graph per element type: \`${func.name}\` never reads ${[...body.bareNames].join(', ')}, so the call site's element type is all it needs`
        }
    }

    // The call sites decide the rewrite too: an argument whose written type is the transitional
    // `Tensor` cannot bind a free `T` (measured; see argumentTypeText), so the function waits for
    // the value that is passed to it to be written concretely. Reported with its call site, which
    // is what a migration lane has to change first.
    for (const record of funcs) {
        if (record.verdict !== 'rewrite') continue
        const sites = callSites(files, record.func.name)
        const bad = []
        for (const site of sites) {
            for (const arg of site.args) {
                const t = argumentTypeText(site, arg)
                if (!t.ok) bad.push({ file: site.file, line: site.line, callee: record.func.name, context: site.context, why: t.why })
            }
        }
        if (bad.length) {
            blockers.push(...bad)
            record.verdict = 'judge'
            record.why = `${bad[0].file}:${bad[0].line} calls \`${record.func.name}\` with a value whose written type is \`Tensor\` (${bad[0].why}), so a free \`T\` cannot bind there`
        }
    }

    // every site of a signature carries the verdict of the function it stands in; the rest
    // (a binding annotation, a cast, an import name) keeps the default above
    for (const record of funcs) for (const s of record.sites) s.record = record
    for (const s of sites) {
        s.verdict = 'judge'
        s.reason = defaultReason(s)
        if (s.category === 'import') {
            s.verdict = 'skip'
            s.reason = 'import list: the alias constructor is exported by name here'
            continue
        }
        if (s.record) {
            s.verdict = s.record.verdict
            s.reason = s.record.why
        }
    }
    // a signature the rule writes also needs the head and the guard clause, once per function
    for (const record of funcs) {
        if (record.verdict !== 'rewrite') continue
        for (const s of record.sites) {
            s.verdict = 'rewrite'
            s.reason = record.why
        }
    }

    return { sites, funcs, blockers }
}

// ---------------------------------------------------------------------------------------------
// Writing: the type tokens, the head, the guard clause, and the import the guard needs.

function editsForFunction(files, record, options) {
    const { func } = record
    const edits = []
    const file = files.find((f) => f.rel === record.file)
    const spelling = options.spelling === 'tensor' ? 'Tensor<T>' : 'T[..]'
    for (const s of record.sites) edits.push({ start: s.index, end: s.index + 'Tensor'.length, text: spelling })
    edits.push({ start: func.nameEnd, end: func.nameEnd, text: `::<T>` })
    if (!func.whenSpan) {
        edits.push({ start: func.insertWhenAt, end: func.insertWhenAt, text: ` when ${PREDICATE}::<T>()` })
    }
    return { edits, needsImport: !file.scanned.masked.includes(`${PREDICATE}::<`) || !hasPredicateImport(file.scanned.masked) }
}

function hasPredicateImport(masked) {
    const m = masked.match(/^[ \t]*import\s*\{([^}]*)\}\s*from\s*types\b/m)
    return !!m && new RegExp(`(^|[,\\s])${PREDICATE}([,\\s]|$)`).test(m[1])
}

function importEdit(masked) {
    const m = masked.match(/^[ \t]*import\s*\{([^}]*)\}\s*from\s*types\b/m)
    if (m) {
        const names = m[1].split(',').map((s) => s.trim()).filter(Boolean)
        if (names.includes(PREDICATE)) return null
        names.push(PREDICATE)
        names.sort((a, b) => (a.toLowerCase() < b.toLowerCase() ? -1 : a.toLowerCase() > b.toLowerCase() ? 1 : 0))
        const start = m.index + m[0].indexOf('{') + 1
        const end = m.index + m[0].lastIndexOf('}')
        return { start, end, text: ' ' + names.join(', ') + ' ' }
    }
    const imports = [...masked.matchAll(/^[ \t]*import\b[^\n]*$/gm)]
    if (imports.length) {
        const last = imports[imports.length - 1]
        const at = last.index + last[0].length
        return { start: at, end: at, text: `\nimport { ${PREDICATE} } from ${PREDICATE_MODULE}` }
    }
    const firstDecl = masked.search(/^\s*(?:module|func|let|var|type|export)\b/m)
    const at = firstDecl === -1 ? 0 : firstDecl
    return { start: at, end: at, text: `import { ${PREDICATE} } from ${PREDICATE_MODULE}\n\n` }
}

// ---------------------------------------------------------------------------------------------
// The tree walk and the report.

function walkCml(roots) {
    const out = []
    const seen = new Set()
    const visit = (p) => {
        const abs = path.resolve(p)
        if (seen.has(abs) || !fs.existsSync(abs)) return
        seen.add(abs)
        const stat = fs.statSync(abs)
        if (stat.isDirectory()) {
            for (const e of fs.readdirSync(abs, { withFileTypes: true }).sort((a, b) => a.name.localeCompare(b.name))) {
                if (e.name === 'out' || e.name === '.git' || e.name === 'node_modules') continue
                visit(path.join(abs, e.name))
            }
        } else if (abs.endsWith('.cml')) out.push(abs)
    }
    for (const r of roots) visit(r)
    return out.sort()
}

function topDir(rel) {
    const parts = rel.split('/')
    return parts.length > 2 ? parts.slice(0, 2).join('/') : rel
}

function main(argv) {
    let write = false
    let json = false
    let quietSites = false
    let root = REPO_ROOT
    let spelling = 'array'
    const paths = []
    for (let i = 0; i < argv.length; i++) {
        const a = argv[i]
        if (a === '--write') write = true
        else if (a === '--json') json = true
        else if (a === '--quiet-sites') quietSites = true
        else if (a === '--root') root = argv[++i]
        else if (a.startsWith('--root=')) root = a.slice('--root='.length)
        else if (a === '--path') paths.push(...String(argv[++i]).split(','))
        else if (a.startsWith('--path=')) paths.push(...a.slice('--path='.length).split(','))
        else if (a === '--spelling') spelling = argv[++i]
        else if (a.startsWith('--spelling=')) spelling = a.slice('--spelling='.length)
        else {
            console.error(`usage: node scripts/migrate-bare-tensor.mjs [--path DIR|FILE] [--write] [--json]`)
            console.error(`       [--spelling array|tensor] [--quiet-sites] [--root DIR]`)
            console.error(`unknown flag: ${a}`)
            return 2
        }
    }
    if (!['array', 'tensor'].includes(spelling)) {
        console.error(`--spelling takes \`array\` (T[..], the spelling that compiles today) or \`tensor\``)
        return 2
    }
    const roots = paths.length ? paths.map((p) => path.resolve(root, p)) : [root]
    const files = walkCml(roots).map((abs) => {
        const rel = path.relative(root, abs).replace(/\\/g, '/')
        return { abs, rel, scanned: scanFile(fs.readFileSync(abs, 'utf8')) }
    })
    if (!files.length) {
        console.error(`no .cml file under ${roots.join(', ')}: nothing to report`)
        return 2
    }

    const { sites, funcs, blockers } = analyse(files, { spelling })

    const byCategory = new Map()
    const byDir = new Map()
    const byReason = new Map()
    for (const s of sites) {
        byCategory.set(s.category, (byCategory.get(s.category) || 0) + 1)
        const d = topDir(s.file)
        const cur = byDir.get(d) || { sites: 0, files: new Set(), rewrite: 0, judge: 0, skip: 0 }
        cur.sites++
        cur.files.add(s.file)
        cur[s.verdict]++
        byDir.set(d, cur)
        if (s.verdict !== 'rewrite') {
            const key = `${s.verdict}: ${s.reason}`
            byReason.set(key, (byReason.get(key) || 0) + 1)
        }
    }
    const rewriteSites = sites.filter((s) => s.verdict === 'rewrite')
    const judgeSites = sites.filter((s) => s.verdict === 'judge')
    const skipSites = sites.filter((s) => s.verdict === 'skip')

    const report = {
        scope: roots.map((r) => path.relative(root, r).replace(/\\/g, '/') || '.'),
        files: files.length,
        filesWithSites: new Set(sites.map((s) => s.file)).size,
        sites: sites.length,
        rewrite: rewriteSites.length,
        judge: judgeSites.length,
        skip: skipSites.length,
        functionsRewritten: funcs.filter((r) => r.verdict === 'rewrite').length,
        byCategory: CATEGORIES.map(([id, label]) => ({ id, label, count: byCategory.get(id) || 0 })),
        byDir: [...byDir.entries()].map(([dir, v]) => ({
            dir,
            files: v.files.size,
            sites: v.sites,
            rewrite: v.rewrite,
            judge: v.judge,
            skip: v.skip,
        })).sort((a, b) => b.sites - a.sites),
        byReason: [...byReason.entries()].map(([reason, count]) => ({ reason, count })).sort((a, b) => b.count - a.count),
        blockers,
        written: write,
        spelling,
        siteList: sites.map((s) => ({
            file: s.file,
            index: s.index,
            line: s.line,
            category: s.category,
            sub: s.sub,
            verdict: s.verdict,
            reason: s.reason,
            context: s.context,
        })),
    }

    if (json) {
        console.log(JSON.stringify(report, null, 2))
    } else {
        printReport(report, quietSites)
    }

    if (write && spelling === 'tensor') {
        console.error(
            '--spelling tensor writes `Tensor<T>`, which this compiler refuses today ' +
                '("the arguments of the type \'Tensor<...>\' are not accepted: the element type of a Tensor is a numeric scalar"). ' +
                'It is a report-only spelling until the deletion step lands; nothing is written.'
        )
        return 2
    }
    if (write && rewriteSites.length) applyWrites(files, funcs, spelling)

    return 0
}

function printReport(report, quietSites) {
    const scope = report.scope.join(', ')
    console.log(`migrate-bare-tensor: ${report.written ? 'writing' : 'report only (pass --write to apply)'}; scope ${scope}`)
    console.log(`scanned ${report.files} .cml files; bare \`Tensor\` occurrences: ${report.sites} in ${report.filesWithSites} files`)
    console.log(`  rewrite ${report.rewrite}   judge ${report.judge}   skip ${report.skip}   (functions rewritten: ${report.functionsRewritten})`)
    console.log('by position:')
    for (const c of report.byCategory) console.log(`  ${c.label.padEnd(34)} ${String(c.count).padStart(5)}`)
    console.log('by verdict reason:')
    for (const r of report.byReason.slice(0, 20)) console.log(`  ${String(r.count).padStart(5)}  ${r.reason}`)
    console.log('by directory (top 25):')
    for (const d of report.byDir.slice(0, 25)) {
        console.log(
            `  ${String(d.sites).padStart(5)} in ${String(d.files).padStart(3)} file(s)  rewrite ${String(d.rewrite).padStart(4)} judge ${String(d.judge).padStart(4)} skip ${String(d.skip).padStart(4)}  ${d.dir}`
        )
    }
    if (report.blockers.length) {
        console.log(`call sites whose argument still has the transitional \`Tensor\` type (${report.blockers.length}):`)
        for (const b of report.blockers.slice(0, 20)) console.log(`  ${b.file}:${b.line}  ${b.callee}(${b.context.slice(0, 60)})  ${b.why}`)
    }
    if (!quietSites) {
        console.log(`sites:`)
        for (const s of report.siteList) {
            console.log(`  ${s.file}:${s.line}  ${s.verdict.padEnd(7)} ${s.category}${s.sub ? '/' + s.sub : ''}  ${s.reason}`)
            console.log(`      ${s.context}`)
        }
    }
}

function applyWrites(files, funcs, spelling) {
    let changed = 0
    for (const f of files) {
        const records = funcs.filter((r) => r.verdict === 'rewrite' && r.file === f.rel)
        if (!records.length) continue
        const edits = []
        let needsImport = false
        for (const record of records) {
            const r = editsForFunction(files, record, { spelling })
            edits.push(...r.edits)
            needsImport = needsImport || r.needsImport
        }
        // one import per file, however many signatures want the predicate
        if (needsImport) {
            const ie = importEdit(f.scanned.masked)
            if (ie) edits.push(ie)
        }
        const ordered = edits.sort((a, b) => b.start - a.start || b.end - a.end)
        let text = f.scanned.text
        for (const e of ordered) text = text.slice(0, e.start) + e.text + text.slice(e.end)
        fs.writeFileSync(f.abs, text)
        changed++
    }
    console.log(`migrate-bare-tensor: wrote ${changed} file(s)`)
}

process.exitCode = main(process.argv.slice(2))