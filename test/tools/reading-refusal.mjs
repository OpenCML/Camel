// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * The machine-readable half of a transform's refusal, read the way a plan asserts it.
 *
 * A transform refuses by name: `REINTERP jvp function=f skip=body`, and the cause is a value
 * (`RefusalCause`, `modules/transforms/refusal_readings.h`) that the line spells from one table.
 * The line is for a person and it carries the prose a value does not, so a plan that pinned the
 * whole line made the wording the criterion: `skip=component` renamed to `skip=part` was a green
 * case turning red with nothing about the transform changed. The cause cannot be recovered from
 * the sentence either -- that second reading is the defect the `transpose` refusals had removed
 * (`docs/handover/questions/questions-2026-10-29.md`, A3).
 *
 * So the site that says the line also says the fact, once, as one record:
 *
 *     READING_REFUSAL {"transform":"jvp","function":"total","cause":"rule","uri":"tensor:relu"}
 *
 * This module is the only reader of that record. A plan names the fields it decides and nothing
 * else (`expect_refusal = ["jvp cause=rule function=total uri=tensor:relu"]`): the cause as the
 * named value the table spells, and the place the refusal names -- the operator's URI, the rule,
 * the callee, the construct, a node, a parameter, a position. A field the plan does not name is not
 * compared, so a record that gains a field tomorrow does not turn a case red, and the sentence is
 * not part of the criterion at all.
 *
 * The record is JSON on one line, the shape `GRAPH_STATS {json}` established
 * (`src/passes/trans/stats/stats.cpp`); a line that does not parse is reported as a red rather than
 * skipped, because a record nothing can read is a reading nobody can cite.
 */

export const REFUSAL_PREFIX = 'READING_REFUSAL '

/** The fields a record may carry, and the ones a plan may name. */
export const REFUSAL_FIELDS = [
    'transform',
    'function',
    'cause',
    'uri',
    'rule',
    'callee',
    'construct'
]

/**
 * Every refusal record in `text` (a run's stderr), in the order the run wrote them.
 *
 * Throws when a line carries the prefix and is not the JSON object the prefix promises: a truncated
 * or hand-written record has to fail where it is read, not be skipped and reported as "no refusal".
 */
export function refusals(text) {
    const found = []
    for (const line of String(text || '').split('\n')) {
        const at = line.indexOf(REFUSAL_PREFIX)
        if (at < 0) continue
        const body = line.slice(at + REFUSAL_PREFIX.length).trim()
        let record
        try {
            record = JSON.parse(body)
        } catch (error) {
            throw new Error(
                `a line says \`${REFUSAL_PREFIX.trim()}\` and its payload is not JSON ` +
                    `(${error.message}): ${body}`
            )
        }
        if (record === null || typeof record !== 'object' || Array.isArray(record)) {
            throw new Error(`a \`${REFUSAL_PREFIX.trim()}\` payload is not an object: ${body}`)
        }
        found.push(record)
    }
    return found
}

/**
 * One plan assertion, `field=value ...`, as the fields it names.
 *
 * Throws on an unknown field or on a named field with no value: a misspelled field would otherwise
 * compare nothing and pass, which is the failure mode `expect_refusal` exists to remove.
 */
export function parseRefusalSpec(text) {
    if (typeof text !== 'string' || text.trim() === '') {
        throw new Error('expect_refusal: an entry must name at least one field, like `jvp cause=body`')
    }
    const spec = {}
    let rest = text.trim()
    // The transform may be named on its own first, `jvp cause=body`: it is the subject of every
    // record, so naming it by hand every time would be the same word in every entry of a plan.
    const first = /^\S+/.exec(rest)[0]
    if (!first.includes('=')) {
        spec.transform = first
        rest = rest.slice(first.length).trim()
    }
    while (rest.length > 0) {
        const match = /^([A-Za-z_][A-Za-z0-9_]*)=(?:"([^"]*)"|(\S+))/.exec(rest)
        if (!match) {
            throw new Error(
                `expect_refusal: '${rest.split(/\s+/)[0]}' is not \`field=value\` ` +
                    `(a value with a space is quoted: construct="a call")`
            )
        }
        const field = match[1]
        const value = match[2] !== undefined ? match[2] : match[3]
        if (!REFUSAL_FIELDS.includes(field)) {
            throw new Error(
                `expect_refusal: '${field}' is not a field of a refusal record ` +
                    `(${REFUSAL_FIELDS.join(', ')})`
            )
        }
        if (field in spec) {
            throw new Error(`expect_refusal: '${field}' is named twice in one entry`)
        }
        spec[field] = value
        rest = rest.slice(match[0].length).trim()
    }
    return spec
}

/** A spec as one assertion, values with a space quoted the way `parseRefusalSpec` reads them. */
export function formatRefusalSpec(spec) {
    return Object.entries(spec)
        .map(([field, value]) => (String(value).includes(' ') ? `${field}="${value}"` : `${field}=${value}`))
        .join(' ')
}

/** Whether the record answers every field the spec names. */
export function matchesRefusal(record, spec) {
    return Object.entries(spec).every(([field, value]) => record[field] === value)
}

/** The records a failure message shows, as `field=value` words, capped so a wall of them stays readable. */
export function describeRefusals(records) {
    if (records.length === 0) return '(no refusal records)'
    return records
        .slice(0, 8)
        .map((record) => REFUSAL_FIELDS.filter((field) => record[field] !== undefined)
            .map((field) => `${field}=${record[field]}`)
            .join(' '))
        .join(' | ')
}