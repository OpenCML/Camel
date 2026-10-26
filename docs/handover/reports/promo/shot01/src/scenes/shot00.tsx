/**
 * Shot 0 of "The program is the graph" (6 s): recovered.
 * Script: ../../../script.md (shot 0).
 *
 * What the picture claims: a graph recovered from running code (a trace) holds only the path that
 * run took. The arm of a branch the run did not take is missing, and an effect whose result
 * nothing used (a print) leaves no node. No language is shown: the bars stand for anyone's code.
 * The one shot without the brand's colour: chalk-dim only.
 *
 * Timeline (seconds)
 *   0.0 - 0.6   ten unreadable bars
 *   0.6 - 3.6   a scan line runs down them; a node is traced only where the run went
 *               bar 3 branches on data: one arm is drawn, the other ends in a torn stroke
 *               bar 7 prints: it flickers, and leaves no node; its edge is torn too
 *   3.6 - 4.4   the recovered graph hangs, torn in two places
 *   4.4 - 5.6   it greys and crumples to a point, the caret of shot 1
 */
import {Line, Node, Rect, makeScene2D} from '@motion-canvas/2d';
import {Vector2, all, chain, createRef, createSignal, easeInCubic, easeInOutCubic, linear, sequence, waitFor} from '@motion-canvas/core';
import {CHALK_DIM, GNode, addNode, at, edge, ground, stroke, subtitles, trace} from '../lib/plotter';

const SHOW_SUBTITLE = true;

export default makeScene2D(function* (view) {
  ground(view);
  const world = createRef<Node>();
  view.add(<Node ref={world} />);
  const w = world();
  const say = subtitles(view, SHOW_SUBTITLE);

  // ---- ten bars that stand for code: indent and width only ------------------------------------
  const bars: [number, number][] = [[0, 360], [0, 300], [0, 420], [0, 260], [1, 330], [1, 280], [0, 120], [1, 380], [0, 340], [0, 220]];
  const BAR_X = -820;
  const BAR_Y = -360;
  const barY = (i: number) => BAR_Y + i * 70;
  const barRefs = bars.map(() => createRef<Rect>());
  bars.forEach(([indent, width], i) => {
    w.add(<Rect ref={barRefs[i]} x={BAR_X + indent * 60} y={barY(i)} offsetX={-1} width={width} height={20} radius={10} fill={CHALK_DIM} opacity={0} />);
  });
  const scanY = createSignal(BAR_Y - 60);
  const scan = createRef<Line>();
  w.add(<Line ref={scan} points={() => [[BAR_X - 40, scanY()], [BAR_X + 520, scanY()]]} stroke={CHALK_DIM} lineWidth={2} opacity={0} />);

  // ---- the graph the run recovers ---------------------------------------------------------------
  const graph = createRef<Node>();
  w.add(<Node ref={graph} />);
  const g = graph();
  const dimNode = (x: number, y: number) => addNode(g, at(x, y), '', {kind: 'value', stroke: CHALK_DIM});
  const nA = dimNode(60, -300);
  const nB = dimNode(260, -300);
  const nC = dimNode(260, -150);
  const nD = dimNode(460, -150); // the branch: the run went right
  const nE = dimNode(660, -150);
  const nF = dimNode(660, 40);
  const nG = dimNode(660, 200);
  const dimEdge = (a: GNode, b: GNode) => edge(g, a, b, {color: CHALK_DIM});
  // the node each bar leaves, if any, and the edge that reaches it
  const steps: [number, GNode, ReturnType<typeof dimEdge> | null][] = [
    [0, nA, null],
    [1, nB, dimEdge(nA, nB)],
    [2, nC, dimEdge(nB, nC)],
    [3, nD, dimEdge(nC, nD)],
    [4, nE, dimEdge(nD, nE)],
    [8, nF, dimEdge(nE, nF)],
    [9, nG, dimEdge(nF, nG)],
  ];
  // the arm not taken (bar 6) and the print (bar 7): torn strokes that end in nothing
  const torn = (from: Vector2, dir: Vector2) => {
    const ref = createRef<Line>();
    const u = dir.normalized;
    const n = new Vector2(-u.y, u.x);
    const p = (k: number, j: number) => from.add(u.scale(k)).add(n.scale(j));
    g.add(<Line ref={ref} points={[p(0, 0), p(70, 0), p(84, 7), p(92, -6), p(104, 8), p(110, -4)]} stroke={CHALK_DIM} lineWidth={3} lineCap="round" end={0} opacity={0} />);
    return ref;
  };
  const tornArm = torn(new Vector2(460, -150 + 40), new Vector2(0, 1));
  const tornPrint = torn(new Vector2(660 - 40, -150 + 20), new Vector2(-1, 1));

  // ---- 0.0 - 0.6: the bars --------------------------------------------------------------------
  yield chain(waitFor(0.3), say('编译器需要的是图。'));
  yield* sequence(0.04, ...barRefs.map((b) => b().opacity(0.55, 0.25)));

  // ---- 0.6 - 3.6: the run -----------------------------------------------------------------------
  scan().opacity(0.8);
  const perBar = 0.3;
  for (let i = 0; i < bars.length; i++) {
    const step = steps.find((s) => s[0] === i);
    const moves: ReturnType<typeof all>[] = [scanY(barY(i), perBar, linear)];
    if (step) {
      moves.push(all(trace(step[1], perBar), step[2] ? stroke(step[2], perBar) : waitFor(0)));
      moves.push(barRefs[i]().opacity(0.85, 0.1));
    }
    if (i === 6) {
      tornArm().opacity(1);
      moves.push(tornArm().end(1, perBar)); // never run: the arm ends in a tear
    }
    if (i === 7) {
      moves.push(chain(barRefs[i]().opacity(0.9, 0.08), barRefs[i]().opacity(0.3, 0.2))); // printed; no node
      tornPrint().opacity(1);
      moves.push(tornPrint().end(1, perBar));
    }
    if (i === 3) {
      yield say('多数语言让它事后重建——重建出来的图会断。');
    }
    yield* all(...moves);
  }
  yield* scanY(BAR_Y + 10 * 70, 0.2, linear);
  yield* scan().opacity(0, 0.2);

  // ---- 3.6 - 4.4: it hangs --------------------------------------------------------------------
  yield* waitFor(0.6);

  // ---- 4.4 - 5.6: it greys and crumples to a point ----------------------------------------------
  const point = new Vector2(-560, -330);
  yield* all(
    g.opacity(0.5, 0.3),
    ...barRefs.map((b) => b().opacity(0, 0.5)),
  );
  yield* all(
    g.scale(0, 0.8, easeInCubic),
    g.position(point, 0.8, easeInOutCubic),
  );
  const dot = createRef<Rect>();
  w.add(<Rect ref={dot} position={point} width={5} height={52} radius={2} fill={CHALK_DIM} opacity={0} />);
  yield* dot().opacity(1, 0.15);
  yield* waitFor(0.3);
});
