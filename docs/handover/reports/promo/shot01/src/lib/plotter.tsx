/**
 * The pen of "Plotter on navy" (../../../style.md), shared by the scenes after shot 1: the
 * palette, the ground, nodes that are traced rather than spawned, edges drawn as one stroke,
 * beads that carry values, code written on the ground with its gutter, and the preview subtitle.
 */
import {Gradient, Line, Node, Rect, Txt, View2D} from '@motion-canvas/2d';
import {
  Reference,
  ThreadGenerator,
  Vector2,
  all,
  createRef,
  createSignal,
  easeInOutCubic,
  easeOutCubic,
  waitFor,
} from '@motion-canvas/core';

// ---- palette (style.md 1) -------------------------------------------------------------------
export const GROUND_TOP = '#2D3968';
export const GROUND_BOTTOM = '#242946';
export const GROUND_MID = '#293158';
export const CHALK = '#D8DEF5';
export const CHALK_DIM = '#8E9AC0';
export const KEYWORD = '#AEB9DE';
export const SAND = '#F2B35B';
export const CYAN = '#5EE0F0';
export const VIOLET = '#C3A6FF';
export const MINT = '#7BE8B0';
export const THREAD = '#F7E3B5';
export const CORAL = '#FF7A70';
export const ROSE = '#FF9CC2';

// Without ligatures: the code shows the characters a viewer types (`->` is two characters).
export const MONO = 'JetBrains Mono NL, JetBrains Mono, Cascadia Mono, Consolas, monospace';
export const CJK = 'Noto Sans SC, Noto Sans CJK SC, Microsoft YaHei, PingFang SC, sans-serif';

export const STROKE = 4;
export const R = 36; // half the size of a node (72 px)

export type P = ReturnType<typeof Vector2.createSignal>;
export const at = (x: number, y: number): P => Vector2.createSignal(new Vector2(x, y));

/** The logo's navy, top to bottom. */
export function ground(view: View2D) {
  view.add(
    <Rect
      width={1920}
      height={1080}
      fill={
        new Gradient({
          type: 'linear',
          from: [0, -540],
          to: [0, 540],
          stops: [
            {offset: 0, color: GROUND_TOP},
            {offset: 1, color: GROUND_BOTTOM},
          ],
        })
      }
    />,
  );
}

// ---- nodes ----------------------------------------------------------------------------------

export type NodeKind = 'value' | 'op' | 'effect' | 'ring' | 'weight';

export interface GNode {
  root: Reference<Node>;
  shape: Reference<Rect>;
  notch: Reference<Rect>;
  glyph: Reference<Txt>;
  value: Reference<Txt>;
  pos: P;
  /** Half the width and half the height: where an edge stops. */
  hw: number;
  hh: number;
  kind: NodeKind;
}

export interface NodeOptions {
  kind?: NodeKind;
  width?: number;
  stroke?: string;
  glyphSize?: number;
  zIndex?: number;
}

/** A node of the graph, drawn but not yet traced: `trace` draws its outline. */
export function addNode(parent: Node, pos: P, text: string, options: NodeOptions = {}): GNode {
  const kind = options.kind ?? 'op';
  const round = kind === 'value' || kind === 'ring' || kind === 'weight';
  const size = kind === 'weight' ? 40 : R * 2;
  const glyphSize = options.glyphSize ?? (kind === 'weight' ? 20 : round ? 32 : 30);
  // Wide enough that the label never touches the border: the glyphs plus padding on each side.
  const textWidth = text.length * glyphSize * 0.6;
  const padding = kind === 'weight' ? 14 : kind === 'value' ? 20 : 26;
  const width = options.width ?? Math.max(size, Math.ceil(textWidth + 2 * padding));
  const root = createRef<Node>();
  const shape = createRef<Rect>();
  const notch = createRef<Rect>();
  const glyph = createRef<Txt>();
  const value = createRef<Txt>();
  parent.add(
    <Node ref={root} position={pos} zIndex={options.zIndex ?? 2}>
      <Rect
        ref={shape}
        width={width}
        height={size}
        radius={round ? size / 2 : 18}
        fill={GROUND_MID}
        stroke={options.stroke ?? CHALK}
        lineWidth={kind === 'weight' ? 3 : STROKE}
        lineCap="round"
        end={0}
        opacity={0}
      />
      <Rect
        ref={notch}
        width={18}
        height={18}
        x={width / 2 - 6}
        y={size / 2 - 6}
        fill={GROUND_MID}
        opacity={0}
      />
      <Txt
        ref={glyph}
        text={text}
        fontFamily={MONO}
        fontSize={glyphSize}
        fill={kind === 'weight' ? CHALK_DIM : CHALK}
        opacity={0}
      />
      <Txt
        ref={value}
        text=""
        fontFamily={MONO}
        fontSize={28}
        fill={MINT}
        y={size / 2 + 30}
        opacity={0}
      />
    </Node>,
  );
  return {root, shape, notch, glyph, value, pos, hw: width / 2, hh: size / 2, kind};
}

/** The pen traces the node's outline around its glyph. */
export function* trace(n: GNode, duration = 0.3): ThreadGenerator {
  n.shape().opacity(1);
  if (n.kind === 'effect') {
    n.notch().opacity(1);
  }
  yield* all(n.shape().end(1, duration, easeInOutCubic), n.glyph().opacity(1, duration * 0.6));
}

/** A node that is drawn but does not run: dim outline and glyph. */
export function dim(n: GNode, on: boolean, duration = 0.3): ThreadGenerator {
  return all(
    n.shape().stroke(on ? CHALK_DIM : CHALK, duration),
    n.glyph().fill(on ? CHALK_DIM : n.kind === 'weight' ? CHALK_DIM : CHALK, duration),
    n.shape().opacity(on ? 0.6 : 1, duration),
  );
}

/** The value a node computed, written beneath it. */
export function* showValue(n: GNode, text: string, color = MINT): ThreadGenerator {
  n.value().text(text);
  n.value().fill(color);
  yield* n.value().opacity(1, 0.2);
}

/** A brief sand flash on the outline: the node ran. */
export function* flash(n: GNode, back = CHALK): ThreadGenerator {
  yield* n.shape().stroke(SAND, 0.12).to(back, 0.35);
}

// ---- edges ----------------------------------------------------------------------------------

/** Where a straight line from `from` towards `to` leaves the node's box (an ellipse for rounds). */
function exitPoint(n: GNode | null, centre: Vector2, toward: Vector2, gap: number): Vector2 {
  if (n === null) {
    return centre;
  }
  const d = toward.sub(centre);
  if (d.magnitude < 1e-6) {
    return centre;
  }
  const u = d.normalized;
  if (n.kind !== 'op' && n.kind !== 'effect' && Math.abs(n.hw - n.hh) < 1) {
    return centre.add(u.scale(n.hh + gap));
  }
  const tx = Math.abs(u.x) > 1e-6 ? n.hw / Math.abs(u.x) : Infinity;
  const ty = Math.abs(u.y) > 1e-6 ? n.hh / Math.abs(u.y) : Infinity;
  return centre.add(u.scale(Math.min(tx, ty) + gap));
}

export interface EdgeOptions {
  color?: string;
  width?: number;
  arrow?: boolean;
  /** Sideways offset of the stroke's middle, in px: a gentle bend. */
  bend?: number;
  dash?: number[];
  zIndex?: number;
}

/** One stroke from node `a` to node `b`, not yet drawn (`stroke` draws it). */
export function edge(parent: Node, a: GNode, b: GNode, options: EdgeOptions = {}): Reference<Line> {
  const ref = createRef<Line>();
  const bend = options.bend ?? 0;
  parent.add(
    <Line
      ref={ref}
      points={() => {
        const pa = a.pos();
        const pb = b.pos();
        const mid = pa.add(pb).scale(0.5);
        const n = pb.sub(pa).perpendicular.normalized;
        const via = mid.add(n.scale(bend));
        const start = exitPoint(a, pa, bend === 0 ? pb : via, 2);
        const end = exitPoint(b, pb, bend === 0 ? pa : via, options.arrow === false ? 2 : 8);
        return bend === 0 ? [start, end] : [start, via, end];
      }}
      radius={bend === 0 ? 0 : 400}
      stroke={options.color ?? CHALK}
      lineWidth={options.width ?? STROKE}
      lineCap="round"
      lineJoin="round"
      endArrow={options.arrow !== false}
      arrowSize={10}
      lineDash={options.dash ?? []}
      end={0}
      opacity={0}
      zIndex={options.zIndex ?? 1}
    />,
  );
  return ref;
}

/** The pen draws the stroke. */
export function* stroke(e: Reference<Line>, duration = 0.25): ThreadGenerator {
  e().opacity(1);
  yield* e().end(1, duration, easeOutCubic);
}

// ---- beads ----------------------------------------------------------------------------------

/** A bead carrying a value along straight paths, all in the same frames. */
export function* beads(parent: Node, paths: [Vector2, Vector2][], duration: number, color = SAND): ThreadGenerator {
  const refs = paths.map(([from]) => {
    const ref = createRef<Rect>();
    parent.add(
      <Rect
        ref={ref}
        size={18}
        radius={9}
        fill={color}
        shadowColor={color}
        shadowBlur={22}
        position={from}
        zIndex={6}
      />,
    );
    return ref;
  });
  yield* all(...refs.map((ref, i) => ref().position(paths[i][1], duration, easeInOutCubic)));
  refs.forEach((ref) => ref().remove());
}

/**
 * Beads along drawn edges, from the edge's start to its arrowhead, all in the same frames: a value
 * travels beside the labels, never over them.
 */
export function* beadsAlong(parent: Node, edges: Reference<Line>[], duration: number, color = SAND): ThreadGenerator {
  const paths = edges.map((e): [Vector2, Vector2] => {
    const pts = e().points();
    return [new Vector2(pts[0] as any), new Vector2(pts[pts.length - 1] as any)];
  });
  yield* beads(parent, paths, duration, color);
}

// ---- code on the ground ---------------------------------------------------------------------

export interface Tok {
  t: string;
  c: string;
  /** Not shown until typed. */
  hidden?: boolean;
}
export const kw = (t: string, hidden = false): Tok => ({t, c: KEYWORD, hidden});
export const ch = (t: string, hidden = false): Tok => ({t, c: CHALK, hidden});
export const dm = (t: string, hidden = false): Tok => ({t, c: CHALK_DIM, hidden});
export const st = (t: string, hidden = false): Tok => ({t, c: SAND, hidden});
export const nm = (t: string, hidden = false): Tok => ({t, c: MINT, hidden});

export interface TokRef {
  full: string;
  text: ReturnType<typeof createSignal<string>>;
}

export interface Code {
  root: Reference<Rect>;
  lines: TokRef[][];
}

/**
 * Code written straight on the ground: a gutter rule, dim line numbers, one Txt per token so
 * that each keeps its colour. Anchored at its top-left corner (`x`, `y`).
 */
export function code(parent: Node, x: number, y: number, lines: Tok[][], size = 40): Code {
  const root = createRef<Rect>();
  const refs: TokRef[][] = lines.map((line) =>
    line.map((tok) => ({full: tok.t, text: createSignal(tok.hidden ? '' : tok.t)})),
  );
  const lineHeight = Math.round(size * 1.35);
  parent.add(
    <Rect ref={root} x={x} y={y} offset={[-1, -1]} layout direction="column" alignItems="start" opacity={0}>
      {lines.map((line, i) => (
        <Rect layout direction="row" alignItems="center" height={lineHeight}>
          <Txt
            text={String(i + 1)}
            fontFamily={MONO}
            fontSize={Math.round(size * 0.55)}
            fill={CHALK_DIM}
            width={size}
            textAlign="right"
            marginRight={Math.round(size * 0.9)}
          />
          {line.map((tok, j) => (
            <Txt text={refs[i][j].text} fontFamily={MONO} fontSize={size} fill={tok.c} textWrap="pre" />
          ))}
        </Rect>
      ))}
    </Rect>,
  );
  parent.add(
    <Line
      points={[
        [x + size + Math.round(size * 0.45), y],
        [x + size + Math.round(size * 0.45), y + lineHeight * lines.length],
      ]}
      stroke={CHALK_DIM}
      lineWidth={1}
      opacity={() => root().opacity() * 0.6}
    />,
  );
  return {root, lines: refs};
}

/** Types the hidden or blank tokens of one line, character by character. */
export function* typeLine(c: Code, line: number, cps = 30): ThreadGenerator {
  for (const tok of c.lines[line]) {
    const shown = tok.text();
    if (shown === tok.full) {
      continue;
    }
    for (let k = shown.length + 1; k <= tok.full.length; k++) {
      tok.text(tok.full.slice(0, k));
      yield* waitFor(1 / cps);
    }
  }
}

/** Blanks a line so that `typeLine` can write it. */
export function blank(c: Code, line: number) {
  for (const tok of c.lines[line]) {
    tok.text('');
  }
}

// ---- match cuts (style.md 5.4) ---------------------------------------------------------------

/**
 * The end of a shot that hands one shape to the next: everything in `world` fades except the
 * nodes in `keep` (and what holds them), so the last frame is that shape alone on the ground.
 */
export function* handOff(world: Node, keep: Node[], duration = 0.4): ThreadGenerator {
  const holds = (n: Node) => keep.some((k) => {
    for (let p: Node | null = k; p !== null; p = p.parent()) {
      if (p === n) return true;
    }
    return false;
  });
  const faded: Node[] = [];
  const visit = (n: Node) => {
    if (keep.includes(n)) return;
    if (holds(n)) {
      n.children().forEach(visit);
    } else {
      faded.push(n);
    }
  };
  world.children().forEach(visit);
  yield* all(...faded.map((n) => n.opacity(0, duration)));
}

/**
 * The start of a shot that takes a node from the last one: drawn from the first frame at `from`
 * (in its parent's coordinates) and `fromScale`, it glides to its own place. With `glyph` false
 * the label stays hidden: the shape carries over, the name is the new shot's to write.
 */
export function* arrive(n: GNode, from: Vector2, fromScale = 1, duration = 0.6, glyph = true): ThreadGenerator {
  const to = n.pos();
  n.pos(from);
  n.root().scale(fromScale);
  n.shape().opacity(1);
  n.shape().end(1);
  if (glyph) {
    n.glyph().opacity(1);
  }
  if (n.kind === 'effect') {
    n.notch().opacity(1);
  }
  yield* all(n.pos(to, duration, easeInOutCubic), n.root().scale(1, duration, easeInOutCubic));
}

// ---- labels and the subtitle ----------------------------------------------------------------

export function label(parent: Node, x: number, y: number, text: string, color = CHALK_DIM, size = 24): Reference<Txt> {
  const ref = createRef<Txt>();
  parent.add(<Txt ref={ref} text={text} x={x} y={y} fontFamily={MONO} fontSize={size} fill={color} opacity={0} zIndex={5} />);
  return ref;
}

/** The preview subtitle: outside the world, in a band that never moves. */
export function subtitles(view: View2D, show: boolean) {
  const ref = createRef<Txt>();
  view.add(<Txt ref={ref} text="" fontFamily={CJK} fontWeight={500} fontSize={40} fill={CHALK} y={540 - 86} opacity={0} />);
  return function* (text: string): ThreadGenerator {
    if (!show) {
      return;
    }
    if (ref().opacity() > 0) {
      yield* ref().opacity(0, 0.15);
    }
    ref().text(text);
    yield* ref().opacity(1, 0.15);
  };
}
