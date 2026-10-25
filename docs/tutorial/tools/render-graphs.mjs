#!/usr/bin/env node
// Draws the tutorial's graph pictures from the examples the pages name and keeps them from
// drifting away from those examples.
//
//   node docs/tutorial/tools/render-graphs.mjs           redraw every picture
//   node docs/tutorial/tools/render-graphs.mjs --check   redraw in memory and compare, changing nothing
//
// Each picture has a source (an example under docs/tutorial/examples) and the passes the page
// names in its command fence; the script runs `camel <source> <passes...>` and takes the DOT the
// dump prints, so the picture is the graph this tree draws, not a drawing kept by hand. A picture
// may name a `cluster` to keep only that subgraph, which is how the `twice` before/after pair is
// made, and a `title` for the SVG's <title>.
//
// Two files are written per picture, both in docs/tutorial/images:
//   <name>.dot  the normalised DOT the picture was drawn from (committed: this is what is compared)
//   <name>.svg  the picture itself, drawn with Graphviz `dot`
// Position attributes (`origin`, `span`, `srcfile`) and the node ids inside `tooltip` are dropped
// before comparing: they move whenever the example or the compiler's numbering moves, and they are
// exactly the noise a reader does not want in the picture. Everything else is compared, so a change
// in the graph's shape fails `--check` until the picture is drawn again.
//
// `camel` and `dot` are taken from PATH, or from $CAMEL and $DOT, or from `--camel <path>` and
// `--dot <path>`; `--images <dir>` compares (or writes) under another directory, which is how the
// planted-drift case of the plan below proves the check can go red. `--check` needs `camel` only --
// it draws nothing, it compares -- and `dot` is needed to write a picture; the gate runs the check
// in test/plans/feat/docs/tutorial_images.plan.toml, and a picture that has drifted fails there.

import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const TUTORIAL = path.resolve(HERE, '..');

function option(name, fallback) {
    const at = process.argv.indexOf(`--${name}`);
    return at >= 0 && process.argv[at + 1] ? process.argv[at + 1] : fallback;
}

const IMAGES = path.resolve(option('images', path.join(TUTORIAL, 'images')));
const CAMEL = option('camel', process.env.CAMEL || 'camel');
const DOT = option('dot', process.env.DOT || 'dot');

// The pictures the tutorial shows, in the order the chapters use them.
const SHEETS = [
    {
        name: '01_hello',
        source: 'examples/01_hello.cml',
        passes: ['std::rgir'],
        title: 'main 的子图与入口对它的调用',
    },
    {
        name: '01_twice_before',
        source: 'examples/01_rewrite.cml',
        passes: ['std::rgir'],
        cluster: 'twice',
        title: 'std::opt 之前：两个乘法加一个加法',
    },
    {
        name: '01_twice_after',
        source: 'examples/01_rewrite.cml',
        passes: ['std::opt', 'std::rgir'],
        cluster: 'twice',
        title: 'std::opt 之后：一个乘法乘常量 4',
    },
    {
        name: '09_dependency',
        source: 'examples/09_dependency.cml',
        passes: ['std::rgir'],
        cluster: 'total',
        title: 'total 的子图：谁要谁的结果',
    },
    {
        name: '10_var_order',
        source: 'examples/10_var_order.cml',
        passes: ['std::rgir'],
        cluster: 'twice',
        title: 'twice 的子图：格子和两次写之间的同步边',
    },
    {
        name: '17_rule_negated',
        source: 'examples/17_rule.cml',
        passes: ['std::opt', 'std::rgir'],
        cluster: 'negated',
        title: '自己的定律没有交出去：x * -1 原样留着',
    },
    {
        name: '17_rule_combined',
        source: 'examples/17_rule.cml',
        passes: ['std::opt', 'std::rgir'],
        cluster: 'combined',
        title: '标准库的定律：x * 3 + x * 5 剩下一乘',
    },
    {
        name: '17_rule_saturate',
        source: 'examples/17_rule.cml',
        passes: ['std::opt::saturate', 'std::rgir'],
        cluster: 'doubled',
        title: 'std::opt::saturate：一个值的几种写法都留着',
    },
];

function dump(sheet) {
    const file = path.join(TUTORIAL, sheet.source);
    if (!fs.existsSync(file)) throw new Error(`${sheet.source} does not exist`);
    const args = [file, ...sheet.passes];
    const out = execFileSync(CAMEL, args, { cwd: TUTORIAL, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
    if (!out.includes('digraph')) throw new Error(`${CAMEL} ${args.join(' ')} printed no DOT`);
    return out;
}

// The block of a `subgraph cluster_*` whose own label is `label`, braces included and nested
// clusters kept whole. Clusters nest (a function's subgraph sits inside the entry's), so every
// opener is examined, not only the outermost.
function clusterOf(dot, label) {
    const lines = dot.split('\n');
    for (let i = 0; i < lines.length; i++) {
        if (!/^\s*subgraph cluster_\w+ \{\s*$/.test(lines[i])) continue;
        let depth = 0;
        const block = [];
        for (let j = i; j < lines.length; j++) {
            block.push(lines[j]);
            depth += (lines[j].match(/\{/g) || []).length - (lines[j].match(/\}/g) || []).length;
            if (depth === 0) break;
        }
        const named = block.find((line) => /^\s*label="/.test(line));
        if (named && named.trim() === `label="${label}";`) return block.join('\n');
    }
    throw new Error(`no cluster labelled "${label}" in the dump`);
}

function normalise(dot) {
    return dot
        .split('\n')
        .map((line) =>
            line
                .replace(/,? ?origin=\d+/g, '')
                .replace(/,? ?span="[^"]*"/g, '')
                .replace(/,? ?srcfile="[^"]*"/g, '')
                .replace(/tooltip="([^"\\]*)\\n[^"]*"/g, 'tooltip="$1"')
                .replace(/[ \t]+$/g, ''),
        )
        .join('\n')
        .replace(/\n{2,}/g, '\n')
        .replace(/\n+$/, '')
        .concat('\n');
}

function svgOf(dot, title) {
    const withTitle = dot.replace('digraph GraphIR {', `digraph GraphIR {\n    label="${title}";\n    labelloc="t";`);
    return execFileSync(DOT, ['-Tsvg'], { input: withTitle, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
}

const check = process.argv.includes('--check');
fs.mkdirSync(IMAGES, { recursive: true });

const failed = [];
let written = 0;
for (const sheet of SHEETS) {
    const dotFile = path.join(IMAGES, `${sheet.name}.dot`);
    const svgFile = path.join(IMAGES, `${sheet.name}.svg`);
    let dot;
    try {
        dot = dump(sheet);
        if (sheet.cluster) dot = `digraph GraphIR {\n${clusterOf(dot, sheet.cluster)}\n}\n`;
        dot = normalise(dot);
    } catch (error) {
        failed.push(`${sheet.name}: ${error.message}`);
        continue;
    }
    if (check) {
        if (!fs.existsSync(dotFile) || !fs.existsSync(svgFile)) {
            failed.push(`${sheet.name}: ${fs.existsSync(dotFile) ? '' : `${sheet.name}.dot `}${fs.existsSync(svgFile) ? '' : `${sheet.name}.svg`} is not drawn yet`);
            continue;
        }
        const svg = fs.readFileSync(svgFile, 'utf8');
        if (!svg.includes('<svg') || svg.length < 200) failed.push(`${sheet.name}: ${sheet.name}.svg is not a picture`);
        const kept = fs.readFileSync(dotFile, 'utf8');
        if (kept !== dot) failed.push(`${sheet.name}: the graph this tree draws is not the graph the picture was drawn from`);
        continue;
    }
    fs.writeFileSync(dotFile, dot);
    fs.writeFileSync(svgFile, svgOf(dot, sheet.title));
    written++;
    console.log(`drew ${path.relative(path.resolve(TUTORIAL, '..', '..'), svgFile)}`);
}

for (const problem of failed) console.log(problem);
if (failed.length) {
    console.log(`${failed.length} picture(s) could not be drawn`);
    process.exit(1);
}
if (check) {
    console.log(`${SHEETS.length} picture(s) are what the examples draw`);
} else {
    console.log(`${written} picture(s) drawn into docs/tutorial/images`);
}