#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * A value with several writings is *drawn*, and this case is its guard: it runs the same program
 * (`saturate_draw.cml`, where `x + x` and `x * 2` are two writings of one value) through the
 * pipelines that draw that state and checks the drawing as a property of the dump — never as a
 * count of lines that happen to be there.
 *
 * What it checks, and why each one is a property:
 *
 *   * every class is one dashed cluster holding one class node (`type="CLASS"`) and two or more
 *     writings, all of them naming that one class, all of one type;
 *   * no writing is marked and no cost is printed: `chosen` and `writings_cost` are gone, and not
 *     even `unknown` takes their place — what a writing costs is the target library's judgement,
 *     and the kernel has none of its own;
 *   * the value a graph returns is read from its class, not from one writing: the `EXIT` edge of
 *     the graph that holds the pair leaves the class node;
 *   * `std::jgir` carries the same classes as `classes` (`id`, `writings`), and
 *     `std::jgir egraph=true` writes the document `egraph-serialize` reads, whose every child is an
 *     e-class of the document, whose roots are e-classes, in which the two writings are one
 *     e-class, and whose `class_data` is empty;
 *   * a pass that consumes the program without a named extraction is refused (the driver used to
 *     extract by itself and no longer does), and with `rewrite::extract cost=rules::nodes` the
 *     program prints 42;
 *   * the pass declares `rounds` and `strict`, and a parameter written after it reaches it: 0
 *     rounds applies no rule at all (so the check above refuses that dump), 1 round already leaves
 *     the pair in one class, a bit-for-bit pass keeps that pair, an int takes a constant
 *     expression, and a bool where the declaration says int is refused at the call.
 *
 * Every pipeline that draws the state ends in `std::nop`, which is the recipe design principles 9.1
 * gives for looking at a graph without running it: a dump hands the program on, so without the
 * `std::nop` the pipeline falls through to `std::default`, which is a pass that executes and is
 * therefore refused with `PassNeedsExtraction` -- and its complaint would be printed into the dump
 * this case reads.
 *
 * The guard is shown red on a dump that lacks the drawing: `guard` mode runs the pipeline without
 * `std::cxx::saturate`, where the value has one writing and no cluster can exist, and requires
 * the *same* check to refuse it, naming the case file and the dump line of the writing that
 * stands alone. A check that cannot refuse anything is not evidence, so the red half is part of
 * the case and not a note about it.
 *
 *   node draw_classes.mjs gir|rgir|jgir|egraph|guard|params|extract
 */

import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import url from 'node:url'
import { spawnSync } from 'node:child_process'

const HERE = path.dirname(url.fileURLToPath(import.meta.url))
const REPO_ROOT = path.resolve(HERE, '..', '..', '..', '..', '..')
const HOME = process.env.CAMEL_HOME || path.join(REPO_ROOT, 'out', 'latest')
const CAMEL = path.join(HOME, 'bin', process.platform === 'win32' ? 'camel.exe' : 'camel')
const PROGRAM = path.join(HERE, 'saturate_draw.cml')
// What a failure names: the case a reader can open, never an absolute path of this machine.
const CASE = path.relative(REPO_ROOT, PROGRAM).replace(/\\/g, '/')
const SCRATCH =
    process.env.CAMEL_TEST_TMPDIR || fs.mkdtempSync(path.join(os.tmpdir(), 'camel-draw-'))
const MODE = process.argv[2] || 'gir'

let failed = false
function check(label, ok, detail) {
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}${ok ? '' : ': ' + detail}`)
    if (!ok) failed = true
}

function camel(args) {
    const env = { ...process.env, CAMEL_HOME: HOME }
    return spawnSync(CAMEL, args, { env, encoding: 'utf8', cwd: SCRATCH, timeout: 60000 })
}
function run(specs) {
    const result = camel([PROGRAM, ...specs])
    return {
        status: result.status,
        stdout: result.stdout || '',
        output: `${result.stdout || ''}${result.stderr || ''}`,
    }
}

// ---- the drawing, read as a property of the dump -------------------------------------------

const attribute = (text, name) => {
    const match = new RegExp(`${name}="([^"]*)"`).exec(text)
    return match ? match[1] : null
}
const nodeName = (text) => {
    const match = /^\s*([A-Za-z_][A-Za-z0-9_]*)\s*\[/.exec(text)
    return match ? match[1] : null
}
// A writing of the pair this case is about: the operator's own label carries `add` or `mul`.
const isAdd = (text) => /label="[^"]*add/i.test(text)
const isMul = (text) => /label="[^"]*mul/i.test(text)

/// Every `subgraph cluster_class_…` of a dump, with the lines inside it (their dump line numbers
/// kept, so a refusal can name one).
function classClusters(dot) {
    const lines = dot.split(/\r?\n/)
    const clusters = []
    for (let i = 0; i < lines.length; i++) {
        const header = /^\s*subgraph cluster_class_(\S+) \{\s*$/.exec(lines[i])
        if (!header) continue
        const body = []
        let depth = 0
        let j = i + 1
        for (; j < lines.length; j++) {
            const opens = (lines[j].match(/\{/g) || []).length
            const closes = (lines[j].match(/\}/g) || []).length
            if (depth === 0 && closes > 0) break
            depth += opens - closes
            body.push({ line: j + 1, text: lines[j] })
        }
        clusters.push({ ident: header[1], line: i + 1, body })
        i = j
    }
    return clusters
}

/// Everything the drawing must be, as refusals that name the case and the line they caught.
/// `rich` adds what only the styled dump promises (`style=dashed` on the cluster). The list is
/// empty exactly when the dump draws the state.
function drawingProblems(dot, rich = false) {
    const problems = []
    const lines = dot.split(/\r?\n/)
    const clusters = classClusters(dot)
    if (clusters.length === 0) {
        const alone = lines.findIndex((text) => /type="OPER"/.test(text))
        problems.push(
            `${CASE}: the writing drawn at dump line ${alone + 1} is not in a class: ` +
                `the dump has no \`subgraph cluster_class_…\`, so the value has one writing`
        )
        return problems
    }
    const pairs = []
    for (const cluster of clusters) {
        const members = cluster.body.filter(({ text }) => attribute(text, 'class') !== null)
        const classNodes = cluster.body.filter(({ text }) => attribute(text, 'type') === 'CLASS')
        const names = new Set(members.map(({ text }) => attribute(text, 'class')))
        const types = new Set(members.map(({ text }) => attribute(text, 'value_type')))
        if (classNodes.length !== 1) {
            problems.push(
                `${CASE}: the class at dump line ${cluster.line} has ${classNodes.length} class ` +
                    `node(s), not one`
            )
        }
        if (members.length < 2) {
            problems.push(
                `${CASE}: the class at dump line ${cluster.line} holds ${members.length} ` +
                    `writing(s): a class is a value with several writings`
            )
        }
        if (names.size !== 1) {
            problems.push(
                `${CASE}: the writings at dump line ${cluster.line} name ${names.size} classes`
            )
        }
        if (classNodes.length === 1 && names.size === 1) {
            const label = attribute(classNodes[0].text, 'label')
            if (label !== `class ${[...names][0]}`) {
                problems.push(
                    `${CASE}: the class node at dump line ${classNodes[0].line} is labelled ` +
                        `'${label}', and its writings name '${[...names][0]}'`
                )
            }
        }
        if (types.size > 1) {
            problems.push(
                `${CASE}: the writings at dump line ${cluster.line} have ${types.size} types ` +
                    `(${[...types].join(', ')}): one class is one value, one type`
            )
        }
        if (rich && classNodes.length === 1) {
            const dashed = cluster.body.some(({ text }) => /style=dashed/.test(text))
            if (!dashed) {
                problems.push(
                    `${CASE}: the class at dump line ${cluster.line} is not drawn dashed`
                )
            }
        }
        const add = members.find(({ text }) => isAdd(text))
        const mul = members.find(({ text }) => isMul(text))
        if (add && mul) {
            pairs.push({ cluster, add, mul })
        }
    }
    if (pairs.length === 0) {
        problems.push(
            `${CASE}: no class of the dump holds an add and a mul writing together, ` +
                `so \`x + x\` and \`x * 2\` are not shown as one value`
        )
        return problems
    }
    // `x + x` and `x * 2` is the pair this case exists for: one class, two writings. No cost is
    // read and none is drawn -- not a mark on a writing, not a model's name -- because the cost is
    // the target library's judgement and the kernel has none.
    const { add, mul } = pairs[0]
    if (/chosen\s*=/.test(dot)) {
        problems.push(`${CASE}: the dump marks a writing as the one to keep (\`chosen\`)`)
    }
    if (/cost\s*=/.test(dot)) {
        problems.push(`${CASE}: the dump names a cost (\`cost\`, \`writings_cost\`)`)
    }
    if (add.line === mul.line) {
        problems.push(`${CASE}: the add and the mul of the pair are the same line`)
    }
    // The value the graph returns is a value, so its edge leaves the class and not a writing.
    const classNode = pairs[0].cluster.body.find(
        ({ text }) => attribute(text, 'type') === 'CLASS'
    )
    const ident = classNode ? nodeName(classNode.text) : null
    const exit = lines.findIndex(
        (text) => ident !== null && /^\s*\S+\s*->/.test(text) && /type="EXIT"/.test(text) &&
            new RegExp(`^\\s*${ident}\\s*->`).test(text)
    )
    if (exit < 0) {
        problems.push(
            `${CASE}: the graph's result is read from the writing, not from the class node ` +
                `${ident} the class holds`
        )
    }
    return problems
}

// ---- the modes ------------------------------------------------------------------------------

const verdicts = []

if (MODE === 'gir' || MODE === 'rgir') {
    const rich = MODE === 'rgir'
    const result = run(['std::cxx::saturate', rich ? 'std::rgir' : 'std::gir', 'std::nop'])
    check(`${MODE} exit`, result.status === 0, result.output)
    const problems = drawingProblems(result.stdout, rich)
    check(`${MODE} drawing`, problems.length === 0, problems.join(' | '))
    if (problems.length === 0) {
        const clusters = classClusters(result.stdout)
        const pair = clusters.find(
            (cluster) =>
                cluster.body.some(({ text }) => isAdd(text)) &&
                cluster.body.some(({ text }) => isMul(text))
        )
        const writings = pair.body.filter(({ text }) => attribute(text, 'class') !== null)
        console.log(
            `DRAW ${MODE} classes=${clusters.length} pair=add+mul pair_writings=${writings.length} ` +
                `marked=0 cost=none${rich ? ' dashed=1' : ''}`
        )
    }
    verdicts.push(`DRAW-${MODE}`)
} else if (MODE === 'jgir') {
    const result = run(['std::cxx::saturate', 'std::jgir', 'std::nop'])
    check('jgir exit', result.status === 0, result.output)
    let document = null
    try {
        document = JSON.parse(result.stdout)
    } catch (error) {
        check('jgir parses', false, `${error.message} :: ${result.stdout.slice(0, 200)}`)
    }
    if (document) {
        const graphs = document.graphs || []
        const carried = graphs.filter((graph) => Array.isArray(graph.classes))
        const writings = carried.flatMap((graph) => graph.classes)
        const labels = new Map()
        for (const graph of graphs) {
            for (const node of graph.nodes || []) labels.set(node.id, node.label)
        }
        const pair = writings.find((entry) => {
            const names = entry.writings.map((id) => labels.get(id) || '')
            return names.some((name) => /add/i.test(name)) && names.some((name) => /mul/i.test(name))
        })
        check('jgir carries the classes', writings.length > 0, JSON.stringify(document.graphs?.[0]?.classes))
        check(
            'jgir every class holds several writings and nothing about a cost',
            writings.every(
                (entry) =>
                    Array.isArray(entry.writings) &&
                    entry.writings.length >= 2 &&
                    Object.keys(entry).sort().join(',') === 'id,writings'
            ),
            JSON.stringify(writings)
        )
        check('jgir the pair is one class', Boolean(pair), JSON.stringify(writings))
        if (pair) {
            console.log(
                `DRAW jgir classes=${writings.length} pair=add+mul ` +
                    `pair_writings=${pair.writings.length} chosen=none cost=none`
            )
        }
    }
    verdicts.push('DRAW-jgir')
} else if (MODE === 'egraph') {
    const result = run(['std::cxx::saturate', 'std::jgir', 'egraph=true', 'std::nop'])
    check('egraph exit', result.status === 0, result.output)
    let document = null
    try {
        document = JSON.parse(result.stdout)
    } catch (error) {
        check('egraph parses', false, `${error.message} :: ${result.stdout.slice(0, 200)}`)
    }
    if (document) {
        const keys = Object.keys(document).sort().join(',')
        check(
            'egraph the document is the crate\'s four keys',
            keys === 'class_data,nodes,root_eclasses',
            `keys=${keys}`
        )
        const nodes = document.nodes || {}
        const classes = new Set(Object.values(nodes).map((node) => node.eclass))
        const dangling = []
        for (const [id, node] of Object.entries(nodes)) {
            for (const child of node.children || []) {
                if (!classes.has(child)) dangling.push(`${id}->${child}`)
            }
            if (typeof node.op !== 'string' || node.op.length === 0) dangling.push(`${id}: no op`)
        }
        check('egraph every child is an e-class of the document', dangling.length === 0, dangling.slice(0, 4).join(', '))
        const roots = document.root_eclasses || []
        check(
            'egraph the roots are e-classes',
            roots.length > 0 && roots.every((root) => classes.has(root)),
            JSON.stringify(roots)
        )
        const add = Object.entries(nodes).find(([, node]) => /add/i.test(node.op))
        const mul = Object.entries(nodes).find(([, node]) => /mul/i.test(node.op))
        check(
            'egraph the two writings are one e-class',
            Boolean(add) && Boolean(mul) && add[1].eclass === mul[1].eclass,
            `add=${add && add[1].eclass} mul=${mul && mul[1].eclass}`
        )
        const data = document.class_data || {}
        check(
            'egraph the document reports no cost: class_data is empty and no node carries one',
            Object.keys(data).length === 0 &&
                Object.values(nodes).every((node) => !('cost' in node)),
            JSON.stringify(data).slice(0, 200)
        )
        if (add && mul && add[1].eclass === mul[1].eclass) {
            console.log(
                `DRAW egraph nodes=${Object.keys(nodes).length} classes=${classes.size} ` +
                    `roots=${roots.length} pair=one-eclass eclass=${add[1].eclass}`
            )
        }
    }
    verdicts.push('DRAW-egraph')
} else if (MODE === 'guard') {
    // The red half first: without the saturation the program has one writing per value, and the
    // same check must refuse it, naming the case and the line of the writing that stands alone.
    const plain = run(['std::gir', 'std::nop'])
    check('guard plain exit', plain.status === 0, plain.output)
    const refusal = drawingProblems(plain.stdout)
    check('guard refuses a graph without the drawing', refusal.length > 0, 'the check accepted a dump with no class')
    const named = refusal.length > 0 ? /^(.*?): the writing drawn at dump line (\d+) is not in a class/.exec(refusal[0]) : null
    check(
        'guard names the case file and the dump line it caught',
        Boolean(named) && named[1] === CASE,
        refusal[0] || ''
    )
    if (named) {
        const line = plain.stdout.split(/\r?\n/)[Number(named[2]) - 1] || ''
        check(
            `guard the line it caught is the writing itself`,
            /type="(OPER|DATA)"/.test(line),
            `dump line ${named[2]}: ${line.trim()}`
        )
        console.log(`DRAW guard red=${CASE}:${named[2]}`)
    }
    // The green half: the very same check accepts the dump the saturation produces.
    const saturated = run(['std::cxx::saturate', 'std::gir', 'std::nop'])
    check('guard saturated exit', saturated.status === 0, saturated.output)
    const problems = drawingProblems(saturated.stdout)
    check('guard accepts the drawing', problems.length === 0, problems.join(' | '))
    if (problems.length === 0) {
        const clusters = classClusters(saturated.stdout)
        console.log(`DRAW guard green classes=${clusters.length}`)
    }
    verdicts.push('DRAW-guard')
} else if (MODE === 'extract') {
    // Nothing is extracted by the pass, and nothing extracts on the way either: the pipeline names
    // the extraction and its cost, and a pass that consumes the program without one is refused.
    const refusedBackend = run(['std::cxx::saturate', 'std::nvm'])
    check(
        'extract a backend is refused the state',
        refusedBackend.status === 1 &&
            /PassNeedsExtraction/.test(refusedBackend.output) &&
            /rewrite::extract cost=/.test(refusedBackend.output),
        refusedBackend.output
    )
    const extracted = run(['std::cxx::saturate', 'rewrite::extract cost=rules::nodes', 'std::nvm'])
    check(
        'extract one writing per value still runs',
        extracted.status === 0 && /(^|\n)42(\r?\n|$)/.test(extracted.stdout),
        extracted.output
    )
    // A rewrite pass that does not declare that it accepts the state is refused too: the rule is
    // about every pass that did not ask for the state.
    const refused = run(['std::cxx::saturate', 'std::cxx::fold', 'std::nvm'])
    check(
        'extract a rewrite pass is refused the state',
        refused.status === 1 && /PassNeedsExtraction/.test(refused.output) && /std::cxx::fold/.test(refused.output),
        refused.output
    )
    if (!failed) {
        console.log('DRAW extract printed=42 refused_fold=1 refused_backend=1')
    }
    verdicts.push('DRAW-extract')
} else if (MODE === 'params') {
    // The pass declares `rounds: int = 6` and `strict: bool = false`; a parameter reaches the
    // pass, and the one that bounds the work is read as the declaration says.
    const zero = run(['std::cxx::saturate', 'rounds=0', 'std::gir', 'std::nop'])
    check('params rounds=0 exit', zero.status === 0, zero.output)
    check(
        'params rounds=0 applies no rule: the program keeps its writings',
        drawingProblems(zero.stdout).length > 0,
        'rounds=0 left a class behind'
    )
    const one = run(['std::cxx::saturate', 'rounds=1', 'std::gir', 'std::nop'])
    check(
        'params rounds=1 reaches the state',
        one.status === 0 && drawingProblems(one.stdout).length === 0,
        one.output
    )
    const strict = run(['std::cxx::saturate', 'strict=true', 'std::gir', 'std::nop'])
    check(
        'params strict=true keeps the rules that hold bit for bit',
        strict.status === 0 && drawingProblems(strict.stdout).length === 0,
        strict.output
    )
    const expression = run(['std::cxx::saturate', 'rounds=1+1', 'std::gir', 'std::nop'])
    check(
        'params an int parameter takes a constant expression',
        expression.status === 0 && drawingProblems(expression.stdout).length === 0,
        expression.output
    )
    const wrong = run(['std::cxx::saturate', 'rounds=true', 'std::gir', 'std::nop'])
    check(
        'params the declared type is read: a bool where an int is declared is refused at the call',
        wrong.status === 1 &&
            /The argument 'rounds' of 'std::cxx::saturate' has type 'bool', not 'int'/.test(
                wrong.output
            ),
        wrong.output
    )
    if (!failed) {
        console.log(
            'DRAW params rounds0=no-class rounds1=one-class strict=one-class expression=ok wrongtype=refused'
        )
    }
    verdicts.push('DRAW-params')
} else {
    check(`mode '${MODE}'`, false, 'gir, rgir, jgir, egraph, guard, params or extract')
}

console.log(`draw classes ${MODE}: ${failed ? 'FAIL' : 'PASS'}`)
process.exit(failed ? 1 : 0)