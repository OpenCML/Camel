/**
 * Shot 5 of "The program is the graph" (8 s): say where; ship anywhere.
 * Script: ../../../script.md (shot 5).
 *
 * What the picture claims, and why it holds (design principles 13.1, 13.3, 9.8, 10.6):
 * - `#gpu` on a function declares where its body runs; `tiles::compile` reads it, and the compiler
 *   derives the transfers: an upload where a value enters the region, a download where one leaves.
 *   No line of the code writes them, so no line lights up when they appear. The device itself is
 *   chosen at run time (`--devices 0`).
 * - The same program is exported by a pass: ONNX (`onnx::export`) and standalone C++ (`std::cpp`),
 *   with the same meaning, so each card holds the same drawing.
 *
 * Timeline (seconds)
 *   0.0 - 0.8   the code and the graph of `forward`; the strip with the device run
 *   0.8 - 1.4   `#gpu` is typed above the function
 *   1.4 - 2.4   a rose region is traced around the body; ↑ and ↓ appear where edges cross it
 *   2.4 - 4.0   a value runs: slower outside, twice as fast inside the region
 *   4.2 - 5.4   the graph folds into a bundle
 *   5.4 - 7.0   it fans out into two cards, model.onnx and model.cpp, the same drawing on each;
 *               one mint tick spans both; the strip shows the two export lines
 *   7.0 - 8.0   hold
 */
import {Line, Node, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {Vector2, all, chain, createRef, easeInOutCubic, sequence, waitFor} from '@motion-canvas/core';
import {
  CHALK,
  CHALK_DIM,
  CYAN,
  GROUND_MID,
  MINT,
  MONO,
  ROSE,
  addNode,
  at,
  beadsAlong,
  ch,
  code,
  dm,
  edge,
  ground,
  kw,
  stroke,
  subtitles,
  trace,
  typeLine,
} from '../lib/plotter';

const SHOW_SUBTITLE = true;

/** The graph of `forward` in miniature, drawn statically: what a card holds. */
function miniGraph(parent: Node, scale: number) {
  const n = (x: number, w: number, round: boolean, text: string) => (
    <Rect x={x * scale} width={w * scale} height={72 * scale} radius={round ? 36 * scale : 18 * scale} stroke={CHALK} lineWidth={3} fill={GROUND_MID}>
      <Txt text={text} fontFamily={MONO} fontSize={28 * scale} fill={CHALK} />
    </Rect>
  );
  const e = (a: number, b: number) => (
    <Line points={[[a * scale, 0], [b * scale, 0]]} stroke={CHALK} lineWidth={3} endArrow arrowSize={7} />
  );
  parent.add(
    <Node>
      {e(-270, -150)}
      {e(-50, 40)}
      {e(200, 300)}
      {n(-306, 72, true, 'x')}
      {n(-100, 100, false, 'mlp')}
      {n(120, 160, false, 'softmax')}
      {n(336, 72, true, '')}
    </Node>,
  );
}

export default makeScene2D(function* (view) {
  ground(view);
  const world = createRef<Node>();
  view.add(<Node ref={world} />);
  const w = world();
  const say = subtitles(view, SHOW_SUBTITLE);

  // ---- the code --------------------------------------------------------------------------------
  const src = code(w, -890, -470, [
    [kw('#gpu', true)],
    [kw('with'), ch(' <m: Mlp>')],
    [kw('func'), ch(' forward(x: f32[b, 784]): f32[b, 10] {')],
    [ch('    '), ch('x'), dm('->'), ch('mlp<m>'), dm('->'), ch('softmax')],
    [ch('}')],
  ], 38);

  // ---- the graph of forward ------------------------------------------------------------------------
  const graph = createRef<Node>();
  w.add(<Node ref={graph} y={60} />);
  const g = graph();
  const Y = 0;
  const x = addNode(g, at(-560, Y), 'x', {kind: 'value'});
  const mlp = addNode(g, at(-220, Y), 'mlp');
  const m = addNode(g, at(-220, Y - 36 - 20), 'm', {kind: 'weight'});
  const sm = addNode(g, at(120, Y), 'softmax');
  const out = addNode(g, at(460, Y), '', {kind: 'ring'});
  const e1 = edge(g, x, mlp);
  const e2 = edge(g, mlp, sm);
  const e3 = edge(g, sm, out);
  // the region of the body, and the transfers the compiler derives where edges cross it
  const region = createRef<Rect>();
  g.add(<Rect ref={region} x={-50} y={Y - 12} width={560} height={210} radius={28} stroke={ROSE} lineWidth={3} fill={ROSE + '1A'} end={0} opacity={0} zIndex={0} />);
  const regionName = createRef<Txt>();
  g.add(<Txt ref={regionName} text="gpu" x={-300} y={Y - 92} fontFamily={MONO} fontSize={24} fill={ROSE} opacity={0} />);
  const up = createRef<Txt>();
  const down = createRef<Txt>();
  g.add(<Txt ref={up} text="↑ upload" x={-410} y={Y + 44} fontFamily={MONO} fontSize={22} fill={CHALK_DIM} opacity={0} />);
  // the weights enter the region from outside too
  const upM = createRef<Txt>();
  g.add(<Txt ref={upM} text="↑" x={-220} y={Y - 130} fontFamily={MONO} fontSize={22} fill={CHALK_DIM} opacity={0} />);
  g.add(<Txt ref={down} text="↓ download" x={310} y={Y + 44} fontFamily={MONO} fontSize={22} fill={CHALK_DIM} opacity={0} />);

  // ---- the strip ------------------------------------------------------------------------------------
  const strip = createRef<Txt>();
  view.add(<Txt ref={strip} text="camel --devices 0 model.cml std::macro tiles::compile std::jit" y={380} fontFamily={MONO} fontSize={28} fill={CHALK_DIM} opacity={0} />);
  const strip2 = createRef<Txt>();
  view.add(<Txt ref={strip2} text={'camel model.cml std::macro std::opt onnx::export out=model.onnx\ncamel model.cml std::macro std::opt std::cpp'} y={372} fontFamily={MONO} fontSize={26} fill={CHALK_DIM} lineHeight={38} textAlign="center" opacity={0} />);

  // ---- 0.0 - 0.8: the code and the graph ---------------------------------------------------------
  yield chain(waitFor(0.3), say('说明在哪里运行，数据搬运由编译器推出来。'));
  yield* all(
    src.root().opacity(1, 0.4),
    strip().opacity(1, 0.4),
    sequence(0.06, ...[x, m, mlp, sm, out].map((n) => trace(n, 0.3))),
    sequence(0.08, ...[e1, e2, e3].map((e) => stroke(e))),
  );

  // ---- 0.8 - 1.4: #gpu ------------------------------------------------------------------------------
  yield* typeLine(src, 0, 14);

  // ---- 1.4 - 2.4: the region, and the transfers the compiler derives ---------------------------------
  region().opacity(1);
  yield* all(region().end(1, 0.7, easeInOutCubic), regionName().opacity(1, 0.4));
  yield* all(up().opacity(1, 0.3), upM().opacity(1, 0.3), down().opacity(1, 0.3));

  // ---- 2.4 - 4.0: a value runs, twice as fast inside --------------------------------------------------
  yield* beadsAlong(g, [e1], 0.6);
  yield* beadsAlong(g, [e2], 0.3);
  yield* beadsAlong(g, [e3], 0.6);
  yield* out.shape().stroke(MINT, 0.3);
  yield* waitFor(0.2);

  // ---- 4.2 - 5.4: the graph folds into a bundle --------------------------------------------------------
  yield say('或者，把同一张图导出成 ONNX 或 C++。');
  yield* all(
    region().opacity(0, 0.4),
    regionName().opacity(0, 0.4),
    up().opacity(0, 0.4),
    upM().opacity(0, 0.4),
    down().opacity(0, 0.4),
    g.scale(0.25, 0.9, easeInOutCubic),
    g.position(new Vector2(0, 60), 0.9, easeInOutCubic),
    strip().opacity(0, 0.4),
  );

  // ---- 5.4 - 7.0: two cards, the same drawing; one tick for both ---------------------------------------
  const cards: [string, number][] = [['model.onnx', -420], ['model.cpp', 420]];
  const cardRefs = cards.map(() => createRef<Node>());
  cards.forEach(([name], i) => {
    w.add(
      <Node ref={cardRefs[i]} x={0} y={60} opacity={0} scale={0.3}>
        <Rect width={720} height={260} radius={24} fill={GROUND_MID} stroke={CHALK_DIM} lineWidth={2} />
        <Txt text={name} x={-290} y={-100} offsetX={-1} fontFamily={MONO} fontSize={26} fill={CYAN} />
      </Node>,
    );
    miniGraph(cardRefs[i](), 0.85);
  });
  yield* all(
    g.opacity(0, 0.4),
    ...cardRefs.map((c, i) => all(c().opacity(1, 0.3), c().scale(1, 0.9, easeInOutCubic), c().position(new Vector2(cards[i][1], 20), 0.9, easeInOutCubic))),
    chain(waitFor(0.4), strip2().opacity(1, 0.4)),
  );
  const tick = createRef<Line>();
  const tickMark = createRef<Txt>();
  w.add(<Line ref={tick} points={[[-780, 200], [780, 200]]} stroke={MINT} lineWidth={3} end={0} />);
  w.add(<Txt ref={tickMark} text="✓ same meaning" y={236} fontFamily={MONO} fontSize={26} fill={MINT} opacity={0} />);
  yield* all(tick().end(1, 0.6, easeInOutCubic), chain(waitFor(0.3), tickMark().opacity(1, 0.3)));

  // ---- hold ---------------------------------------------------------------------------------------------
  yield* waitFor(1.0);
});
