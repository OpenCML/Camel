// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Shot 6 of "The program is the graph" (6 s): data is a stream.
 * Script: ../../../script.md (shot 6).
 *
 * What the picture claims, and why it holds (design principles 5.4):
 * - `rows(xs)` is a generator: a stream `f32[784]..`, one element per pull; nothing in the pipe
 *   moves between pulls (an element is computed if and only if a consumer pulls it).
 * - `chunks<64>` (stdlib stream; the size is a `with` parameter) pulls 64 elements for each array it
 *   hands on, and the loop pulls one array per round (`for b in data`).
 * - A stream is consumed once: a second consumer is a compile-time error (StreamConsumedTwice).
 *
 * Timeline (seconds)
 *   0.0 - 0.7   the code; the pipe with its step function and state; chunks; the loop
 *   0.7 - 1.1   the loop pulls a batch; chunks pulls from the pipe
 *   1.1 - 3.0   64 elements leave the pipe, one per pull, faster and faster; the count climbs
 *   3.0 - 3.7   one batch goes into the loop; its state goes round once
 *   3.8 - 4.8   a second consumer reaches for the pipe; its thread snaps: StreamConsumedTwice
 *   4.8 - 6.0   hold
 */
import {Circle, Line, Node, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {Vector2, all, chain, createRef, createSignal, easeInOutCubic, easeOutCubic, waitFor} from '@motion-canvas/core';
import {
  CHALK,
  CHALK_DIM,
  CORAL,
  GROUND_MID,
  MONO,
  SAND,
  addNode,
  at,
  ch,
  code,
  dm,
  edge,
  ground,
  handOff,
  kw,
  stroke,
  subtitles,
  trace,
} from '../lib/plotter';

const SHOW_SUBTITLE = true;

export default makeScene2D(function* (view) {
  ground(view);
  const world = createRef<Node>();
  view.add(<Node ref={world} />);
  const w = world();
  const say = subtitles(view, SHOW_SUBTITLE);

  // ---- the code --------------------------------------------------------------------------------
  const src = code(w, -890, -470, [
    [kw('func'), ch(' rows(xs: f32[?, 784]): f32[784].. {')],
    [ch('    '), kw('for'), ch(' x '), kw('in'), ch(' xs '), kw('do'), ch(' '), kw('yield'), ch(' x')],
    [ch('}')],
    [ch('')],
    [kw('let'), ch(' data = xs'), dm('->'), ch('rows'), dm('->'), ch('chunks<64>')],
    [kw('let'), ch(' trained = '), kw('loop'), ch(' p = p0 '), kw('for'), ch(' b '), kw('in'), ch(' data '), kw('do'), ch(' p'), dm('->'), ch('sgd(b)')],
  ], 34);

  // ---- the pipe: a step function and its state ----------------------------------------------------
  const Y = 60;
  const pipe = createRef<Rect>();
  w.add(<Rect ref={pipe} x={-480} y={Y} width={660} height={90} radius={45} stroke={CHALK_DIM} lineWidth={3} end={0} />);
  const stepFn = addNode(w, at(-700, Y), 'rows', {glyphSize: 26});
  const state = createRef<Circle>();
  const stateX = createSignal(-580);
  w.add(<Circle ref={state} x={stateX} y={Y} size={16} fill={CHALK_DIM} opacity={0} />);
  const mouth = new Vector2(-150, Y);
  const pipeName = createRef<Txt>();
  w.add(<Txt ref={pipeName} text="f32[784].." x={-480} y={Y + 76} fontFamily={MONO} fontSize={24} fill={CHALK_DIM} opacity={0} />);

  const batch = addNode(w, at(110, Y), 'chunks<64>');
  const count = createRef<Txt>();
  const n = createSignal(0);
  w.add(<Txt ref={count} text={() => `${n()} / 64`} x={110} y={Y + 70} fontFamily={MONO} fontSize={26} fill={SAND} opacity={0} />);

  // the loop: a frame with its return arc and the state on the arc
  const loop = createRef<Node>();
  const loopState = createSignal(0);
  w.add(
    <Node ref={loop} x={540} y={Y} opacity={0}>
      <Rect width={240} height={110} radius={22} stroke={CHALK} lineWidth={4} fill={GROUND_MID} />
      <Txt text="loop p" fontFamily={MONO} fontSize={28} fill={CHALK} />
      <Line points={[[-70, -55], [-70, -105], [70, -105], [70, -62]]} radius={30} stroke={CHALK_DIM} lineWidth={3} endArrow arrowSize={9} />
      <Circle
        size={14}
        fill={SAND}
        position={() => {
          const t = loopState();
          const pts = [new Vector2(-70, -55), new Vector2(-70, -105), new Vector2(70, -105), new Vector2(70, -62)];
          const seg = Math.min(2, Math.floor(t * 3));
          const local = t * 3 - seg;
          return Vector2.lerp(pts[seg], pts[seg + 1], local);
        }}
      />
    </Node>,
  );
  const loopNode = addNode(w, at(540, Y), '', {width: 240});
  loopNode.root().opacity(0);

  // the pulls: short threads drawn from a consumer back to what it pulls from
  const pullLine = (from: Vector2, to: Vector2, color = CHALK) => {
    const ref = createRef<Line>();
    w.add(<Line ref={ref} points={[from, to]} stroke={color} lineWidth={2} lineDash={[6, 6]} end={0} zIndex={3} />);
    return ref;
  };
  const loopPull = pullLine(new Vector2(420, Y - 20), new Vector2(110 + batch.hw + 6, Y - 20));
  const batchPull = pullLine(new Vector2(110 - batch.hw - 6, Y - 20), mouth.add(new Vector2(0, -20)));
  const toLoop = edge(w, batch, loopNode);

  // ---- 0.0 - 0.7: everything drawn ---------------------------------------------------------------
  yield chain(waitFor(0.3), say('数据是流：按需生成，'));
  yield* all(
    src.root().opacity(1, 0.4),
    pipe().end(1, 0.6, easeInOutCubic),
    trace(stepFn, 0.4),
    state().opacity(1, 0.4),
    pipeName().opacity(1, 0.4),
    trace(batch, 0.4),
    loop().opacity(1, 0.4),
    count().opacity(1, 0.4),
    stroke(toLoop, 0.4),
  );

  // ---- 0.7 - 1.1: the loop pulls a batch; chunks pulls from the pipe --------------------------------
  yield* loopPull().end(1, 0.25, easeOutCubic);
  yield* batchPull().end(1, 0.25, easeOutCubic);

  // ---- 1.1 - 3.0: 64 pulls, one element each ----------------------------------------------------
  yield say('拉一次，来一批。');
  for (let k = 1; k <= 64; k++) {
    const d = Math.max(0.025, 0.28 * Math.pow(0.86, k));
    const bead = createRef<Circle>();
    w.add(<Circle ref={bead} position={mouth} size={16} fill={SAND} shadowColor={SAND} shadowBlur={16} zIndex={6} />);
    stateX(-580 + ((k * 7) % 60));
    yield* bead().position(new Vector2(110 - batch.hw - 4, Y), d, easeInOutCubic);
    bead().remove();
    n(k);
  }

  // ---- 3.0 - 3.7: one batch into the loop; the state goes round ------------------------------------
  const badge = createRef<Node>();
  w.add(
    <Node ref={badge} position={batch.pos()} zIndex={6}>
      {[0, 1, 2].flatMap((i) => [0, 1, 2].map((j) => <Circle x={(i - 1) * 10} y={(j - 1) * 10} size={7} fill={SAND} />))}
    </Node>,
  );
  yield* badge().position(new Vector2(420, Y), 0.4, easeInOutCubic);
  badge().remove();
  yield* all(loopState(1, 0.4, easeInOutCubic), loopPull().opacity(0.3, 0.3), batchPull().opacity(0.3, 0.3));
  n(0);

  // ---- 3.8 - 4.8: a second consumer; its thread snaps --------------------------------------------
  const ghost = createRef<Rect>();
  w.add(<Rect ref={ghost} x={-150} y={-150} width={170} height={64} radius={18} stroke={CHALK_DIM} lineWidth={2} lineDash={[8, 8]} opacity={0}>
    <Txt text="other" fontFamily={MONO} fontSize={26} fill={CHALK_DIM} />
  </Rect>);
  const ghostPull = pullLine(new Vector2(-150, -118), mouth.add(new Vector2(0, -46)), CHALK_DIM);
  const snap = createRef<Txt>();
  w.add(<Txt ref={snap} text="✕  StreamConsumedTwice" x={30} y={-60} fontFamily={MONO} fontSize={24} fill={CORAL} opacity={0} />);
  yield* ghost().opacity(1, 0.3);
  yield* ghostPull().end(1, 0.3, easeOutCubic);
  yield* all(ghostPull().stroke(CORAL, 0.1), snap().opacity(1, 0.2));
  yield* all(ghostPull().end(0.45, 0.3, easeOutCubic), ghost().opacity(0.4, 0.4));

  // ---- hold, then hand the pipe to shot 7, where it straightens into the rail ----------------------
  yield* waitFor(0.3);
  yield* handOff(w, [pipe()]);
});
