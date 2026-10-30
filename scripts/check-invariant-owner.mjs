#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * An invariant names its owner: the finished graph, the draft, or the encoder.
 *
 * A rule that says "this must hold" is only checkable where it can be asked of a node that is
 * finished. Tonight's order-edge work paid for that lesson twice: four writer guards and a choke
 * point were all written at the place an edge is appended, and all of them were ineffective,
 * because at that moment the node has no payload and `nodeIsFreelySchedulable` answers for a
 * half-made node. The filter that worked had to sit in the payload plan, where the node is
 * finished. The rule is therefore about *where* an invariant lives, not only about what it says.
 *
 * This is a skeleton and it does not fail a build yet: it reports the commits that add an invariant
 * -shaped comment without naming its owner, so the vocabulary can settle before anything is gated.
 *
 * Usage:
 *   node scripts/check-invariant-owner.mjs [--base <rev>] [--rev <rev>]
 *
 * The owner vocabulary, in the order a reader should prefer them:
 *   the finished graph / the frozen graph / 完工的图
 *   the draft / 草稿
 *   the encoder / the payload plan / 编码器
 *   the seal check / 封印检查        (a net, not an owner: an invariant may name it as a backstop)
 */

import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";

const argv = process.argv.slice(2);


const option = (name, fallback) => {
    const at = argv.indexOf(name);
    return at >= 0 && at + 1 < argv.length ? argv[at + 1] : fallback;
};
const base = option("--base", "origin/develop");
const rev = option("--rev", "HEAD");

const OWNERS = [
    ["finished graph", /finished graph|frozen graph|完工的图/],
    ["draft", /\bdraft\b|草稿/],
    ["encoder", /\bencoder\b|payload plan|编码器/],
    ["seal check (backstop only)", /seal check|封印检查/],
];

const INVARIANT = /invariant|不变式|must hold|cannot drift|same question|one question|只能|may only/i;
/// The wider question the census asks: a comment that states a rule, not only one that says "invariant".
const RULE = /invariant|不变式|\bmust\b|必须|never\b|cannot\b/i;
const COMMENT = /^\s*(?:\/\/|\*|#)/;

const git = (args) =>
    execFileSync("git", args, { encoding: "utf8", maxBuffer: 64 * 1024 * 1024 });

// ---------------------------------------------------------------------------------------------
// --census: the map, not the gate.
//
// Every invariant comment the repository already has, with the owner it names and, where it names
// none, the owner today's facts say it should have. The point is to see where our invariants live
// before any of this is wired into a plan: a rule whose owner cannot be named is a rule that will be
// checked at the wrong moment, which is what the order-edge work cost a night to learn.
// ---------------------------------------------------------------------------------------------

const FILES = [
    "src/runtime/graph_edges.cpp",
    "src/runtime/draft.cpp",
    "src/runtime/graph.cpp",
    "src/runtime/draft_session.cpp",
    "src/runtime/draft_inline.cpp",
    "src/runtime/control.cpp",
    "src/runtime/graph_txn.cpp",
];

/// The owner today's facts say a rule of this file belongs to, when its comment names none.
const SUGGEST = [
    [/graph_edges\.cpp$/, "finished graph (the seal check)"],
    [/draft\.cpp$|draft_session\.cpp$|draft_inline\.cpp$/, "draft"],
    [/graph\.cpp$/, "encoder (the payload plan)"],
    [/control\.cpp$|exec_plan\.cpp$/, "finished graph (the scheduler reads it)"],
];

const firstSentence = (text) => {
    const flat = text.replace(/\s+/g, " ").replace(/^(\/\/+|\*|#)\s*/, "").trim();
    const end = flat.search(/[.。;；](\s|$)/);
    return (end > 0 ? flat.slice(0, end + 1) : flat).slice(0, 110);
};

const census = () => {
    const rows = [];
    for (const file of FILES) {
        const source = readFileSync(file, "utf8");
        const lines = source.split("\n");
        for (let at = 0; at < lines.length; at += 1) {
            const line = lines[at];
            if (!COMMENT.test(line) || !RULE.test(line)) {
                continue;
            }
            // The comment block this line belongs to, for the owner question.
            let begin = at;
            while (begin > 0 && COMMENT.test(lines[begin - 1])) {
                begin -= 1;
            }
            let end = at;
            while (end + 1 < lines.length && COMMENT.test(lines[end + 1])) {
                end += 1;
            }
            const block = lines.slice(begin, end + 1).join("\n");
            const owner = OWNERS.find(([, pattern]) => pattern.test(block));
            rows.push({
                file,
                line: at + 1,
                text: firstSentence(block),
                owner: owner ? owner[0] : null,
                suggested: (SUGGEST.find(([pattern]) => pattern.test(file)) || [null, "encoder"])[1],
            });
            at = end;
        }
    }
    return rows;
};

if (argv.includes("--census")) {
    const rows = census();
    const unnamed = rows.filter((row) => row.owner === null);
    console.log(`${rows.length} invariant comments in ${FILES.length} files; ${unnamed.length} name no owner`);
    console.log("");
    console.log("| file:line | first sentence | owner named | should belong to |");
    console.log("|---|---|---|---|");
    for (const row of rows) {
        const text = row.text.replace(/\|/g, "\\|");
        console.log(`| ${row.file}:${row.line} | ${text} | ${row.owner ?? "**none**"} | ${row.suggested} |`);
    }
    process.exit(0);
}
let diff;
try {
    diff = git(["diff", "--unified=0", `${base}...${rev}`]);
} catch {
    console.error(`check-invariant-owner: cannot diff ${base}...${rev}`);
    process.exit(2);
}

/** Added comment lines, grouped by the file that added them. */
const added = new Map();
let file = null;
for (const line of diff.split("\n")) {
    if (line.startsWith("+++ b/")) {
        file = line.slice(6);
        continue;
    }
    if (file === null || !line.startsWith("+") || line.startsWith("+++")) {
        continue;
    }
    const text = line.slice(1);
    if (!COMMENT.test(text) || !INVARIANT.test(text)) {
        continue;
    }
    if (!added.has(file)) {
        added.set(file, []);
    }
    added.get(file).push(text.trim());
}

const unnamed = [];
for (const [where, lines] of added) {
    const joined = lines.join("\n");
    if (!OWNERS.some(([, pattern]) => pattern.test(joined))) {
        unnamed.push({ where, first: lines[0] });
    }
}

if (unnamed.length === 0) {
    console.log(
        `INVARIANT-OWNER ok: ${added.size} file(s) add an invariant comment and each names its owner`
    );
    process.exit(0);
}

console.log(
    `INVARIANT-OWNER would flag ${unnamed.length} of ${added.size} file(s): an invariant comment ` +
        `that does not say where it is owned (${OWNERS.map(([name]) => name).join(" / ")})`
);
for (const { where, first } of unnamed) {
    console.log(`  ${where}: ${first.slice(0, 120)}`);
}
// A skeleton: reports, does not gate. The exit stays 0 until the vocabulary has settled.
process.exit(0);