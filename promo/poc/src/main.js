// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The renderer: a world scene (the graphs, in depth, under a moving camera) and a HUD scene (the
// code, fixed to the screen), through bloom and a grade with the glitch, then the text overlay.
// `window.renderAt(t)` draws the frame at time t and returns it as a PNG data URL; `?play` plays
// the film in real time, `?t=12.3` shows one frame.
import * as THREE from 'three';
import { EffectComposer } from 'three/examples/jsm/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/examples/jsm/postprocessing/UnrealBloomPass.js';
import { ShaderPass } from 'three/examples/jsm/postprocessing/ShaderPass.js';
import { OutputPass } from 'three/examples/jsm/postprocessing/OutputPass.js';
import { FillLayer, GlowLayer, GlyphLayer, SegLayer, makeAtlas } from './layers.js';
import { DURATION, makeFilm } from './film.js';
import { clamp, ease, lin } from './geom.js';

const W = 1920;
const H = 1080;

const Grade = {
    uniforms: {
        tDiffuse: { value: null },
        uGlitch: { value: 0 },
        uSeed: { value: 0 },
        uTime: { value: 0 },
        uFade: { value: 1 },
    },
    vertexShader: `varying vec2 vUv; void main() { vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }`,
    fragmentShader: `
        uniform sampler2D tDiffuse; uniform float uGlitch; uniform float uSeed; uniform float uTime; uniform float uFade;
        varying vec2 vUv;
        float h(float n) { return fract(sin(n) * 43758.5453123); }
        void main() {
            vec2 uv = vUv;
            float band = floor(uv.y * 26.0 + h(uSeed) * 9.0);
            float on = step(1.0 - uGlitch * 0.6, h(band * 13.17 + uSeed * 1.31));
            uv.x += on * (h(band + uSeed * 3.7) - 0.5) * 0.14 * uGlitch;
            float row = floor(uv.y * 270.0);
            uv.x += (h(row + uSeed * 11.0) - 0.5) * 0.007 * uGlitch;
            vec2 d = uv - 0.5;
            float ca = 0.0012 + uGlitch * 0.02;
            vec3 col;
            col.r = texture2D(tDiffuse, uv + d * ca + vec2(ca * 0.7, 0.0)).r;
            col.g = texture2D(tDiffuse, uv).g;
            col.b = texture2D(tDiffuse, uv - d * ca - vec2(ca * 0.7, 0.0)).b;
            // a torn frame flashes brighter for an instant
            col *= 1.0 + uGlitch * 0.35 * step(0.5, h(uSeed * 2.3));
            vec3 bg = mix(vec3(0.004, 0.006, 0.014), vec3(0.012, 0.018, 0.04), vUv.y);
            col += bg;
            float v = smoothstep(1.15, 0.3, length((vUv - 0.5) * vec2(1.0, 0.82)) * 1.3);
            col *= mix(0.5, 1.0, v);
            float g = h(dot(floor(vUv * vec2(1920.0, 1080.0)), vec2(1.0, 57.0)) + fract(uTime * 7.31) * 113.0) - 0.5;
            col += g * 0.018;
            gl_FragColor = vec4(col * uFade, 1.0);
        }`,
};

async function boot() {
    const mono = new FontFace('Promo Mono NL', 'url(/PromoMonoNL-Regular.ttf)');
    document.fonts.add(await mono.load());
    await document.fonts.load('48px "Noto Sans"');
    await document.fonts.load('48px "Noto Sans CJK SC"');

    const renderer = new THREE.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
    renderer.setPixelRatio(1);
    renderer.setSize(W, H);
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 1.0;
    renderer.setClearColor(0x000000, 1);

    const chars = ' !"#$%&\'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~·×→';
    const atlas = makeAtlas('Promo Mono NL', chars);
    const world = new THREE.Scene();
    const hud = new THREE.Scene();
    const layers = {
        fillW: new FillLayer(),
        segW: new SegLayer(60000),
        glowW: new GlowLayer(4000),
        glyphW: new GlyphLayer(atlas, 3000),
        segH: new SegLayer(400),
        glowH: new GlowLayer(400),
        glyphH: new GlyphLayer(atlas, 3000),
    };
    world.add(layers.fillW.mesh, layers.segW.mesh, layers.glowW.mesh, layers.glyphW.mesh);
    // a dark backing behind the code, so that the graph can pass behind it
    const backing = new THREE.Mesh(
        new THREE.PlaneGeometry(1920, 1080),
        new THREE.ShaderMaterial({
            uniforms: { uA: { value: 0 } },
            vertexShader: `varying vec2 vUv; void main() { vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }`,
            fragmentShader: `uniform float uA; varying vec2 vUv;
                void main() { float a = (1.0 - smoothstep(0.30, 0.56, vUv.x)) * (1.0 - smoothstep(0.40, 0.62, vUv.y)); gl_FragColor = vec4(0.006, 0.009, 0.02, a * uA * 0.86); }`,
            transparent: true,
            depthTest: false,
            depthWrite: false,
        }),
    );
    backing.renderOrder = -1;
    hud.add(backing, layers.segH.mesh, layers.glowH.mesh, layers.glyphH.mesh);
    layers.fillW.mesh.renderOrder = 0;
    layers.segW.mesh.renderOrder = 1;
    layers.glowW.mesh.renderOrder = 2;
    layers.glyphW.mesh.renderOrder = 3;

    const film = makeFilm(layers);
    const first = film.draw(0);

    const target = new THREE.WebGLRenderTarget(W, H, { type: THREE.HalfFloatType, samples: 4 });
    const composer = new EffectComposer(renderer, target);
    const worldPass = new RenderPass(world, first.cam);
    const hudPass = new RenderPass(hud, first.hudCam);
    hudPass.clear = false;
    hudPass.clearDepth = true;
    const bloom = new UnrealBloomPass(new THREE.Vector2(W, H), 1.05, 0.55, 0.82);
    const grade = new ShaderPass(Grade);
    composer.addPass(worldPass);
    composer.addPass(hudPass);
    composer.addPass(bloom);
    composer.addPass(grade);
    composer.addPass(new OutputPass());

    const out = document.createElement('canvas');
    out.width = W;
    out.height = H;
    const g = out.getContext('2d');
    document.body.appendChild(out);
    out.style.width = '100vw';

    // a lost context draws nothing, silently: a frame must fail instead
    let lost = false;
    renderer.domElement.addEventListener('webglcontextlost', () => {
        lost = true;
    });

    function frame(t) {
        if (lost || renderer.getContext().isContextLost()) throw new Error('WebGL context lost');
        const { cam, fx } = film.draw(t);
        worldPass.camera = cam;
        grade.uniforms.uGlitch.value = fx.glitch;
        backing.material.uniforms.uA.value = fx.backing;
        grade.uniforms.uSeed.value = fx.seed;
        grade.uniforms.uTime.value = t;
        grade.uniforms.uFade.value = clamp(lin(t, 0, 0.35)) * (1 - lin(t, DURATION - 0.25, DURATION));
        composer.render();
        // opaque frames: the ground is black under everything
        g.fillStyle = '#000';
        g.fillRect(0, 0, W, H);
        g.drawImage(renderer.domElement, 0, 0);
        overlay(g, t, film.T, fx.glitch);
    }

    window.renderAt = (t) => {
        frame(t);
        return out.toDataURL('image/png');
    };
    window.cues = film.cues;
    window.debug = { layers, atlas, renderer };

    const q = new URLSearchParams(location.search);
    if (q.has('t')) frame(parseFloat(q.get('t')));
    if (q.has('play')) {
        const t0 = performance.now();
        const tick = () => {
            frame(((performance.now() - t0) / 1000) % DURATION);
            requestAnimationFrame(tick);
        };
        tick();
    }
    window.ready = true;
}

/** The words: a statement in the lower third, the question in the middle, the title at the end. */
function overlay(g, t, T, glitch) {
    const lines = [
        { t0: T.line1, t1: T.turn - 0.12, y: 905, en: 'AI compilers trace Python to recover a graph the language threw away.', zh: 'AI 编译器追踪 Python，想找回语言丢掉的那张图。', size: 40 },
        { t0: T.question, t1: T.questionOut, y: 560, en: 'What if the language never threw it away?', zh: '如果语言从未丢掉它呢？', size: 54 },
        { t0: T.final, t1: 31, y: 930, en: 'The graph was always there.', zh: '那张图，一直都在。', size: 50 },
    ];
    for (const L of lines) {
        const a = ease(lin(t, L.t0, L.t0 + 0.5)) * (1 - ease(lin(t, L.t1 - 0.45, L.t1)));
        if (a <= 0) continue;
        const rise = (1 - ease(lin(t, L.t0, L.t0 + 0.8))) * 14;
        g.save();
        g.textAlign = 'center';
        g.textBaseline = 'alphabetic';
        g.globalAlpha = a;
        g.shadowColor = 'rgba(120,170,255,0.45)';
        g.shadowBlur = 18;
        g.fillStyle = '#EEF3FF';
        g.font = `300 ${L.size}px "Noto Sans"`;
        g.letterSpacing = '0.5px';
        const jitter = glitch > 0.05 ? Math.sin(t * 997.0) * 8 * glitch : 0;
        g.fillText(L.en, 960 + jitter, L.y + rise);
        g.shadowBlur = 0;
        g.globalAlpha = a * 0.72;
        g.fillStyle = '#B8C4E0';
        g.font = `400 ${Math.round(L.size * 0.6)}px "Noto Sans CJK SC"`;
        g.fillText(L.zh, 960, L.y + rise + L.size * 0.95);
        g.restore();
    }
}

boot();
