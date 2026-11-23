// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT
/**
 * How one reading of a test's failure is compared with another.
 *
 * A merge gate that only counts reds cannot tell "the same three tests still fail" from "the same
 * three tests now fail a different way", and the second is a regression the count hides. Comparing
 * the failures' first lines answers that -- but only if the lines are compared after the parts that
 * change every run are removed, and only if that removal is one function rather than a habit.
 *
 * Both directions are defects, and this function is the single place either can be introduced:
 *
 *  - Too little: a line carrying a path, a duration or an id differs between two runs of the same
 *    failure, so every comparison says "changed" and the gate refuses every merge while always
 *    having a reason -- which is the failure mode this project has been bitten by before.
 *  - Too much: two different failures collapse into one string, so a real change of failure is
 *    erased and the gate says "same". That is the quieter and worse direction.
 *
 * `normalizeReading` removes what is *the same failure said twice* (where the tree lives, how long
 * it took, which process it was) and keeps what is *what went wrong* (the sentence, the values it
 * read, the file and line it names). `READING_NORMAL_VERSION` moves whenever the rules move: a
 * cached baseline is only comparable with a reading taken under the same rules, and a cache entry
 * without this number would look valid after a rule changed.
 */

/**
 * Increment on every change to `normalizeReading`. It is part of every baseline cache key: the same
 * commit measured under different rules is not the same baseline.
 */
export const READING_NORMAL_VERSION = 1

/** A path, keeping its last component (and a trailing `:line:col`), dropping where it lived. */
const PATH = /(?:\.{1,2}[\\/]|[A-Za-z]:[\\/]|\/)(?:[^\s:()[\],'"]+[\\/])+([^\s()[\],'"]+)/g
/** A duration in any unit the runner or a test prints. */
const DURATION = /\b\d+(?:\.\d+)?\s*(?:ns|us|µs|ms|msec|sec|s|seconds?|minutes?)\b/gi
/** A hexadecimal address or handle. */
const HEX = /\b0x[0-9a-f]+\b/gi
/** A uuid. */
const UUID = /\b[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b/gi
/** `key=1234` for the keys whose value is an identity rather than a measurement. */
const IDENTIFIER = /\b(seed|id|pid|tid|port|token|attempt)=\d+/gi

/**
 * The comparable form of one reading: the same failure said twice gives the same string; two
 * different failures give two different strings.
 */
export function normalizeReading(line) {
    if (line === null || line === undefined) return null
    return String(line)
        .replace(/\r/g, '')
        .replace(UUID, '<uuid>')
        .replace(HEX, '<hex>')
        .replace(IDENTIFIER, '$1=<n>')
        .replace(DURATION, '<duration>')
        .replace(PATH, '$1')
        .replace(/\s+/g, ' ')
        .trim()
}