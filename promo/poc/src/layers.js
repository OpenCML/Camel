// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Immediate-mode drawing layers: every frame is redrawn from scratch by the film's timeline, so a
// frame is a pure function of its time and frames can be rendered in any order. Each layer is one
// instanced draw call: glyphs from a font atlas, stroke segments, soft lights and filled regions.
import * as THREE from 'three';

const QUAD = new THREE.PlaneGeometry(1, 1);

function instanced(capacity, attrs) {
    const geo = new THREE.InstancedBufferGeometry();
    geo.index = QUAD.index;
    geo.setAttribute('position', QUAD.getAttribute('position'));
    geo.setAttribute('uv', QUAD.getAttribute('uv'));
    const arrays = {};
    for (const [name, size] of Object.entries(attrs)) {
        arrays[name] = new Float32Array(capacity * size);
        const attr = new THREE.InstancedBufferAttribute(arrays[name], size);
        attr.setUsage(THREE.DynamicDrawUsage);
        geo.setAttribute(name, attr);
    }
    geo.instanceCount = 0;
    return { geo, arrays };
}

function flush(geo, names, n) {
    geo.instanceCount = n;
    for (const name of names) geo.getAttribute(name).needsUpdate = true;
}

// ---- glyphs ------------------------------------------------------------------------------------

/** A monospace font atlas: one cell per character, white on transparent. */
export function makeAtlas(family, chars) {
    const F = 96;
    const CW = 72;
    const CH = 128;
    const cols = 28;
    const rows = Math.ceil(chars.length / cols);
    const canvas = document.createElement('canvas');
    canvas.width = 2048;
    canvas.height = 2048;
    const g = canvas.getContext('2d');
    g.fillStyle = '#fff';
    g.font = `${F}px "${family}"`;
    g.textBaseline = 'alphabetic';
    g.textAlign = 'center';
    const map = new Map();
    [...chars].forEach((ch, i) => {
        const c = i % cols;
        const r = Math.floor(i / cols);
        g.fillText(ch, c * CW + CW / 2, r * CH + 92);
        // rows run top-down in the pixel data (a DataTexture is not flipped): [u0, vBottom, u1, vTop]
        map.set(ch, [c * CW / 2048, (r + 1) * CH / 2048, (c + 1) * CW / 2048, r * CH / 2048]);
    });
    if (rows * CH > 2048) throw new Error('atlas overflow');
    // uploaded from its pixels: a canvas upload comes out empty under the headless software GL
    const pixels = g.getImageData(0, 0, 2048, 2048).data;
    const tex = new THREE.DataTexture(new Uint8Array(pixels.buffer), 2048, 2048, THREE.RGBAFormat);
    tex.generateMipmaps = true;
    tex.needsUpdate = true;
    tex.minFilter = THREE.LinearMipmapLinearFilter;
    tex.magFilter = THREE.LinearFilter;
    tex.anisotropy = 4;
    // a quad of `size` holds the cell; the advance is 0.6 em
    return { tex, map, quadW: CW / F, quadH: CH / F, baseline: (92 - CH / 2) / F };
}

export class GlyphLayer {
    constructor(atlas, capacity = 6000) {
        this.atlas = atlas;
        const { geo, arrays } = instanced(capacity, { aPos: 3, aSize: 2, aRot: 2, aUV: 4, aCol: 4 });
        this.geo = geo;
        this.a = arrays;
        this.material = new THREE.ShaderMaterial({
            uniforms: { map: { value: atlas.tex } },
            vertexShader: `
                attribute vec3 aPos; attribute vec2 aSize; attribute vec2 aRot; attribute vec4 aUV; attribute vec4 aCol;
                varying vec2 vUv; varying vec4 vCol;
                void main() {
                    vec2 p = position.xy * aSize;
                    float c = cos(aRot.x), s = sin(aRot.x);
                    p = vec2(c * p.x - s * p.y, s * p.x + c * p.y);
                    vec3 w = aPos + vec3(p.x, p.y * cos(aRot.y), p.y * sin(aRot.y));
                    vUv = mix(aUV.xy, aUV.zw, uv);
                    vCol = aCol;
                    gl_Position = projectionMatrix * modelViewMatrix * vec4(w, 1.0);
                }`,
            fragmentShader: `
                uniform sampler2D map; varying vec2 vUv; varying vec4 vCol;
                void main() {
                    float a = texture2D(map, vUv).a * vCol.a;
                    if (a < 0.003) discard;
                    gl_FragColor = vec4(vCol.rgb, a);
                }`,
            transparent: true,
            depthWrite: false,
            depthTest: false,
        });
        this.mesh = new THREE.Mesh(geo, this.material);
        this.mesh.frustumCulled = false;
        this.n = 0;
    }
    begin() {
        this.n = 0;
    }
    /** One character centred at (x, y, z), `size` the em in world units. */
    glyph(ch, x, y, z, size, col, rot = 0, tilt = 0) {
        const uv = this.atlas.map.get(ch);
        if (!uv || ch === ' ' || col[3] <= 0.003) return;
        const i = this.n++;
        const a = this.a;
        a.aPos.set([x, y - this.atlas.baseline * size * 0, z], i * 3);
        a.aSize.set([size * this.atlas.quadW, size * this.atlas.quadH], i * 2);
        a.aRot.set([rot, tilt], i * 2);
        a.aUV.set(uv, i * 4);
        a.aCol.set(col, i * 4);
    }
    /** A string whose first character's left edge is at x; returns the advance. */
    text(str, x, y, z, size, col) {
        const adv = size * 0.6;
        for (let k = 0; k < str.length; k++) this.glyph(str[k], x + adv * (k + 0.5), y, z, size, col);
        return adv * str.length;
    }
    end() {
        flush(this.geo, ['aPos', 'aSize', 'aRot', 'aUV', 'aCol'], this.n);
    }
}

// ---- strokes -----------------------------------------------------------------------------------

export class SegLayer {
    constructor(capacity = 40000, additive = false) {
        const { geo, arrays } = instanced(capacity, { aA: 3, aB: 3, aW: 1, aC: 4 });
        this.geo = geo;
        this.a = arrays;
        this.material = new THREE.ShaderMaterial({
            vertexShader: `
                attribute vec3 aA; attribute vec3 aB; attribute float aW; attribute vec4 aC;
                varying vec4 vC; varying float vY;
                void main() {
                    vec4 va = modelViewMatrix * vec4(aA, 1.0);
                    vec4 vb = modelViewMatrix * vec4(aB, 1.0);
                    vec2 d = vb.xy - va.xy;
                    float L = length(d);
                    vec2 dir = L > 1e-5 ? d / L : vec2(1.0, 0.0);
                    vec2 nrm = vec2(-dir.y, dir.x);
                    float hw = aW * 0.5;
                    float s = position.x + 0.5;
                    vec4 v = mix(va, vb, s);
                    v.xy += dir * (s < 0.5 ? -hw : hw) * 0.5;
                    v.xy += nrm * position.y * 2.0 * hw;
                    vY = position.y * 2.0;
                    vC = aC;
                    gl_Position = projectionMatrix * v;
                }`,
            fragmentShader: `
                varying vec4 vC; varying float vY;
                void main() {
                    float e = 1.0 - smoothstep(0.45, 1.0, abs(vY));
                    gl_FragColor = vec4(vC.rgb, vC.a * e);
                }`,
            transparent: true,
            depthWrite: false,
            depthTest: false,
            blending: additive ? THREE.AdditiveBlending : THREE.NormalBlending,
        });
        this.mesh = new THREE.Mesh(geo, this.material);
        this.mesh.frustumCulled = false;
        this.n = 0;
        this.cap = capacity;
    }
    begin() {
        this.n = 0;
    }
    seg(a, b, w, col) {
        if (col[3] <= 0.003 || this.n >= this.cap) return;
        const i = this.n++;
        this.a.aA.set(a, i * 3);
        this.a.aB.set(b, i * 3);
        this.a.aW[i] = w;
        this.a.aC.set(col, i * 4);
    }
    end() {
        flush(this.geo, ['aA', 'aB', 'aW', 'aC'], this.n);
    }
}

// ---- lights ------------------------------------------------------------------------------------

export class GlowLayer {
    constructor(capacity = 6000) {
        const { geo, arrays } = instanced(capacity, { aP: 3, aS: 1, aC: 4 });
        this.geo = geo;
        this.a = arrays;
        this.material = new THREE.ShaderMaterial({
            vertexShader: `
                attribute vec3 aP; attribute float aS; attribute vec4 aC;
                varying vec2 vUv; varying vec4 vC;
                void main() {
                    vec4 v = modelViewMatrix * vec4(aP, 1.0);
                    v.xy += position.xy * aS;
                    vUv = uv; vC = aC;
                    gl_Position = projectionMatrix * v;
                }`,
            fragmentShader: `
                varying vec2 vUv; varying vec4 vC;
                void main() {
                    float r = length(vUv - 0.5) * 2.0;
                    float a = exp(-r * r * 5.0) * (1.0 - smoothstep(0.85, 1.0, r));
                    gl_FragColor = vec4(vC.rgb * a * vC.a, 1.0);
                }`,
            transparent: true,
            depthWrite: false,
            depthTest: false,
            blending: THREE.AdditiveBlending,
        });
        this.mesh = new THREE.Mesh(geo, this.material);
        this.mesh.frustumCulled = false;
        this.n = 0;
        this.cap = capacity;
    }
    begin() {
        this.n = 0;
    }
    dot(p, size, col) {
        if (col[3] <= 0.003 || this.n >= this.cap) return;
        const i = this.n++;
        this.a.aP.set(p, i * 3);
        this.a.aS[i] = size;
        this.a.aC.set(col, i * 4);
    }
    end() {
        flush(this.geo, ['aP', 'aS', 'aC'], this.n);
    }
}

// ---- regions -----------------------------------------------------------------------------------

/** Filled rounded rectangles with a soft inner gradient: the glass of a region. */
export class FillLayer {
    constructor(capacity = 64) {
        const { geo, arrays } = instanced(capacity, { aP: 3, aSz: 2, aR: 1, aC: 4 });
        this.geo = geo;
        this.a = arrays;
        this.material = new THREE.ShaderMaterial({
            vertexShader: `
                attribute vec3 aP; attribute vec2 aSz; attribute float aR; attribute vec4 aC;
                varying vec2 vL; varying vec2 vSz; varying float vR; varying vec4 vC;
                void main() {
                    vL = position.xy * aSz; vSz = aSz; vR = aR; vC = aC;
                    gl_Position = projectionMatrix * modelViewMatrix * vec4(aP + vec3(vL, 0.0), 1.0);
                }`,
            fragmentShader: `
                varying vec2 vL; varying vec2 vSz; varying float vR; varying vec4 vC;
                void main() {
                    vec2 q = abs(vL) - (vSz * 0.5 - vR);
                    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - vR;
                    float inside = 1.0 - smoothstep(-1.5, 1.5, d);
                    float rim = exp(d / 40.0);
                    gl_FragColor = vec4(vC.rgb, vC.a * inside * (0.35 + 0.65 * rim));
                }`,
            transparent: true,
            depthWrite: false,
            depthTest: false,
        });
        this.mesh = new THREE.Mesh(geo, this.material);
        this.mesh.frustumCulled = false;
        this.n = 0;
    }
    begin() {
        this.n = 0;
    }
    rect(p, w, h, r, col) {
        if (col[3] <= 0.003) return;
        const i = this.n++;
        this.a.aP.set(p, i * 3);
        this.a.aSz.set([w, h], i * 2);
        this.a.aR[i] = r;
        this.a.aC.set(col, i * 4);
    }
    end() {
        flush(this.geo, ['aP', 'aSz', 'aR', 'aC'], this.n);
    }
}
