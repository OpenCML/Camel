/**
 * Shot 1 of "The program is the graph" (7 s): one ink.
 *
 * The body of `f` is typed on the navy ground; each operator and constant lifts off the code line
 * as a glyph, flies into the frame of `f`, and has its node traced around it. The two equal halves
 * of `x * 2 + x * 2` are marked and slide together into one node, used twice; then x = 3 flows
 * through. Script: ../../script.md (shot 1). Style: ../../style.md ("Plotter on navy").
 *
 * Timeline (seconds)
 *   0.0 - 0.5   header and brace appear; the frame of `f`, port `x` and the result ring are traced
 *   0.5 - 3.0   `x * 2 + x * 2` is typed; glyphs fly into the graph, nodes and edges are traced
 *   3.0 - 3.4   the equal halves are marked (sand underline, sand outline, "=")
 *   3.4 - 4.4   the lower half slides onto the upper one and fades
 *   4.4 - 4.7   the two strokes into `+` separate: one node, used twice
 *   4.7 - 6.4   x = 3 flows through: 3 * 2 = 6, 6 + 6 = 12; the ring is re-traced in mint
 *   6.4 - 7.0   hold (a slow 3 % push-in runs under the whole shot)
 */
import {Gradient, Line, Node, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {
  Reference,
  ThreadGenerator,
  Vector2,
  all,
  cancel,
  chain,
  createRef,
  createSignal,
  easeInOutCubic,
  easeOutCubic,
  linear,
  loop,
  waitFor,
} from '@motion-canvas/core';

// Show the Chinese subtitle in the preview; the final edit burns subtitles in separately.
const SHOW_SUBTITLE = true;

// ---- palette (style.md 1) -------------------------------------------------------------------
const GROUND_TOP = '#2D3968';
const GROUND_BOTTOM = '#242946';
const GROUND_MID = '#293158'; // node fill: hides the strokes beneath, adds no colour
const CHALK = '#D8DEF5';
const CHALK_DIM = '#8E9AC0';
const KEYWORD = '#AEB9DE';
const SAND = '#F2B35B';
const MINT = '#7BE8B0';

const MONO = 'JetBrains Mono NL, JetBrains Mono, Cascadia Mono, Consolas, monospace';
const CJK = 'Noto Sans SC, Noto Sans CJK SC, Microsoft YaHei, PingFang SC, sans-serif';

// ---- geometry (scene coordinates, origin at the centre of 1920x1080) -------------------------
const R = 36; // half size of every node (72 px)
const STROKE = 4;
const CODE_SIZE = 52;
const FRAME = {x: 420, width: 820, height: 560}; // the frame of `f`, inside the safe margin
const LEFT = FRAME.x - FRAME.width / 2; // port `x` sits on this edge
const RIGHT = FRAME.x + FRAME.width / 2; // the result ring sits on this edge
const GUTTER_X = -960 + 112;

type P = ReturnType<typeof Vector2.createSignal>;
const at = (x: number, y: number): P => Vector2.createSignal(new Vector2(x, y));

interface GraphNode {
  node: Reference<Node>;
  shape: Reference<Rect>;
  glyph: Reference<Txt>;
  value: Reference<Txt>;
}

export default makeScene2D(function* (view) {
  // ---- ground: the logo's navy ---------------------------------------------------------------
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

  // Everything drawn lives in `world`, so the slow push-in is one scale tween.
  const world = createRef<Node>();
  view.add(<Node ref={world} />);

  // ---- code, written straight on the ground behind a gutter rule ------------------------------
  const code = createRef<Rect>();
  const caret = createSignal(0);
  const underA = createRef<Rect>();
  const underB = createRef<Rect>();

  // One Txt per character of the body: digits and operators keep their colour, and each glyph
  // can be found on screen when it lifts off.
  const body = 'x * 2 + x * 2';
  const chars = [...body].map(() => createSignal(''));
  const glyphRefs = [...body].map(() => createRef<Txt>());
  const colourOf = (c: string) => (/[0-9]/.test(c) ? MINT : c === 'x' ? CHALK : CHALK_DIM);
  const glyph = (k: number) => (
    <Txt
      ref={glyphRefs[k]}
      text={chars[k]}
      fontFamily={MONO}
      fontSize={CODE_SIZE}
      fill={colourOf(body[k])}
      textWrap="pre"
    />
  );
  const half = (from: number, to: number, under: Reference<Rect>) => (
    <Rect layout direction="column" alignItems="stretch">
      <Rect layout direction="row">{[...Array(to - from).keys()].map((i) => glyph(from + i))}</Rect>
      <Rect ref={under} height={5} radius={3} fill={SAND} marginTop={6} opacity={0} />
    </Rect>
  );
  const word = (text: string, fill: string) => (
    <Txt text={text} fontFamily={MONO} fontSize={CODE_SIZE} fill={fill} textWrap="pre" />
  );
  const lineNo = (n: number) => (
    <Txt
      text={String(n)}
      fontFamily={MONO}
      fontSize={26}
      fill={CHALK_DIM}
      width={44}
      textAlign="right"
      marginRight={48}
      marginTop={14}
    />
  );

  world().add(
    <Line
      points={[
        [GUTTER_X, -120],
        [GUTTER_X, 120],
      ]}
      stroke={CHALK_DIM}
      lineWidth={1}
      opacity={0.6}
    />,
  );
  world().add(
    <Rect
      ref={code}
      x={GUTTER_X - 56}
      offsetX={-1}
      layout
      direction="column"
      alignItems="start"
      gap={14}
      opacity={0}
    >
      <Rect layout direction="row" alignItems="start">
        {lineNo(1)}
        {word('func', KEYWORD)}
        {word(' f(x: ', CHALK)}
        {word('int', KEYWORD)}
        {word('): ', CHALK)}
        {word('int', KEYWORD)}
        {word(' {', CHALK)}
      </Rect>
      <Rect layout direction="row" alignItems="start">
        {lineNo(2)}
        {word('    ', CHALK)}
        {half(0, 5, underA)}
        <Rect layout direction="row">{[5, 6, 7].map(glyph)}</Rect>
        {half(8, 13, underB)}
        <Rect width={5} height={CODE_SIZE} marginLeft={4} radius={2} fill={SAND} opacity={caret} />
      </Rect>
      <Rect layout direction="row" alignItems="start">
        {lineNo(3)}
        {word('}', CHALK)}
      </Rect>
    </Rect>,
  );

  // ---- the frame of `f` --------------------------------------------------------------------
  const frame = createRef<Rect>();
  const frameName = createRef<Txt>();
  world().add(
    <Rect
      ref={frame}
      x={FRAME.x}
      width={FRAME.width}
      height={FRAME.height}
      radius={28}
      stroke={CHALK_DIM}
      lineWidth={2}
      lineDash={[14, 10]}
      end={0}
    />,
  );
  world().add(
    <Txt
      ref={frameName}
      text="f"
      fontFamily={MONO}
      fontSize={28}
      fill={CHALK_DIM}
      x={LEFT + 36}
      y={-FRAME.height / 2 + 34}
      opacity={0}
    />,
  );

  // ---- graph ---------------------------------------------------------------------------------
  const pX = at(LEFT, 0);
  const pTwo1 = at(210, -190);
  const pTwo2 = at(210, 190);
  const pMul1 = at(430, -95);
  const pMul2 = at(430, 95);
  const pPlus = at(660, 0);
  const pOut = at(RIGHT, 0);
  const spread = createSignal(0);

  // Edges are added before nodes so that each node covers the ends of its strokes.
  const edge = (from: P, to: P, lateral: () => number = () => 0): Reference<Line> => {
    const ref = createRef<Line>();
    world().add(
      <Line
        ref={ref}
        points={() => {
          const offset = new Vector2(0, lateral());
          const a = from().add(offset);
          const b = to().add(offset);
          const u = b.sub(a).normalized;
          return [a.add(u.scale(R)), b.sub(u.scale(R + 6))];
        }}
        stroke={CHALK}
        lineWidth={STROKE}
        lineCap="round"
        endArrow
        arrowSize={10}
        end={0}
        opacity={0} // a zero-length stroke with round caps would still show a dot
      />,
    );
    return ref;
  };

  const eXM1 = edge(pX, pMul1);
  const eT1M1 = edge(pTwo1, pMul1);
  const eM1P = edge(pMul1, pPlus, () => -spread() / 2);
  const ePO = edge(pPlus, pOut);
  const eXM2 = edge(pX, pMul2);
  const eT2M2 = edge(pTwo2, pMul2);
  const eM2P = edge(pMul2, pPlus, () => spread() / 2);

  // A node is drawn, not spawned: its outline is traced (end 0 -> 1) around the glyph that flew in.
  const addNode = (pos: P, text: string, round: boolean): GraphNode => {
    const node = createRef<Node>();
    const shape = createRef<Rect>();
    const glyphRef = createRef<Txt>();
    const value = createRef<Txt>();
    world().add(
      <Node ref={node} position={pos} zIndex={2}>
        <Rect
          ref={shape}
          size={R * 2}
          radius={round ? R : 18}
          fill={GROUND_MID}
          stroke={CHALK}
          lineWidth={STROKE}
          lineCap="round"
          end={0}
          opacity={0}
        />
        <Txt
          ref={glyphRef}
          text={text}
          fontFamily={MONO}
          fontSize={round ? 38 : 44}
          fill={CHALK}
          opacity={0}
        />
        <Txt
          ref={value}
          text=""
          fontFamily={MONO}
          fontSize={30}
          fill={MINT}
          y={R + 32}
          opacity={0}
        />
      </Node>,
    );
    return {node, shape, glyph: glyphRef, value};
  };

  const port = addNode(pX, 'x', true);
  const two1 = addNode(pTwo1, '2', true);
  const two2 = addNode(pTwo2, '2', true);
  const mul1 = addNode(pMul1, '×', false);
  const mul2 = addNode(pMul2, '×', false);
  const plus = addNode(pPlus, '+', false);
  const out = addNode(pOut, '', true); // the result: a hollow ring

  const same = createRef<Txt>();
  world().add(
    <Txt
      ref={same}
      text="="
      fontFamily={MONO}
      fontSize={52}
      fill={SAND}
      x={320}
      y={0}
      opacity={0}
      zIndex={3}
    />,
  );
  const caption = createRef<Txt>();
  world().add(
    <Txt
      ref={caption}
      text="one value, one node"
      fontFamily={MONO}
      fontSize={26}
      fill={CHALK_DIM}
      x={FRAME.x}
      y={FRAME.height / 2 + 44}
      opacity={0}
    />,
  );

  // ---- subtitle (preview only; outside `world`, so it takes no part in the push-in) -----------
  const subtitle = createRef<Txt>();
  view.add(
    <Txt
      ref={subtitle}
      text=""
      fontFamily={CJK}
      fontWeight={500}
      fontSize={40}
      fill={CHALK}
      y={540 - 86}
      opacity={0}
    />,
  );
  const say = function* (text: string) {
    if (!SHOW_SUBTITLE) return;
    if (subtitle().opacity() > 0) yield* subtitle().opacity(0, 0.15);
    subtitle().text(text);
    yield* subtitle().opacity(1, 0.15);
  };

  // ---- the pen ---------------------------------------------------------------------------------
  const trace = function* (n: GraphNode, duration = 0.3) {
    n.shape().opacity(1);
    yield* all(n.shape().end(1, duration, easeInOutCubic), n.glyph().opacity(1, 0.1));
  };
  const stroke = function* (e: Reference<Line>) {
    e().opacity(1);
    yield* e().end(1, 0.25, easeOutCubic);
  };
  const pulse = function* (n: GraphNode) {
    yield* all(
      n.node().scale(1.12, 0.12).to(1, 0.18),
      n.shape().stroke(SAND, 0.12).to(CHALK, 0.25),
    );
  };

  // The glyph at position k of the body lifts off the code line and flies to `target`, where it
  // becomes the node's own glyph (`*` arrives as `×`) and the node is traced around it.
  const fly = function* (k: number, n: GraphNode, target: P): ThreadGenerator {
    const from = glyphRefs[k]().absolutePosition().transformAsPoint(world().worldToLocal());
    const flying = createRef<Txt>();
    world().add(
      <Txt
        ref={flying}
        text={body[k]}
        fontFamily={MONO}
        fontSize={CODE_SIZE}
        fill={colourOf(body[k])}
        position={from}
        zIndex={5}
      />,
    );
    // A quadratic path: first along the code line, then up or down into the graph, so the glyph
    // never crosses the other lines of code.
    const start = flying().position();
    const progress = createSignal(0);
    flying().position(() => {
      const end = target();
      const control = new Vector2(Vector2.lerp(start, end, 0.6).x, start.y);
      const t = progress();
      return Vector2.lerp(Vector2.lerp(start, control, t), Vector2.lerp(control, end, t), t);
    });
    yield* all(
      progress(1, 0.4, easeInOutCubic),
      flying().fontSize(n.glyph().fontSize(), 0.4, easeInOutCubic),
      flying().fill(CHALK, 0.4),
    );
    flying().remove();
    yield* trace(n);
  };

  const dot = (from: Vector2) => {
    const ref = createRef<Rect>();
    world().add(
      <Rect
        ref={ref}
        size={18}
        radius={9}
        fill={SAND}
        shadowColor={SAND}
        shadowBlur={22}
        position={from}
        zIndex={4}
      />,
    );
    return ref;
  };
  const travel = function* (pairs: [Vector2, Vector2][], duration: number) {
    const dots = pairs.map(([a]) => dot(a));
    yield* all(...dots.map((d, i) => d().position(pairs[i][1], duration, easeInOutCubic)));
    dots.forEach((d) => d().remove());
  };

  // ---- 0.0 - 0.5: header, frame, port and ring -------------------------------------------------
  yield world().scale(1.03, 7, linear);
  yield chain(waitFor(0.3), say('在 Camel 里，你写下的就是图本身。'));
  yield* all(
    code().opacity(1, 0.3),
    frame().end(1, 0.5, easeInOutCubic),
    frameName().opacity(1, 0.3),
    chain(waitFor(0.1), all(trace(port), trace(out))),
  );

  // ---- 0.5 - 3.0: type the body; glyphs fly into the graph ------------------------------------
  const onType: Record<number, () => ThreadGenerator> = {
    1: () => pulse(port),
    3: () => chain(fly(2, mul1, pMul1), stroke(eXM1)),
    5: () => chain(fly(4, two1, pTwo1), stroke(eT1M1)),
    7: () => chain(fly(6, plus, pPlus), all(stroke(eM1P), chain(waitFor(0.1), stroke(ePO)))),
    9: () => pulse(port),
    11: () => chain(fly(10, mul2, pMul2), all(stroke(eXM2), stroke(eM2P))),
    13: () => chain(fly(12, two2, pTwo2), stroke(eT2M2)),
  };
  const blink = yield loop(Infinity, function* () {
    caret(1);
    yield* waitFor(0.42);
    caret(0);
    yield* waitFor(0.42);
  });
  for (let k = 1; k <= body.length; k++) {
    chars[k - 1](body[k - 1]);
    if (onType[k]) yield onType[k]();
    yield* waitFor(0.135);
  }
  cancel(blink);
  caret(0);
  yield* waitFor(0.55); // the last glyph lands and its edge is drawn

  // ---- 3.0 - 3.4: mark the equal halves ---------------------------------------------------------
  const halves = [two1, mul1, two2, mul2];
  yield say('一个值，一个节点。');
  yield* all(
    underA().opacity(1, 0.25),
    underB().opacity(1, 0.25),
    same().opacity(1, 0.25),
    ...halves.map((n) => n.shape().stroke(SAND, 0.25)),
  );
  yield* waitFor(0.15);

  // ---- 3.4 - 4.4: the halves slide together ----------------------------------------------------
  const twoAt = new Vector2(240, -170);
  const mulAt = new Vector2(450, 0);
  yield* all(
    pTwo1(twoAt, 1.0, easeInOutCubic),
    pTwo2(twoAt, 1.0, easeInOutCubic),
    pMul1(mulAt, 1.0, easeInOutCubic),
    pMul2(mulAt, 1.0, easeInOutCubic),
    same().opacity(0, 0.3),
    chain(
      waitFor(0.7),
      all(
        two2.node().opacity(0, 0.3),
        mul2.node().opacity(0, 0.3),
        eXM2().opacity(0, 0.3),
        eT2M2().opacity(0, 0.3),
        underB().opacity(0, 0.3),
        two1.shape().stroke(CHALK, 0.3),
        mul1.shape().stroke(CHALK, 0.3),
      ),
    ),
  );

  // ---- 4.4 - 4.7: one node, used twice ---------------------------------------------------------
  // The stroke that came from the second `×` stays: it is the second use of the same node.
  yield* all(spread(30, 0.3, easeOutCubic), caption().opacity(1, 0.3), underA().opacity(0.5, 0.3));

  // ---- 4.7 - 6.4: x = 3 flows through -----------------------------------------------------------
  port.value().text('3');
  two1.value().text('2');
  mul1.value().text('6');
  plus.value().text('12');
  out.value().text('12');
  yield* all(port.value().opacity(1, 0.2), two1.value().opacity(1, 0.2));

  yield* travel(
    [
      [pX(), pMul1()],
      [pTwo1(), pMul1()],
    ],
    0.45,
  );
  yield* mul1.value().opacity(1, 0.15);

  const apart = new Vector2(0, 15);
  yield* travel(
    [
      [pMul1().sub(apart), pPlus().sub(apart)],
      [pMul1().add(apart), pPlus().add(apart)],
    ],
    0.45,
  );
  yield* plus.value().opacity(1, 0.15);

  yield* travel([[pPlus(), pOut()]], 0.35);
  // The ring is re-traced in mint: the pen goes round once more.
  out.shape().stroke(MINT);
  out.shape().end(0);
  yield* all(out.shape().end(1, 0.3, easeInOutCubic), out.value().opacity(1, 0.15));

  // ---- 6.4 - 7.0: hold -------------------------------------------------------------------------
  yield* waitFor(0.5);
});
