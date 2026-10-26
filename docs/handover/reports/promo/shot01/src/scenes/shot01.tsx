/**
 * Shot 01 (7 s): "The program is the graph."
 *
 * The body of `f` is typed on the left while its graph grows inside the frame of `f` on the
 * right; the two equal halves of `x * 2 + x * 2` are marked and collapse into one node, used
 * twice; then x = 3 flows through. Script: ../../script.md, shot 1. Style: ../../style.md.
 *
 * Timeline (seconds)
 *   0.0 - 0.5   panel, header line and the frame of `f` fade in; port `x` and the result ring pop
 *   0.5 - 2.3   `x * 2 + x * 2` is typed; each token pops its node, each edge draws after it
 *   2.5 - 3.0   the equal halves are marked (sand underline, sand outline, "=")
 *   3.0 - 4.1   the lower half moves onto the upper one and fades; the graph re-centres
 *   4.1 - 4.4   the two edges into `+` separate: one node, used twice
 *   4.4 - 6.2   x = 3 flows through: 3 * 2 = 6, 6 + 6 = 12
 *   6.2 - 7.0   hold (a slow 3 % push-in runs under the whole shot)
 */
import {Line, Node, Rect, Txt, makeScene2D} from '@motion-canvas/2d';
import {
  Reference,
  ThreadGenerator,
  Vector2,
  all,
  chain,
  createRef,
  createSignal,
  easeInOutCubic,
  easeOutBack,
  easeOutCubic,
  linear,
  loop,
  sequence,
  waitFor,
} from '@motion-canvas/core';

// Show the Chinese subtitle in the preview; the final edit burns subtitles in separately.
const SHOW_SUBTITLE = true;

// ---- palette (style.md 1) -----------------------------------------------------------------
const INK = '#0D1117';
const PANEL = '#141B26';
const LINE = '#2A3445';
const TEXT = '#E6EDF3';
const DIM = '#7D8CA3';
const KEYWORD = '#B7C4D8';
const STEEL = '#6C86B0';
const SAND = '#E0A94B';
const MINT = '#6EE7A8';

const MONO = 'JetBrains Mono, Cascadia Mono, Consolas, monospace';
const SANS = 'Inter, Segoe UI, Noto Sans, sans-serif';
const CJK = 'Noto Sans SC, Microsoft YaHei, PingFang SC, sans-serif';

// ---- geometry (scene coordinates, origin at the centre of 1920x1080) -----------------------
const R = 36; // half size of every node (72 px)
const FRAME = {x: 450, width: 860, height: 560}; // the frame of `f`
const LEFT = FRAME.x - FRAME.width / 2; // port `x` sits on this edge
const RIGHT = FRAME.x + FRAME.width / 2; // the result ring sits on this edge
const CODE_SIZE = 52;

type P = ReturnType<typeof Vector2.createSignal>;
const at = (x: number, y: number): P => Vector2.createSignal(new Vector2(x, y));

interface GraphNode {
  node: Reference<Node>;
  shape: Reference<Rect>;
  value: Reference<Txt>;
}

export default makeScene2D(function* (view) {
  view.fill(INK);

  // Everything lives in `world`, so the slow push-in is one scale tween.
  const world = createRef<Node>();
  view.add(<Node ref={world} />);

  // ---- code panel ----------------------------------------------------------------------------
  const panel = createRef<Rect>();
  const caret = createSignal(0);
  const underA = createRef<Rect>();
  const underB = createRef<Rect>();

  // One Txt per character of the body, so that digits and operators keep their colour.
  const body = 'x * 2 + x * 2';
  const chars = [...body].map(() => createSignal(''));
  const colourOf = (c: string) => (/[0-9]/.test(c) ? MINT : c === 'x' ? TEXT : DIM);
  const glyph = (k: number) => (
    <Txt
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

  world().add(
    <Rect
      ref={panel}
      x={-520}
      width={780}
      height={360}
      radius={22}
      fill={PANEL}
      stroke={LINE}
      lineWidth={2}
      opacity={0}
      layout
      direction="column"
      justifyContent="center"
      alignItems="start"
      padding={[0, 56]}
      gap={10}
    >
      <Txt
        layout={false}
        text="f.cml"
        fontFamily={MONO}
        fontSize={22}
        fill={DIM}
        x={-390 + 56}
        y={-180 + 34}
        offsetX={-1}
      />
      <Rect layout direction="row">
        {word('func', KEYWORD)}
        {word(' f(x: ', TEXT)}
        {word('int', KEYWORD)}
        {word('): ', TEXT)}
        {word('int', KEYWORD)}
        {word(' {', TEXT)}
      </Rect>
      <Rect layout direction="row" alignItems="start">
        {word('    ', TEXT)}
        {half(0, 5, underA)}
        <Rect layout direction="row">{[5, 6, 7].map(glyph)}</Rect>
        {half(8, 13, underB)}
        <Rect width={5} height={CODE_SIZE} marginLeft={4} radius={2} fill={SAND} opacity={caret} />
      </Rect>
      {word('}', TEXT)}
    </Rect>,
  );

  // ---- the frame of `f` ------------------------------------------------------------------------
  const frame = createRef<Rect>();
  world().add(
    <Rect
      ref={frame}
      x={FRAME.x}
      width={FRAME.width}
      height={FRAME.height}
      radius={28}
      stroke={STEEL}
      lineWidth={2}
      lineDash={[14, 10]}
      opacity={0}
    >
      <Txt
        text="f"
        fontFamily={MONO}
        fontSize={28}
        fill={DIM}
        x={-FRAME.width / 2 + 34}
        y={-FRAME.height / 2 + 34}
      />
    </Rect>,
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

  // Edges are added before nodes so that each node covers the ends of its lines.
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
        stroke={STEEL}
        lineWidth={5}
        lineCap="round"
        endArrow
        arrowSize={11}
        end={0}
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

  const addNode = (pos: P, text: string, round: boolean, fill = PANEL): GraphNode => {
    const node = createRef<Node>();
    const shape = createRef<Rect>();
    const value = createRef<Txt>();
    world().add(
      <Node ref={node} position={pos} scale={0} opacity={0} zIndex={2}>
        <Rect
          ref={shape}
          size={R * 2}
          radius={round ? R : 16}
          fill={fill}
          stroke={STEEL}
          lineWidth={4}
        />
        <Txt text={text} fontFamily={MONO} fontSize={round ? 38 : 44} fill={TEXT} />
        <Txt
          ref={value}
          text=""
          fontFamily={MONO}
          fontSize={32}
          fill={MINT}
          y={R + 32}
          opacity={0}
        />
      </Node>,
    );
    return {node, shape, value};
  };

  const port = addNode(pX, 'x', true);
  const two1 = addNode(pTwo1, '2', true);
  const two2 = addNode(pTwo2, '2', true);
  const mul1 = addNode(pMul1, '×', false);
  const mul2 = addNode(pMul2, '×', false);
  const plus = addNode(pPlus, '+', false);
  const out = addNode(pOut, '', true, INK); // the result: a hollow ring

  const same = createRef<Txt>();
  const caption = createRef<Txt>();
  world().add(
      <Txt
        ref={same}
        text="="
        fontFamily={MONO}
        fontSize={56}
        fill={SAND}
        x={320}
        y={0}
        opacity={0}
        zIndex={3}
      />,
  );
  world().add(
      <Txt
        ref={caption}
        text="one value, one node"
        fontFamily={SANS}
        fontSize={28}
        fill={DIM}
        x={FRAME.x}
        y={FRAME.height / 2 + 44}
        opacity={0}
      />,
  );

  // ---- subtitle (preview only; outside `world`, so it does not take part in the push-in) -----
  const subtitle = createRef<Txt>();
  view.add(
    <Txt
      ref={subtitle}
      text=""
      fontFamily={CJK}
      fontWeight={500}
      fontSize={40}
      fill={TEXT}
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

  // ---- helpers ---------------------------------------------------------------------------------
  const pop = function* (n: GraphNode) {
    n.node().opacity(1);
    yield* n.node().scale(1, 0.35, easeOutBack);
  };
  const draw = function* (e: Reference<Line>) {
    yield* e().end(1, 0.3, easeOutCubic);
  };
  const pulse = function* (n: GraphNode) {
    yield* all(
      n.node().scale(1.15, 0.12).to(1, 0.18),
      n.shape().stroke(SAND, 0.12).to(STEEL, 0.25),
    );
  };
  const dot = (from: Vector2) => {
    const ref = createRef<Rect>();
    world().add(
      <Rect
        ref={ref}
        size={20}
        radius={10}
        fill={SAND}
        shadowColor={SAND}
        shadowBlur={24}
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

  // ---- 0.0 - 0.5: panel, header, frame ---------------------------------------------------------
  yield world().scale(1.03, 7, linear);
  yield loop(Infinity, function* () {
    caret(1);
    yield* waitFor(0.42);
    caret(0);
    yield* waitFor(0.42);
  });
  yield chain(waitFor(0.3), say('在 Camel 里，程序就是图。'));
  yield* all(
    panel().opacity(1, 0.4),
    frame().opacity(1, 0.4),
    sequence(0.1, pop(port), pop(out)),
  );
  yield* waitFor(0.1);

  // ---- 0.5 - 2.3: type the body, grow the graph ------------------------------------------------
  const onType: Record<number, () => ThreadGenerator> = {
    1: () => pulse(port),
    3: () => all(pop(mul1), draw(eXM1)),
    5: () => all(pop(two1), draw(eT1M1)),
    7: () => all(pop(plus), draw(eM1P), chain(waitFor(0.15), draw(ePO))),
    9: () => pulse(port),
    11: () => all(pop(mul2), draw(eXM2), draw(eM2P)),
    13: () => all(pop(two2), draw(eT2M2)),
  };
  for (let k = 1; k <= body.length; k++) {
    chars[k - 1](body[k - 1]);
    if (onType[k]) yield onType[k]();
    yield* waitFor(0.135);
  }
  yield* waitFor(0.25);

  // ---- 2.5 - 3.0: mark the equal halves --------------------------------------------------------
  const halves = [two1, mul1, two2, mul2];
  yield say('同一个值写两遍，编译器只看到一次。');
  yield* all(
    underA().opacity(1, 0.25),
    underB().opacity(1, 0.25),
    same().opacity(1, 0.25),
    ...halves.map((n) => n.shape().stroke(SAND, 0.25)),
  );
  yield* all(...halves.map((n) => n.node().scale(1.1, 0.12).to(1, 0.13)));

  // ---- 3.0 - 4.1: collapse -------------------------------------------------------------------
  const twoAt = new Vector2(240, -170);
  const mulAt = new Vector2(450, 0);
  yield* all(
    pTwo1(twoAt, 1.1, easeInOutCubic),
    pTwo2(twoAt, 1.1, easeInOutCubic),
    pMul1(mulAt, 1.1, easeInOutCubic),
    pMul2(mulAt, 1.1, easeInOutCubic),
    same().opacity(0, 0.3),
    chain(
      waitFor(0.8),
      all(
        two2.node().opacity(0, 0.3),
        mul2.node().opacity(0, 0.3),
        eXM2().opacity(0, 0.3),
        eT2M2().opacity(0, 0.3),
        underB().opacity(0, 0.3),
        two1.shape().stroke(STEEL, 0.3),
        mul1.shape().stroke(STEEL, 0.3),
      ),
    ),
  );

  // ---- 4.1 - 4.4: one node, used twice -------------------------------------------------------
  // The edge that came from the second `×` stays: it is the second use of the same node.
  yield* all(spread(18, 0.3, easeOutCubic), caption().opacity(1, 0.3), underA().opacity(0.5, 0.3));

  // ---- 4.4 - 6.2: x = 3 flows through --------------------------------------------------------
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

  const lift = new Vector2(0, 9);
  yield* travel(
    [
      [pMul1().sub(lift), pPlus().sub(lift)],
      [pMul1().add(lift), pPlus().add(lift)],
    ],
    0.45,
  );
  yield* plus.value().opacity(1, 0.15);

  yield* travel([[pPlus(), pOut()]], 0.35);
  yield* all(out.value().opacity(1, 0.15), out.shape().stroke(MINT, 0.25));

  // ---- 6.2 - 7.0: hold -----------------------------------------------------------------------
  yield* waitFor(0.8);
});
