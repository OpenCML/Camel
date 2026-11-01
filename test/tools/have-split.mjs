#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Precondition of the `split` plan (test/plans/feat/transforms/split.plan.toml).
//
// `split::<F>(f): Split<F>` is design doc 4.2. Until it exists there is nothing to run: the
// cases of that plan call the transform, so running them would be red for a reason that is not
// a defect -- the operator has not landed. The plan asks this gate first and skips, with the
// reason below, while the implementation is missing.
//
// Two shapes count as "landed", because 4.2 allows either: the native half
// (`modules/transforms/split.cpp`) and the rule half (`stdlib/transforms/split.cml`). The
// question is about the repository, not about the build, so it is answered by reading the tree.
// Exit 0 when split exists, 1 with the reason when it does not.

import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..')
const WANTED = [
    path.join(ROOT, 'modules', 'transforms', 'split.cpp'),
    path.join(ROOT, 'stdlib', 'transforms', 'split.cml'),
    path.join(ROOT, 'stdlib', 'transforms.cml'),
]

const present = WANTED.filter((file) => fs.existsSync(file))
const hasSplit =
    present.some((file) => /split\.(cpp|cml)$/.test(file)) ||
    present.some((file) => {
        try {
            // A mention in a comment is not the operator: what counts is a declaration of it.
            return /(export\s+)?func\s+split\b|split::</.test(fs.readFileSync(file, 'utf8'))
        } catch {
            return false
        }
    })

if (hasSplit) {
    process.exit(0)
}
console.log(
    'split is not implemented yet: neither modules/transforms/split.cpp nor stdlib/transforms/split.cml ' +
        '(design doc 4.2) exists, so the plan has nothing to run; it starts running when the operator lands',
)
process.exit(1)