// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Shapes as polylines of [x, y, z] points, drawn partially (a pen tracing them) or broken into
// shards that fly apart, hang and gather again. Everything is a pure function of time and a seed.

export const clamp = (x, a = 0, b = 1) => Math.min(b, Math.max(a, x));
export const lin = (t, a, b) => clamp((t - a) / (b - a));
export const ease = (x) => (x < 0.5 ? 4 * x * x * x : 1 - Math.pow(-2 * x + 2, 3) / 2);
export const easeOut = (x) => 1 - Math.pow(1 - x, 3);
export const easeIn = (x) => x * x * x;
export const mix = (a, b, k) => a + (b - a) * k;
export const mix3 = (a, b, k) => [mix(a[0], b[0], k), mix(a[1], b[1], k), mix(a[2], b[2], k)];
export const add3 = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
export const sub3 = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
export const scale3 = (a, k) => [a[0] * k, a[1] * k, a[2] * k];
export const len3 = (a) => Math.hypot(a[0], a[1], a[2]);
export const norm3 = (a) => {
    const l = len3(a) || 1;
    return [a[0] / l, a[1] / l, a[2] / l];
};
export const mixCol = (a, b, k) => [mix(a[0], b[0], k), mix(a[1], b[1], k), mix(a[2], b[2], k), mix(a[3], b[3], k)];
export const withA = (c, a) => [c[0], c[1], c[2], c[3] * a];

/** Deterministic noise: a hash of an integer key to [0, 1). */
export function rand(key) {
    let t = (key * 2654435761 + 0x6d2b79f5) >>> 0;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
}
export const srand = (key) => rand(key) * 2 - 1;

export function roundRect(cx, cy, w, h, r, z = 0, per = 7) {
    r = Math.min(r, w / 2, h / 2);
    const pts = [];
    const corners = [
        [cx + w / 2 - r, cy + h / 2 - r, 0],
        [cx + w / 2 - r, cy - h / 2 + r, -Math.PI / 2],
        [cx - w / 2 + r, cy - h / 2 + r, -Math.PI],
        [cx - w / 2 + r, cy + h / 2 - r, Math.PI / 2],
    ];
    // start at the top middle, clockwise
    pts.push([cx, cy + h / 2, z]);
    for (const [ox, oy, a0] of corners) {
        for (let k = 0; k <= per; k++) {
            const a = Math.PI / 2 + a0 - (k / per) * (Math.PI / 2);
            pts.push([ox + r * Math.cos(a), oy + r * Math.sin(a), z]);
        }
    }
    pts.push([cx, cy + h / 2, z]);
    return pts;
}

export function diamond(cx, cy, s, z = 0) {
    return [[cx, cy + s, z], [cx + s, cy, z], [cx, cy - s, z], [cx - s, cy, z], [cx, cy + s, z]];
}

export function bezier(p0, p1, p2, p3, n = 24) {
    const pts = [];
    for (let i = 0; i <= n; i++) {
        const t = i / n;
        const u = 1 - t;
        pts.push([0, 1, 2].map((k) => u * u * u * p0[k] + 3 * u * u * t * p1[k] + 3 * u * t * t * p2[k] + t * t * t * p3[k]));
    }
    return pts;
}

/** Subdivide each straight piece so that a shattered polyline breaks into many small shards. */
export function densify(pts, step = 18) {
    const out = [pts[0]];
    for (let i = 1; i < pts.length; i++) {
        const a = pts[i - 1];
        const b = pts[i];
        const n = Math.max(1, Math.ceil(len3(sub3(b, a)) / step));
        for (let k = 1; k <= n; k++) out.push(mix3(a, b, k / n));
    }
    return out;
}

export function polyLength(pts) {
    let L = 0;
    for (let i = 1; i < pts.length; i++) L += len3(sub3(pts[i], pts[i - 1]));
    return L;
}

/** The point at fraction f of the polyline's length, and the direction there. */
export function pointAt(pts, f) {
    const L = polyLength(pts) * clamp(f);
    let acc = 0;
    for (let i = 1; i < pts.length; i++) {
        const d = len3(sub3(pts[i], pts[i - 1]));
        if (acc + d >= L || i === pts.length - 1) {
            const k = d > 0 ? clamp((L - acc) / d) : 0;
            return { p: mix3(pts[i - 1], pts[i], k), dir: norm3(sub3(pts[i], pts[i - 1])) };
        }
        acc += d;
    }
    return { p: pts[pts.length - 1], dir: [1, 0, 0] };
}

/**
 * Draw a polyline into a SegLayer: `from`..`to` are fractions of its length (a pen tracing it), and
 * `fx(a, b, i)` may move each segment (shards) and return [a, b, colour] or null to skip it.
 */
export function drawPoly(layer, pts, width, col, from = 0, to = 1, fx = null) {
    if (to <= from || col[3] <= 0.003) return;
    const L = polyLength(pts);
    const s0 = from * L;
    const s1 = to * L;
    let acc = 0;
    for (let i = 1; i < pts.length; i++) {
        const a = pts[i - 1];
        const b = pts[i];
        const d = len3(sub3(b, a));
        const e0 = acc;
        const e1 = acc + d;
        acc = e1;
        if (e1 <= s0 || e0 >= s1 || d === 0) continue;
        const pa = mix3(a, b, clamp((s0 - e0) / d));
        const pb = mix3(a, b, clamp((s1 - e0) / d));
        if (fx) {
            const r = fx(pa, pb, i);
            if (r) layer.seg(r[0], r[1], width, r[2] ?? col);
        } else {
            layer.seg(pa, pb, width, col);
        }
    }
}

/** An arrowhead at the end of a polyline. */
export function arrowHead(layer, pts, width, col, size = 12, fx = null) {
    const end = pts[pts.length - 1];
    const dir = norm3(sub3(end, pts[Math.max(0, pts.length - 3)]));
    const n = [-dir[1], dir[0], 0];
    const back = sub3(end, scale3(dir, size));
    const l = add3(back, scale3(n, size * 0.55));
    const r = sub3(back, scale3(n, size * 0.55));
    for (const [a, b, i] of [[l, end, 9001], [r, end, 9002]]) {
        if (fx) {
            const out = fx(a, b, i);
            if (out) layer.seg(out[0], out[1], width, out[2] ?? col);
        } else layer.seg(a, b, width, col);
    }
}

/** Rotate point p about centre c by angle a around a unit axis (Rodrigues). */
export function rotateAbout(p, c, axis, a) {
    const v = sub3(p, c);
    const cs = Math.cos(a);
    const sn = Math.sin(a);
    const [kx, ky, kz] = axis;
    const dot = kx * v[0] + ky * v[1] + kz * v[2];
    const cross = [ky * v[2] - kz * v[1], kz * v[0] - kx * v[2], kx * v[1] - ky * v[0]];
    return [
        c[0] + v[0] * cs + cross[0] * sn + kx * dot * (1 - cs),
        c[1] + v[1] * cs + cross[1] * sn + ky * dot * (1 - cs),
        c[2] + v[2] * cs + cross[2] * sn + kz * dot * (1 - cs),
    ];
}

/**
 * Shards: a segment of a shattered shape, `key` its identity. It bursts away from `centre` at
 * `t0`, hangs and drifts, and from `g0` to `g1` gathers into `point` (shrinking to nothing).
 */
export function shard(a, b, key, s, now) {
    const dt = now - s.t0;
    if (dt <= 0) return [a, b];
    const mid = mix3(a, b, 0.5);
    const out = norm3(add3(sub3(mid, s.centre), [srand(key * 3 + 1) * 120, srand(key * 3 + 2) * 120, srand(key * 3 + 3) * 420]));
    const burst = s.strength * (0.25 + 1.0 * Math.pow(rand(key * 7 + 5), 2));
    const k = 1 - Math.exp(-dt * 2.6);
    const drift = [srand(key * 11 + 1) * 7, srand(key * 11 + 2) * 7 + 2, srand(key * 11 + 3) * 16];
    let m = add3(mid, add3(scale3(out, burst * k), scale3(drift, dt)));
    const axis = norm3([srand(key * 13 + 1), srand(key * 13 + 2), srand(key * 13 + 3)]);
    const spin = (0.6 + 2.2 * rand(key * 17 + 4)) * (1 - Math.exp(-dt * 2)) * 2.4 + dt * 0.35 * srand(key * 19);
    let pa = rotateAbout(a, mid, axis, spin);
    let pb = rotateAbout(b, mid, axis, spin);
    pa = add3(pa, sub3(m, mid));
    pb = add3(pb, sub3(m, mid));
    if (s.g0 !== undefined && now > s.g0) {
        const g = ease(lin(now, s.g0 + rand(key * 23) * 0.15, s.g1));
        const target = s.point;
        const shrink = 1 - g;
        const c2 = mix3(m, target, g);
        pa = add3(c2, scale3(sub3(pa, m), shrink));
        pb = add3(c2, scale3(sub3(pb, m), shrink));
    }
    return [pa, pb];
}
