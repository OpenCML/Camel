/**
 * Shot 7 of "The program is the graph" (7 s): the railway.
 * Script: ../../../script.md (shot 7).
 *
 * What the picture claims, and why it holds (design principles 3.9; stdlib conventions 4):
 * - `read_text` can fail: it gives a `string!`. `->` carries an error past every step that takes
 *   only the value, unchanged, and the steps it passes do not run; the error is handled once, at
 *   `!!`, which gives `defaults` in its place.
 * - The program writes only the happy path: one rail, and one `!!` at its end.
 *
 * Timeline (seconds)
 *   0.0 - 0.8   the code; the rail with its switch points; the side track beneath
 *   0.8 - 2.6   first run: the value rides the rail through every step; cfg is written in mint
 *   2.8 - 5.6   second run: read_text fails; the bead turns coral, switches onto the side track,
 *               passes under decode_as and validate (dark), rejoins at !!, and turns sand with
 *               defaults; `!! defaults` is underlined in the code
 *   5.6 - 7.0   hold
 */
import {Line, Node, Rect, makeScene2D} from '@motion-canvas/2d';
import {Vector2, all, chain, createRef, easeInOutCubic, linear, sequence, waitFor} from '@motion-canvas/core';
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
  handOff,
  kw,
  showValue,
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
  const src = code(w, -890, -460, [
    [kw('let'), ch(' cfg = path')],
    [ch('    '), dm('->'), ch('read_text')],
    [ch('    '), dm('->'), ch('json::decode_as::<Config>')],
    [ch('    '), dm('->'), ch('validate '), ch('!! defaults')],
  ], 40);

  // ---- the rail ------------------------------------------------------------------------------------
  const Y = 20; // the main rail
  const SIDE = Y + 130; // the side track
  const path = addNode(w, at(-800, Y), 'path', {kind: 'value'});
  const read = addNode(w, at(-460, Y), 'read_text');
  const decode = addNode(w, at(-100, Y), 'decode_as');
  const valid = addNode(w, at(250, Y), 'validate');
  const handle = addNode(w, at(560, Y), '!!');
  const cfg = addNode(w, at(800, Y), '', {kind: 'ring'});
  const defaults = addNode(w, at(740, SIDE + 90), 'defaults', {kind: 'value'});
  const steps = [path, read, decode, valid, handle, cfg];
  const rail = [edge(w, path, read), edge(w, read, decode), edge(w, decode, valid), edge(w, valid, handle), edge(w, handle, cfg)];
  const eDefaults = edge(w, defaults, handle, {color: CHALK_DIM});

  // the side track: a stub leaves under every step that can fail and joins the coral rail;
  // the rail rejoins the main one at `!!`
  const side = createRef<Line>();
  w.add(<Line ref={side} points={[[-420, SIDE], [520, SIDE]]} stroke={CORAL} lineWidth={3} opacity={0.85} end={0} />);
  const stubs = [read, decode, valid].map((n) => {
    const ref = createRef<Line>();
    w.add(<Line ref={ref} points={[n.pos().add(new Vector2(n.hw - 10, n.hh)), n.pos().add(new Vector2(n.hw + 40, SIDE - Y))]} radius={30} stroke={CORAL} lineWidth={3} opacity={0.85} end={0} />);
    return ref;
  });
  const rejoin = createRef<Line>();
  w.add(<Line ref={rejoin} points={[[520, SIDE], [handle.pos().x - 10, handle.pos().y + handle.hh]]} stroke={CORAL} lineWidth={3} opacity={0.85} endArrow arrowSize={9} end={0} />);

  const underline = createRef<Rect>();
  w.add(<Rect ref={underline} x={-890 + 40 + 36 + 15 * 24} y={-460 + 54 * 4 - 6} offsetX={-1} width={0} height={4} fill={SAND} />);

  // The pipe, as shot 6 left it (the match cut), straightens into the rail.
  const pipe = createRef<Rect>();
  w.add(<Rect ref={pipe} x={-480} y={60} width={660} height={90} radius={45} stroke={CHALK_DIM} lineWidth={3} zIndex={0} />);
  yield* all(
    pipe().position(new Vector2(0, Y), 0.6, easeInOutCubic),
    pipe().size(new Vector2(1600, 0), 0.6, easeInOutCubic),
    pipe().radius(0, 0.6, easeInOutCubic),
    pipe().stroke(CHALK, 0.6),
    pipe().lineWidth(2, 0.6),
  );

  // ---- 0.0 - 0.8: the code and the rails -------------------------------------------------------------
  yield chain(waitFor(0.3), say('失败是一个值。'));
  yield* all(
    src.root().opacity(1, 0.4),
    sequence(0.06, ...[...steps, defaults].map((n) => trace(n, 0.3))),
    sequence(0.06, ...rail.map((e) => stroke(e, 0.25))),
    stroke(eDefaults, 0.3),
    side().end(1, 0.7, easeInOutCubic),
    sequence(0.08, ...stubs.map((s) => s().end(1, 0.3))),
    rejoin().end(1, 0.5),
    pipe().opacity(0, 0.6),
  );

  // ---- 0.8 - 2.6: the happy path: the value hops from step to step along the rail --------------
  for (const e of rail) {
    yield* beadsAlong(w, [e], 0.26);
    yield* waitFor(0.03);
  }
  yield* all(cfg.shape().stroke(MINT, 0.3), showValue(cfg, 'cfg'));
  yield* waitFor(0.3);

  // ---- 2.8 - 5.6: read_text fails; the error rides the side track -------------------------------------
  yield say('箭头把它带上旁路：只写正常路径，在一处处理。');
  cfg.value().opacity(0);
  cfg.shape().stroke(CHALK);
  yield* beadsAlong(w, [rail[0]], 0.3);
  yield* read.shape().stroke(CORAL, 0.15);
  const bead = createRef<Rect>();
  const stubTop = read.pos().add(new Vector2(read.hw - 10, read.hh));
  w.add(<Rect ref={bead} size={20} radius={10} fill={CORAL} shadowColor={CORAL} shadowBlur={22} position={stubTop} zIndex={7} />);
  // down the stub, along the side track, under the steps that do not run
  yield* bead().position(read.pos().add(new Vector2(read.hw + 40, SIDE - Y)), 0.3, easeInOutCubic);
  yield* all(bead().position(new Vector2(520, SIDE), 1.0, linear), dim(decode, true, 0.3), dim(valid, true, 0.3));
  yield* bead().position(handle.pos().add(new Vector2(-10, handle.hh + 4)), 0.3, easeInOutCubic);
  bead().remove();
  // handled once: `!!` gives defaults in its place
  yield* beadsAlong(w, [eDefaults], 0.3);
  yield* all(handle.shape().stroke(SAND, 0.2).to(CHALK, 0.3), underline().width(11 * 24, 0.4, easeInOutCubic));
  yield* beadsAlong(w, [rail[4]], 0.3);
  yield* all(cfg.shape().stroke(MINT, 0.3), showValue(cfg, 'defaults'), read.shape().stroke(CHALK, 0.4));

  // ---- hold, then hand the shape of path to shot 8 ---------------------------------------------------------
  yield* waitFor(0.7);
  yield* all(handOff(w, [path.root()]), path.glyph().opacity(0, 0.4));
  yield* waitFor(0.1);
});
