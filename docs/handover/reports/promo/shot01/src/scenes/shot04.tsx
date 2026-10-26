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
  CHALK_DIM,
  CYAN,
  MINT,
  MONO,
  VIOLET,
  addNode,
  at,
  beads,
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

  // ---- the forward graph of loss ----------------------------------------------------------------
  const FY = -160;
  const x = addNode(w, at(-600, FY), 'x', {kind: 'value'});
  const mlp = addNode(w, at(-320, FY), 'mlp', {width: 120});
  const m = addNode(w, at(-320, FY - 36 - 23), 'm', {kind: 'weight'});
  const y = addNode(w, at(-60, FY - 140), 'y', {kind: 'value'});
  const mse = addNode(w, at(-60, FY), 'mse', {width: 110});
  const l = addNode(w, at(220, FY), '', {kind: 'ring'});
  const fEdges = [edge(w, x, mlp), edge(w, mlp, mse), edge(w, y, mse), edge(w, mse, l)];
  const frame = createRef<Rect>();
  w.add(<Rect ref={frame} x={-190} y={FY - 60} width={1000} height={290} radius={28} stroke={CHALK_DIM} lineWidth={2} lineDash={[14, 10]} end={0} zIndex={0} />);
  const frameName = createRef<Txt>();
  w.add(<Txt ref={frameName} text="loss" x={-655} y={FY - 180} fontFamily={MONO} fontSize={26} fill={CHALK_DIM} opacity={0} />);

  // ---- the reflection: the reverse graph, edges reversed -------------------------------------------
  const xB = addNode(w, at(-600, -FY), 'x', {kind: 'value', stroke: VIOLET});
  const mlpB = addNode(w, at(-320, -FY), 'mlp', {width: 120, stroke: VIOLET});
  const mseB = addNode(w, at(-60, -FY), 'mse', {width: 110, stroke: VIOLET});
  const times2 = addNode(w, at(80, -FY), '×2', {width: 90, stroke: VIOLET});
  const seed = addNode(w, at(220, -FY), '1.0', {kind: 'value', stroke: VIOLET, glyphSize: 26});
  const g = addNode(w, at(-320, -FY + 36 + 70), 'g', {kind: 'ring', stroke: VIOLET});
  const mirrorNodes = [seed, times2, mseB, mlpB, xB, g];
  const eSeed = edge(w, seed, times2, {color: VIOLET});
  const eTimes = edge(w, times2, mseB, {color: VIOLET});
  const eMse = edge(w, mseB, mlpB, {color: VIOLET});
  const eXB = edge(w, xB, mlpB, {color: VIOLET});
  const eG = edge(w, mlpB, g, {color: VIOLET});
  const mEdges = [eSeed, eTimes, eMse, eXB, eG];
  const baseline = createRef<Line>();
  w.add(<Line ref={baseline} points={[[-720, BASE], [380, BASE]]} stroke={VIOLET} lineWidth={2} opacity={0.8} end={0} />);
  const tag = createRef<Txt>();
  w.add(<Txt ref={tag} text="with_grad" x={520} y={BASE} fontFamily={MONO} fontSize={30} fill={VIOLET} opacity={0} />);
  // jvp's twin: the forward graph again, violet, above it and the same way round
  const twin = createRef<Node>();
  w.add(<Node ref={twin} opacity={0} y={-26} />);
  for (const [a, b] of [[x, mlp], [mlp, mse], [mse, l]] as const) {
    twin().add(<Line points={[a.pos().add(new Vector2(a.hw + 4, 0)), b.pos().sub(new Vector2(b.hw + 10, 0))]} stroke={VIOLET} lineWidth={3} endArrow arrowSize={9} />);
  }

  // ---- 0.0 - 0.7: the forward graph, the code, the strip ------------------------------------------
  yield chain(waitFor(0.3), say('求导是对图的变换，'));
  yield* all(
    src.root().opacity(1, 0.4),
    strip().opacity(1, 0.4),
    frame().end(1, 0.6, easeInOutCubic),
    frameName().opacity(1, 0.4),
    sequence(0.05, ...[x, mlp, m, y, mse, l].map((n) => trace(n, 0.3))),
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
  yield* waitFor(0.25);
  yield* all(sequence(0.08, ...mEdges.map((e) => stroke(e, 0.3))), tag().opacity(1, 0.3));

  // ---- 3.0 - 4.0: grad, vjp, jvp, back to with_grad ---------------------------------------------------
  for (const name of ['grad', 'vjp', 'jvp']) {
    tag().text(name);
    if (name === 'jvp') {
      yield* twin().opacity(0.9, 0.12).wait(0.18).to(0, 0.12);
    } else {
      yield* waitFor(0.25);
    }
  }
  tag().text('with_grad');
  yield* waitFor(0.2);

  // ---- 4.0 - 7.3: std::opt sweeps across both graphs ---------------------------------------------------
  yield say('结果仍是一张图——所以优化器能看到它的全部。');
  yield* active(1);
  const sweep = createRef<Line>();
  const sx = createSignal(-860);
  w.add(<Line ref={sweep} points={() => [[sx(), -340], [sx(), 340]]} stroke={CYAN} lineWidth={4} opacity={0} zIndex={8} shadowColor={CYAN} shadowBlur={16} />);
  sweep().opacity(1);
  const fold = addNode(w, at(150, -FY), '2.0', {kind: 'value', stroke: VIOLET, glyphSize: 26});
  const eFold = edge(w, fold, mseB, {color: VIOLET});
  const eShared = edge(w, x, mlpB, {color: VIOLET, bend: -40});
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
  yield* beads(w, [[x.pos(), mlp.pos()]], 0.25);
  yield* beads(w, [[mlp.pos(), mse.pos()], [y.pos(), mse.pos()]], 0.25);
  yield* beads(w, [[mse.pos(), l.pos()]], 0.2);
  yield* l.shape().stroke(MINT, 0.2);
  yield* beads(w, [[fold.pos(), mseB.pos()]], 0.22, VIOLET);
  yield* beads(w, [[mseB.pos(), mlpB.pos()]], 0.22, VIOLET);
  yield* beads(w, [[mlpB.pos(), g.pos()]], 0.2, VIOLET);
  yield* g.shape().stroke(MINT, 0.25);
  yield* waitFor(0.4);
});

