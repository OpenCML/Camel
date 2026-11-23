// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT
/**
 * Both directions of `normalizeReading`, because both are defects and only one of them is loud.
 *
 * Under-normalising makes every comparison say "changed": the gate refuses every merge and always
 * has a reason, which reads as a working gate. Over-normalising makes two different failures the
 * same string: the gate says "unchanged" and the regression goes through in silence. Each case
 * below is one of those two, named.
 */
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { normalizeReading, READING_NORMAL_VERSION } from './reading-normal.mjs'

const same = (a, b, why) => assert.equal(normalizeReading(a), normalizeReading(b), why)
const differ = (a, b, why) => assert.notEqual(normalizeReading(a), normalizeReading(b), why)

test('under-normalisation: the same failure read twice is equal', () => {
    // The same sentence from two runs, differing only in what every run differs in: where the tree
    // is, how long it took, which process it was, and which address it saw.
    same(
        'D:\\Projects\\Camel\\out\\latest\\bin\\camel.exe failed after 12 ms (pid=4412)',
        'D:\\Projects\\Camel-wt\\lane2\\out\\latest\\bin\\camel.exe failed after 0.9 s (pid=99)',
        'a path, a duration and a pid are not part of what went wrong',
    )
    same(
        '/home/zjwei/camel-work/Camel/src/kernel/core/mm/mm.cpp:23: error: 0x7ffd0000 was not mapped',
        '/home/zjwei/camel-work/lane-x/src/kernel/core/mm/mm.cpp:23: error: 0x1c2a was not mapped',
        'a line number survives, an address does not',
    )
    same(
        'plan seed=1234 diverged at attempt=2 (uuid 3f2504e0-4f89-11d3-9a0c-0305e82c3301)',
        'plan seed=7 diverged at attempt=9 (uuid 00000000-0000-0000-0000-000000000000)',
        'identities are not readings',
    )
    // Under-normalisation is also a matter of degree: a rule that only strips the whole path but
    // leaves the directory name would still differ between two checkouts.
    same('at ./build/tmp/x.cml:4', 'at /abs/elsewhere/tmp/x.cml:4', 'the path is the same file')
})

test('over-normalisation: two different failures stay different', () => {
    // The dangerous direction. Each pair differs in the one thing a reader would act on.
    differ(
        'error: undefined symbol: matmul',
        'error: undefined symbol: matmul_grad',
        'two different missing symbols must not collapse',
    )
    differ(
        'expected 3, got 4',
        'expected 3, got 5',
        'the value read is the reading',
    )
    differ(
        'src/a.cpp:10: error: no matching function',
        'src/a.cpp:11: error: no matching function',
        'the line it points at is the reading',
    )
    differ(
        'exit 1 after 3 ms',
        'exit 2 after 3 ms',
        'the exit code is the reading, the duration is not',
    )
    differ(
        'fatal: stack exhausted',
        'fatal: stack exhausted\n\twhile lowering :onnx/matmul',
        'a second line of context is a different failure, not noise',
    )
})

test('the version is what a cache key is built from', () => {
    assert.equal(typeof READING_NORMAL_VERSION, 'number')
    assert.ok(READING_NORMAL_VERSION >= 1)
    // A constant with no reader is a promise; this asserts the module's own version is used.
    assert.equal(normalizeReading(null), null)
    assert.equal(normalizeReading('  a   b  '), 'a b')
})