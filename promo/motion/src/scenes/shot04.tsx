// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Shot 4 of "The program is the graph" (9 s): a gradient is a reflection; a pass is a sweep.
 * Script: ../../../script.md (shot 4).
 *
 * What the picture claims, and why it holds (design principles 9.1, 9.9; README "A taste"):
 * - `with_grad(loss)` rewrites the graph of `loss` into the graph of its value and gradient with
 *   respect to its first `with` parameter `m`; `std::macro` expands it ahead of time, so the
 *   passes after it see the derivative as an ordinary graph. The reverse graph is drawn as the
 *   mirror image of the forward one, edges reversed; the gradient `g` comes out where the weights
 *   went in.
 * - `grad`, `vjp` and `jvp` are the same kind of transform (`jvp` runs forward, so its twin is
 *   drawn above the line, the same way round).
 * - `std::opt` is a function from that graph to a graph: a value read in both halves (`x`) is one
 *   node, and a chain of constants folds. No node count is shown: the drawing is a sketch of the
 *   graph, not a measurement of it.
 *
 * Timeline (seconds)
 *   0.0 - 0.7   the forward graph of `loss`, the code, the pipeline strip
 *   0.7 - 3.0   std::macro: a baseline, and the graph reflected beneath it in violet
 *   3.0 - 4.0   the label flicks through grad, vjp, jvp (a twin above the line) and back
 *   4.0 - 7.3   std::opt: a cyan sweep; behind it `x` read twice becomes one, 1.0 and ×2 fold
 *   7.3 - 9.0   std::jit: a second sweep; beads run forward and then back to `g`
 */
import {Line, Node, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {Vector2, all, chain, createRef, createSignal, easeInOutCubic, linear, sequence, waitFor} from '@motion-canvas/core';
import {
  CHALK,
  CHALK_DIM,
  CYAN,
  MINT,
  MONO,
  VIOLET,
  addNode,
  handOff,
  at,
  beadsAlong,
  ch,
  code,
  edge,
  ground,
  kw,
  stroke,
  subtitles,
  trace,
} from '../lib/plotter';

const SHOW_SUBTITLE = true;
const BASE = 0; // the reflection line

export default makeScene2D(function* (view) {
  ground(view);
  const world = createRef<Node>();
  view.add(<Node ref={world} />);
  const w = world();
  const say = subtitles(view, SHOW_SUBTITLE);

  // ---- code and the pipeline strip ------------------------------------------------------------
  const src = code(w, -890, -480, [
    [kw('let'), ch(' step = with_grad(loss)')],
    [kw('let'), ch(' (l, g) = step<m>(x, y)')],
  ], 36);
  const strip = createRef<Rect>();
  const passes = ['std::macro', 'std::opt', 'std::jit'].map(() => createRef<Txt>());
  view.add(
    <Rect ref={strip} y={380} layout direction="row" gap={22} alignItems="center" opacity={0}>
      <Txt text="camel train.cml" fontFamily={MONO} fontSize={28} fill={CHALK_DIM} />
      {['std::macro', 'std::opt', 'std::jit'].map((p, i) => (
        <Txt ref={passes[i]} text={p} fontFamily={MONO} fontSize={28} fill={CHALK_DIM} />
      ))}
    </Rect>,
  );
  const active = (i: number) => all(...passes.map((p, k) => p().fill(k === i ? CYAN : CHALK_DIM, 0.3)));

  // The graph layer: offset so that the graph sits to the right of the code and clear of it.
  const graphLayer = createRef<Node>();
  w.add(<Node ref={graphLayer} x={230} y={40} />);
  const gw = graphLayer();

  // ---- the forward graph of loss ----------------------------------------------------------------
  const FY = -160;
  const x = addNode(gw, at(-600, FY), 'x', {kind: 'value'});
  const mlp = addNode(gw, at(-320, FY), 'mlp');
  const m = addNode(gw, at(-320, FY - 36 - 23), 'm', {kind: 'weight'});
  const y = addNode(gw, at(-60, FY - 140), 'y', {kind: 'value'});
  const mse = addNode(gw, at(-60, FY), 'mse');
  const l = addNode(gw, at(220, FY), '', {kind: 'ring'});
  const fEdges = [edge(gw, x, mlp), edge(gw, mlp, mse), edge(gw, y, mse), edge(gw, mse, l)];
  const frame = createRef<Rect>();
  gw.add(<Rect ref={frame} x={-190} y={FY - 60} width={1000} height={290} radius={28} stroke={CHALK_DIM} lineWidth={2} lineDash={[14, 10]} end={0} zIndex={0} />);
  const frameName = createRef<Txt>();
  gw.add(<Txt ref={frameName} text="loss" x={-655} y={FY - 180} fontFamily={MONO} fontSize={26} fill={CHALK_DIM} opacity={0} />);

  // ---- the reflection: the reverse graph, edges reversed -------------------------------------------
  const xB = addNode(gw, at(-600, -FY), 'x', {kind: 'value', stroke: VIOLET});
  const mlpB = addNode(gw, at(-320, -FY), 'mlp', {stroke: VIOLET});
  const mseB = addNode(gw, at(-60, -FY), 'mse', {stroke: VIOLET});
  const times2 = addNode(gw, at(80, -FY), '×2', {stroke: VIOLET});
  const seed = addNode(gw, at(220, -FY), '1.0', {kind: 'value', stroke: VIOLET, glyphSize: 26});
  const g = addNode(gw, at(-320, -FY + 36 + 70), 'g', {kind: 'ring', stroke: VIOLET});
  const mirrorNodes = [seed, times2, mseB, mlpB, xB, g];
  const eSeed = edge(gw, seed, times2, {color: VIOLET});
  const eTimes = edge(gw, times2, mseB, {color: VIOLET});
  const eMse = edge(gw, mseB, mlpB, {color: VIOLET});
  const eXB = edge(gw, xB, mlpB, {color: VIOLET});
  const eG = edge(gw, mlpB, g, {color: VIOLET});
  const mEdges = [eSeed, eTimes, eMse, eXB, eG];
  const baseline = createRef<Line>();
  gw.add(<Line ref={baseline} points={[[-720, BASE], [380, BASE]]} stroke={VIOLET} lineWidth={2} opacity={0.8} end={0} />);
  const tag = createRef<Txt>();
  gw.add(<Txt ref={tag} text="with_grad" x={520} y={BASE} fontFamily={MONO} fontSize={30} fill={VIOLET} opacity={0} />);
  // jvp's twin: the forward graph again, violet, above it and the same way round
  const twin = createRef<Node>();
  gw.add(<Node ref={twin} opacity={0} y={-26} />);
  for (const [a, b] of [[x, mlp], [mlp, mse], [mse, l]] as const) {
    twin().add(<Line points={[a.pos().add(new Vector2(a.hw + 4, 0)), b.pos().sub(new Vector2(b.hw + 10, 0))]} stroke={VIOLET} lineWidth={3} endArrow arrowSize={9} />);
  }

  // The frame of mlp, as shot 3 left it (the match cut), folds into the node mlp.
  const carried = createRef<Rect>();
  const carriedName = createRef<Txt>();
  w.add(<Rect ref={carried} x={0} y={150} width={980} height={250} radius={28} stroke={CHALK_DIM} lineWidth={2} lineDash={[14, 10]} zIndex={3} />);
  w.add(<Txt ref={carriedName} text="mlp" x={-455} y={55} fontFamily={MONO} fontSize={26} fill={CHALK_DIM} zIndex={4} />);
  const mlpAt = mlp.pos().add(graphLayer().position());
  function* foldIn() {
    yield* all(
      carried().position(mlpAt, 0.7, easeInOutCubic),
      carried().size(new Vector2(mlp.hw * 2, mlp.hh * 2), 0.7, easeInOutCubic),
      carried().radius(18, 0.7, easeInOutCubic),
      carried().stroke(CHALK, 0.7),
      carried().lineWidth(4, 0.7),
      carriedName().position(mlpAt, 0.7, easeInOutCubic),
      carriedName().fontSize(30, 0.7, easeInOutCubic),
      carriedName().fill(CHALK, 0.7),
    );
    mlp.shape().opacity(1);
    mlp.shape().end(1);
    mlp.glyph().opacity(1);
    carried().remove();
    carriedName().remove();
  }

  // ---- 0.0 - 0.7: the forward graph, the code, the strip ------------------------------------------
  yield chain(waitFor(0.3), say('求导是对图的变换，'));
  yield* all(
    src.root().opacity(1, 0.4),
    strip().opacity(1, 0.4),
    frame().end(1, 0.6, easeInOutCubic),
    frameName().opacity(1, 0.4),
    sequence(0.05, ...[x, m, y, mse, l].map((n) => trace(n, 0.3))),
    foldIn(),
    sequence(0.06, ...fEdges.map((e) => stroke(e))),
  );

  // ---- 0.7 - 3.0: std::macro reflects the graph ---------------------------------------------------
  yield* active(0);
  yield* baseline().end(1, 0.5, easeInOutCubic);
  // each mirrored node leaves its forward twin and comes to rest beneath the line
  const sources = [l, mse, mse, mlp, x, m];
  for (const [i, n] of mirrorNodes.entries()) {
    const target = n.pos();
    n.pos(sources[i].pos());
    yield n.pos(target, 0.6, easeInOutCubic);
    yield trace(n, 0.45);
    yield* waitFor(0.06);
  }
  yield* waitFor(0.1);
  yield* all(sequence(0.08, ...mEdges.map((e) => stroke(e, 0.3))), tag().opacity(1, 0.3));

  // ---- 3.0 - 4.0: grad, vjp, jvp, back to with_grad ---------------------------------------------------
  for (const name of ['grad', 'vjp', 'jvp']) {
    tag().text(name);
    if (name === 'jvp') {
      yield* twin().opacity(0.9, 0.12).wait(0.18).to(0, 0.12);
    } else {
      yield* waitFor(0.15);
    }
  }
  tag().text('with_grad');
  yield* waitFor(0.1);

  // ---- 4.0 - 7.3: std::opt sweeps across both graphs ---------------------------------------------------
  yield say('结果仍是一张图——所以优化器能看到它的全部。');
  yield* active(1);
  const sweep = createRef<Line>();
  const sx = createSignal(-860);
  gw.add(<Line ref={sweep} points={() => [[sx(), -340], [sx(), 340]]} stroke={CYAN} lineWidth={4} opacity={0} zIndex={8} shadowColor={CYAN} shadowBlur={16} />);
  sweep().opacity(1);
  const fold = addNode(gw, at(150, -FY), '2.0', {kind: 'value', stroke: VIOLET, glyphSize: 26});
  const eFold = edge(gw, fold, mseB, {color: VIOLET});
  const eShared = edge(gw, x, mlpB, {color: VIOLET, bend: -40});
  yield* all(
    sx(860, 2.2, linear),
    // x is read in both halves: one node (the sweep passes it first)
    chain(
      waitFor(0.4),
      all(xB.pos(x.pos(), 0.8, easeInOutCubic), xB.root().opacity(0, 0.8), eXB().opacity(0, 0.4), chain(waitFor(0.5), stroke(eShared, 0.4))),
    ),
    // 1.0 and ×2 fold into the constant 2.0
    chain(
      waitFor(1.6),
      all(
        seed.pos(fold.pos(), 0.6, easeInOutCubic),
        times2.pos(fold.pos(), 0.6, easeInOutCubic),
        seed.root().opacity(0, 0.6),
        times2.root().opacity(0, 0.6),
        eSeed().opacity(0, 0.3),
        eTimes().opacity(0, 0.3),
        chain(waitFor(0.4), all(trace(fold, 0.3), stroke(eFold, 0.3))),
      ),
    ),
  );
  yield* sweep().opacity(0, 0.2);

  // ---- 7.3 - 9.0: std::jit, and the program runs -------------------------------------------------------
  yield* active(2);
  sx(-860);
  sweep().opacity(1);
  yield* sx(860, 0.5, linear);
  sweep().opacity(0);
  yield* beadsAlong(gw, [fEdges[0]], 0.25);
  yield* beadsAlong(gw, [fEdges[1], fEdges[2]], 0.25);
  yield* beadsAlong(gw, [fEdges[3]], 0.2);
  yield* l.shape().stroke(MINT, 0.2);
  yield* beadsAlong(gw, [eFold], 0.22, VIOLET);
  yield* beadsAlong(gw, [eMse], 0.22, VIOLET);
  yield* beadsAlong(gw, [eG], 0.2, VIOLET);
  yield* g.shape().stroke(MINT, 0.25);
  yield* waitFor(0.3);
  // hand mlp (and its weights) to shot 5
  yield* all(handOff(w, [mlp.root(), m.root()]), strip().opacity(0, 0.25));
});

