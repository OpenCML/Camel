/**
 * Shot 2 of "The program is the graph" (9 s): order comes from dependency; `sync` is a thread.
 * Script: ../../../script.md (shot 2).
 *
 * What the picture claims, and why it holds (design principles 3.1, 3.4):
 * - Without `sync`, the statement `println(cosine(a, b))` and the write are effects whose results
 *   nothing uses, so they do not run, and nothing needs `cosine` or the two `encode`s either:
 *   only `return 0` runs (EffectNotExecuted).
 * - With `sync`, the effect statements are linked in written order (the thread): println, then
 *   write_text, then the block's value. The pure `let`s are not on the chain.
 * - `println` needs `cosine`, which needs `a` and `b`; `a` and `b` do not depend on each other,
 *   so they may run at the same time: the two beads leave `x` in the same frame.
 *
 * Layout: the computation across the top, the two effects down the middle (the path of the write
 * docked between them), the text written to the left of the write, the result on the right.
 *
 * Timeline (seconds)
 *   0.0 - 0.9   code without `sync`; the graph is traced, dim except the result
 *   0.9 - 2.4   only `return 0` runs; EffectNotExecuted appears by the effects
 *   2.6 - 3.3   ` sync` is typed into the header
 *   3.3 - 4.4   the thread is stitched through the effects: println -> write_text -> result
 *   4.4 - 5.1   two beads leave x together: the two encodes run side by side
 *   5.1 - 5.8   they meet at cosine
 *   5.8 - 7.6   println runs, then write_text, in written order; the thread reaches the result
 *   7.6 - 9.0   hold
 */
import {Line, Node, makeScene2D} from '@motion-canvas/2d';
import {Vector2, all, chain, createRef, easeInOutCubic, linear, sequence, waitFor} from '@motion-canvas/core';
import {
  CHALK,
  MINT,
  SAND,
  THREAD,
  addNode,
  arrive,
  at,
  beadsAlong,
  ch,
  code,
  dim,
  dm,
  edge,
  flash,
  ground,
  handOff,
  kw,
  label,
  nm,
  showValue,
  st,
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

  // ---- the code ------------------------------------------------------------------------------
  const src = code(w, -900, -200, [
    [kw('func'), ch(' main(): '), kw('int'), kw(' sync', true), ch(' {')],
    [ch('    '), kw('let'), ch(' a = x'), dm('->'), ch('encode<wa>')],
    [ch('    '), kw('let'), ch(' b = x'), dm('->'), ch('encode<wb>')],
    [ch('    '), ch('println(cosine(a, b))')],
    [ch('    '), st("'done'"), dm('->'), ch('write_text<'), st("'run.log'"), ch('>')],
    [ch('    '), kw('return'), ch(' '), nm('0')],
    [ch('}')],
  ]);

  // ---- the graph -----------------------------------------------------------------------------
  const x = addNode(w, at(60, -290), 'x', {kind: 'value'});
  const encA = addNode(w, at(320, -400), 'encode');
  const encB = addNode(w, at(320, -180), 'encode');
  const wa = addNode(w, at(320, -400 - 36 - 20), 'wa', {kind: 'weight'});
  const wb = addNode(w, at(320, -180 - 36 - 20), 'wb', {kind: 'weight'});
  const cos = addNode(w, at(590, -290), 'cosine');
  const pr = addNode(w, at(590, -40), 'println', {kind: 'effect'});
  const wt = addNode(w, at(590, 200), 'write_text', {kind: 'effect'});
  const path = addNode(w, at(590, 200 - 36 - 20), "'run.log'", {kind: 'weight'});
  const done = addNode(w, at(300, 200), "'done'", {kind: 'value'});
  const zero = addNode(w, at(830, -40), '0', {kind: 'value'});
  const result = addNode(w, at(830, 200), '', {kind: 'ring'});

  const eXA = edge(w, x, encA);
  const eXB = edge(w, x, encB);
  const eAC = edge(w, encA, cos);
  const eBC = edge(w, encB, cos);
  const eCP = edge(w, cos, pr);
  const eDW = edge(w, done, wt);
  const eZR = edge(w, zero, result);
  const dataEdges = [eXA, eXB, eAC, eBC, eCP, eDW, eZR];

  // The thread: dashed, through the notches of the effects in written order, then to the result.
  const notchOf = (n: typeof pr) => n.pos().add(new Vector2(n.hw - 6, n.hh - 6));
  const thread = createRef<Line>();
  w.add(
    <Line
      ref={thread}
      points={() => {
        const a = notchOf(pr);
        const b = notchOf(wt);
        const c = result.pos().add(new Vector2(-36, 6));
        return [a, a.add(b).scale(0.5).add(new Vector2(70, 0)), b, b.add(c).scale(0.5).add(new Vector2(0, 40)), c];
      }}
      radius={70}
      stroke={THREAD}
      lineWidth={4}
      lineDash={[12, 9]}
      lineCap="round"
      end={0}
      opacity={0}
      zIndex={4}
    />,
  );
  yield (function* drift() {
    while (true) {
      yield* thread().lineDashOffset(thread().lineDashOffset() - 21, 0.6, linear);
    }
  })();

  const unrun = label(w, 300, -40, 'EffectNotExecuted');

  // ---- 0.0 - 0.9: the code and the graph, everything dim but the result ------------------------
  const computeNodes = [x, encA, encB, wa, wb, cos, pr, wt, path, done];
  yield chain(waitFor(0.3), say('效果在被需要时才执行；'));
  yield* all(
    src.root().opacity(1, 0.4),
    arrive(x, new Vector2(10.3, 0), 1.03),
    sequence(0.04, ...[...computeNodes.filter((n) => n !== x), zero, result].map((n) => trace(n, 0.3))),
    sequence(0.05, ...dataEdges.map((e) => stroke(e, 0.25))),
  );
  yield* all(...computeNodes.map((n) => dim(n, true, 0.3)), ...[eXA, eXB, eAC, eBC, eCP, eDW].map((e) => e().opacity(0.35, 0.3)));

  // ---- 0.9 - 2.4: only `return 0` runs ----------------------------------------------------------
  yield* beadsAlong(w, [eZR], 0.5);
  yield* all(result.shape().stroke(MINT, 0.3), showValue(result, '0'));
  yield* unrun().opacity(1, 0.3);
  yield* waitFor(0.7);

  // ---- 2.6 - 3.3: sync ------------------------------------------------------------------------
  yield say('sync 把它们按顺序串起来——');
  yield* all(typeLine(src, 0, 12), unrun().opacity(0, 0.4));

  // ---- 3.3 - 4.4: the thread through the effects ----------------------------------------------
  thread().opacity(1);
  yield* all(
    thread().end(1, 1.0, easeInOutCubic),
    dim(pr, false, 0.4),
    dim(wt, false, 0.4),
    dim(path, false, 0.4),
    dim(done, false, 0.4),
    eDW().opacity(1, 0.4),
    eCP().opacity(1, 0.4),
  );

  // ---- 4.4 - 5.1: the two encodes, side by side -----------------------------------------------
  yield say('不依赖，就不等待。');
  yield* all(dim(x, false, 0.2), dim(encA, false, 0.2), dim(encB, false, 0.2), dim(wa, false, 0.2), dim(wb, false, 0.2), eXA().opacity(1, 0.2), eXB().opacity(1, 0.2));
  yield* beadsAlong(w, [eXA, eXB], 0.6);
  yield* all(flash(encA), flash(encB));

  // ---- 5.1 - 5.8: they meet at cosine ---------------------------------------------------------
  yield* all(dim(cos, false, 0.2), eAC().opacity(1, 0.2), eBC().opacity(1, 0.2));
  yield* beadsAlong(w, [eAC, eBC], 0.5);
  yield* flash(cos);

  // ---- 5.8 - 7.6: the effects, in written order -----------------------------------------------
  yield* beadsAlong(w, [eCP], 0.45);
  yield* all(flash(pr), pr.shape().stroke(SAND, 0.15).to(CHALK, 0.4));
  yield* beadsAlong(w, [eDW], 0.45);
  yield* flash(wt);
  yield* result.shape().stroke(SAND, 0.15).to(MINT, 0.3);

  // ---- hold, then hand x to shot 3 ---------------------------------------------------------------
  yield* waitFor(0.3);
  yield* handOff(w, [x.root()]);
  yield* waitFor(0.1);
});
