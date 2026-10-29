// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Every rule of the standard rule library, decided by an SMT solver instead of sampled.
//
// `stdlib/rules.cml` is read as the single truth: the rule declarations, the licence each one asks
// for (`#requires(...)`), the condition each one is guarded by (`when`), and the two sides of each
// one. For every rule this script states one proposition -- "the two sides denote the same value for
// every input the rule admits" -- and asks Z3 whether its negation is unsatisfiable:
//
//   proved   the negation is unsatisfiable: the two sides agree for every admitted input;
//   refuted  the negation is satisfiable, and the model Z3 returns is printed as the counterexample;
//   unknown  no query was asked or the solver did not decide. The reason is printed and says which
//            of the three cases it is: a side that cannot be translated (a transcendental kernel, a
//            shape, a region), a proposition that is not an equality (an error bound), or a query
//            the solver left open within the per-query budget.
//
// The proposition depends on the licence, because the licence is what the rewrite is allowed to
// change (design principles 7.2: `reassociate` and `contract` are real equality on finite values
// plus bit equality on the special values; `refine_accuracy` is an error bound). The licence is
// therefore translated into premises, not into a sampling filter and not into a tolerance:
//
//   no `#requires`       no premise. The proposition is bit identity: `QF_BV` `=` for the integers
//                        (two's-complement wraparound, so a ring) and `fp.to_ieee_bv` equality for
//                        the floats, which is what keeps `+0.0` and `-0.0` apart.
//   `reassociate`        the arithmetic is read over the reals: every operand is its exact real
//                        value and no operand is rounded, so the proposition is the algebraic
//                        identity the rule writes. The domain of that model is the finite values,
//                        which is why the guards' `finite_value(...)` conjuncts come out trivially
//                        true instead of being dropped by hand -- they are translated like any other
//                        conjunct and discharged by the model.
//   `no_infs`            the same model, plus the statement that no intermediate value is an
//                        infinity: an intermediate product or sum is taken in R, so a rewrite that
//                        removes an overflow is not a difference.
//   `no_signed_zeros`    the same model, plus the statement that a zero has no sign: R has one zero,
//                        so a rule whose only difference is the sign of a zero is not refuted by it.
//   `refine_accuracy`    the proposition is `error(right) <= error(left) + bound`, not an equality.
//                        No query is asked and the verdict is `unknown` with that reason.
//
// The guard (`when`) is a premise too, and its conjuncts are read from the library, not restated
// here: the helper functions it calls (`combine_same_sign`, `fold_coefficients`, ...) are expanded
// from their own definitions, and the type predicates (`is_real`, `hasScalarKernel`, ...) are
// evaluated per instance from theirs. A licence or a guard construct this script cannot put into a
// query is reported as `unknown` with the reason; nothing is skipped in silence.
//
// Two things make the verdicts falsifiable, and both run on every invocation:
//
//   * a sanity block of four ground queries whose answers are known (the bit view of a float keeps
//     `+0.0` and `-0.0` apart, integer addition wraps, the float model rounds): a translation that
//     stopped being faithful fails here before any rule is reported `proved`;
//   * a mutation: one rule of the library is broken on purpose (`x * (c1 + c2)` becomes
//     `x * (c1 - c2)`) and must come back `refuted` with a model where the coefficient is not zero,
//     then the unbroken rule must come back `proved`. A checker that answers `proved` to everything
//     fails this pair.
//
// The solver is an optional dependency of the build, not of the runtime: this script starts it from
// `$CAMEL_Z3`, then `z3` on `PATH`, then the usual user-local location. With no solver it prints a
// `SKIPPED` banner and the command that installs one -- and exits zero, because nothing was checked;
// `--require-solver` turns that into a failure, and `--probe-solver` answers the same question with an
// exit code so a test plan can skip on it.
//
//   node scripts/rule-proofs.mjs [--require-solver] [--timeout MS] [--verbose]
//   node scripts/rule-proofs.mjs --probe-solver   # exit 0 when a solver is on the host, else 1
import fs from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..')
const LIBRARY = path.join(ROOT, 'stdlib', 'rules.cml')
const PERMISSIONS = path.join(ROOT, 'src', 'core', 'operator_semantics.cpp')

function fail(message) {
    console.error(`rule-proofs: ${message}`)
    process.exit(1)
}

// ---- the solver ---------------------------------------------------------------------------

function findSolver() {
    const named = []
    if (process.env.CAMEL_Z3) named.push(process.env.CAMEL_Z3)
    named.push('z3')
    if (process.env.HOME) named.push(path.join(process.env.HOME, '.local', 'bin', 'z3'))
    if (process.env.LOCALAPPDATA) named.push(path.join(process.env.LOCALAPPDATA, 'z3', 'bin', 'z3.exe'))
    named.push('/usr/local/bin/z3', '/usr/bin/z3', 'C:\\Program Files\\z3\\bin\\z3.exe')
    for (const candidate of named) {
        const probe = spawnSync(candidate, ['--version'], { encoding: 'utf8', timeout: 20000 })
        if (probe.error || probe.status !== 0) continue
        const version = (probe.stdout || '').trim().split('\n')[0]
        if (!/Z3|z3/.test(version)) continue
        return { command: candidate, version }
    }
    return null
}

// ---- the library as source text ---------------------------------------------------------------

// The library is read the way the language reads it: comments carry no semantics, so they are
// removed before anything is parsed. There are no string literals in the file, which is what makes
// this safe.
function withoutComments(text) {
    return text.replace(/\/\*[\s\S]*?\*\//g, (block) => block.replace(/[^\n]/g, ' ')).replace(/\/\/[^\n]*/g, '')
}

function parseParams(text) {
    const params = []
    for (const part of splitTopLevel(text, ',')) {
        const words = part.trim()
        if (words === '') continue
        const colon = words.indexOf(':')
        if (colon < 0) throw new Error(`a parameter is not a name and a type: ${JSON.stringify(words)}`)
        const head = words.slice(0, colon).trim().split(/\s+/)
        const type = words.slice(colon + 1).trim()
        params.push({ name: head[head.length - 1], constant: head[0] === 'const', type })
    }
    return params
}

function splitTopLevel(text, separator) {
    const parts = []
    let depth = 0
    let start = 0
    for (let index = 0; index < text.length; index++) {
        const char = text[index]
        if (char === '(' || char === '[' || char === '<' || char === '{') depth++
        else if (char === ')' || char === ']' || char === '>' || char === '}') depth--
        else if (char === separator && depth === 0) {
            parts.push(text.slice(start, index))
            start = index + 1
        }
    }
    parts.push(text.slice(start))
    return parts
}

function matchingBrace(text, open) {
    let depth = 0
    for (let index = open; index < text.length; index++) {
        if (text[index] === '{') depth++
        else if (text[index] === '}') {
            depth--
            if (depth === 0) return index
        }
    }
    return -1
}

function readLibrary() {
    if (!fs.existsSync(LIBRARY)) fail(`the standard library is not at ${LIBRARY}`)
    const source = withoutComments(fs.readFileSync(LIBRARY, 'utf8'))
    const functions = new Map()
    const rules = []

    const funcPattern = /(?:^|\n)\s*(export\s+)?func\s+([A-Za-z_]\w*)\s*(::\s*<\s*(\w+)\s*>)?\s*\(/g
    for (let match = funcPattern.exec(source); match; match = funcPattern.exec(source)) {
        const open = source.indexOf('{', match.index + match[0].length)
        const close = open < 0 ? -1 : matchingBrace(source, open)
        if (close < 0) continue
        const body = source.slice(open + 1, close).trim()
        functions.set(match[2], {
            exported: Boolean(match[1]),
            typeParam: match[4] || null,
            params: parseParams(source.slice(match.index + match[0].length, open).replace(/\)\s*:\s*[\w?[\]]+\s*$/, '')),
            body
        })
        funcPattern.lastIndex = close
    }

    const rulePattern = /export\s+rule\s+([A-Za-z_]\w*)/g
    for (let match = rulePattern.exec(source); match; match = rulePattern.exec(source)) {
        const name = match[1]
        const open = source.indexOf('{', match.index + match[0].length)
        const close = open < 0 ? -1 : matchingBrace(source, open)
        if (close < 0) throw new Error(`the body of rule ${name} is not closed`)
        const head = source.slice(match.index + match[0].length, open)
        const before = source.slice(0, match.index)
        const requires = /#requires\s*\(([^)]*)\)\s*$/.exec(before)
        const licence = requires
            ? requires[1]
                  .split(',')
                  .map((word) => word.trim())
                  .filter(Boolean)
            : []
        const body = source.slice(open + 1, close)
        const typeParam = /^\s*::\s*<\s*(\w+)\s*>/.exec(head)
        const paramsOpen = head.indexOf('(')
        if (paramsOpen < 0) throw new Error(`rule ${name} has no parameter list`)
        let depth = 0
        let paramsClose = -1
        for (let index = paramsOpen; index < head.length; index++) {
            if (head[index] === '(') depth++
            else if (head[index] === ')') {
                depth--
                if (depth === 0) {
                    paramsClose = index
                    break
                }
            }
        }
        if (paramsClose < paramsOpen) throw new Error(`the parameter list of rule ${name} is not closed`)
        const params = parseParams(head.slice(paramsOpen + 1, paramsClose))
        const whenMatch = /^\s*when\s+([\s\S]*)$/.exec(head.slice(paramsClose + 1))
        rules.push({
            name,
            typeParam: typeParam ? typeParam[1] : null,
            params,
            licence,
            when: whenMatch ? whenMatch[1].trim() : null,
            body: body.trim(),
            source: `${name}` // only for messages
        })
    }
    return { source, functions, rules }
}

// The licence names of the language are read out of the core that declares them, so a licence added
// there without a row here is reported instead of being ignored.
function readLicenceNames() {
    if (!fs.existsSync(PERMISSIONS)) return { names: [], aliases: [] }
    const text = fs.readFileSync(PERMISSIONS, 'utf8')
    const names = []
    const table = /kPermissionNames\s*=\s*\{\{([\s\S]*?)\}\}/.exec(text)
    if (table) {
        for (const entry of table[1].matchAll(/\{\s*k\w+\s*,\s*"([\w_]+)"\s*\}/g)) names.push(entry[1])
    }
    const aliases = []
    for (const entry of text.matchAll(/name\s*==\s*"([\w_]+)"/g)) {
        if (!names.includes(entry[1])) aliases.push(entry[1])
    }
    return { names, aliases }
}

// ---- expressions ------------------------------------------------------------------------------

const SYMBOLS = [
    '<=>',
    '==',
    '!=',
    '>=',
    '<=',
    '&&',
    '||',
    '::',
    '=>',
    '+',
    '-',
    '*',
    '/',
    '!',
    '<',
    '>',
    '(',
    ')',
    '[',
    ']',
    ',',
    ':',
    '.',
    '?'
]

function tokenize(text) {
    const tokens = []
    let index = 0
    while (index < text.length) {
        const char = text[index]
        if (/\s/.test(char)) {
            index++
            continue
        }
        if (/[0-9]/.test(char) || (char === '.' && /[0-9]/.test(text[index + 1] || ''))) {
            let end = index
            while (end < text.length && /[0-9]/.test(text[end])) end++
            if (text[end] === '.') {
                end++
                while (end < text.length && /[0-9]/.test(text[end])) end++
            }
            if (text[end] === 'e' || text[end] === 'E') {
                let probe = end + 1
                if (text[probe] === '+' || text[probe] === '-') probe++
                if (/[0-9]/.test(text[probe] || '')) {
                    end = probe
                    while (end < text.length && /[0-9]/.test(text[end])) end++
                }
            }
            tokens.push({ type: 'number', text: text.slice(index, end) })
            index = end
            continue
        }
        if (/[A-Za-z_]/.test(char)) {
            let end = index
            while (end < text.length && /[A-Za-z0-9_]/.test(text[end])) end++
            tokens.push({ type: 'name', text: text.slice(index, end) })
            index = end
            continue
        }
        const symbol = SYMBOLS.find((candidate) => text.startsWith(candidate, index))
        if (!symbol) throw new Error(`a character in ${JSON.stringify(text)} is not an operator: ${char}`)
        tokens.push({ type: 'symbol', text: symbol })
        index += symbol.length
    }
    return tokens
}

function parseExpression(text) {
    const tokens = tokenize(text)
    let at = 0
    const peek = () => tokens[at]
    const take = () => tokens[at++]
    const isSymbol = (text) => peek() && peek().type === 'symbol' && peek().text === text

    function primary() {
        const token = take()
        if (!token) throw new Error('an expression ends early')
        if (token.type === 'number') {
            const node = { kind: 'number', text: token.text }
            return postfix(node)
        }
        if (token.type === 'name') {
            if (token.text === 'true' || token.text === 'false') return { kind: 'bool', value: token.text === 'true' }
            if (isSymbol('(')) {
                take()
                const args = []
                if (!isSymbol(')')) {
                    for (;;) {
                        args.push(disjunction())
                        if (isSymbol(',')) {
                            take()
                            continue
                        }
                        break
                    }
                }
                if (!isSymbol(')')) throw new Error(`a call to ${token.text} is not closed`)
                take()
                return postfix({ kind: 'call', name: token.text, typeArgs: [], args })
            }
            return postfix({ kind: 'name', text: token.text })
        }
        if (token.text === '(') {
            const inner = disjunction()
            if (!isSymbol(')')) throw new Error('a parenthesized expression is not closed')
            take()
            return postfix(inner)
        }
        if (token.text === '-') return { kind: 'call', name: 'neg', typeArgs: [], args: [unary()] }
        if (token.text === '!') return { kind: 'call', name: 'not', typeArgs: [], args: [unary()] }
        throw new Error(`an expression starts with ${JSON.stringify(token.text)}`)
    }

    // `f<axis>(x)` applies a type argument, `c < 0.0` compares. The two are told apart by looking
    // ahead: a type argument list holds only names and commas and is followed by a call. A guard full
    // of comparisons must not be read as a type application.
    function looksLikeTypeArguments() {
        if (!isSymbol('<')) return false
        let depth = 0
        for (let index = at; index < tokens.length; index++) {
            const token = tokens[index]
            if (token.type === 'number') return false
            if (token.text === '<') depth++
            else if (token.text === '>') {
                depth--
                if (depth === 0) {
                    const next = tokens[index + 1]
                    return Boolean(next && next.type === 'symbol' && next.text === '(')
                }
            } else if (token.text !== ',' && token.type !== 'name') return false
        }
        return false
    }

    // `<axis>` after a name, and `as TYPE` after a value: both are read and dropped, because the
    // first names an axis this script does not model and the second only restates the instance type.
    function postfix(node) {
        let current = node
        if (isSymbol('::') && tokens[at + 1] && tokens[at + 1].text === '<') {
            take()
            const typeArgs = typeArgumentList()
            if (!isSymbol('(')) throw new Error('a type application is not followed by a call')
            take()
            const args = []
            if (!isSymbol(')')) {
                for (;;) {
                    args.push(disjunction())
                    if (isSymbol(',')) {
                        take()
                        continue
                    }
                    break
                }
            }
            if (!isSymbol(')')) throw new Error('a call is not closed')
            take()
            current = { kind: 'call', name: current.text, typeArgs, args }
        } else if (current.kind === 'name' && looksLikeTypeArguments()) {
            const typeArgs = typeArgumentList()
            if (!isSymbol('(')) throw new Error('a type application is not followed by a call')
            take()
            const args = []
            if (!isSymbol(')')) {
                for (;;) {
                    args.push(disjunction())
                    if (isSymbol(',')) {
                        take()
                        continue
                    }
                    break
                }
            }
            if (!isSymbol(')')) throw new Error('a call is not closed')
            take()
            current = { kind: 'call', name: current.text, typeArgs, args }
        }
        if (peek() && peek().type === 'name' && peek().text === 'as') {
            take()
            while (peek() && peek().type === 'name') take()
            while (isSymbol('?') || isSymbol('[') || isSymbol(']')) take()
            current = { kind: 'cast', value: current }
        }
        return current
    }

    function typeArgumentList() {
        if (!isSymbol('<')) throw new Error('a type argument list does not start with <')
        take()
        const args = []
        let depth = 1
        let text = ''
        for (;;) {
            const token = take()
            if (!token) throw new Error('a type argument list is not closed')
            if (token.text === '<') depth++
            if (token.text === '>') {
                depth--
                if (depth === 0) break
            }
            if (depth === 1 && token.text === ',') {
                args.push(text.trim())
                text = ''
                continue
            }
            text += token.text
        }
        if (text.trim() !== '') args.push(text.trim())
        return args
    }

    function unary() {
        if (isSymbol('-')) {
            take()
            return { kind: 'call', name: 'neg', typeArgs: [], args: [unary()] }
        }
        if (isSymbol('!')) {
            take()
            return { kind: 'call', name: 'not', typeArgs: [], args: [unary()] }
        }
        return primary()
    }

    function product() {
        let left = unary()
        while (isSymbol('*') || isSymbol('/')) {
            const operator = take().text
            left = { kind: 'binary', operator, left, right: unary() }
        }
        return left
    }

    function sum() {
        let left = product()
        while (isSymbol('+') || isSymbol('-')) {
            const operator = take().text
            left = { kind: 'binary', operator, left, right: product() }
        }
        return left
    }

    function comparison() {
        let left = sum()
        while (['==', '!=', '<', '>', '<=', '>='].some((symbol) => isSymbol(symbol))) {
            const operator = take().text
            left = { kind: 'binary', operator, left, right: sum() }
        }
        return left
    }

    function conjunction() {
        let left = comparison()
        while (isSymbol('&&')) {
            take()
            left = { kind: 'binary', operator: '&&', left, right: comparison() }
        }
        return left
    }

    function disjunction() {
        let left = conjunction()
        while (isSymbol('||')) {
            take()
            left = { kind: 'binary', operator: '||', left, right: conjunction() }
        }
        return left
    }

    const expression = disjunction()
    if (at !== tokens.length) throw new Error(`an expression has a tail: ${JSON.stringify(tokens[at].text)}`)
    return expression
}

function substitute(node, bindings) {
    if (!node || typeof node !== 'object') return node
    if (node.kind === 'name' && bindings.has(node.text)) return bindings.get(node.text)
    const copy = { ...node }
    if (copy.left) copy.left = substitute(copy.left, bindings)
    if (copy.right) copy.right = substitute(copy.right, bindings)
    if (copy.value) copy.value = substitute(copy.value, bindings)
    if (copy.args) copy.args = copy.args.map((argument) => substitute(argument, bindings))
    return copy
}

function walk(node, visit) {
    visit(node)
    if (node.left) walk(node.left, visit)
    if (node.right) walk(node.right, visit)
    if (node.value) walk(node.value, visit)
    if (node.args) for (const argument of node.args) walk(argument, visit)
}

// ---- what a rule admits: the types it is instantiated at --------------------------------------

const UNIVERSE = ['int', 'i32', 'float', 'f32']
const BUILTIN_TYPE_PREDICATES = new Set(['is_integer', 'is_float', 'is_function'])
const BUILTIN_TYPE_ANSWERS = {
    is_integer: (type) => type === 'int' || type === 'i32',
    is_float: (type) => type === 'float' || type === 'f32',
    is_function: () => false
}

// `match type T { case float | f32 => true case _ => false }` is read as the table it is.
function readMatchType(text) {
    const open = text.indexOf('{')
    const close = text.lastIndexOf('}')
    if (open < 0 || close < open) throw new Error('a match has no arms')
    const arms = []
    let fallback = null
    for (const arm of text.slice(open + 1, close).matchAll(/case\s+([\w\s|]+?)\s*=>\s*([^\n]+?)(?=\n|$)/g)) {
        const patterns = arm[1]
            .split('|')
            .map((word) => word.trim())
            .filter(Boolean)
        const value = arm[2].trim()
        if (patterns.includes('_')) fallback = value
        else arms.push({ patterns, value })
    }
    return { arms, fallback }
}

function evaluateTypePredicate(node, type, functions, depth = 0) {
    if (depth > 12) throw new Error('a type predicate expands forever')
    if (node.kind === 'bool') return node.value
    if (node.kind === 'binary' && (node.operator === '&&' || node.operator === '||')) {
        const left = evaluateTypePredicate(node.left, type, functions, depth + 1)
        const right = evaluateTypePredicate(node.right, type, functions, depth + 1)
        return node.operator === '&&' ? left && right : left || right
    }
    if (node.kind === 'call' && node.name === 'not') return !evaluateTypePredicate(node.args[0], type, functions, depth + 1)
    if (node.kind === 'call' && BUILTIN_TYPE_PREDICATES.has(node.name)) {
        return BUILTIN_TYPE_ANSWERS[node.name](type)
    }
    if (node.kind === 'call' && functions.has(node.name)) {
        const body = functions.get(node.name).body.replace(/^return\s+/, '')
        if (!body.startsWith('match')) {
            const expression = parseExpression(body)
            return evaluateTypePredicate(substitute(expression, new Map([[parametersOf(functions, node.name).typeParam || 'T', { kind: 'type', text: type }]])), type, functions, depth + 1)
        }
        const { arms, fallback } = readMatchType(body)
        for (const arm of arms) if (arm.patterns.includes(type)) return arm.value === 'true'
        if (fallback === null) throw new Error(`a match has no arm for ${type}`)
        return fallback === 'true'
    }
    throw new Error('a guard conjunct is not a type predicate')
}

function parametersOf(functions, name) {
    return functions.get(name) || { typeParam: null, params: [] }
}

// `exact_limit::<T>()` is a match over the type returning a number; it is the only value-level
// function of the library whose answer depends on the instance.
function evaluateTypeMatch(text, type) {
    const { arms, fallback } = readMatchType(text)
    for (const arm of arms) if (arm.patterns.includes(type)) return arm.value
    if (fallback === null) throw new Error(`a match has no arm for ${type}`)
    return fallback
}

class Unsupported extends Error {}

function expand(node, functions, depth = 0) {
    if (depth > 24) throw new Unsupported('a helper function expands forever')
    if (node.kind === 'call') {
        const name = node.name
        if (name === 'neg' || name === 'not') return { ...node, args: node.args.map((a) => expand(a, functions, depth + 1)) }
        if (functions.has(name)) {
            const definition = functions.get(name)
            const body = definition.body.trim()
            // A function whose body is a `match` over the type is a value that depends on the
            // instance (`exact_limit`); the translation reads it per instance instead of expanding.
            if (body.replace(/^return\s+/, '').startsWith('match')) {
                return { ...node, args: node.args.map((argument) => expand(argument, functions, depth + 1)) }
            }
            if (!body.startsWith('return ')) throw new Unsupported(`the body of ${name} is not one expression`)
            const expression = parseExpression(body.slice('return '.length))
            const bindings = new Map()
            definition.params.forEach((parameter, index) => {
                if (index < node.args.length) bindings.set(parameter.name, node.args[index])
            })
            const replaced = substitute(expression, bindings)
            if (replaced.kind === 'name' && replaced.text === definition.typeParam) {
                throw new Unsupported(`${name} is a type predicate`)
            }
            return expand(replaced, functions, depth + 1)
        }
        if (['abs', 'trunc', 'float', 'exact_limit', 'log', 'exp', 'log1p', 'expm1', 'log_array', 'exp_array', 'log_softmax', 'softmax', 'sigmoid', 'sqrt', 'hypot'].includes(name)) {
            return { ...node, args: node.args.map((a) => expand(a, functions, depth + 1)) }
        }
        throw new Unsupported(`the function '${name}' is not one this check can translate`)
    }
    const copy = { ...node }
    if (copy.left) copy.left = expand(copy.left, functions, depth + 1)
    if (copy.right) copy.right = expand(copy.right, functions, depth + 1)
    if (copy.value) copy.value = expand(copy.value, functions, depth + 1)
    return copy
}

// ---- the SMT translation ----------------------------------------------------------------------

const FORMATS = {
    int: { model: 'bv', width: 64, sort: '(_ BitVec 64)' },
    i32: { model: 'bv', width: 32, sort: '(_ BitVec 32)' },
    float: { model: 'fp', exponent: 11, significand: 53, sort: '(_ FloatingPoint 11 53)' },
    f32: { model: 'fp', exponent: 8, significand: 24, sort: '(_ FloatingPoint 8 24)' }
}

const TRANSCENDENTAL = new Set(['log', 'exp', 'log1p', 'expm1', 'log_array', 'exp_array', 'log_softmax', 'softmax', 'sigmoid', 'sqrt', 'hypot'])

function decimalBits(value, exponent, significand) {
    const buffer = new DataView(new ArrayBuffer(8))
    let bits
    if (exponent === 11 && significand === 53) {
        buffer.setFloat64(0, value)
        bits = buffer.getBigUint64(0)
    } else if (exponent === 8 && significand === 24) {
        buffer.setFloat32(0, value)
        const back = buffer.getFloat32(0)
        const same = Object.is(back, value) || (back === 0 && value === 0 && Object.is(back + 0, value + 0))
        if (!same) throw new Unsupported(`${value} is not exactly representable as f32`)
        bits = BigInt(buffer.getUint32(0))
    } else {
        throw new Unsupported(`the ${exponent}/${significand} floating format has no literal reader`)
    }
    const text = bits.toString(2).padStart(exponent + significand, '0')
    return {
        sign: text.slice(0, 1),
        exponent: text.slice(1, 1 + exponent),
        significand: text.slice(1 + exponent)
    }
}

function floatLiteral(text, format) {
    const value = Number(text)
    if (Number.isNaN(value)) throw new Unsupported(`${text} is not a number this check can read`)
    const parts = decimalBits(Object.is(value, -0) ? -0 : value, format.exponent, format.significand)
    return `(fp #b${parts.sign} #b${parts.exponent} #b${parts.significand})`
}

function integerLiteral(text, width) {
    const value = BigInt(text)
    if (value < 0n) return `(bvneg (_ bv${-value} ${width}))`
    if (value >= 1n << BigInt(width)) throw new Unsupported(`${text} does not fit the ${width}-bit width`)
    return `(_ bv${value} ${width})`
}

function realLiteral(text) {
    const value = Number(text)
    if (!Number.isFinite(value)) throw new Unsupported(`${text} is not a real literal`)
    if (Number.isInteger(value)) return `${value}.0`
    return `${text}`
}

// The translation of one side in one model. `types` maps a parameter name to its element type; the
// element type decides the model. Nothing is approximated: a construct without a translation throws
// `Unsupported` with the reason, and the rule becomes `unknown`.
function translate(node, context) {
    const { model, format, types } = context
    if (node.kind === 'number') {
        if (model === 'bv') return integerLiteral(node.text, format.width)
        if (model === 'fp') return floatLiteral(node.text, format)
        return realLiteral(node.text)
    }
    if (node.kind === 'cast') return translate(node.value, context)
    if (node.kind === 'bool') throw new Unsupported('a boolean literal where a value is expected')
    if (node.kind === 'name') {
        const type = types.get(node.text)
        if (!type) throw new Unsupported(`'${node.text}' is not a parameter of this rule`)
        return smtName(node.text)
    }
    if (node.kind === 'call') {
        const name = node.name
        if (name === 'neg') {
            const value = translate(node.args[0], context)
            return model === 'fp' ? `(fp.neg ${value})` : model === 'bv' ? `(bvneg ${value})` : `(- ${value})`
        }
        if (name === 'not') {
            const value = translateBoolean(node.args[0], context)
            return `(not ${value})`
        }
        if (TRANSCENDENTAL.has(name)) {
            throw new Unsupported(`the ${name} kernel is transcendental, so no SMT theory states its value`)
        }
        if (name === 'abs') {
            const value = translate(node.args[0], context)
            if (model === 'fp') return `(fp.abs ${value})`
            if (model === 'real') return `(ite (<= ${value} 0.0) (- ${value}) ${value})`
            throw new Unsupported('abs has no integer reading')
        }
        if (name === 'trunc') {
            if (model !== 'fp') throw new Unsupported('the integrality a trunc asserts is not a real predicate')
            return `(fp.roundToIntegral RTZ ${translate(node.args[0], context)})`
        }
        if (name === 'float') {
            if (model !== 'fp') throw new Unsupported('a widening conversion has no real reading')
            return `((_ to_fp 11 53) RNE ${translate(node.args[0], context)})`
        }
        if (name === 'exact_limit') {
            const text = context.typeMatch('exact_limit')
            // `exact_limit` is declared returning `float`, so its value is a 64-bit float whatever the
            // instance type is; the guard widens the coefficient before comparing with it.
            if (model === 'fp') return floatLiteral(text, FORMATS.float)
            return realLiteral(text)
        }
        throw new Unsupported(`the function '${name}' is outside the translated operators`)
    }
    if (node.kind === 'binary') {
        const left = translate(node.left, context)
        const right = translate(node.right, context)
        switch (node.operator) {
            case '+':
                return model === 'fp' ? `(fp.add RNE ${left} ${right})` : model === 'bv' ? `(bvadd ${left} ${right})` : `(+ ${left} ${right})`
            case '-':
                return model === 'fp' ? `(fp.sub RNE ${left} ${right})` : model === 'bv' ? `(bvsub ${left} ${right})` : `(- ${left} ${right})`
            case '*':
                return model === 'fp' ? `(fp.mul RNE ${left} ${right})` : model === 'bv' ? `(bvmul ${left} ${right})` : `(* ${left} ${right})`
            case '/':
                if (model === 'bv') {
                    context.premises.push(`(not (= ${right} (_ bv0 ${format.width})))`)
                    return `(bvsdiv ${left} ${right})`
                }
                if (model === 'fp') return `(fp.div RNE ${left} ${right})`
                context.premises.push(`(not (= ${right} 0.0))`)
                return `(/ ${left} ${right})`
            default:
                throw new Unsupported(`the operator ${node.operator} is not arithmetic`)
        }
    }
    throw new Unsupported('a value this check cannot translate')
}

function translateBoolean(node, context) {
    const { model } = context
    if (node.kind === 'bool') return node.value ? 'true' : 'false'
    if (node.kind === 'call' && node.name === 'not') return `(not ${translateBoolean(node.args[0], context)})`
    if (node.kind === 'binary') {
        const operator = node.operator
        if (operator === '&&' || operator === '||') {
            const symbol = operator === '&&' ? 'and' : 'or'
            return `(${symbol} ${translateBoolean(node.left, context)} ${translateBoolean(node.right, context)})`
        }
        if (['==', '!=', '<', '>', '<=', '>='].includes(operator)) {
            if (operator === '==' || operator === '!=') {
                // `(c1 > 0.0) == (c2 > 0.0)` compares two predicates; `c != 0.0` compares two values.
                // A predicate is tried first, and only a translation that refuses to be a predicate
                // falls back to the equality of two values.
                let left
                let right
                try {
                    left = translateBoolean(node.left, context)
                    right = translateBoolean(node.right, context)
                    return operator === '==' ? `(= ${left} ${right})` : `(not (= ${left} ${right}))`
                } catch (problem) {
                    if (!(problem instanceof Unsupported)) throw problem
                }
                left = translate(node.left, context)
                right = translate(node.right, context)
                const equal = model === 'fp' ? `(fp.eq ${left} ${right})` : `(= ${left} ${right})`
                return operator === '==' ? equal : `(not ${equal})`
            }
            const left = translate(node.left, context)
            const right = translate(node.right, context)
            const symbols = model === 'fp'
                ? { '<': 'fp.lt', '>': 'fp.gt', '<=': 'fp.leq', '>=': 'fp.geq' }
                : model === 'bv'
                  ? { '<': 'bvslt', '>': 'bvsgt', '<=': 'bvsle', '>=': 'bvsge' }
                  : { '<': '<', '>': '>', '<=': '<=', '>=': '>=' }
            return `(${symbols[operator]} ${left} ${right})`
        }
        throw new Unsupported(`the operator ${operator} is not a predicate`)
    }
    throw new Unsupported('a premise this check cannot translate')
}

// A parameter name may be an SMT keyword; both sides of a rule use the same map, so the name only
// has to be stable, and a prefix keeps it away from the solver's own vocabulary.
function smtName(name) {
    return `p_${name}`
}

// ---- the proposition of a rule ----------------------------------------------------------------

// The clause of the licence that decides what is being proved, and the premise it puts into the
// query. A licence this table does not map is reported `unknown` with the reason, never dropped.
const LICENCE_TABLE = new Map([
    [
        'reassociate',
        {
            model: 'real',
            premise:
                'the arithmetic is read over R, so an operand is its exact value and no operand is rounded; ' +
                'the domain of the model is the finite values, which discharges the guards\' finiteness conjuncts'
        }
    ],
    [
        'no_infs',
        {
            model: 'real',
            premise: 'no intermediate value is an infinity: an intermediate product or sum is taken in R, so removing an overflow is not a difference'
        }
    ],
    [
        'no_signed_zeros',
        {
            model: 'real',
            premise: 'a zero has no sign: R has one zero, so a rule whose only difference is the sign of a zero is not refuted'
        }
    ],
    [
        'refine_accuracy',
        {
            model: 'error-bound',
            premise: 'error(right) <= error(left) + bound in ulps, which is not an equality and is not decided here'
        }
    ],
    [
        'contract',
        {
            mapped: false,
            reason:
                'no rule of the library asks for it; a fused multiply-add changes what is rounded, and its ' +
                'proposition would be a rounding-path proposition, which is `reassociate` in the library'
        }
    ],
    [
        'approx_math',
        {
            mapped: false,
            reason:
                'no rule of the library asks for it; an approximate math function replaces the value of a kernel, ' +
                'so the two sides are two different functions and no equality between them is claimed'
        }
    ],
    [
        'change_precision',
        {
            mapped: false,
            reason:
                'no rule of the library asks for it; the proposition is a relative error bound of 2^-8 for bf16 ' +
                'against a wider type, an error bound rather than an equality between the rule\'s two sides'
        }
    ],
    [
        'nondeterministic',
        {
            mapped: false,
            reason:
                'the value may depend on the schedule, so no equality between the two sides holds for every ' +
                'input; such a rule is measured by sampling, and mixing the two verdicts is what must not happen'
        }
    ],
    [
        'flush_subnormals',
        {
            mapped: false,
            reason:
                'subnormal values are not modelled apart in the real reading; a rule that asks for it would need ' +
                'the subnormal grid as a premise, and no rule of the library asks for it'
        }
    ],
    [
        'no_nans',
        {
            mapped: false,
            reason:
                'the SMT floating-point theory has one NaN, so the bit view already agrees on NaN inputs and this ' +
                'assumption would remove nothing; no rule of the library asks for it'
        }
    ],
    [
        'unordered_reduce',
        {
            mapped: false,
            reason: 'it grants a reduction order, not a numeric assumption; no rule of the library asks for it'
        }
    ],
    [
        'finite_math',
        {
            mapped: false,
            reason:
                'it is the conjunction of no_nans, no_infs, no_signed_zeros and flush_subnormals, which the library ' +
                'spells individually instead; a rule that asks for it would be mapped to the same four premises'
        }
    ]
])

// The model a rule is decided in: the strongest premise set its licence adds.
function modelOf(rule) {
    const models = []
    const reasons = []
    for (const licence of rule.licence) {
        const row = LICENCE_TABLE.get(licence)
        if (!row) {
            reasons.push(`the licence '${licence}' has no row in the licence table of this check`)
            continue
        }
        if (row.mapped === false) {
            reasons.push(`the licence '${licence}' is not mapped: ${row.reason}`)
            continue
        }
        models.push(row.model)
    }
    if (reasons.length > 0) return { model: null, reason: reasons.join('; ') }
    if (models.includes('error-bound')) {
        return { model: null, reason: LICENCE_TABLE.get('refine_accuracy').premise }
    }
    return { model: models.includes('real') ? 'real' : 'bits', reason: null }
}

function instancesOf(rule, functions) {
    let types = UNIVERSE
    const conjuncts = flattenConjunction(rule.when ? parseExpression(rule.when) : null)
    const valueConjuncts = []
    for (const conjunct of conjuncts) {
        let answers = null
        try {
            answers = UNIVERSE.filter((type) => evaluateTypePredicate(conjunct, type, functions))
        } catch (problem) {
            valueConjuncts.push(conjunct)
            continue
        }
        if (answers.length > 0) types = types.filter((type) => answers.includes(type))
    }
    // A parameter of a concrete type pins the instance as well (`x: f32[..]` is an f32 rule). A `const`
    // parameter never does: it is a coefficient or an axis, a value the rule matches, not the element
    // type.
    for (const parameter of rule.params) {
        if (parameter.constant) continue
        const element = parameter.type.replace(/\[\s*\.\.\s*\]$/, '').trim()
        if (!rule.typeParam || element !== rule.typeParam) {
            if (FORMATS[element]) types = types.filter((type) => type === element)
        }
    }
    return { types, valueConjuncts }
}

function flattenConjunction(node) {
    if (!node) return []
    if (node.kind === 'binary' && node.operator === '&&') {
        return [...flattenConjunction(node.left), ...flattenConjunction(node.right)]
    }
    return [node]
}

// ---- one query ---------------------------------------------------------------------------------

function runQuery(solver, text, timeoutMs) {
    const result = spawnSync(solver.command, ['-smt2', '-in', `-t:${timeoutMs}`], {
        input: text,
        encoding: 'utf8',
        timeout: Math.max(60000, timeoutMs * 3),
        maxBuffer: 32 * 1024 * 1024
    })
    if (result.error) return { answers: [], text: '', stderr: String(result.error.message), errors: [String(result.error.message)] }
    const output = result.stdout || ''
    const lines = output.split('\n').map((line) => line.trim())
    const answers = lines.filter((line) => line === 'sat' || line === 'unsat' || line === 'unknown')
    // A solver error means the query was not read as written -- an operator it does not know, a sort
    // mismatch. An answer next to an error is not an answer, and a checker that took one would report
    // a sat over an unconstrained query.
    const errors = [...output.matchAll(/\(error "([^"]*)"\)/g)].map((entry) => entry[1])
    return { answers, text: output, stderr: result.stderr || '', errors }
}

function declare(name, type, real) {
    const format = FORMATS[type]
    if (!format) throw new Unsupported(`the type ${type} has no SMT sort`)
    return `(declare-const ${smtName(name)} ${real ? 'Real' : format.sort})`
}

function decide(solver, query, timeoutMs) {
    const started = Date.now()
    const result = runQuery(solver, query, timeoutMs)
    const seconds = (Date.now() - started) / 1000
    if (result.errors.length > 0) return { answer: 'error', detail: result.errors[0], seconds, raw: result }
    const answer = result.answers[result.answers.length - 1]
    return { answer: answer || 'no-answer', seconds, raw: result }
}

// A rule instance: declare its parameters, then ask two questions -- are the premises satisfiable
// (a rule whose premises are unsatisfiable can never fire, and answering `proved` for it would be
// vacuous), and is the proposition's negation unsatisfiable.
function proveInstance(solver, rule, functions, type, valueConjuncts, timeoutMs, options) {
    const model = options.model
    const smtModel = model === 'real' ? 'real' : FORMATS[type].model
    const format = model === 'real' ? { model: 'real', exponent: 11, significand: 53 } : FORMATS[type]
    const types = new Map()
    const declarations = []
    for (const parameter of rule.params) {
        const element = parameter.type.replace(/\[\s*\.\.\s*\]$/, '').trim()
        const elementType = FORMATS[element] ? element : type
        types.set(parameter.name, elementType)
        declarations.push(declare(parameter.name, elementType, smtModel === 'real'))
    }
    const context = {
        model: smtModel,
        format,
        types,
        premises: [],
        typeMatch: (name) => {
            const definition = functions.get(name)
            if (!definition) throw new Unsupported(`the library has no ${name}`)
            return evaluateTypeMatch(definition.body.replace(/^return\s+/, ''), type)
        }
    }
    let premises
    let left
    let right
    try {
        premises = valueConjuncts.map((conjunct) => translateBoolean(expand(conjunct, functions), context))
        const split = splitSides(rule.body)
        left = translate(expand(parseExpression(split.left), functions), context)
        right = translate(expand(parseExpression(split.right), functions), context)
        premises = [...premises, ...context.premises]
    } catch (problem) {
        if (problem instanceof Unsupported) return { verdict: 'unknown', reason: problem.message, queries: 0, seconds: 0 }
        return { verdict: 'unknown', reason: `the rule could not be read: ${problem.message}`, queries: 0, seconds: 0 }
    }
    const logic = smtModel === 'bv' ? 'QF_BV' : smtModel === 'fp' ? 'QF_FP' : 'QF_NRA'
    const assertion =
        smtModel === 'fp' ? `(not (= (fp.to_ieee_bv ${left}) (fp.to_ieee_bv ${right})))` : `(not (= ${left} ${right}))`
    const head = [`(set-logic ${logic})`, `(set-option :produce-models true)`, ...declarations, ...premises.map((premise) => `(assert ${premise})`)]
    const satisfiable = decide(solver, [...head, '(check-sat)', '(exit)'].join('\n'), timeoutMs)
    if (satisfiable.answer === 'unknown' || satisfiable.answer === 'no-answer') {
        return {
            verdict: 'unknown',
            reason: `the premise set of ${type} was left open by the solver (${satisfiable.answer}, ${satisfiable.seconds.toFixed(1)} s)`,
            queries: 1,
            seconds: satisfiable.seconds
        }
    }
    if (satisfiable.answer === 'unsat') {
        return {
            verdict: 'unknown',
            reason: `the premises of the ${type} instance are unsatisfiable, so the rule can never fire and a proof would be vacuous`,
            queries: 1,
            seconds: satisfiable.seconds
        }
    }
    const proposition = decide(solver, [...head, `(assert ${assertion})`, '(check-sat)', '(exit)'].join('\n'), timeoutMs)
    if (options.dump) console.log([...head, `(assert ${assertion})`, '(check-sat)'].join('\n'))
    const seconds = satisfiable.seconds + proposition.seconds
    if (proposition.answer === 'unsat') return { verdict: 'proved', reason: null, queries: 2, seconds }
    if (proposition.answer === 'error') {
        return {
            verdict: 'unknown',
            reason: `the solver did not read the ${type} query: ${proposition.detail}`,
            queries: 2,
            seconds
        }
    }
    if (proposition.answer === 'unknown' || proposition.answer === 'no-answer') {
        return {
            verdict: 'unknown',
            reason: `the solver left the ${type} query open (${proposition.answer} after ${proposition.seconds.toFixed(1)} s of a ${(timeoutMs / 1000).toFixed(0)} s budget)`,
            queries: 2,
            seconds
        }
    }
    const names = [...types.keys()]
    const readback = smtModel === 'fp' ? names.map((name) => `(fp.to_ieee_bv ${smtName(name)})`) : names.map(smtName)
    const base = [...head, `(assert ${assertion})`, '(check-sat)', `(get-value (${readback.join(' ')}))`, '(exit)'].join('\n')
    let witness = readModel(solver, base, timeoutMs, types, smtModel)
    // A model that puts a parameter at zero, or at a value with eighteen digits, is a counterexample but
    // a hard one to read. When the caller asks for a readable one, the same query is asked again with
    // "every parameter is small and not zero" added; that narrows which model is *displayed*, and the
    // verdict stays `refuted` either way. If the narrowed query has no model, the first one stands.
    if (options.readableWitness) {
        const bounds = names.map((name) => {
            const format = FORMATS[types.get(name)]
            const zero = smtModel === 'real' ? '0.0' : format.model === 'bv' ? `(_ bv0 ${format.width})` : floatLiteral('0.0', format)
            const nonzero = `(assert (not (= ${smtName(name)} ${zero})))`
            if (smtModel === 'real') return `${nonzero}(assert (and (<= ${smtName(name)} 4.0) (>= ${smtName(name)} (- 4.0))))`
            if (format.model === 'bv') {
                const limit = `(_ bv4 ${format.width})`
                return `${nonzero}(assert (and (bvsle ${smtName(name)} ${limit}) (bvsge ${smtName(name)} (bvneg ${limit}))))`
            }
            return nonzero
        })
        const readable = readModel(
            solver,
            [...head, `(assert ${assertion})`, ...bounds, '(check-sat)', `(get-value (${readback.join(' ')}))`, '(exit)'].join('\n'),
            timeoutMs,
            types,
            smtModel
        )
        if (readable.size > 0) witness = readable
    }
    return { verdict: 'refuted', reason: null, queries: 3, seconds, witness }
}

function splitSides(body) {
    let depth = 0
    for (let index = 0; index < body.length; index++) {
        if (depth === 0) {
            if (body.startsWith('<=>', index)) {
                return { left: body.slice(0, index).trim(), right: body.slice(index + 3).trim(), both: true }
            }
            if (body.startsWith('=>', index)) {
                return { left: body.slice(0, index).trim(), right: body.slice(index + 2).trim(), both: false }
            }
        }
        const char = body[index]
        if ('([{<'.includes(char)) depth++
        else if (')]}>'.includes(char)) depth--
    }
    throw new Unsupported('the rule has no => between its two sides')
}

// The model Z3 returns is printed as the values of the rule's parameters, in the type's own
// reading: a bit pattern becomes the signed integer or the decimal it stands for. The answer of
// `get-value` is a list of `(<term> <value>)` pairs in the order the terms were asked for, so it is
// read positionally -- a value is never attributed to the wrong parameter.
function readModel(solver, query, timeoutMs, types, smtModel) {
    const result = runQuery(solver, query, timeoutMs)
    const rendered = new Map()
    const forms = readForms(result.text)
    const last = forms.reverse().find((form) => Array.isArray(form) && form.every((entry) => Array.isArray(entry) && entry.length === 2))
    if (!last) return rendered
    const names = [...types.keys()]
    names.forEach((name, index) => {
        const pair = last[index]
        if (!pair) return
        rendered.set(name, renderValue(String(pair[1]), FORMATS[types.get(name)], smtModel))
    })
    return rendered
}

// A reader for the s-expressions a solver prints, used so that a value is taken whole (an `fp`
// literal is a list of its own) instead of by a pattern that guesses where it ends.
function readForms(text) {
    const forms = []
    let index = 0
    function skipSpace() {
        while (index < text.length && /\s/.test(text[index])) index++
    }
    function read() {
        skipSpace()
        if (index >= text.length) return null
        if (text[index] === '(') {
            index++
            const list = []
            for (;;) {
                skipSpace()
                if (index >= text.length) return list
                if (text[index] === ')') {
                    index++
                    return list
                }
                const item = read()
                if (item === null) return list
                list.push(item)
            }
        }
        if (text[index] === ')') {
            index++
            return null
        }
        let start = index
        while (index < text.length && !/[\s()]/.test(text[index])) index++
        const token = text.slice(start, index)
        return token === '' ? null : token
    }
    for (;;) {
        skipSpace()
        if (index >= text.length) break
        const form = read()
        if (form === null) {
            index++
            continue
        }
        if (typeof form === 'string') continue // a bare `sat`, `unsat`, `unknown` or `(error ...)`
        forms.push(form)
    }
    return forms
}

function renderValue(text, format, smtModel) {
    if (text.startsWith('#x')) {
        const bits = BigInt(`0x${text.slice(2)}`)
        if (smtModel === 'bv' || format.model === 'bv') {
            const width = BigInt(format.width)
            const half = 1n << (width - 1n)
            const signed = bits >= half ? bits - (1n << width) : bits
            return signed.toString()
        }
        return decodeFloat(bits, format)
    }
    if (text.startsWith('(fp ')) return text
    return text
}

// The bit pattern of the value a refuted query found, as the decimal the type reads as, so that a
// counterexample can be written down without decoding it by hand.
function decodeFloat(bits, format) {
    const buffer = new DataView(new ArrayBuffer(8))
    if (format.exponent === 11 && format.significand === 53) {
        buffer.setBigUint64(0, bits)
        return `${buffer.getFloat64(0)} (0x${bits.toString(16)})`
    }
    if (format.exponent === 8 && format.significand === 24) {
        buffer.setUint32(0, Number(bits))
        return `${buffer.getFloat32(0)} (0x${bits.toString(16)})`
    }
    return `0x${bits.toString(16)}`
}

// ---- the report --------------------------------------------------------------------------------

function pad(text, width) {
    return text.length >= width ? text : text + ' '.repeat(width - text.length)
}

function main() {
    const argv = process.argv.slice(2)
    const requireSolver = argv.includes('--require-solver')
    const probe = argv.includes('--probe-solver')
    const verbose = argv.includes('--verbose')
    const dump = argv.includes('--dump')
    const onlyIndex = argv.indexOf('--only')
    const only = onlyIndex >= 0 ? argv[onlyIndex + 1] : null
    const timeoutIndex = argv.indexOf('--timeout')
    const timeoutMs =
        timeoutIndex >= 0 && argv[timeoutIndex + 1] ? Number(argv[timeoutIndex + 1]) : Number(process.env.CAMEL_Z3_TIMEOUT_MS || 40000)

    const library = readLibrary()
    const licences = readLicenceNames()
    const solver = findSolver()
    // `--probe-solver` is the same discovery asked as a yes/no question, so a test plan can skip on
    // the answer without carrying a second copy of the search order. Its exit code is the answer and
    // its output is the reason a skip shows.
    if (probe) {
        console.log(
            solver
                ? `rule-proofs: solver ${solver.version} at ${solver.command}`
                : 'rule-proofs: no SMT solver was found: looked for $CAMEL_Z3, `z3` on PATH, $HOME/.local/bin/z3, /usr/local/bin/z3, /usr/bin/z3; install one with `pip install z3-solver`, or point CAMEL_Z3 at it'
        )
        process.exit(solver ? 0 : 1)
    }
    if (!solver) {
        console.log(
            'rule-proofs: SKIPPED: no SMT solver was found, so no rule was decided (this is not a pass).\n' +
                '  looked for $CAMEL_Z3, `z3` on PATH, $HOME/.local/bin/z3, /usr/local/bin/z3, /usr/bin/z3\n' +
                '  install one with `pip install z3-solver` (the `z3` binary lands in the venv), or point CAMEL_Z3 at it\n' +
                '  a plan that needs it skips on `--probe-solver` and prints this same reason; `--require-solver` exits 1'
        )
        process.exit(requireSolver ? 1 : 0)
    }
    console.log(`rule-proofs: solver ${solver.version} at ${solver.command}`)

    // The licence table, as the report of the mapping: every name the core declares gets a line, so a
    // licence without a premise is visible instead of missing.
    const declared = [...licences.names, ...licences.aliases]
    const used = new Set(library.rules.flatMap((rule) => rule.licence))
    console.log(`rule-proofs: licences of the library: ${[...used].sort().join(', ') || '(none)'}; the core declares ${declared.length}`)
    for (const name of declared) {
        const row = LICENCE_TABLE.get(name)
        if (!row) {
            console.log(`  unmapped ${pad(name, 18)} the licence table has no row: report it, do not skip it`)
            continue
        }
        if (row.mapped === false) console.log(`  unmapped ${pad(name, 18)} ${row.reason}`)
        else console.log(`  mapped   ${pad(name, 18)} ${row.premise}`)
    }
    for (const name of used) {
        if (!declared.includes(name)) {
            console.log(`  unmapped ${pad(name, 18)} the library asks for it and the core does not declare it`)
        }
    }

    // The sanity block: four ground answers that a faithful translation must give. It runs before a
    // single rule is decided, because a translation that stopped distinguishing the two zeros, the
    // wraparound or the rounding would report every rule `proved` and look like a success.
    const sanity = [
        {
            what: 'the bit view of a float keeps +0.0 and -0.0 apart',
            query: '(set-logic QF_FP)\n(assert (= (fp.to_ieee_bv (fp #b0 #b00000000000 #b0000000000000000000000000000000000000000000000000000)) (fp.to_ieee_bv (fp #b1 #b00000000000 #b0000000000000000000000000000000000000000000000000000))))\n(check-sat)\n',
            expect: 'unsat'
        },
        {
            what: 'the bit view of +0.0 is all zeroes',
            query: '(set-logic QF_FP)\n(assert (not (= (fp.to_ieee_bv (fp #b0 #b00000000000 #b0000000000000000000000000000000000000000000000000000)) #x0000000000000000)))\n(check-sat)\n',
            expect: 'unsat'
        },
        {
            what: 'integer addition wraps at the width',
            query: '(set-logic QF_BV)\n(assert (not (= (bvadd #xffffffffffffffff #x0000000000000001) #x0000000000000000)))\n(check-sat)\n',
            expect: 'unsat'
        },
        {
            what: 'the float model rounds (1.0 + 2^-53 is 1.0)',
            query: '(set-logic QF_FP)\n(assert (not (fp.eq (fp.add RNE (fp #b0 #b01111111111 #b0000000000000000000000000000000000000000000000000000) (fp #b0 #b01111001010 #b0000000000000000000000000000000000000000000000000000)) (fp #b0 #b01111111111 #b0000000000000000000000000000000000000000000000000000))))\n(check-sat)\n',
            expect: 'unsat'
        }
    ]
    for (const check of sanity) {
        const answer = decide(solver, check.query, timeoutMs)
        if (answer.answer !== check.expect) {
            fail(
                `the translation is not faithful: the sanity query "${check.what}" answered ${answer.answer} ` +
                    `where ${check.expect} is the only right answer, so no verdict below can be trusted`
            )
        }
    }
    console.log(`rule-proofs: sanity ok (${sanity.length} ground queries)`)

    const verdicts = []
    let queries = 0
    for (const rule of library.rules) {
        if (only && rule.name !== only) continue
        const decided = modelOf(rule)
        const instantiation = instancesOf(rule, library.functions)
        const perInstance = []
        if (!decided.model) {
            for (const type of instantiation.types) {
                perInstance.push({ type, verdict: 'unknown', reason: decided.reason, queries: 0, seconds: 0 })
            }
            if (perInstance.length === 0) {
                perInstance.push({ type: '-', verdict: 'unknown', reason: decided.reason, queries: 0, seconds: 0 })
            }
        } else {
            for (const type of instantiation.types) {
                const outcome = proveInstance(solver, rule, library.functions, type, instantiation.valueConjuncts, timeoutMs, {
                    model: decided.model,
                    readableWitness: true,
                    dump
                })
                perInstance.push({ type, ...outcome })
                queries += outcome.queries
            }
            if (perInstance.length === 0) {
                perInstance.push({ type: '-', verdict: 'unknown', reason: 'the guard admits no type of the translated four', queries: 0, seconds: 0 })
            }
        }
        const verdict = perInstance.some((entry) => entry.verdict === 'refuted')
            ? 'refuted'
            : perInstance.some((entry) => entry.verdict === 'unknown')
              ? 'unknown'
              : 'proved'
        verdicts.push({ rule, verdict, perInstance, model: decided.model, licence: rule.licence.join('+') || '(none)' })
        if (verbose) {
            for (const entry of perInstance) {
                console.log(`  ${pad(rule.name, 30)} ${pad(entry.type, 6)} ${pad(entry.verdict, 8)} ${entry.reason || ''} ${entry.seconds ? `${entry.seconds.toFixed(1)} s` : ''}`)
            }
        }
    }

    // ---- the falsification pair --------------------------------------------------------------
    // The same query, run twice on a library that differs by one operator. If the second reading is
    // not `proved`, the machinery is not deciding anything; if the first is not `refuted`, it is not
    // finding anything. When the library's int_combine no longer reads `(c1 + c2)` there is nothing
    // to break, and that is said out loud instead of being a pass.
    const mutated = library.rules.find((rule) => rule.name === 'int_combine')
    if (!mutated) fail('the library no longer holds int_combine, which is the rule this check breaks on purpose')
    const brokenBody = mutated.body.replace('(c1 + c2)', '(c1 - c2)')
    let falsification = 'falsification: skipped, int_combine no longer reads `(c1 + c2)`, so there is nothing to break'
    if (brokenBody !== mutated.body) {
        const broken = { ...mutated, body: brokenBody }
        const brokenInstantiation = instancesOf(broken, library.functions)
        const brokenType = brokenInstantiation.types[0]
        const brokenOutcome = proveInstance(solver, broken, library.functions, brokenType, brokenInstantiation.valueConjuncts, timeoutMs, {
            model: 'bits',
            readableWitness: true
        })
        queries += brokenOutcome.queries
        const restored = verdicts.find((entry) => entry.rule.name === 'int_combine')
        if (brokenOutcome.verdict !== 'refuted') {
            fail(
                `the deliberately broken int_combine (x * c1 + x * c2 => x * (c1 - c2)) came back ${brokenOutcome.verdict}` +
                    `${brokenOutcome.reason ? `: ${brokenOutcome.reason}` : ''}; a checker that cannot refute it reports nothing`
            )
        }
        if (restored && restored.verdict !== 'proved') {
            fail(`the library's int_combine came back ${restored.verdict} where the restored rule must be proved`)
        }
        const model = [...brokenOutcome.witness.entries()].map(([name, value]) => `${name} = ${value}`).join(', ')
        const restoredTypes = restored ? restored.perInstance.map((instance) => instance.type).join(' ') : 'the types the guard admits'
        falsification =
            `falsification: int_combine broken to (x * c1 + x * c2 => x * (c1 - c2)) is refuted at ${brokenType} (${model}); ` +
            `the library's int_combine is proved at ${restoredTypes}`
    }

    // ---- the table ---------------------------------------------------------------------------
    const width = Math.max(...verdicts.map((entry) => entry.rule.name.length), 4)
    console.log('')
    console.log(`${pad('verdict', 9)} ${pad('rule', width)} ${pad('licence', 36)} ${pad('proposition', 12)} instances`)
    for (const entry of verdicts) {
        const proposition = entry.model === 'bits' ? 'bits' : entry.model === 'real' ? 'real equality' : 'error bound'
        const instances = entry.perInstance.map((instance) => `${instance.type}${instance.verdict === 'proved' ? '' : `(${instance.verdict})`}`).join(' ')
        console.log(`${pad(entry.verdict, 9)} ${pad(entry.rule.name, width)} ${pad(entry.licence, 36)} ${pad(proposition, 12)} ${instances}`)
        if (entry.verdict !== 'proved') {
            const reasons = [...new Set(entry.perInstance.filter((instance) => instance.reason).map((instance) => instance.reason))]
            for (const reason of reasons) console.log(`          ${pad('', width)} ${reason}`)
            for (const instance of entry.perInstance) {
                if (!instance.witness) continue
                const model = [...instance.witness.entries()].map(([name, value]) => `${name} = ${value}`).join(', ')
                console.log(`          ${pad('', width)} ${instance.type}: ${model}`)
            }
        }
    }

    const counts = { proved: 0, refuted: 0, unknown: 0 }
    for (const entry of verdicts) counts[entry.verdict]++
    const total = verdicts.length
    console.log('')
    console.log(`rule-proofs: proved ${counts.proved}, refuted ${counts.refuted}, unknown ${counts.unknown}, total ${total}`)
    console.log(`rule-proofs: ${queries} queries against ${solver.version}, per-query budget ${(timeoutMs / 1000).toFixed(0)} s`)
    console.log(`rule-proofs: ${falsification}`)
    const expected = /export\s+rule\s/.test(library.source) ? (library.source.match(/export\s+rule\s/g) || []).length : 0
    if (!only && total !== expected) fail(`${total} rule(s) were decided and the library declares ${expected}`)
    const refutedNames = verdicts
        .filter((entry) => entry.verdict === 'refuted')
        .map((entry) => {
            const witness = entry.perInstance.find((instance) => instance.witness)
            if (!witness) return entry.rule.name
            const model = [...witness.witness.entries()].map(([name, value]) => `${name} = ${value}`).join(', ')
            return `${entry.rule.name} (${witness.type}: ${model})`
        })
    if (refutedNames.length > 0) fail(`refuted: ${refutedNames.join('; ')}: a rule of the library does not hold under its own licence`)
    console.log(`rule-proofs: ok: ${counts.proved} of ${total} proved, ${counts.unknown} unknown with a reason, none refuted`)
}

main()