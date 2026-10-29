// The standard rule library, compared before and after every rewrite, rule by rule.
//
// The case `differential_equivalence.cml` prints one `pair` line per (rule instance, input) with
// the value the witness computes and the value the rewritten witness computes, one `fired` line
// with the rewrite count, and one `rearranged` line with the node counts of a saturation on both
// sides of a `<=>` law. This script is the judgement on that output, and it reads the library with
// it, so that the three lists cannot drift apart:
//
//   * every `export rule` of `stdlib/rules.cml` is reported by the case, unless this file declares
//     it out of scope with a reason -- a rule added to the library and to no witness fails here;
//   * every label the case reports names an exported rule of the library, so a label that outlives
//     its rule fails here;
//   * every rule that is reported fired: `fired` above zero, or `rearranged` with fewer nodes
//     afterwards. A rule that does not fire is a rule this check did not test, and a comparison of
//     a function with itself is not a pass;
//   * the two readings of every pair agree, under the proposition the rule's licence implies:
//     - a rule that asks for nothing beyond the default `reassociate`/`contract` must produce the
//       same bits (design principles 7.2: finite values equal as reals, special values bit for
//       bit), and the readings are compared as the shortest texts of the values, so a `-0.0` where
//       `+0.0` was is a difference;
//     - a rule that also asks for `no_infs`, `no_signed_zeros` or `finite_math` is compared as
//       reals, because changing the rounding path is what that licence allows: the two numbers
//       may differ in their last bits and a zero may lose its sign. An input that is itself NaN or
//       an infinity is left out for such a rule, since the licence excludes those values; a finite
//       input that yields a NaN or an infinity on either side is a difference, not a skip.
//
// What this cannot catch, and the case says so too: the domain is a list of boundaries and special
// values, not the whole type, so a law that fails at one value the list does not hold passes. The
// real-equality comparison tolerates a relative difference of 2^-20 (about eight f32 ulps), so a
// wrongness smaller than that is not a difference here. NaN payloads and NaN signs read as `nan`.
import fs from 'node:fs'
import path from 'node:path'

const RELATIVE = Math.pow(2, -20)
const ABSOLUTE = 1e-30

const OVERSIZE = 4

const OUT_OF_SCOPE = new Map([
    [
        'float_log1p',
        'it asks for refine_accuracy and turns `log(1+x)` into `log1p(x)`, which changes the bits on ' +
            'purpose: its proposition is an error bound measured against a wider type, not equality, ' +
            'and `check` measures it (`rules.accuracy_scalar_target` pins the readings)',
    ],
    [
        'float_expm1',
        'it asks for refine_accuracy and turns `exp(x)-1` into `expm1(x)`, which changes the bits on ' +
            'purpose: its proposition is an error bound, not equality',
    ],
    [
        'array_log_softmax_of_softmax',
        'it asks for refine_accuracy and turns `log(softmax(x))` into `log_softmax(x)`, which changes ' +
            'the bits on purpose (`-inf` becomes a finite value): its proposition is an error bound, not ' +
            'equality',
    ],
])

const payload = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))
const input = payload.input ?? payload

function fail(message) {
    console.error(message)
    process.exit(1)
}

const stdout = input.normalized?.stdout ?? input.stdout ?? payload.stdout ?? ''
const casePath = input.case_path ?? payload.case_path ?? ''
const repoRoot =
    input.repo_root ?? payload.repo_root ?? path.resolve(path.dirname(casePath), '..', '..', '..', '..')
const testName = input.test_name ?? payload.test_name ?? 'rules.differential_equivalence'
const library = path.join(repoRoot, 'stdlib', 'rules.cml')

if ((input.exit_code ?? payload.exit_code ?? 0) !== 0) {
    fail(`${testName}: the check case exited ${input.exit_code ?? payload.exit_code}`)
}
if (!fs.existsSync(library)) {
    fail(`the standard library is not at ${library}, so this check cannot say what it exports`)
}
if (stdout.trim() === '') {
    fail(`${testName}: the check case printed nothing, so no rule was compared`)
}

// ---- the library: its exported rules and the licence each one asks for ----

const rules = new Map()
let pendingLicence = []
for (const line of fs.readFileSync(library, 'utf8').split('\n')) {
    const requires = /#requires\(([^)]*)\)/.exec(line)
    if (requires) {
        pendingLicence = requires[1]
            .split(',')
            .map((word) => word.trim())
            .filter(Boolean)
    }
    const declared = /^\s*export\s+rule\s+([A-Za-z_][A-Za-z0-9_]*)/.exec(line)
    if (declared) {
        rules.set(declared[1], pendingLicence)
        pendingLicence = []
    }
}
if (rules.size === 0) {
    fail(`${library} exports no rule, so this check examines nothing`)
}

// A rule that asks for nothing beyond the default grants has to keep the bits; one that asks for an
// assumption about special values or the rounding path is judged as real equality.
const bitExact = (name) =>
    (rules.get(name) ?? []).every((licence) => licence === 'reassociate' || licence === 'contract')

// ---- the readings ----

const readings = new Map() // label -> { fired, rearranged, pairs: [{ before, after, input }] }
function entry(label) {
    if (!readings.has(label)) {
        readings.set(label, { fired: null, rearranged: null, pairs: [] })
    }
    return readings.get(label)
}

let lineCount = 0
for (const raw of stdout.split('\n')) {
    const line = raw.trim()
    if (line === '') continue
    lineCount++
    const fired = /^fired (\S+) (-?\d+)$/.exec(line)
    if (fired) {
        entry(fired[1]).fired = Number(fired[2])
        continue
    }
    const rearranged = /^rearranged (\S+) (\d+) (\d+)$/.exec(line)
    if (rearranged) {
        entry(rearranged[1]).rearranged = [Number(rearranged[2]), Number(rearranged[3])]
        continue
    }
    const pair = /^pair (\S+) (.*?) \| (.*?) \| (.*)$/.exec(line)
    if (pair) {
        entry(pair[1]).pairs.push({ before: pair[2], after: pair[3], input: pair[4] })
        continue
    }
    const error = /^error (\S+) (.*)$/.exec(line)
    if (error) {
        fail(`${testName}: the engine reported ${error[2]} for ${error[1]}`)
    }
    fail(`${testName}: a line of the case is not a reading: ${JSON.stringify(line)}`)
}
if (readings.size === 0) {
    fail(`${testName}: ${lineCount} line(s) of output and not one reading`)
}

// ---- the numbers of a reading, for the real-equality proposition ----

function numbers(text) {
    const body = text.startsWith('[') && text.endsWith(']') ? text.slice(1, -1).trim() : text
    const items = body === '' ? [] : body.split(',').map((item) => item.trim())
    return items.map((item) => {
        if (item === 'nan' || item === '-nan') return NaN
        if (item === 'inf') return Infinity
        if (item === '-inf') return -Infinity
        const value = Number(item)
        if (!Number.isFinite(value)) {
            throw new Error(`a reading that is not a number: ${JSON.stringify(item)} in ${JSON.stringify(text)}`)
        }
        return value
    })
}

function special(text) {
    return /(^|[\s,[])(-?nan|-?inf)($|[\s,\]])/.test(text)
}

// A pair a real-equality law is not judged on: the input itself holds a NaN or an infinity, or the
// program before the rewrite already left the licence's domain by producing one. Nothing else is
// skipped, so a rewrite that turns a finite value into a NaN or an infinity is a difference.
function outsideLicence(inputText, before) {
    if (special(inputText)) return true
    return numbers(before).some((value) => !Number.isFinite(value))
}

function realEqual(before, after) {
    const xs = numbers(before)
    const ys = numbers(after)
    if (xs.length !== ys.length) return false
    for (let index = 0; index < xs.length; index++) {
        const x = xs[index]
        const y = ys[index]
        if (!Number.isFinite(y)) return false
        const scale = Math.max(Math.abs(x), Math.abs(y), ABSOLUTE)
        if (Math.abs(x - y) > RELATIVE * scale) return false
    }
    return true
}

// ---- the judgement ----

const missing = [...rules.keys()].filter((name) => !OUT_OF_SCOPE.has(name) && ![...readings.keys()].some((label) => label.split('/')[0] === name))
if (missing.length > 0) {
    fail(
        `${testName}: ${missing.length} rule(s) of ${path.basename(library)} are reported by no line of ` +
            `the check: ${missing.join(', ')}`
    )
}

const stale = [...OUT_OF_SCOPE.keys()].filter((name) => !rules.has(name))
if (stale.length > 0) {
    fail(
        `${testName}: ${stale.length} name(s) are declared out of scope and are not rules of the ` +
            `library any more: ${stale.join(', ')}`
    )
}

const strangers = [...readings.keys()].filter((label) => !rules.has(label.split('/')[0]))
if (strangers.length > 0) {
    fail(
        `${testName}: ${strangers.length} label(s) name a rule the library does not export: ` +
            `${strangers.join(', ')}`
    )
}

const silent = []
const differing = []
let compared = 0
let leftOut = 0
for (const [label, reading] of readings) {
    const rule = label.split('/')[0]
    const fired = reading.fired ?? (reading.rearranged ? reading.rearranged[0] : 0)
    if (!(fired > 0)) {
        silent.push(`${label} (${reading.fired === null ? 'no rewrite count' : `fired ${reading.fired}`})`)
    }
    if (reading.rearranged) {
        const [before, after] = reading.rearranged
        if (!(after < before)) {
            fail(
                `${testName}: the saturation applied ${rule} to ${label} and kept ${after} nodes of the ` +
                    `${before} it started with, so the two sides of that law are the same writing`
            )
        }
    }
    if (reading.pairs.length === 0) {
        silent.push(`${label} (no value compared)`)
    }
    for (const pair of reading.pairs) {
        if (bitExact(rule)) {
            compared++
            if (pair.before !== pair.after) {
                differing.push(`${label} on ${pair.input}: ${pair.before} became ${pair.after}`)
            }
            continue
        }
        // real equality: the values the licence excludes are not readings of this law
        let outside
        try {
            outside = outsideLicence(pair.input, pair.before)
        } catch (problem) {
            fail(`${testName}: ${problem.message}`)
        }
        if (outside) {
            leftOut++
            continue
        }
        compared++
        let agree
        try {
            agree = realEqual(pair.before, pair.after)
        } catch (problem) {
            fail(`${testName}: ${problem.message}`)
        }
        if (!agree) {
            differing.push(`${label} on ${pair.input}: ${pair.before} became ${pair.after}`)
        }
    }
}

if (silent.length > 0) {
    fail(
        `${testName}: ${silent.length} rule instance(s) did not fire on their witness or compared no ` +
            `value, so they are not verified: ${silent.join(', ')}`
    )
}
if (differing.length > 0) {
    fail(
        `${testName}: ${differing.length} rewrite(s) changed a value of the finite domain:\n  ` +
            differing.slice(0, OVERSIZE * 3).join('\n  ') +
            (differing.length > OVERSIZE * 3 ? `\n  ...and ${differing.length - OVERSIZE * 3} more` : '')
    )
}

const covered = rules.size - OUT_OF_SCOPE.size
console.log(
    `${covered} of ${rules.size} exported rule(s) of ${path.basename(library)} fired on a witness and ` +
        `kept their value over ${compared} pair(s) of readings in ${readings.size} instance(s); ` +
        `${OUT_OF_SCOPE.size} are out of scope with a reason (${[...OUT_OF_SCOPE.keys()].sort().join(', ')}); ` +
        `${leftOut} pair(s) were left out as outside the domain of a real-equality licence; ` +
        `every judged pair agrees under the proposition of the rule's licence`
)
