// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// "The graph was always there", beats 1-3 (the proof of concept): a frame is `draw(t)`.
//
// Every claim on screen is checked (../../script.md, "Claims and sources"):
// - the tear messages are what torch.compile 2.14.1 and jax.jit 0.12 print for this Python code
//   (Data-dependent branching, Tensor.item(), print, a recompile on `len(xs) == 3`;
//   TracerBoolConversionError, ConcretizationTypeError, a tracer printed once at trace time);
// - the Camel graph is the GIR `camel reveal.cml std::gir` prints for `loss`: its nodes, its edges,
//   the branch (BRCH, the two arms, JOIN), and each node's source span, which is where its
//   characters fly from; the two results are what the program prints (0.91875 and 1.8375).
import * as THREE from 'three';
import {
    add3, arrowHead, bezier, clamp, densify, diamond, drawPoly, ease, easeIn, easeOut, lin, mix, mix3, mixCol,
    pointAt, rand, roundRect, scale3, shard, srand, sub3, withA,
} from './geom.js';

export const BEAT = 0.625;
export const DURATION = 30;

const T = {
    pyType0: 0.45,
    pyType1: 3.0,
    trace0: 3.2,
    tear1: 9 * BEAT,
    tear2: 12 * BEAT,
    tear3: 14 * BEAT,
    call2: 15 * BEAT,
    tear4: 16 * BEAT,
    line1: 18 * BEAT,
    turn: 20 * BEAT,
    question: 22 * BEAT,
    questionOut: 27 * BEAT,
    gather: 28 * BEAT,
    camType0: 29 * BEAT,
    pull: 37 * BEAT,
    run0: 39 * BEAT,
    run1: 42 * BEAT,
    final: 45 * BEAT,
};
export const TIMES = T;

// linear HDR colours: above ~1 they bloom
const C = {
    code: [0.66, 0.73, 0.88, 1],
    kw: [0.78, 0.66, 1.0, 1],
    fn: [0.62, 0.9, 1.0, 1],
    str: [1.0, 0.76, 0.42, 1],
    num: [0.55, 1.0, 0.78, 1],
    deco: [1.0, 0.68, 0.38, 1],
    ty: [0.5, 0.84, 0.92, 1],
    punct: [0.45, 0.5, 0.64, 1],
    trace: [0.74, 0.8, 0.92, 1],
    traceLabel: [0.8, 0.84, 0.92, 1],
    tear: [0.92, 0.36, 0.36, 1],
    tearText: [0.9, 0.42, 0.42, 1],
    tearDim: [0.62, 0.38, 0.4, 1],
    camel: [0.78, 0.92, 0.9, 1],
    camelLabel: [0.9, 0.95, 0.95, 1],
    region: [0.5, 0.9, 0.82, 1],
    regionFill: [0.03, 0.1, 0.095, 0.4],
    data: [2.2, 1.35, 0.45, 1],
    dataText: [1.0, 0.8, 0.45, 1],
    mint: [0.5, 1.0, 0.76, 1],
};

// ---- code --------------------------------------------------------------------------------------

const PY = [
    [['@torch.compile', 'deco']],
    [['def', 'kw'], [' loss_fn', 'fn'], ['(xs, best):', 'punct']],
    [['    l = ', 'code'], ['0', 'num']],
    [['    ', 'code'], ['for', 'kw'], [' x ', 'code'], ['in', 'kw'], [' xs:', 'code']],
    [['        l = l + model(x).mean()', 'code']],
    [['    ', 'code'], ['if', 'kw'], [' l > best:', 'code']],
    [['        l = l * ', 'code'], ['0.5', 'num']],
    [['    ', 'code'], ['print', 'fn'], ['(', 'punct'], ['"loss"', 'str'], [', l.item())', 'code']],
    [['    ', 'code'], ['return', 'kw'], [' l', 'code']],
];

const CM = [
    [['with', 'kw'], [' <', 'punct'], ['m', 'code'], [': ', 'punct'], ['Mlp', 'ty'], ['>', 'punct']],
    [['func', 'kw'], [' ', 'code'], ['loss', 'fn'], ['(', 'punct'], ['x', 'code'], [': ', 'punct'], ['float[..]', 'ty'], [', ', 'punct'], ['best', 'code'], [': ', 'punct'], ['float', 'ty'], ['): ', 'punct'], ['float', 'ty'], [' {', 'punct']],
    [['    ', 'code'], ['let', 'kw'], [' h = ', 'code'], ['relu', 'fn'], ['(x @ m.w1 + m.b1)', 'code']],
    [['    ', 'code'], ['let', 'kw'], [' l = ', 'code'], ['float', 'fn'], ['(', 'punct'], ['mean', 'fn'], ['(h @ m.w2 + m.b2))', 'code']],
    [['    ', 'code'], ['if', 'kw'], [' l > best ', 'code'], ['then', 'kw'], [' l * ', 'code'], ['0.5', 'num'], [' ', 'code'], ['else', 'kw'], [' l', 'code']],
    [['}', 'punct']],
];

/** Lay out code (HUD pixels) and give every character the time it is typed. */
function layoutCode(lines, x0, y0, size, lh, t0, cps) {
    const adv = size * 0.6;
    const chars = [];
    const text = [];
    let t = t0;
    lines.forEach((tokens, li) => {
        let col = 0;
        let lead = true;
        let s = '';
        for (const [str, cls] of tokens) {
            for (const ch of str) {
                if (ch !== ' ') lead = false;
                chars.push({ ch, li, col, x: x0 + adv * (col + 0.5), y: y0 - lh * li, cls, t });
                if (!(lead && ch === ' ')) t += 1 / cps;
                col++;
                s += ch;
            }
        }
        text.push(s);
        t += 2 / cps;
    });
    return { chars, text, adv, size, lh, x0, y0, end: t };
}

const py = layoutCode(PY, -860, 250, 30, 46, T.pyType0, 62);
const cm = layoutCode(CM, -880, -262, 28, 44, T.camType0, 35);

/** Columns [c0, c1) of the n-th occurrence of `needle` in line `li`. */
function span(code, li, needle, n = 0) {
    let at = -1;
    for (let k = 0; k <= n; k++) at = code.text[li].indexOf(needle, at + 1);
    if (at < 0) throw new Error(`no "${needle}" in line ${li}`);
    return [li, at, at + needle.length];
}
function spanChars(code, [li, c0, c1]) {
    return code.chars.filter((c) => c.li === li && c.col >= c0 && c.col < c1);
}
function spanTime(code, sp) {
    const cs = spanChars(code, sp);
    return cs[cs.length - 1].t;
}

// ---- beat 1: the traced graph and its tears ---------------------------------------------------

// world coordinates; the camera of beat 1 frames them on the right of the screen
const TR = { nodes: {}, edges: [] };
function trNode(id, label, p, t, kind = 'op', group = 1) {
    const w = kind === 'value' ? Math.max(48, label.length * 12 + 26) : Math.max(64, label.length * 12 + 34);
    TR.nodes[id] = { id, label, p, t, kind, w, h: 44, group };
}
function trEdge(a, b, t, opts = {}) {
    TR.edges.push({ a, b, t, ...opts });
}
{
    const rows = [300, 190, 80];
    const it = (k) => T.trace0 + 0.5 + 0.5 * k;
    trNode('zero', '0', [380, 400, 0], T.trace0 + 0.3, 'value');
    rows.forEach((y, k) => {
        const t = it(k);
        trNode(`x${k}`, `x${k}`, [0, y, -20], t, 'value');
        trNode(`model${k}`, 'model', [130, y, -20], t + 0.06);
        trNode(`mean${k}`, 'mean', [265, y, -20], t + 0.12);
        trNode(`add${k}`, '+', [380, y, 0], t + 0.18);
        trEdge(`x${k}`, `model${k}`, t + 0.08);
        trEdge(`model${k}`, `mean${k}`, t + 0.14);
        trEdge(`mean${k}`, `add${k}`, t + 0.2);
        trEdge(k === 0 ? 'zero' : `add${k - 1}`, `add${k}`, t + 0.2);
    });
    trNode('best', 'best', [265, -30, 20], 5.3, 'value');
    trNode('gt', '>', [380, -30, 0], 5.36);
    trEdge('add2', 'gt', 5.42);
    trEdge('best', 'gt', 5.42);
    // graph 2: the resume function, from `l * 0.5` to `.item()`
    trNode('half', '0.5', [0, -160, 30], 6.15, 'value', 2);
    trNode('mul', '*', [130, -160, 30], 6.2, 'op', 2);
    trNode('item', '.item()', [290, -160, 30], T.tear2 - 0.3, 'op', 2);
    trNode('print', 'print', [430, -160, 30], T.tear3 - 0.3, 'op', 2);
    trEdge('half', 'mul', 6.25);
    trEdge('mul', 'item', T.tear2 - 0.22, { tearAt: T.tear2 });
    trEdge('item', 'print', T.tear3 - 0.22, { tearAt: T.tear3 });
}
// the branch the trace attempted at `if l > best`: two arms that tear at tear 1
const ARMS = [
    bezier([416, -30, 0], [470, -30, 0], [480, 40, 10], [540, 50, 10]),
    bezier([416, -30, 0], [470, -30, 0], [480, -100, 10], [540, -110, 10]),
];

function nodeShape(n, z = null) {
    const [x, y, zz] = n.p;
    const round = n.kind === 'value' || n.kind === 'ring' || n.kind === 'join';
    return roundRect(x, y, n.w, n.h, round ? n.h / 2 : 12, z ?? zz, 6);
}

// the spans that burn red at each tear
const HOT = [
    [T.tear1, span(py, 5, 'l > best')],
    [T.tear2, span(py, 7, '.item()')],
    [T.tear3, span(py, 7, 'print')],
    [T.tear4, span(py, 3, 'for x in xs')],
];
function edgePath(A, B) {
    const a = A.p;
    const b = B.p;
    const dx = b[0] - a[0];
    const dy = b[1] - a[1];
    if (Math.abs(dy) > Math.abs(dx)) {
        const s = Math.sign(dy);
        return [[a[0], a[1] + s * (A.h / 2 + 3), a[2]], [b[0], b[1] - s * (B.h / 2 + 9), b[2]]];
    }
    const s = Math.sign(dx);
    const p0 = [a[0] + s * (A.w / 2 + 3), a[1], a[2]];
    const p3 = [b[0] - s * (B.w / 2 + 9), b[1], b[2]];
    if (Math.abs(dy) < 1) return [p0, p3];
    return bezier(p0, [mix(p0[0], p3[0], 0.5), p0[1], p0[2]], [mix(p0[0], p3[0], 0.5), p3[1], p3[2]], p3, 16);
}

// the boxes Dynamo compiles: graph 1 (the loop and the comparison) and graph 2
const BOX1 = { c: [190, 205, -10], w: 520, h: 470, t: T.tear1 + 0.12, label: 'graph 1' };
const BOX2 = { c: [145, -160, 30], w: 360, h: 92, t: T.tear2 + 0.12, label: 'graph 2' };

// ---- beat 3: the Camel graph (the GIR of `loss`) -----------------------------------------------

const CG = { nodes: {}, edges: [] };
function cgNode(id, label, p, sp, kind = 'op', extra = {}) {
    const t = spanTime(cm, sp);
    const w = kind === 'value' ? Math.max(56, label.length * 15 + 30) : Math.max(76, label.length * 15 + 40);
    CG.nodes[id] = { id, label, p, sp, t, kind, w, h: 54, ...extra };
}
cgNode('m', 'm', [40, 330, -90], span(cm, 0, 'm', 0), 'value');
cgNode('x', 'x', [-40, 0, 0], span(cm, 1, 'x', 0), 'value');
cgNode('best', 'best', [1270, -280, 40], span(cm, 1, 'best'), 'value');
cgNode('relu', 'relu', [490, 0, 0], span(cm, 2, 'relu'));
cgNode('mm1', '@', [170, 0, 0], span(cm, 2, '@'));
cgNode('w1', '.w1', [170, 160, -60], span(cm, 2, '.w1'));
cgNode('add1', '+', [330, 0, 0], span(cm, 2, '+'));
cgNode('b1', '.b1', [330, 160, -60], span(cm, 2, '.b1'));
cgNode('float', 'float', [1130, 0, 0], span(cm, 3, 'float'));
cgNode('mean', 'mean', [975, 0, 0], span(cm, 3, 'mean'));
cgNode('mm2', '@', [660, 0, 0], span(cm, 3, '@'));
cgNode('w2', '.w2', [660, 160, -60], span(cm, 3, '.w2'));
cgNode('add2', '+', [820, 0, 0], span(cm, 3, '+'));
cgNode('b2', '.b2', [820, 160, -60], span(cm, 3, '.b2'));
cgNode('gt', '>', [1270, -135, 20], span(cm, 4, '>'));
cgNode('mul', '*', [1585, 75, 0], span(cm, 4, '*'));
cgNode('half', '0.5', [1585, 165, -20], span(cm, 4, '0.5'), 'value');
cgNode('join', '', [1712, 0, 0], span(cm, 4, 'else'), 'join');
cgNode('out', '', [1860, 0, 0], span(cm, 5, '}'), 'ring');
const REGION = { c: [1555, 10, -15], w: 400, h: 370, sp: span(cm, 4, 'if'), brch: [1400, 0, 0] };
REGION.t = spanTime(cm, REGION.sp);
const FRAME = { c: [910, 40, -40], w: 2150, h: 760, sp: span(cm, 1, 'loss') };
FRAME.t = spanTime(cm, FRAME.sp);
const THEN_SP = span(cm, 4, 'then');
const ELSE_SP = span(cm, 4, 'else');
CG.nodes.join.w = 22;
CG.nodes.join.h = 22;
CG.nodes.out.w = 64;
CG.nodes.out.h = 64;

function cgEdge(a, b, opts = {}) {
    CG.edges.push({ a, b, ...opts });
}
for (const acc of ['w1', 'b1', 'w2', 'b2']) cgEdge('m', acc, { curve: true });
cgEdge('x', 'mm1');
cgEdge('w1', 'mm1');
cgEdge('mm1', 'add1');
cgEdge('b1', 'add1');
cgEdge('add1', 'relu');
cgEdge('relu', 'mm2');
cgEdge('w2', 'mm2');
cgEdge('mm2', 'add2');
cgEdge('b2', 'add2');
cgEdge('add2', 'mean');
cgEdge('mean', 'float');
cgEdge('float', 'gt');
cgEdge('best', 'gt');
cgEdge('half', 'mul');
cgEdge('join', 'out');
// the edges into and inside the branch region, drawn as paths
const BR = REGION.brch;
const PATHS = {
    floatIn: [[1130 + 47, 0, 0], [BR[0] - 30, 0, 0]],
    ctrl: bezier([1270, -135 + 30, 20], [1270, -60, 20], [BR[0] - 60, -40, 0], [BR[0] - 14, -14, 0], 18),
    then: bezier([BR[0] + 26, 0, 0], [1470, 0, 0], [1480, 75, 0], [1585 - 45, 75, 0], 20),
    thenOut: bezier([1585 + 45, 75, 0], [1665, 75, 0], [1680, 0, 0], [1712 - 14, 0, 0], 16),
    else: bezier([BR[0] + 26, 0, 0], [1470, 0, 0], [1480, -95, 0], [1560, -95, 0], 20).concat(
        bezier([1560, -95, 0], [1650, -95, 0], [1670, 0, 0], [1712 - 14, 0, 0], 16).slice(1)),
};

/** When the edge from a to b is drawn: once both ends exist. */
function edgeTime(e) {
    return Math.max(CG.nodes[e.a].t, CG.nodes[e.b].t) + 0.32;
}

// data runs: each edge carries light at its source's depth in the graph
const DEPTH = {};
function depth(id) {
    if (DEPTH[id] !== undefined) return DEPTH[id];
    const preds = CG.edges.filter((e) => e.b === id).map((e) => e.a);
    if (id === 'gt') preds.push('float', 'best');
    const d = preds.length ? Math.max(...preds.map(depth)) + 1 : 0;
    DEPTH[id] = d;
    return d;
}
['m', 'x', 'best', 'w1', 'b1', 'w2', 'b2', 'mm1', 'add1', 'relu', 'mm2', 'add2', 'mean', 'float', 'gt'].forEach(depth);
const HOP = BEAT / 5;
const RUNS = [
    { t: T.run0, best: '0.5', arm: 'then', cmp: 'true', result: '0.91875' },
    { t: T.run1, best: '9.0', arm: 'else', cmp: 'false', result: '1.8375' },
];

// ---- cameras -----------------------------------------------------------------------------------

const camW = new THREE.PerspectiveCamera(35, 16 / 9, 5, 20000);
const hudCam = new THREE.OrthographicCamera(-960, 960, 540, -540, -2000, 2000);
hudCam.position.z = 1000;

function cameraAt(t, cam = camW) {
    // beat 1 holds still on the traced graph; beat 2 drifts a little through the fragments; beat 3
    // holds a wide shot of the whole graph while it is written, and moves in to the branch only for
    // the payoff
    const B1 = { tgt: [-130, 95, 0], pos: [-90, -40, 1300] };
    const B2 = { tgt: [-60, 90, 0], pos: [-20, -20, 1120] };
    const WIDE = { tgt: [930, 40, 0], pos: [930, -110, 2080] };
    const BRANCH = { tgt: [1420, 10, 0], pos: [1420, -70, 1250] };
    let v;
    if (t < T.turn) v = B1;
    else if (t < T.gather) {
        const k = ease(lin(t, T.turn, T.gather));
        v = { tgt: mix3(B1.tgt, B2.tgt, k), pos: mix3(B1.pos, B2.pos, k) };
    } else if (t < T.pull) {
        const k = ease(lin(t, T.gather, T.camType0 + 0.5));
        v = { tgt: mix3(B2.tgt, WIDE.tgt, k), pos: mix3(B2.pos, WIDE.pos, k) };
    } else {
        const k = ease(lin(t, T.pull, T.run0 - 0.1));
        v = { tgt: mix3(WIDE.tgt, BRANCH.tgt, k), pos: mix3(WIDE.pos, BRANCH.pos, k) };
    }
    cam.position.set(...v.pos);
    cam.lookAt(...v.tgt);
    cam.updateMatrixWorld();
    return cam;
}

const tmp = new THREE.Vector3();
function toHud(p, cam) {
    tmp.set(p[0], p[1], p[2]).project(cam);
    return [tmp.x * 960, tmp.y * 540, 0];
}
function hudToWorld(h, cam, dist) {
    tmp.set(h[0] / 960, h[1] / 540, 0.5).unproject(cam);
    const dir = tmp.sub(cam.position).normalize();
    return [cam.position.x + dir.x * dist, cam.position.y + dir.y * dist, cam.position.z + dir.z * dist];
}

// the point the fragments gather into: where the Camel caret stands, at the gather's end
const CARET_HUD = [cm.x0 + 2, cm.y0, 0];
const GATHER = hudToWorld(CARET_HUD, cameraAt(T.camType0 - 0.02, new THREE.PerspectiveCamera(35, 16 / 9, 5, 20000)), 900);

// ---- the shatters ------------------------------------------------------------------------------

const SH4 = { t0: T.tear4, centre: [230, 120, 0], strength: 60, g0: T.gather, g1: T.camType0 - 0.05, point: GATHER };
// a shard after tear 4 also takes part in the big shatter
function shardLife(a, b, key, s, pivot) {
    if (TNOW < s.t0) return [a, b, null];
    const [pa, pb] = shard(a, b, key, s, TNOW, pivot);
    let q = [pa, pb];
    if (TNOW > SH4.t0 && s !== SH4) {
        // a piece that broke off earlier joins the recompile's shatter as one fragment
        const moved = shard(pivot, add3(pivot, [1, 0, 0]), key, s, TNOW)[0];
        q = shard(pa, pb, key + 77777, SH4, TNOW, moved);
    }
    return [q[0], q[1], shardColour(key, s.t0)];
}

/** The fragment a segment of a polyline belongs to: the polyline cut into `n` pieces. */
function piece(pts, i, n) {
    const L = pts.length - 1;
    const k = i > L ? n - 1 : Math.min(n - 1, Math.floor(((i - 1) / L) * n));
    const at = Math.min(L, Math.round(((k + 0.5) / n) * L));
    return { k, pivot: pts[at] };
}
function shardColour(key, t0) {
    const dt = TNOW - t0;
    const hot = Math.exp(-dt * 1.2);
    const cool = withA(C.trace, 0.42 + 0.2 * rand(key * 31));
    const c = mixCol(cool, C.tear, hot);
    // the turn: hanging in the dark, dim; gathering: a little brighter into the caret
    const dim = TNOW > T.turn ? 0.6 : 1;
    const g = TNOW > T.gather ? 1 + 0.3 * ease(lin(TNOW, T.gather, T.camType0)) : 1;
    return [c[0] * g, c[1] * g, c[2] * g, c[3] * dim];
}
let TNOW = 0;

// ---- drawing -----------------------------------------------------------------------------------

function codeColour(cls) {
    return C[cls] ?? C.code;
}

/** Draw typed code into the HUD glyph layer; `dim` scales its alpha. */
function drawCode(L, code, t, alpha, hot = null) {
    for (const c of code.chars) {
        if (t < c.t) continue;
        let col = codeColour(c.cls);
        col = [col[0], col[1], col[2], col[3] * alpha];
        if (hot) col = hot(c, col);
        let x = c.x;
        L.glyph(c.ch, x, c.y, 0, code.size, col);
    }
}

function caret(seg, code, t, tStart, tEnd, alpha = 1) {
    let pos = null;
    for (const c of code.chars) if (c.t <= t) pos = c;
    let x = code.x0;
    let y = code.y0;
    if (pos) {
        x = pos.x + code.adv * 0.5 + 2;
        y = pos.y;
    }
    if (t < tStart || t > tEnd + 0.6) return;
    // solid while typing, blinking before and after
    const typing = t >= code.chars[0].t && t <= tEnd;
    const blink = typing ? 1 : Math.floor(t * 2.4) % 2 ? 0.15 : 1;
    seg.seg([x, y - code.size * 0.55, 0], [x, y + code.size * 0.6, 0], 2.5, [0.9, 0.82, 0.62, alpha * blink]);
}

/** A node of either graph: its outline traced from `t0` over `dur`, with its label. */
function drawNode(S, G, n, t, col, labelCol, fx = null, labelFx = null, drawT = 0.3) {
    const p = lin(t, n.t + 0.12, n.t + 0.12 + drawT);
    if (p <= 0) return;
    if (n.kind === 'join') {
        drawPoly(S, densify(roundRect(n.p[0], n.p[1], 22, 22, 11, n.p[2], 5), 10), 2.6, col, 0, p, fx);
        return;
    }
    const pts = densify(nodeShape(n), n.shardStep ?? 14);
    drawPoly(S, pts, n.kind === 'ring' ? 2.4 : 1.8, col, 0, easeOut(p), fx);
    if (n.label) {
        const size = n.labelSize ?? 22;
        const adv = size * 0.6;
        const x0 = n.p[0] - (adv * n.label.length) / 2;
        for (let k = 0; k < n.label.length; k++) {
            let pos = [x0 + adv * (k + 0.5), n.p[1], n.p[2]];
            let c = withA(labelCol, clamp(p * 1.6));
            let rot = 0;
            if (labelFx) {
                const r = labelFx(pos, k);
                if (!r) continue;
                [pos, c, rot] = [r[0], r[1] ?? c, r[2] ?? 0];
            }
            G.glyph(n.label[k], pos[0], pos[1], pos[2], size, c, rot);
        }
    }
}

function labelShard(key, t0) {
    return (pos) => {
        if (TNOW < t0) return [pos];
        const a = 1 - lin(TNOW, t0, t0 + 0.6);
        return a > 0 ? [pos, withA(C.traceLabel, a)] : null;
    };
}

// ---- the frame ---------------------------------------------------------------------------------

export function makeFilm(layers) {
    const { segW, glyphW, glowW, fillW, segH, glyphH, glowH } = layers;
    const cues = buildCues();

    function draw(t) {
        TNOW = t;
        const cam = cameraAt(t);
        for (const L of [segW, glyphW, glowW, fillW, segH, glyphH, glowH]) L.begin();
        const fx = { glitch: 0, fade: 1, seed: 0, backing: 0 };

        if (t < T.camType0 + 0.1) beatOne(t, cam, fx);
        if (t >= T.gather) beatThree(t, cam, fx);

        for (const L of [segW, glyphW, glowW, fillW, segH, glyphH, glowH]) L.end();
        return { cam, hudCam, fx };
    }

    // ---- beat 1 ----------------------------------------------------------------------------------
    function beatOne(t, cam, fx) {
        const cut = t >= T.turn;
        const codeAlpha = t < T.tear4 ? 1 : mix(1, 0.0, ease(lin(t, T.tear4 + 0.4, T.turn - 0.1)));

        if (!cut) {
            // the active line of the trace
            const line = traceLine(t);
            if (line !== null && t < T.tear4 + 0.2) {
                const y = py.y0 - py.lh * line;
                const red = isTearLine(t);
                const c = red ? withA(C.tear, 0.11) : [0.5, 0.65, 1.0, 0.06];
                segH.seg([py.x0 - 20, y, -1], [py.x0 + 600, y, -1], py.lh * 0.92, c);
            }
            drawCode(glyphH, py, t, codeAlpha, (c, col) => hotCode(c, col, t));
            caret(segH, py, t, 0, T.pyType1);
            if (t > T.call2 - 0.2 && t < T.turn) drawCall2(glyphH, t, codeAlpha);
            drawLog(glyphH, segH, t);
            if (t < T.tear4 + 3) {
                glyphH.text('messages as printed by torch 2.14.1 (torch.compile) and jax 0.12 (jax.jit)', -880, -505, 0, 13,
                    withA([0.4, 0.45, 0.6, 1], lin(t, T.tear1, T.tear1 + 0.5) * (1 - lin(t, T.tear4 + 1.5, T.turn - 0.2))));
            }
        }

        // the traced graph, its tears, and the shards (which outlive the cut)
        const tearKey = { 1: 1000, 2: 2000, 3: 3000 };
        for (const n of Object.values(TR.nodes)) {
            if (t < n.t + 0.1) continue;
            const torn = n.id === 'item' ? T.tear2 : n.id === 'print' ? T.tear3 : null;
            const key = 10000 + hash(n.id) * 100;
            // a node is one fragment
            const shFx = (a, b) => {
                if (torn !== null && t >= torn) return shardLife(a, b, key, { t0: torn, centre: add3(n.p, [-60, 30, 0]), strength: 22 }, n.p);
                if (t >= T.tear4) return shardLife(a, b, key, SH4, n.p);
                return [a, b, null];
            };
            const lblFx = torn !== null && t >= torn ? labelShard(key, torn) : t >= T.tear4 ? labelShard(key, T.tear4) : null;
            const col = n.group === 2 && t >= T.tear2 && t < T.tear4 ? withA(C.trace, 0.75) : C.trace;
            drawNode(segW, glyphW, n, t, col, C.traceLabel, shFx, lblFx, 0.22);
        }
        TR.edges.forEach((e, ei) => {
            const A = TR.nodes[e.a];
            const B = TR.nodes[e.b];
            const p = lin(t, e.t, e.t + 0.18);
            if (p <= 0) return;
            const pts = densify(edgePath(A, B), 12);
            const key = 50000 + ei * 500;
            const torn = e.tearAt ?? null;
            // an edge is one fragment; a torn edge breaks cleanly in two
            const efx = (a, b, i) => {
                if (torn !== null && t >= torn) {
                    const h = piece(pts, i, 2);
                    return shardLife(a, b, key + h.k, { t0: torn, centre: mix3(A.p, B.p, 0.5), strength: 26 }, h.pivot);
                }
                if (t >= T.tear4) return shardLife(a, b, key, SH4, pts[Math.floor(pts.length / 2)]);
                return [a, b, null];
            };
            const col = torn !== null && t < torn ? mixCol(C.trace, C.tear, lin(t, torn - 0.2, torn)) : C.trace;
            drawPoly(segW, pts, 1.6, col, 0, p, efx);
            if (p >= 1) arrowHead(segW, pts, 1.6, col, 10, efx);
        });
        // tear 1: the branch the trace tried to follow breaks off
        if (t > T.tear1 - 0.32) {
            ARMS.forEach((arm, k) => {
                const pts = densify(arm, 10);
                const p = lin(t, T.tear1 - 0.32, T.tear1 - 0.05);
                const col = mixCol(withA(C.trace, 0.7), C.tear, lin(t, T.tear1 - 0.1, T.tear1));
                const pv = pts[Math.floor(pts.length / 2)];
                drawPoly(segW, pts, 1.6, col, 0, p, (a, b) => (t >= T.tear1 ? shardLife(a, b, 70000 + k, { t0: T.tear1, centre: [400, -30, 0], strength: 70 }, pv) : [a, b, null]));
            });
        }
        // the compiled graphs close round what they caught
        for (const box of [BOX1, BOX2]) {
            const p = lin(t, box.t, box.t + 0.45);
            if (p <= 0) continue;
            const key = box === BOX1 ? 90000 : 91000;
            const pts = densify(roundRect(box.c[0], box.c[1], box.w, box.h, 26, box.c[2], 8), 30);
            const col = withA(mixCol(C.tear, [0.7, 0.8, 1.2, 1], lin(t, box.t + 0.2, box.t + 1.2)), 0.65);
            // a box breaks into six fragments
            drawPoly(segW, pts, 1.6, col, 0, ease(p), (a, b, i) => {
                if (t < T.tear4) return [a, b, null];
                const f = piece(pts, i, 6);
                return shardLife(a, b, key + f.k, SH4, f.pivot);
            });
            const lbl = [box.c[0] - box.w / 2 + 18, box.c[1] + box.h / 2 - 22, box.c[2]];
            for (let k = 0; k < box.label.length; k++) {
                let pos = [lbl[0] + 9.6 * k, lbl[1], lbl[2]];
                let c = withA([0.75, 0.6, 0.7, 1], lin(t, box.t + 0.25, box.t + 0.6));
                let rot = 0;
                if (t >= T.tear4) {
                    const r = labelShard(key, T.tear4)(pos);
                    if (!r) continue;
                    [pos, c] = r;
                }
                glyphW.glyph(box.label[k], pos[0], pos[1], pos[2], 16, c, rot);
            }
        }
    }

    function hotCode(c, col, t) {
        // the offending span burns red from its tear on
        for (const [tt, [li, c0, c1]] of HOT) {
            if (t >= tt - 0.05 && c.li === li && c.col >= c0 && c.col < c1) {
                const k = Math.exp(-(t - tt) * 1.5) * 0.7 + 0.3;
                return mixCol(col, [1.0, 0.32, 0.4, col[3]], k);
            }
        }
        return col;
    }

    function drawCall2(G, t, alpha) {
        const y = py.y0 - py.lh * 9.4;
        const s = 'loss_fn(xs, best)    # step 2: len(xs) == 5';
        const n = Math.floor(clamp((t - T.call2 + 0.2) * 60, 0, s.length));
        G.text(s.slice(0, n), py.x0, y, 0, 24, withA([0.6, 0.66, 0.8, 1], alpha));
    }

    function drawLog(G, S, t) {
        const entries = [
            [T.tear1, ['torch.compile  Graph Break Reason: Data-dependent branching', 'jax.jit        TracerBoolConversionError']],
            [T.tear2, ['torch.compile  Graph break from `Tensor.item()`', 'jax.jit        ConcretizationTypeError']],
            [T.tear3, ['torch.compile  Failed to trace builtin operator', 'jax.jit        prints JitTracer(float32[]), once, at trace time']],
            [T.tear4, ['torch.compile  Recompiling function loss_fn', '               guard failure: len(xs) == 3', 'jax.jit        traced again for the new input structure']],
        ];
        let y = -236;
        const x = py.x0;
        entries.forEach(([tt, lines], ei) => {
            if (t < tt) return;
            const age = t - tt;
            const latest = !entries[ei + 1] || t < entries[ei + 1][0];
            lines.forEach((line, li) => {
                const n = Math.floor(clamp(age * 90 - li * 12, 0, line.length));
                let col = li === 0 ? C.tearText : C.tearDim;
                if (!latest) col = withA(col, 0.45);
                const fade = 1 - lin(t, T.tear4 + 0.5, T.tear4 + 1.1);
                G.text(line.slice(0, n), x, y, 0, 19, withA(col, fade));
                y -= 25;
            });
            y -= 9;
        });
    }

    // beat 2 has no drawing of its own: the shards of beat 1 hang in the dark, and the question is
    // the overlay's

    // ---- beat 3 ----------------------------------------------------------------------------------
    function beatThree(t, cam, fx) {
        const codeAlpha = 1 - 0.94 * ease(lin(t, T.pull, T.pull + 1.0));
    fx.backing = (1 - ease(lin(t, T.pull, T.pull + 1.0))) * ease(lin(t, T.camType0, T.camType0 + 1.0));
        // the caret, born where the fragments gathered
        if (t < T.camType0 + 0.1) {
            const k = ease(lin(t, T.camType0 - 0.25, T.camType0));
            glowH.dot(CARET_HUD, 26, withA([1.4, 1.1, 0.6, 1], 0.35 * k));
        }
        caret(segH, cm, t, T.camType0 - 0.1, cm.end, codeAlpha);
        // the code; the characters that have flown into the graph keep a bright afterimage briefly
        drawCode(glyphH, cm, t, codeAlpha);

        // the function's frame
        {
            const p = lin(t, FRAME.t + 0.1, FRAME.t + 1.4);
            const pts = densify(roundRect(FRAME.c[0], FRAME.c[1], FRAME.w, FRAME.h, 40, FRAME.c[2], 10), 30);
            drawPoly(segW, pts, 1.6, withA([0.55, 0.75, 1.0, 1], 0.35), 0, ease(p));
            flyLabel(t, cam, FRAME.sp, 'loss', [FRAME.c[0] - FRAME.w / 2 + 30, FRAME.c[1] + FRAME.h / 2 - 34, FRAME.c[2]], FRAME.t, 24, [0.6, 0.8, 1.0, 0.8]);
        }
        // the branch region: whole, the `if` that tore before
        regionDraw(t, cam);

        for (const n of Object.values(CG.nodes)) {
            const lit = runGlow(n.id, t);
            const col = lit > 0 ? mixCol(C.camel, C.data, lit) : C.camel;
            if (n.kind === 'value' || n.kind === 'op' || n.kind === 'ring' || n.kind === 'join') {
                drawNode(segW, glyphW, { ...n, label: '' }, t, col, C.camelLabel);
            }
            if (n.label) flyLabel(t, cam, n.sp, n.label, n.p, n.t, 24, C.camelLabel, true);
        }
        for (const e of CG.edges) {
            const A = CG.nodes[e.a];
            const B = CG.nodes[e.b];
            const t0 = edgeTime(e);
            const p = lin(t, t0, t0 + 0.3);
            if (p <= 0) continue;
            let pts = edgePath(A, B);
            if (e.curve) {
                const a = [A.p[0] + 20, A.p[1] - 22, A.p[2]];
                const b = [B.p[0], B.p[1] + B.h / 2 + 9, B.p[2]];
                pts = bezier(a, [a[0] + 40, a[1] - 60, a[2]], [b[0], b[1] + 70, b[2]], b, 22);
            }
            pts = densify(pts, 12);
            const col = withA(C.camel, 0.85);
            drawPoly(segW, pts, 1.6, col, 0, easeOut(p));
            if (p >= 1) arrowHead(segW, pts, 1.6, col, 10);
            pulses(t, e, pts);
        }
        // the paths into and through the region
        const rp = lin(t, REGION.t + 0.25, REGION.t + 0.75);
        const fp = lin(t, Math.max(REGION.t, CG.nodes.float.t) + 0.3, Math.max(REGION.t, CG.nodes.float.t) + 0.6);
        drawPath(t, PATHS.floatIn, fp, depth('float'), 'floatIn');
        drawPath(t, PATHS.ctrl, lin(t, CG.nodes.gt.t + 0.3, CG.nodes.gt.t + 0.6), depth('gt'), 'ctrl', true);
        const thenT = spanTime(cm, THEN_SP);
        drawPath(t, PATHS.then, lin(t, thenT + 0.1, thenT + 0.45), 10, 'then');
        drawPath(t, PATHS.thenOut, lin(t, CG.nodes.mul.t + 0.3, CG.nodes.mul.t + 0.6), 11, 'thenOut');
        drawPath(t, PATHS.else, lin(t, CG.nodes.join.t + 0.15, CG.nodes.join.t + 0.6), 10, 'else');
        armLabel(t, cam, THEN_SP, 'then', [1478, 98, 0], thenT);
        armLabel(t, cam, ELSE_SP, 'else', [1478, -118, 0], CG.nodes.join.t);
        void rp;

        // the runs: values written beside the ports and the result
        for (const r of RUNS) {
            const on = lin(t, r.t - 0.05, r.t + 0.1) * (1 - lin(t, r.t + 3 * BEAT - 0.15, r.t + 3 * BEAT));
            const last = r === RUNS[RUNS.length - 1];
            const keep = last ? lin(t, r.t - 0.05, r.t + 0.1) : on;
            if (keep <= 0) continue;
            glyphW.text(`best = ${r.best}`, 1270 - 60, -280 - 52, 40, 20, withA(C.dataText, keep));
            const cmpT = r.t + depth('gt') * HOP;
            glyphW.text(r.cmp, 1270 - 26, -135 - 48, 20, 18, withA(C.mint, keep * lin(t, cmpT, cmpT + 0.1)));
            const resT = r.t + 13 * HOP;
            const resA = keep * lin(t, resT, resT + 0.12);
            glyphW.text(r.result, 1860 - (r.result.length * 26 * 0.6) / 2, -62, 0, 26, withA(C.mint, resA));
        }

        // the closing line comes from the overlay
    }

    function regionDraw(t, cam) {
        const p = lin(t, REGION.t + 0.05, REGION.t + 0.6);
        if (p <= 0) return;
        const pts = densify(roundRect(REGION.c[0], REGION.c[1], REGION.w, REGION.h, 34, REGION.c[2], 9), 16);
        // the crack that tore the traced graph appears once and heals
        const heal = lin(t, REGION.t, REGION.t + 0.7);
        if (heal < 1) {
            const c = [REGION.c[0] - REGION.w / 2 + 70, REGION.c[1], 5];
            const bolt = [];
            for (let i = 0; i <= 9; i++) bolt.push([c[0] + srand(83000 + i * 3) * 26 + (i / 9 - 0.5) * 10, c[1] + (i / 9 - 0.5) * 300, 5]);
            const col = mixCol(C.tear, C.region, ease(heal));
            drawPoly(segW, bolt, 1.6, withA(col, 0.8 * (1 - ease(heal))), 0, 1);
        }
        const glow = 1 + 0.7 * Math.exp(-(t - REGION.t - 0.6) * 3) * (t > REGION.t + 0.6 ? 1 : 0);
        drawPoly(segW, pts, 2.4, [C.region[0] * glow, C.region[1] * glow, C.region[2] * glow, 1], 0, ease(p));
        fillW.rect(REGION.c, REGION.w, REGION.h, 34, withA(C.regionFill, ease(p)));
        // BRCH: the diamond at the region's door
        const dp = lin(t, REGION.t + 0.3, REGION.t + 0.6);
        drawPoly(segW, densify(diamond(BR[0], BR[1], 24, BR[2]), 8), 2.4, C.region, 0, dp);
        flyLabel(t, cam, REGION.sp, 'if', [REGION.c[0] - REGION.w / 2 + 30, REGION.c[1] + REGION.h / 2 - 32, REGION.c[2]], REGION.t, 26, [0.7, 1.0, 0.95, 1]);
        // the arm a run takes glows
        for (const r of RUNS) {
            const a = lin(t, r.t + 10 * HOP, r.t + 10.5 * HOP) * (1 - lin(t, r.t + 3 * BEAT - 0.2, r.t + 3 * BEAT));
            if (a > 0) {
                const y = r.arm === 'then' ? 75 : -95;
                fillW.rect([1555, y, -10], 360, 90, 40, withA([0.6, 0.35, 0.08, 1], 0.35 * a));
            }
        }
    }

    function drawPath(t, pts, p, d, id, dashed = false) {
        if (p <= 0) return;
        const dense = densify(pts, 10);
        const col = withA(dashed ? C.region : C.camel, dashed ? 0.7 : 0.85);
        if (dashed) {
            const L = dense.length;
            for (let i = 1; i < L; i += 2) {
                if ((i - 1) / (L - 1) > easeOut(p)) break;
                segW.seg(dense[i - 1], dense[i], 1.8, col);
            }
        } else {
            drawPoly(segW, dense, 2.0, col, 0, easeOut(p));
            if (p >= 1 && id !== 'then' && id !== 'else' && id !== 'floatIn') arrowHead(segW, dense, 2.0, col, 11);
        }
        // the light on these paths
        for (const r of RUNS) {
            if ((id === 'then' || id === 'thenOut') && r.arm !== 'then') continue;
            if (id === 'else' && r.arm !== 'else') continue;
            const t0 = r.t + d * HOP;
            pulseAlong(dense, t, t0, id === 'else' ? HOP * 2 : HOP);
        }
    }

    function armLabel(t, cam, sp, text, p, tSpawn) {
        flyLabel(t, cam, sp, text, p, tSpawn, 18, [0.6, 0.95, 0.9, 0.85]);
    }

    function pulses(t, e, pts) {
        for (const r of RUNS) {
            // 0.5 feeds only the `then` arm; the join's result leaves at depth 12
            if (e.a === 'half' && r.arm !== 'then') continue;
            const d = e.a === 'half' ? 10 : e.a === 'join' ? 12 : depth(e.a);
            pulseAlong(pts, t, r.t + d * HOP, HOP);
        }
    }

    function pulseAlong(pts, t, t0, dur) {
        const k = (t - t0) / dur;
        if (k < 0 || k > 1.6) return;
        const head = clamp(k);
        const fade = k > 1 ? 1 - (k - 1) / 0.6 : 1;
        // a comet: a bright head and a trail
        for (let j = 0; j < 7; j++) {
            const f = head - j * 0.05;
            if (f < 0) break;
            const { p } = pointAt(pts, f);
            glowW.dot(p, j === 0 ? 22 : 14 - j, withA(C.data, fade * (j === 0 ? 0.9 : 0.35 - j * 0.04)));
        }
        drawPoly(segW, pts, 2.2, withA(C.data, 0.85 * fade), Math.max(0, head - 0.25), head);
    }

    function runGlow(id, t) {
        let g = 0;
        for (const r of RUNS) {
            if (id === 'mul' && r.arm !== 'then') continue;
            if (id === 'join' || id === 'out') {
                const at = r.t + (id === 'join' ? 12 : 13) * HOP;
                g = Math.max(g, t >= at ? Math.exp(-(t - at) * 3) : 0);
                continue;
            }
            const d = id === 'mul' ? 11 : DEPTH[id] ?? 0;
            const at = r.t + d * HOP;
            if (t >= at) g = Math.max(g, Math.exp(-(t - at) * 4));
        }
        return clamp(g);
    }

    /**
     * The characters of a span lift off the code and fly into the graph, where they become the
     * label at world point `p`; afterwards they are drawn in the world.
     */
    function flyLabel(t, cam, sp, label, p, tSpawn, size, col, centre = false) {
        const src = spanChars(cm, sp);
        const flyT = 0.42;
        const adv = size * 0.6;
        const x0 = centre ? p[0] - (adv * label.length) / 2 : p[0];
        for (let k = 0; k < label.length; k++) {
            // the source character: the label's k-th character, else the span's last
            const s = src.find((c) => c.ch === label[k] && c.col >= sp[1] + (label.length === src.length ? k : 0)) ?? src[Math.min(k, src.length - 1)];
            const wp = [x0 + adv * (k + 0.5), p[1], p[2]];
            const t0 = tSpawn + k * 0.02;
            if (t < t0) continue;
            if (t < t0 + flyT) {
                const f = ease(lin(t, t0, t0 + flyT));
                const end = toHud(wp, cam);
                const start = [s.x, s.y, 0];
                const lift = Math.sin(f * Math.PI) * 30;
                const pos = [mix(start[0], end[0], f), mix(start[1], end[1], f) + lift, 0];
                const projSize = size * (worldScale(wp, cam));
                const sz = mix(cm.size, projSize, f);
                glyphH.glyph(label[k], pos[0], pos[1], 0, sz, [0.98, 0.94, 0.86, 1], 0);
            } else {
                glyphW.glyph(label[k], wp[0], wp[1], wp[2], size, col);
            }
        }
    }

    return { draw, cues, T };
}

function worldScale(p, cam) {
    const a = toHud(p, cam);
    const b = toHud([p[0] + 10, p[1], p[2]], cam);
    return Math.hypot(b[0] - a[0], b[1] - a[1]) / 10;
}

function traceLine(t) {
    const t0 = T.trace0;
    if (t < t0) return null;
    if (t < t0 + 0.25) return 1;
    if (t < t0 + 0.5) return 2;
    if (t < t0 + 2.0) return Math.floor((t - t0 - 0.5) / 0.25) % 2 === 0 ? 3 : 4;
    if (t < 5.6) return 5;
    if (t < 6.0) return 5;
    if (t < 6.5) return 6;
    if (t < T.call2) return 7;
    if (t < T.tear4 + 0.3) return 3;
    return null;
}
function isTearLine(t) {
    return [T.tear1, T.tear2, T.tear3, T.tear4].some((tt) => t >= tt && t < tt + 0.5);
}
function hash(s) {
    let h = 7;
    for (const ch of s) h = (h * 31 + ch.charCodeAt(0)) % 997;
    return h;
}

function buildCues() {
    return {
        beat: BEAT,
        duration: DURATION,
        times: T,
        pyChars: py.chars.filter((c) => c.ch !== ' ').map((c) => +c.t.toFixed(3)),
        camChars: cm.chars.filter((c) => c.ch !== ' ').map((c) => +c.t.toFixed(3)),
        spawns: Object.values(CG.nodes).map((n) => +n.t.toFixed(3)).sort((a, b) => a - b),
        region: +REGION.t.toFixed(3),
        traceSpawns: Object.values(TR.nodes).map((n) => +n.t.toFixed(3)).sort((a, b) => a - b),
        runs: RUNS.map((r) => ({ t: r.t, hops: 14, hop: HOP })),
    };
}
