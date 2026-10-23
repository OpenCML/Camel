// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

import path from 'path'
import { fileURLToPath } from 'url'

const REPO_ROOT = path.dirname(path.dirname(path.dirname(fileURLToPath(import.meta.url))))

/**
 * Replace the repository root with `<ROOT>` and write the rest of that path with forward slashes,
 * so a snapshot does not depend on where the checkout lives or on the platform's separator. The
 * root is recognised with forward slashes, with native separators, and with escaped backslashes
 * (a Windows path inside a quoted DOT or JSON string), in its own case and in lower case.
 */
export function normalizePathText(text, repoRoot = REPO_ROOT) {
    const forward = repoRoot.replace(/\\/g, '/')
    let output = text
    if (repoRoot.includes('\\')) {
        // Windows: `C:\\repo\\a\\b` (escaped) first, then `C:\repo\a\b` (native). Only the
        // separator form the root itself was written in is rewritten in the rest of the path,
        // so an escape that follows the path (`\"`, `\n`) is left alone.
        const quote = (root) => root.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')
        const rest = '[^\\s"\'`<>|*?,;]*'
        for (const [root, separator] of [
            [repoRoot.replace(/\\/g, '\\\\'), /\\\\/g],
            [repoRoot, /\\/g],
        ]) {
            const pattern = new RegExp(`${quote(root)}(${rest})`, 'gi')
            output = output.replace(pattern, (_, tail) => `<ROOT>${tail.replace(separator, '/')}`)
        }
    }
    return output.replaceAll(forward, '<ROOT>').replaceAll(forward.toLowerCase(), '<ROOT>')
}

/**
 * The normalizers `docs/test-plans.md` documents. One definition serves every comparison path --
 * the plan runner and a checker with its own golden comparison (exemplars/check.mjs) -- so a
 * normalizer added here reaches all of them and no harness can silently compare less than another.
 */
export function applyNormalizers(text, names) {
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
            output = normalizePathText(output)
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
        } else if (name === 'normalize_duration_seconds') {
            output = output.replace(/computed in [0-9]+\.[0-9]+ seconds/g, 'computed in <TIME> seconds')
        }
    }
    return output
}