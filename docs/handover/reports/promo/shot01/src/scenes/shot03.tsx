/**
 * Shot 3 of "The program is the graph" (8 s): a layer is a function with its weights bound.
 * Script: ../../../script.md (shot 3).
 *
 * What the picture claims, and why it holds (design principles 4.4; stdlib conventions 4):
 * `linear<w, b>` is the layer: the weights are its `with` arguments, what the function is
 * configured with, so they dock on top of the node and are not data edges; the data flows through
 * the pipe. The shapes under the edges are the array types the signature states.
 *
 * Timeline (seconds)
 *   0.0 - 0.6   the head of the function is written; `x` is traced
 *   0.6 - 4.2   each `->` stage is typed and its operator traced, left to right; the weights
 *               descend and dock on the two `linear`s
 *   4.2 - 6.4   a bead runs the pipeline; the shapes are written under the edges as it passes
 *   6.6 - 8.0   the pipeline folds into the frame of `mlp`
 */
import {Node, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {Vector2, all, chain, createRef, easeInOutCubic, waitFor} from '@motion-canvas/core';
import {
  CHALK_DIM,
  MINT,
  MONO,
  addNode,
  at,
  beadsAlong,
  ch,
  code,
  dm,
  edge,
  ground,
  kw,
  label,
  stroke,
  subtitles,
  trace,
  typeLine,
  blank,
} from '../lib/plotter';

const SHOW_SUBTITLE = true;

export default makeScene2D(function* (view) {
  ground(view);
  const world = createRef<Node>();
  view.add(<Node ref={world} />);
  const w = world();
  const say = subtitles(view, SHOW_SUBTITLE);

  // ---- the code, at the top left ----------------------------------------------------------------
  const src = code(w, -860, -430, [
    [kw('with'), ch(' <m: Mlp>')],
    [kw('func'), ch(' mlp(x: f32[b, 784]): f32[b, 10] {')],
    [ch('    '), ch('x'), dm('->'), ch('linear<m.w1, m.b1>')],
    [ch('        '), dm('->'), ch('relu')],
    [ch('        '), dm('->'), ch('linear<m.w2, m.b2>')],
    [ch('}')],
  ], 38);
  for (const line of [2, 3, 4, 5]) {
    blank(src, line);
  }

  // ---- the pipeline, full width -------------------------------------------------------------------
  const Y = 170;
  const x = addNode(w, at(-720, Y), 'x', {kind: 'value'});
  const l1 = addNode(w, at(-360, Y), 'linear');
  const relu = addNode(w, at(0, Y), 'relu');
  const l2 = addNode(w, at(360, Y), 'linear');
  const out = addNode(w, at(720, Y), '', {kind: 'ring'});
  const dock = Y - 36 - 23;
  const w1 = addNode(w, at(-395, dock - 260), 'w1', {kind: 'weight'});
  const b1 = addNode(w, at(-325, dock - 260), 'b1', {kind: 'weight'});
  const w2 = addNode(w, at(325, dock - 260), 'w2', {kind: 'weight'});
  const b2 = addNode(w, at(395, dock - 260), 'b2', {kind: 'weight'});
  const e1 = edge(w, x, l1);
  const e2 = edge(w, l1, relu);
  const e3 = edge(w, relu, l2);
  const e4 = edge(w, l2, out);
  const shapes = [
    label(w, -540, Y + 62, 'f32[b, 784]'),
    label(w, -180, Y + 62, 'f32[b, 256]'),
    label(w, 180, Y + 62, 'f32[b, 256]'),
    label(w, 540, Y + 62, 'f32[b, 10]'),
  ];

  // ---- 0.0 - 0.6: the head; x -------------------------------------------------------------------
  yield chain(waitFor(0.3), say('一层网络，就是绑定了权重的函数。'));
  yield* all(src.root().opacity(1, 0.4), trace(x, 0.4));

  // ---- 0.6 - 4.2: each stage is written and traced; the weights dock --------------------------------
  const dockAt = (n: typeof w1, y: number) => n.pos(new Vector2(n.pos().x, y), 0.5, easeInOutCubic);
  yield* typeLine(src, 2, 40);
  yield* all(trace(l1), stroke(e1), trace(w1, 0.2), trace(b1, 0.2));
  yield* all(dockAt(w1, dock), dockAt(b1, dock));
  yield* typeLine(src, 3, 40);
  yield* all(trace(relu), stroke(e2));
  yield* typeLine(src, 4, 40);
  yield* all(trace(l2), stroke(e3), trace(w2, 0.2), trace(b2, 0.2));
  yield* all(dockAt(w2, dock), dockAt(b2, dock));
  yield* typeLine(src, 5, 40);
  yield* all(trace(out), stroke(e4));

  // ---- 4.2 - 6.4: a bead runs the pipeline -------------------------------------------------------------
  yield say('模型读起来，就是它本来的那条管道。');
  const stages = [x, l1, relu, l2, out];
  for (let i = 0; i < 4; i++) {
    yield shapes[i]().opacity(1, 0.3);
    yield* beadsAlong(w, [[e1, e2, e3, e4][i]], 0.45);
  }
  yield* out.shape().stroke(MINT, 0.3);

  // ---- 6.6 - 8.0: the pipeline folds into the frame of `mlp` ---------------------------------------------
  yield* waitFor(0.2);
  const frame = createRef<Rect>();
  const name = createRef<Txt>();
  w.add(<Rect ref={frame} x={0} y={150} width={980} height={250} radius={28} stroke={CHALK_DIM} lineWidth={2} lineDash={[14, 10]} end={0} zIndex={0} />);
  w.add(<Txt ref={name} text="mlp" x={-455} y={55} fontFamily={MONO} fontSize={26} fill={CHALK_DIM} opacity={0} />);
  const xs = [-370, -185, 0, 185, 370];
  const FY = 170;
  const weights: [typeof w1, number][] = [[w1, -220], [b1, -150], [w2, 150], [b2, 220]];
  yield* all(
    ...stages.map((n, i) => n.pos(new Vector2(xs[i], FY), 1.0, easeInOutCubic)),
    ...weights.map(([n, wx]) => n.pos(new Vector2(wx, FY - 36 - 23), 1.0, easeInOutCubic)),
    ...shapes.map((s) => s().opacity(0, 0.4)),
    chain(waitFor(0.4), all(frame().end(1, 0.6, easeInOutCubic), name().opacity(1, 0.4))),
  );
  yield* waitFor(0.2);
});
