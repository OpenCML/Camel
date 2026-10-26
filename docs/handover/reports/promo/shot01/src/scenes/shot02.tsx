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
 * Timeline (seconds)
 *   0.0 - 0.9   code without `sync`; the graph is traced, dim except the result
 *   0.9 - 2.6   only `return 0` runs; EffectNotExecuted appears by the effects
 *   3.2 - 3.9   ` sync` is typed into the header
 *   3.9 - 5.0   the thread is stitched through the effects: println -> write_text -> result
 *   5.0 - 5.7   two beads leave x together: the two encodes run side by side
 *   5.7 - 6.4   they meet at cosine
 *   6.4 - 7.0   println runs
 *   7.0 - 7.7   then write_text, in written order
 *   7.7 - 9.0   the result; hold
 */
import {Line, Node, makeScene2D} from '@motion-canvas/2d';
import {Vector2, all, chain, createRef, easeInOutCubic, linear, sequence, waitFor} from '@motion-canvas/core';
import {
  CHALK,
  MINT,
  SAND,
  THREAD,
  addNode,
  at,
  beads,
  ch,
  code,
  dim,
  dm,
  edge,
  flash,
  ground,
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
  const src = code(w, -890, -200, [
    [kw('func'), ch(' main(): '), kw('int'), kw(' sync', true), ch(' {')],
    [ch('    '), kw('let'), ch(' a = x'), dm('->'), ch('encode<wa>')],
    [ch('    '), kw('let'), ch(' b = x'), dm('->'), ch('encode<wb>')],
    [ch('    '), ch('println(cosine(a, b))')],
    [ch('    '), st("'done'"), dm('->'), ch('write_text<'), st("'run.log'"), ch('>')],
    [ch('    '), kw('return'), ch(' '), nm('0')],
    [ch('}')],
  ]);

  // ---- the graph -----------------------------------------------------------------------------
  const x = addNode(w, at(80, -70), 'x', {kind: 'value'});
  const encA = addNode(w, at(320, -210), 'encode', {width: 150});
  const encB = addNode(w, at(320, 70), 'encode', {width: 150});
  const wa = addNode(w, at(320, -210 - 36 - 23), 'wa', {kind: 'weight'});
  const wb = addNode(w, at(320, 70 - 36 - 23), 'wb', {kind: 'weight'});
  const cos = addNode(w, at(560, -70), 'cosine', {width: 150});
  const pr = addNode(w, at(470, 230), 'println', {kind: 'effect', width: 150});
  const wt = addNode(w, at(700, 230), 'write_text', {kind: 'effect', width: 190});
  const path = addNode(w, at(700, 230 - 36 - 23), "'run.log'", {kind: 'weight', width: 120});
  const done = addNode(w, at(700, 350), "'done'", {kind: 'value', width: 110});
  const zero = addNode(w, at(850, 350), '0', {kind: 'value'});
  const result = addNode(w, at(850, 230), '', {kind: 'ring'});

  const eXA = edge(w, x, encA);
  const eXB = edge(w, x, encB);
  const eAC = edge(w, encA, cos);
  const eBC = edge(w, encB, cos);
  const eCP = edge(w, cos, pr);
  const eDW = edge(w, done, wt);
  const eZR = edge(w, zero, result);
  const dataEdges = [eXA, eXB, eAC, eBC, eCP, eDW, eZR];

  // The thread: dashed, sagging under the effects, through their notches in written order.
  const notchOf = (n: typeof pr) => () => n.pos().add(new Vector2(n.hw - 6, n.hh - 6));
  const thread = createRef<Line>();
  w.add(
    <Line
      ref={thread}
      points={() => {
        const a = notchOf(pr)();
        const b = notchOf(wt)();
        const c = result.pos().add(new Vector2(-26, 26));
        return [a, a.add(b).scale(0.5).add(new Vector2(0, 30)), b, b.add(c).scale(0.5).add(new Vector2(0, 24)), c];
      }}
      radius={60}
      stroke={THREAD}
      lineWidth={4}
      lineDash={[12, 9]}
      lineCap="round"
      end={0}
      opacity={0}
      zIndex={4}
    />,
  );
  yield loop();
  function* loop() {
    while (true) {
      yield* thread().lineDashOffset(thread().lineDashOffset() - 21, 0.6, linear);
    }
  }

  const unrun = label(w, 470, 300, 'EffectNotExecuted');

  // ---- 0.0 - 0.9: the code and the graph, everything dim but the result ------------------------
  const computeNodes = [x, encA, encB, wa, wb, cos, pr, wt, path, done];
  yield chain(waitFor(0.3), say('效果在被需要时才执行；'));
  yield* all(
    src.root().opacity(1, 0.4),
    sequence(0.04, ...[...computeNodes, zero, result].map((n) => trace(n, 0.3))),
    sequence(0.05, ...dataEdges.map((e) => stroke(e, 0.25))),
  );
  yield* all(...computeNodes.map((n) => dim(n, true, 0.3)), ...[eXA, eXB, eAC, eBC, eCP, eDW].map((e) => e().opacity(0.35, 0.3)));

  // ---- 0.9 - 2.6: only `return 0` runs ----------------------------------------------------------
  yield* beads(w, [[zero.pos(), result.pos()]], 0.5);
  yield* all(result.shape().stroke(MINT, 0.3), showValue(result, '0'));
  yield* unrun().opacity(1, 0.3);
  yield* waitFor(0.6);

  // ---- 3.2 - 3.9: sync ------------------------------------------------------------------------
  yield* waitFor(0.2);
  yield say('sync 把它们按顺序串起来——');
  yield* all(typeLine(src, 0, 12), unrun().opacity(0, 0.4));

  // ---- 3.9 - 5.0: the thread through the effects ----------------------------------------------
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

  // ---- 5.0 - 5.7: the two encodes, side by side -----------------------------------------------
  yield say('不依赖，就不等待。');
  yield* all(dim(x, false, 0.2), dim(encA, false, 0.2), dim(encB, false, 0.2), dim(wa, false, 0.2), dim(wb, false, 0.2), eXA().opacity(1, 0.2), eXB().opacity(1, 0.2));
  yield* beads(w, [[x.pos(), encA.pos()], [x.pos(), encB.pos()]], 0.6);
  yield* all(flash(encA), flash(encB));

  // ---- 5.7 - 6.4: they meet at cosine ---------------------------------------------------------
  yield* all(dim(cos, false, 0.2), eAC().opacity(1, 0.2), eBC().opacity(1, 0.2));
  yield* beads(w, [[encA.pos(), cos.pos()], [encB.pos(), cos.pos()]], 0.5);
  yield* flash(cos);

  // ---- 6.4 - 7.7: the effects, in written order -----------------------------------------------
  yield* beads(w, [[cos.pos(), pr.pos()]], 0.45);
  yield* all(flash(pr), pr.shape().stroke(SAND, 0.15).to(CHALK, 0.4));
  yield* beads(w, [[done.pos(), wt.pos()]], 0.45);
  yield* flash(wt);
  yield* result.shape().stroke(SAND, 0.15).to(MINT, 0.3);

  // ---- hold ------------------------------------------------------------------------------------
  yield* waitFor(0.6);
});
