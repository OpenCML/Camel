// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * Shot 9 of "The program is the graph" (5 s): the seal.
 * Script: ../../../script.md (shot 9).
 *
 * What the picture claims, and why it holds (design principles 3.3):
 * - The resources a function reaches are part of its type. `pure` promises the empty set, and the
 *   compiler checks the body against it: `println` reaches stdout, so the promise is broken and
 *   the program is refused with UsesPromiseViolated.
 *
 * Timeline (seconds)
 *   0.0 - 0.8   the code and the graph of normalize, inside its frame
 *   0.8 - 1.5   a mint seal is drawn closed around the frame, with a `pure` tab
 *   1.5 - 2.3   `->println` is typed; its node is traced between `/` and the result
 *   2.3 - 3.1   a line labelled stdout runs from it outward and pierces the seal; the seal tears a
 *               coral notch there; UsesPromiseViolated is stamped
 *   3.1 - 5.0   hold
 */
import {Line, Node, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {Vector2, all, chain, createRef, easeInOutCubic, easeOutCubic, sequence, waitFor} from '@motion-canvas/core';
import {
  CHALK_DIM,
  CORAL,
  MINT,
  MONO,
  addNode,
  at,
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
    [kw('func'), ch(' normalize(x: f32[n]): f32[n] '), kw('pure'), ch(' {')],
    [ch('    '), ch('(x / sum(x))'), dm('->', true), ch('println', true)],
    [ch('}')],
  ], 40);

  // ---- the graph of normalize, in its frame --------------------------------------------------------
  const Y = 60;
  const x = addNode(w, at(-460, Y), 'x', {kind: 'value'});
  const sum = addNode(w, at(-250, Y + 110), 'sum');
  const div = addNode(w, at(-40, Y), '/');
  const pr = addNode(w, at(200, Y), 'println', {kind: 'effect'});
  const out = addNode(w, at(420, Y), '', {kind: 'ring'});
  const eXD = edge(w, x, div);
  const eXS = edge(w, x, sum);
  const eSD = edge(w, sum, div);
  const eDO = edge(w, div, out);
  const eDP = edge(w, div, pr);
  const ePO = edge(w, pr, out);

  const CX = -20;
  const CY = Y + 50;
  const frame = createRef<Rect>();
  // drawn first as shot 8 left the fault's fence (the match cut), then widened into the frame
  w.add(<Rect ref={frame} x={0} y={45} width={229} height={88} radius={24} stroke={CORAL} lineWidth={2} zIndex={0} />);
  const frameName = label(w, CX - 500 + 90, CY - 150 + 28, 'normalize', CHALK_DIM, 26);
  // the seal: drawn closed around the frame, with its tab
  const SEAL_W = 1060;
  const SEAL_H = 360;
  const sealTop = CY - SEAL_H / 2;
  const seal = createRef<Rect>();
  w.add(<Rect ref={seal} x={CX} y={CY} width={SEAL_W} height={SEAL_H} radius={34} stroke={MINT} lineWidth={4} end={0} zIndex={1} />);
  const tab = createRef<Node>();
  w.add(
    <Node ref={tab} x={CX + SEAL_W / 2 - 110} y={sealTop} opacity={0} zIndex={2}>
      <Rect width={110} height={40} radius={10} fill={MINT} />
      <Txt text="pure" fontFamily={MONO} fontSize={24} fill="#242946" />
    </Node>,
  );

  // the stdout line and the tear
  const pierce = new Vector2(200, sealTop);
  const stdoutLine = createRef<Line>();
  w.add(<Line ref={stdoutLine} points={[pr.pos().sub(new Vector2(0, pr.hh + 4)), pierce.sub(new Vector2(0, 120))]} stroke={CHALK_DIM} lineWidth={3} endArrow arrowSize={10} end={0} zIndex={4} />);
  const stdoutName = label(w, 200, sealTop - 150, 'stdout', CHALK_DIM, 26);
  const tear = createRef<Line>();
  w.add(<Line ref={tear} points={[pierce.add(new Vector2(-34, 0)), pierce.add(new Vector2(-14, 14)), pierce.add(new Vector2(0, -2)), pierce.add(new Vector2(14, 14)), pierce.add(new Vector2(34, 0))]} stroke={CORAL} lineWidth={5} lineCap="round" lineJoin="round" end={0} opacity={0} zIndex={5} />);
  const notchBg = createRef<Rect>();
  w.add(<Rect ref={notchBg} position={pierce} width={64} height={10} fill="#283056" opacity={0} zIndex={4} />);
  const verdict = label(w, 520, sealTop - 60, 'UsesPromiseViolated', CORAL, 28);

  // ---- 0.0 - 0.8: the code and the graph -----------------------------------------------------------
  yield chain(waitFor(0.3), say('承诺会被检查：'));
  for (const n of [pr]) n.root().opacity(0);
  for (const e of [eDP, ePO]) e().opacity(0);
  yield* all(
    src.root().opacity(1, 0.4),
    sequence(0.06, ...[x, sum, div, out].map((n) => trace(n, 0.3))),
    sequence(0.06, ...[eXD, eXS, eSD, eDO].map((e) => stroke(e, 0.25))),
    chain(
      all(
        frame().position(new Vector2(CX, CY), 0.7, easeInOutCubic),
        frame().size(new Vector2(1000, 300), 0.7, easeInOutCubic),
        frame().radius(26, 0.7, easeInOutCubic),
        frame().stroke(CHALK_DIM, 0.7),
      ),
      function* () {
        frame().lineDash([14, 10]);
      }(),
    ),
    frameName().opacity(1, 0.4),
  );

  // ---- 0.8 - 1.5: the seal -------------------------------------------------------------------------
  yield* all(seal().end(1, 0.6, easeInOutCubic), chain(waitFor(0.4), tab().opacity(1, 0.2)));

  // ---- 1.5 - 2.3: ->println -------------------------------------------------------------------------
  yield say('写下 pure，就得做到。');
  yield* typeLine(src, 1, 30);
  pr.root().opacity(1);
  yield* all(eDO().opacity(0, 0.2), trace(pr, 0.3), chain(waitFor(0.1), all(stroke(eDP, 0.2), stroke(ePO, 0.2))));

  // ---- 2.3 - 3.1: stdout pierces the seal; the seal tears ----------------------------------------------
  yield* stdoutLine().end(1, 0.45, easeOutCubic);
  tear().opacity(1);
  yield* all(
    notchBg().opacity(1, 0.1),
    tear().end(1, 0.2),
    seal().stroke(CORAL, 0.1).to(MINT, 0.5),
    stdoutName().opacity(1, 0.2),
    verdict().opacity(1, 0.2),
  );

  // ---- hold -------------------------------------------------------------------------------------------
  yield* waitFor(1.2);
});
