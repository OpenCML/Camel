#!/usr/bin/env node
// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Emit the 9 x 6 argument-shape matrix the review page quotes: one small program per (entry point, shape).
// It is a measurement tool, not a test: nothing here asserts a reading, because the readings it produces
// include the ones that must never become expectations (the page records which). Run it, copy the output
// directory to a machine with a built tree, and run each file with `camel <file>.cml std::nvm`.
import fs from 'fs'

const OUT = 'D:/Temp/mx4'
fs.rmSync(OUT, { recursive: true, force: true })
fs.mkdirSync(OUT, { recursive: true })

const ROWS = ['transpose', 'jvp', 'split', 'vmap', 'remat', 'grad', 'grad_aux', 'with_grad', 'with_grad_aux']
const SHAPES = ['s1', 's2', 's3', 's4', 's5', 's6']

const IMPORTS = {
    // The ruleset a transform reads is part of the program being measured: without
    // `rulesets.transpose` the transposed function finds no family (`skip=rule uri=:op/mul_d
    // reason=no-such-family`) and the cell measures the missing import, not the shape.
    transpose: 'import { transpose } from transforms\nimport rulesets.transpose',
    jvp: 'import { jvp } from transforms\nimport rulesets.jvp',
    split: 'import { split } from transforms\nimport rulesets.jvp',
    vmap: 'import { vmap } from transforms\nimport rulesets.vmap',
    remat: 'import { remat } from transforms',
    grad: 'import { grad } from autodiff\nimport rulesets.jvp\nimport rulesets.transpose',
    grad_aux: 'import { grad } from autodiff\nimport rulesets.jvp\nimport rulesets.transpose',
    with_grad: 'import { with_grad } from autodiff\nimport rulesets.jvp\nimport rulesets.transpose',
    with_grad_aux: 'import { with_grad } from autodiff\nimport rulesets.jvp\nimport rulesets.transpose',
}

// Each row: how to call it on an argument expression, what it returns (for the `via` shape), and how
// main prints it.
const ROW = {
    transpose: {
        type: 'float',
        mainCall: (arg) => `    let back = transpose(${arg})\n    println(back(3.0, 1.0))`,
        viaBody: (arg) => `    let back = transpose(${arg})\n    return back(3.0, 1.0)`,
        viaPrint: 'println(via(dbl))',
        inSig: '(x: float) => float',
    },
    jvp: {
        type: '(float, float)',
        mainCall: (arg) => `    let lifted = jvp(${arg})\n    let (v, t) = lifted(2.0, 1.0)\n    println(v)\n    println(t)`,
        viaBody: (arg) => `    let lifted = jvp(${arg})\n    return lifted(2.0, 1.0)`,
        viaPrint: 'let (v, t) = via(dbl)\n    println(v)\n    println(t)',
        inSig: '(y: float) => float',
    },
    split: {
        type: '(float, float)',
        mainCall: (arg) => `    let step = split(${arg})\n    let (v, t) = step(2.0, 1.0)\n    println(v)\n    println(t)`,
        viaBody: (arg) => `    let step = split(${arg})\n    return step(2.0, 1.0)`,
        viaPrint: 'let (v, t) = via(dbl)\n    println(v)\n    println(t)',
        inSig: '(v: float, d: float) => (float, float)',
    },
    vmap: {
        type: 'float[]',
        mainCall: (arg) => `    let batched = vmap(${arg})\n    let out = batched([1.0, 2.0])\n    println(out[0])\n    println(out[1])`,
        viaBody: (arg) => `    let batched = vmap(${arg})\n    return batched([1.0, 2.0])`,
        viaPrint: 'let out = via(dbl)\n    println(out[0])\n    println(out[1])',
        inSig: '(y: float) => float',
    },
    remat: {
        type: 'float',
        mainCall: (arg) => `    let again = remat(${arg})\n    println(again(2.0))`,
        viaBody: (arg) => `    let again = remat(${arg})\n    return again(2.0)`,
        viaPrint: 'println(via(dbl))',
        inSig: '(y: float) => float',
    },
    grad: {
        type: 'float',
        mainCall: (arg) => `    let g = grad(${arg})\n    println(g<3.0>(2.0))`,
        viaBody: (arg) => `    let g = grad(${arg})\n    return g<3.0>(2.0)`,
        viaPrint: 'println(via(dbl))',
        inSig: '<float> (x: float) => float',
    },
    grad_aux: {
        type: '(float, float)',
        mainCall: (arg) => `    let g = grad(${arg}, true)\n    println(g<3.0>(2.0))`,
        viaBody: (arg) => `    let g = grad(${arg}, true)\n    return g<3.0>(2.0)`,
        viaPrint: 'println(via(dbl))',
        inSig: '<float> (x: float) => (float, float)',
    },
    with_grad: {
        type: '(float, float)',
        mainCall: (arg) => `    let vg = with_grad(${arg})\n    let (v, g) = vg<3.0>(2.0)\n    println(v)\n    println(g)`,
        viaBody: (arg) => `    let vg = with_grad(${arg})\n    return vg<3.0>(2.0)`,
        viaPrint: 'let (v, g) = via(dbl)\n    println(v)\n    println(g)',
        inSig: '<float> (x: float) => float',
    },
    with_grad_aux: {
        type: '(float, float)',
        mainCall: (arg) => `    let vg = with_grad(${arg}, true)\n    let (v, g) = vg<3.0>(2.0)\n    println(v)\n    println(g)`,
        viaBody: (arg) => `    let vg = with_grad(${arg}, true)\n    return vg<3.0>(2.0)`,
        viaPrint: 'let (v, g) = via(dbl)\n    println(v)\n    println(g)',
        inSig: '<float> (x: float) => (float, float)',
    },
}

const isAux = (row) => row === 'grad_aux' || row === 'with_grad_aux'
const isAutodiff = (row) => row === 'grad' || row === 'grad_aux' || row === 'with_grad' || row === 'with_grad_aux'

function declared(row) {
    if (row === 'split') return 'func dbl(v: float, d: float): (float, float) {\n    return (v * 2.0, d * 2.0)\n}'
    if (isAux(row)) return 'with <w: float>\nfunc dbl(x: float): (float, float) {\n    return (x * w, 1.0)\n}'
    if (isAutodiff(row)) return 'with <w: float>\nfunc dbl(x: float): float {\n    return x * w\n}'
    return 'func dbl(y: float): float {\n    return y * 2.0\n}'
}

function lambda(row, captured) {
    const x = captured ? 'x' : '2.0'
    if (row === 'split') return `(v: float, d: float) => (v * ${x}, d * ${x})`
    if (isAux(row)) return `(y: float) => (y * ${x}, 1.0)`
    return `(y: float) => y * ${x}`
}

const written = []
function emit(name, text) {
    fs.writeFileSync(`${OUT}/${name}.cml`, text)
    written.push(name)
}
function head(row) {
    return IMPORTS[row] + '\n\n'
}
function plain(row, arg, pre = []) {
    return head(row) + (pre.length ? pre.join('\n') + '\n\n' : '') + 'func main(): int sync {\n' + ROW[row].mainCall(arg) + '\n    return 0\n}\n'
}

for (const row of ROWS) {
    emit(`${row}-s1`, plain(row, 'dbl', [declared(row)]))
    emit(`${row}-s2`, plain(row, lambda(row, false)))
    emit(`${row}-s3`, plain(row, lambda(row, true), ['let x = 3.0']))
    emit(`${row}-s5`, head(row) + 'func main(): int sync {\n    let x = 3.0\n    let h = ' + lambda(row, true) + '\n' + ROW[row].mainCall('h') + '\n    return 0\n}\n')
    // s4: the transform is applied inside a function that receives the callee as a parameter.
    emit(
        `${row}-s4`,
        head(row) + declared(row) + '\n\nfunc via(f: ' + ROW[row].inSig + '): ' + ROW[row].type + ' {\n' + ROW[row].viaBody('f') + '\n}\n\nfunc main(): int sync {\n    ' + ROW[row].viaPrint + '\n    return 0\n}\n',
    )
}
console.log(`wrote ${written.length} programs`)
for (const name of ['transpose-s1', 'jvp-s4', 'grad-s4', 'vmap-s5']) {
    console.log('---', name)
    console.log(fs.readFileSync(`${OUT}/${name}.cml`, 'utf8'))
}