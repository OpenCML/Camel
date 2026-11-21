// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import path from 'path'
import { fileURLToPath } from 'url'

const REPO_ROOT = path.dirname(path.dirname(path.dirname(fileURLToPath(import.meta.url))))

const escapeForRegExp = (root) => root.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')

/**
 * One root, in the three spellings a path can arrive in: forward slashes, native separators, and
 * escaped backslashes (a Windows path inside a quoted DOT or JSON string). On Windows only the
 * separator form the root itself was written in is rewritten in the rest of the path, so an escape
 * that follows the path (`\"`, `\n`) is left alone.
 */
function replaceRoot(text, root, label) {
    let output = text
    if (root.includes('\\')) {
        const rest = '[^\\s"\'`<>|*?,;]*'
        for (const [form, separator] of [
            [root.replace(/\\/g, '\\\\'), /\\\\/g],
            [root, /\\/g],
        ]) {
            const pattern = new RegExp(`${escapeForRegExp(form)}(${rest})`, 'gi')
            output = output.replace(pattern, (_, tail) => `${label}${tail.replace(separator, '/')}`)
        }
    }
    const forward = root.replace(/\\/g, '/')
    return output.replaceAll(forward, label).replaceAll(forward.toLowerCase(), label)
}

/**
 * Replace the repository root with `<ROOT>`, and the installation a run measures with `<INSTALL>`,
 * writing the rest of each path with forward slashes, so a snapshot does not depend on where the
 * checkout lives, where the install lives, or on the platform's separator. Each root is recognised
 * in the three spellings above, in its own case and in lower case.
 *
 * The install root is the second root because a run may measure an install that is not this
 * checkout's -- `--camel <another tree>/out/latest/bin/camel` -- and the compiler then prints that
 * install's library as that install's absolute path. The repository-root rule cannot see it, so an
 * assertion on `stdlib/vjp.cml` hit in the checkout that owned the install and missed in a worktree
 * that measured it: one reading, green or red by where the run started. That is a defect of the
 * normalization, not of the assertion, so the install root is normalized and the assertion is left
 * exactly as it was: the tail stays exact, and a run that could not find the file still prints a
 * path that does not carry the asserted fragment.
 *
 * The repository root goes first, and that order is what keeps a checkout measuring its own
 * `out/latest` byte-identical: that prefix is already `<ROOT>` by the time the install root is
 * asked for, so this rule only fires on an install outside the checkout. A bare root (a whole
 * drive, `/`) is refused rather than obeyed -- replacing it would rewrite every absolute path.
 */
export function normalizePathText(text, repoRoot = REPO_ROOT, installRoot = null) {
    let output = replaceRoot(text, repoRoot, '<ROOT>')
    if (!installRoot) return output
    const install = path.normalize(installRoot)
    if (path.dirname(install) === install) return output
    if (path.normalize(repoRoot) === install) return output
    return replaceRoot(output, install, '<INSTALL>')
}

/**
 * The normalizers `docs/test-plans.md` documents. One definition serves every comparison path --
 * the plan runner and a checker with its own golden comparison (exemplars/check.mjs) -- so a
 * normalizer added here reaches all of them and no harness can silently compare less than another.
 *
 * `roots.installRoot` is the install the run measures (`test/tools/run-tests.mjs`: the binary
 * `--camel` names, its grandparent), used by `normalize_paths` alone. A caller that compares a
 * recorded golden without knowing that install passes nothing and keeps the repository-root
 * reading; the runner passes it to both sides of every comparison, so a golden and the output it is
 * compared with are normalized by one rule.
 */
export function applyNormalizers(text, names, roots = {}) {
    let output = text
    for (const name of names || []) {
        if (name === 'strip_ansi') {
            output = output.replace(/\x1b\[[0-9;]*m/g, '')
        } else if (name === 'normalize_newlines') {
            output = output.replace(/\r\n/g, '\n')
        } else if (name === 'trim_trailing_space') {
            output = output
                .split('\n')
                .map((line) => line.replace(/[ \t]+$/g, ''))
                .join('\n')
        } else if (name === 'normalize_paths') {
            output = normalizePathText(output, undefined, roots.installRoot)
        } else if (name === 'normalize_addresses') {
            output = output.replace(/0x[0-9a-fA-F]+/g, '<ADDR>')
        } else if (name === 'normalize_origins') {
            // An origin id is an opaque per-build identifier for a source position, not graph
            // structure: it is renumbered whenever any other file is parsed or compiled before the
            // dumped program (a pass module written in Camel takes some tokens, a declaration
            // module adds some). The source position itself is printed next to it -- `span` and
            // `srcfile` in the DOT dumps, the span fields in `std::jgir` -- so normalizing the id
            // keeps the assertion on where a node comes from and drops the snapshot of the tree.
            // `origin=<ORIGIN>` still asserts that a node has an origin (a node without one prints
            // no attribute at all).
            output = output
                .replace(/"origin": "[0-9]+"/g, '"origin": "<ORIGIN>"')
                .replace(/\borigin=[0-9]+/g, 'origin=<ORIGIN>')
        } else if (name === 'normalize_node_ids') {
            // A rich DOT dump names each node in its tooltip as `<graph>::node#<dataIndex>`: the
            // frame slot (positive) or static slot (negative) the node's value lives in. That
            // index is the allocator's numbering of one graph, not graph structure: it moves when
            // a lowering change allocates one temporary more or materializes a constant earlier,
            // while the graph the dump describes is the same. What the tooltip is *for* is the
            // relation it states between nodes -- two nodes that name one slot are one value, a
            // GATE names its producer's slot, unrelated nodes must not collide -- so renumber
            // each distinct id by first appearance within its graph (`n0`, `n1`, ...) and compare
            // that relation. Renumbering per graph is what the ids mean: a static slot 1 of `main`
            // and a static slot 1 of `gate` are different slots, and the graph name is printed
            // beside the label. The golden keeps the absolute ids as the record of what the tree
            // emitted when it was recorded; a real structural change still moves the *order* the
            // distinct ids first appear in, or the set of nodes that share one, and stays red.
            const idsByGraph = new Map()
            output = output.replace(/([^\s"\\]+)::node#(-?\d+)/g, (_whole, graph, id) => {
                let ids = idsByGraph.get(graph)
                if (!ids) {
                    ids = new Map()
                    idsByGraph.set(graph, ids)
                }
                let label = ids.get(id)
                if (label === undefined) {
                    label = `n${ids.size}`
                    ids.set(id, label)
                }
                return `${graph}::node#${label}`
            })
        } else if (name === 'normalize_graph_ids') {
            // A transform's reading names the graph it made (`REINTERP remat ... graph=49`,
            // `REINTERP jvp ... graph=56`). That number is the per-run allocation index of the
            // graph, not the reading: it moves when any other declaration module adds a graph (the
            // same reason `normalize_origins` exists for source positions), while the transform
            // still made one graph and set the same bits on it. Replacing it with `<GRAPH>` keeps
            // what the line asserts -- that a graph was reserved and named here -- and drops the
            // snapshot of the run's counter, so a plan can pin the whole line with
            // `expect_stdout_exact` instead of matching a fragment.
            output = output
                .split('\n')
                .map((line) =>
                    line.startsWith('REINTERP ') ? line.replace(/\bgraph=\d+/g, 'graph=<GRAPH>') : line
                )
                .join('\n')
        } else if (name === 'normalize_duration_seconds') {
            output = output.replace(/computed in [0-9]+\.[0-9]+ seconds/g, 'computed in <TIME> seconds')
        }
    }
    return output
}