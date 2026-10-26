/**
 * Shot 10 of "The program is the graph" (6 s): the camel.
 * Script: ../../../script.md (shot 10).
 *
 * What the picture claims: it was one graph all along. The logo is a graph (assets/brand/
 * camel-mark.svg: strokes between node circles), so the pieces of the film's graphs can lie on its
 * outline: edges in the colours of the passes (cyan), derivatives (violet), values (sand), the
 * device (rose), failures (coral) and results (mint); the ring is the `sync` thread.
 *
 * Timeline (seconds)
 *   0.0 - 2.5   the camera pulls back from one node to the whole canvas: the film's graph pieces,
 *               lying on the logo's outline
 *   2.5 - 3.7   the pieces fade and the logo is traced in chalk
 *   3.7 - 4.5   `Let the graph speak.` and its sand underline
 *   4.5 - 6.0   `pip install camel-lang`; hold
 */
import {Circle, Line, Node, Path, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {Vector2, all, chain, createRef, easeInOutCubic, easeOutCubic, sequence, waitFor} from '@motion-canvas/core';
// a copy of assets/brand/camel-mark.svg
import mark from '../assets/camel-mark.svg?raw';
import {CHALK, CHALK_DIM, CORAL, CYAN, GROUND_MID, MINT, MONO, ROSE, SAND, THREAD, VIOLET, ground, subtitles} from '../lib/plotter';

const SHOW_SUBTITLE = true;

interface Stroke {
  id: string;
  d: string;
}
interface Dot {
  x: number;
  y: number;
  r: number;
}

/** The points of a stroke made only of straight segments (`M x y L x y ...`), or null for a curve. */
function straight(d: string): Vector2[] | null {
  if (!/^M[\d.\s]+(L[\d.\s]+)*$/.test(d)) {
    return null;
  }
  return [...d.matchAll(/[ML]([\d.]+) ([\d.]+)/g)].map((m) => new Vector2(+m[1], +m[2]));
}

/** The strokes and node circles of the mark, in its own 1024-unit coordinates. */
function readMark(svg: string): {strokes: Stroke[]; dots: Dot[]} {
  const strokes = [...svg.matchAll(/<path id="([^"]+)" d="([^"]+)"/g)].map((m) => ({id: m[1], d: m[2]}));
  const dots = [...svg.matchAll(/<circle id="[^"]+" cx="([\d.]+)" cy="([\d.]+)" r="([\d.]+)"/g)].map((m) => ({x: +m[1], y: +m[2], r: +m[3]}));
  return {strokes, dots};
}

const PALETTE = [CYAN, SAND, VIOLET, MINT, ROSE, CORAL, CHALK];

export default makeScene2D(function* (view) {
  ground(view);
  const world = createRef<Node>();
  view.add(<Node ref={world} />);
  const w = world();
  const say = subtitles(view, SHOW_SUBTITLE);
  const {strokes, dots} = readMark(mark);

  // the canvas: the mark centred, 1024 units drawn at 0.82
  const S = 0.82;
  const canvas = createRef<Node>();
  w.add(<Node ref={canvas} scale={S} position={[0, -40]} />);
  const c = canvas();
  const O = new Vector2(-512, -512);

  // the film's graph pieces, lying on the outline: each straight stroke an edge with an arrow
  const pieces = createRef<Node>();
  c.add(<Node ref={pieces} />);
  strokes.forEach((s, i) => {
    const ring = s.id.startsWith('ring');
    const color = ring ? THREAD : PALETTE[i % PALETTE.length];
    const pts = straight(s.d);
    if (pts) {
      pieces().add(<Line points={pts.map((p) => p.add(O))} stroke={color} lineWidth={9} lineCap="round" endArrow arrowSize={22} />);
    } else {
      pieces().add(<Path data={s.d} position={O} stroke={color} lineWidth={ring ? 10 : 9} lineDash={ring ? [26, 20] : undefined} lineCap="round" />);
    }
  });
  dots.forEach((d) => {
    pieces().add(<Circle position={O.add(new Vector2(d.x, d.y))} size={d.r * 2 + 22} fill={GROUND_MID} stroke={CHALK} lineWidth={6} />);
  });
  // a few labels from the film, small at this distance
  const tags: [number, number, string][] = [[343.9, 481.1, 'x'], [408, 328, 'mlp'], [624.6, 421.9, '!!']];
  tags.forEach(([x, y, t]) => pieces().add(<Txt position={O.add(new Vector2(x, y - 46))} text={t} fontFamily={MONO} fontSize={30} fill={CHALK_DIM} />));

  // the logo itself, to be traced in chalk
  const logo = createRef<Node>();
  c.add(<Node ref={logo} opacity={0} />);
  const logoStrokes = strokes.map((s) => {
    const pts = straight(s.d);
    if (pts) {
      const ref = createRef<Line>();
      logo().add(<Line ref={ref} points={pts.map((p) => p.add(O))} stroke={CHALK} lineWidth={24} lineCap="round" lineJoin="round" end={0} />);
      return () => ref().end(1, 0.5, easeInOutCubic);
    }
    const ref = createRef<Path>();
    logo().add(<Path ref={ref} data={s.d} position={O} stroke={CHALK} lineWidth={24} lineCap="round" lineJoin="round" opacity={0} />);
    return () => ref().opacity(1, 0.5, easeInOutCubic);
  });
  const logoDots = dots.map((d) => {
    const ref = createRef<Circle>();
    logo().add(<Circle ref={ref} position={O.add(new Vector2(d.x, d.y))} size={d.r * 2} fill={CHALK} scale={0} />);
    return ref;
  });

  // ---- 0.0 - 2.5: pull back from one node ---------------------------------------------------------
  const focus = new Vector2(343.9, 481.1).add(O).scale(S).add(new Vector2(0, -40));
  w.scale(4);
  w.position(focus.scale(-4));
  yield chain(waitFor(0.4), say('Camel。'));
  yield* all(w.scale(1, 2.5, easeInOutCubic), w.position(new Vector2(0, 0), 2.5, easeInOutCubic));

  // ---- 2.5 - 3.7: the pieces fade; the logo is traced in chalk ----------------------------------------
  logo().opacity(1);
  yield* all(
    pieces().opacity(0, 0.8),
    sequence(0.02, ...logoStrokes.map((draw) => draw())),
    chain(waitFor(0.5), sequence(0.02, ...logoDots.map((r) => r().scale(1, 0.25, easeOutCubic)))),
  );

  // ---- 3.7 - 4.5: Let the graph speak. ----------------------------------------------------------------
  const motto = createRef<Txt>();
  const underline = createRef<Rect>();
  w.add(<Txt ref={motto} text="Let the graph speak." y={250} fontFamily={MONO} fontSize={64} fill={CHALK} opacity={0} />);
  w.add(<Rect ref={underline} x={-370} y={300} offsetX={-1} width={0} height={5} radius={2} fill={SAND} />);
  yield say('Camel。让图说话。');
  yield* all(canvas().scale(0.55, 0.6, easeInOutCubic), canvas().position(new Vector2(0, -170), 0.6, easeInOutCubic), chain(waitFor(0.2), motto().opacity(1, 0.4)));
  yield* underline().width(740, 0.4, easeInOutCubic);

  // ---- 4.5 - 6.0: pip install -------------------------------------------------------------------------
  const pip = createRef<Txt>();
  w.add(<Txt ref={pip} text="pip install camel-lang" y={360} fontFamily={MONO} fontSize={32} fill={CHALK_DIM} opacity={0} />);
  yield* pip().opacity(1, 0.3);
  yield* waitFor(1.3);
});
