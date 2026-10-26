/**
 * Shot 8 of "The program is the graph" (6 s): a fault, and a fence.
 * Script: ../../../script.md (shot 8).
 *
 * What the picture claims, and why it holds (design principles 3.9):
 * - A bug (an index out of range) is a fault, not an error value: by default it stops the program,
 *   with its location (runtime diagnostic IndexOutOfRange).
 * - `attempt(f)` is a function transform: it returns a function whose result is `f`'s with `!`
 *   added, and a fault inside `f` becomes an error value at that function boundary. The boundary
 *   is a call, not a span of text, so `map<attempt(parse_row)>` fences each call on its own: the
 *   other five rows finish, and the fourth is an error value.
 *
 * Timeline (seconds)
 *   0.0 - 0.6   the code (without attempt) and the graph: map fans out to six calls
 *   0.6 - 2.0   the rows run; call 4 blots coral; everything freezes and dims; the location is stamped
 *   2.0 - 2.6   a short reverse scrub; `attempt(` and `)` are typed; a fence is drawn round each call
 *   2.6 - 4.8   replay: call 4 blots inside its fence; the blot leaves as a coral bead on a side
 *               track; the other five finish; the results: five mint values and one coral `!`
 *   4.8 - 6.0   hold
 */
import {Circle, Line, Node, Rect, makeScene2D} from '@motion-canvas/2d';
import {Vector2, all, chain, createRef, createSignal, easeInOutCubic, easeOutCubic, linear, sequence, waitFor} from '@motion-canvas/core';
import {
  CHALK,
  CHALK_DIM,
  CORAL,
  MINT,
  SAND,
  addNode,
  at,
  beadsAlong,
  ch,
  code,
  dim,
  dm,
  edge,
  ground,
  kw,
  label,
  stroke,
  subtitles,
  trace,
  typeLine,
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
    [kw('let'), ch(' rows = lines'), dm('->'), ch('map<'), kw('attempt(', true), ch('parse_row'), kw(')', true), ch('>')],
  ], 40);

  // ---- the graph: map fans out to six calls, each to its result --------------------------------
  const ROWS = [-270, -165, -60, 45, 150, 255];
  const FAULT = 3;
  const lines = addNode(w, at(-720, -25), 'lines', {kind: 'value'});
  const map = addNode(w, at(-420, -25), 'map');
  const calls = ROWS.map((y) => addNode(w, at(0, y), 'parse_row', {glyphSize: 26}));
  const results = ROWS.map((y) => addNode(w, at(420, y), '', {kind: 'value', width: 120}));
  const eIn = edge(w, lines, map);
  const fan = calls.map((c) => edge(w, map, c));
  const outs = calls.map((c, i) => edge(w, c, results[i]));

  // the beads that run from each call to its result: progress p, the fault's bead stops at the blot
  const p = createSignal(0);
  const beadRefs = calls.map((c, i) => {
    const ref = createRef<Circle>();
    const a = c.pos().add(new Vector2(c.hw + 6, 0));
    const b = results[i].pos().sub(new Vector2(results[i].hw + 8, 0));
    w.add(<Circle ref={ref} size={18} fill={SAND} shadowColor={SAND} shadowBlur={16} zIndex={6} opacity={0} position={() => Vector2.lerp(a, b, i === FAULT ? 0 : p())} />);
    return ref;
  });
  const blot = createRef<Circle>();
  w.add(<Circle ref={blot} position={calls[FAULT].pos()} size={150} fill={CORAL} opacity={0.85} scale={0} zIndex={7} />);
  const stamp = label(w, 0, 345, 'rows.cml:3:31  IndexOutOfRange', CORAL, 26);

  // the fences: one per call
  const fences = calls.map((c) => {
    const ref = createRef<Rect>();
    w.add(<Rect ref={ref} position={c.pos()} width={c.hw * 2 + 36} height={c.hh * 2 + 16} radius={24} stroke={CORAL} lineWidth={2} end={0} zIndex={3} />);
    return ref;
  });

  // ---- 0.0 - 0.6: the code and the graph -------------------------------------------------------
  yield chain(waitFor(0.3), say('bug 是故障，会带着位置停下——'));
  yield* all(
    src.root().opacity(1, 0.4),
    sequence(0.03, ...[lines, map, ...calls, ...results].map((n) => trace(n, 0.3))),
    stroke(eIn, 0.25),
    sequence(0.03, ...[...fan, ...outs].map((e) => stroke(e, 0.25))),
  );

  // ---- 0.6 - 2.0: the run; call 4 faults; everything stops ---------------------------------------
  yield* beadsAlong(w, [eIn], 0.25);
  yield* beadsAlong(w, fan, 0.35);
  beadRefs.forEach((b) => b().opacity(1));
  yield* p(0.4, 0.35, linear);
  yield* all(blot().scale(1, 0.18, easeOutCubic), calls[FAULT].shape().stroke(CORAL, 0.1));
  yield* all(
    ...[lines, map, ...calls.filter((_, i) => i !== FAULT), ...results].map((n) => dim(n, true, 0.3)),
    ...[eIn, ...fan, ...outs].map((e) => e().opacity(0.35, 0.3)),
    ...beadRefs.map((b) => b().opacity(0.4, 0.3)),
    stamp().opacity(1, 0.2),
  );
  yield* waitFor(0.5);

  // ---- 2.0 - 2.6: reverse scrub; attempt; the fences ---------------------------------------------
  yield say('除非 attempt 把它隔在函数边界里。');
  yield* all(
    p(0, 0.3, easeInOutCubic),
    blot().scale(0, 0.3),
    stamp().opacity(0, 0.2),
    ...[lines, map, ...calls, ...results].map((n) => dim(n, false, 0.3)),
    calls[FAULT].shape().stroke(CHALK, 0.3),
    ...[eIn, ...fan, ...outs].map((e) => e().opacity(1, 0.3)),
    ...beadRefs.map((b) => b().opacity(0, 0.2)),
  );
  yield* all(typeLine(src, 0, 40), chain(waitFor(0.15), all(...fences.map((f) => f().end(1, 0.4, easeInOutCubic)))));

  // ---- 2.6 - 4.8: replay ------------------------------------------------------------------------
  yield* beadsAlong(w, fan, 0.3);
  beadRefs.forEach((b, i) => b().opacity(i === FAULT ? 0 : 1));
  // the fault, inside its fence
  blot().size(60);
  yield* all(p(0.3, 0.2, linear), blot().scale(1, 0.15, easeOutCubic));
  // at the fence the blot becomes a bead on a short side track: an error value, not a stop
  const f = calls[FAULT];
  const r = results[FAULT];
  const side = createRef<Line>();
  const sideStart = f.pos().add(new Vector2(f.hw + 18, 10));
  const sideEnd = r.pos().sub(new Vector2(r.hw + 8, -10));
  w.add(<Line ref={side} points={[sideStart, sideStart.add(new Vector2(40, 34)), sideEnd.add(new Vector2(-40, 34)), sideEnd]} radius={20} stroke={CORAL} lineWidth={3} end={0} zIndex={4} />);
  const errBead = createRef<Circle>();
  w.add(<Circle ref={errBead} position={f.pos()} size={18} fill={CORAL} shadowColor={CORAL} shadowBlur={16} zIndex={7} />);
  yield* all(blot().scale(0, 0.2), errBead().position(sideStart, 0.2));
  yield* all(
    side().end(1, 0.5, easeInOutCubic),
    errBead().position(sideStart.add(new Vector2(40, 34)), 0.15).to(sideEnd.add(new Vector2(-40, 34)), 0.25).to(sideEnd, 0.12),
    p(1, 0.5, linear),
  );
  beadRefs.forEach((b) => b().opacity(0));
  errBead().opacity(0);
  results.forEach((n, i) => {
    n.glyph().opacity(0);
    n.glyph().text(i === FAULT ? '!' : 'Row');
    n.glyph().fill(i === FAULT ? CORAL : MINT);
  });
  yield* all(...results.map((n, i) => all(n.shape().stroke(i === FAULT ? CORAL : MINT, 0.25), n.glyph().opacity(1, 0.25))));

  // ---- hold -------------------------------------------------------------------------------------
  yield* waitFor(1.2);
});
