# Promo scripts

Two films. Both have English narration and Chinese subtitles. The visual language is in
[style.md](style.md), and the opening shot is prototyped in [shot01/](shot01/).

| Film | Length | Audience |
|---|---|---|
| **The program is the graph** | 75 s | AI researchers and engineers |
| **Your library can teach the compiler** | 90 s | people who might write a Camel library |

## Creative rationale

Camel's whole case fits in one turn, so the film is built around that turn.

**Act I:** other stacks *recover* the graph from running code, and the copy tears.

**Act II:** in Camel you *write* the graph. Every keystroke is drawn, with the same pen and on the
same navy ground as Camel's own logo (itself a camel drawn as a graph). Glyphs lift off the code
line and become nodes. Everything else then follows as a consequence on that one drawing:

| Consequence | Image |
|---|---|
| order comes from dependency | beads that move together |
| `sync` | a thread stitched through the effects |
| a layer | an operator with its weights docked on top |
| a pass | a sweep line that redraws the graph |
| a gradient | a reflection of the graph |
| placement | a rose region |

**Act III:** the same program meets the real world.

| What happens | Image |
|---|---|
| a missing file | a value on a railway side track that rejoins at one `!!` |
| a bug | a blot that stops the pen, unless `attempt` fences it in |
| a sneaky `println` | tears a `pure` seal |

**Coda:** the camera pulls back, and every graph drawn in the film sits inside one outline, the
Camel logo. Then: *Let the graph speak.*

Each idea gets exactly one image, and the image is reused, never re-explained. The narration is
plain and short (at most 2.3 words per second), and the picture carries the proof. The sequel tells
one story with the same pen: a library's custom operator goes from a black box to glass (a Camel
definition), gets a native kernel without losing its meaning, teaches the compiler with a rule
stencil, is caught by `check` when a law is false, and ships as values and a pass, a function anyone
can open.

All code on screen is Camel as designed (`docs/design-principles.cn.md`). The **Claims and sources**
table at the end gives the source of every claim. Shot 0 shows the problem only abstractly: unnamed
code bars and no product names.

---

# Part 1: "The program is the graph" (75 s)

Canvas 1920×1080 on the navy ground. Code sits on the left behind a gutter rule, and the graph is on
the right 58 %, unless a shot says otherwise.

## Act I · The graph you never had

### Shot 0 · 0:00–0:06 · Recovered

**Idea:** compilers need the graph. When it is recovered from running code, it breaks.

**Picture:**

- Ten unreadable chalk-dim bars stand in for code: no language, no syntax.
- A chalk-dim scan line runs down them, as if the code were being executed and recorded. Nodes are
  traced only where that run went.
- At one bar (a branch on data) the run goes one way. The other arm is never drawn, and its edge ends
  in a frayed, torn stroke. A bar that printed something leaves no node at all.
- The recovered graph hangs there, torn in two places, then greys and crumples to a point, which
  becomes the caret of shot 1.

**Colour:** chalk-dim only, and no accent. This is the one shot without the brand's colour.

**Narration:** "Compilers need the graph. Most languages make them reconstruct it — and the copy breaks."

**Subtitle:** 编译器需要的是图。多数语言让它事后重建——重建出来的图会断。

## Act II · Write the graph

### Shot 1 · 0:06–0:13 · One ink (prototype: `shot01/`)

**Idea:** what you type is the graph; one value is one node.

**Picture:**

- The header `func f(x: int): int {` and the closing `}` are written on the ground. The frame of `f`
  is traced on the right, its port `x` on the frame's left edge and the result ring on its right.
- `x * 2 + x * 2` is typed. Each operator and constant **lifts off the line as a glyph**, flies to
  its place and has its node traced around it. Each edge is one stroke.
- Both halves `x * 2` get a sand underline and a sand outline, with an `=` between them. The lower
  half slides onto the upper one and fades. One `×` (and one `2`) remains, feeding `+` through two
  parallel strokes. The label `one value, one node` appears.
- A sand bead carries x = 3 through: 6, then 12, and the ring is re-traced in mint.

**Colour:** chalk, sand, mint.

**Narration:** "In Camel, you write the graph itself. One value, one node."

**Subtitle:** 在 Camel 里，你写下的就是图本身。一个值，一个节点。

**Code:**

```camel
func f(x: int): int {
    x * 2 + x * 2
}
```

**Out:** the mint ring slides left and becomes the input `x` of shot 2.

### Shot 2 · 0:13–0:22 · Order comes from dependency; `sync` is a thread

**Idea:** nothing waits unless it depends on something. Effects run when needed, and `sync` gives
order exactly where it is written.

**Picture:**

- 0–3.5 s: `x` feeds two `encode` operators, with weights `wa` and `wb` docked on top. Two sand
  beads leave `x` **in the same frame** and arrive together, then converge on `cosine`. There is no
  label: the simultaneity is the claim.
- 3.5–5 s: the header has no `sync` yet. The effect nodes `println` and `write_text` (notched
  operators) stay chalk-dim and receive no bead. A dim label reads `EffectNotExecuted`.
- 5 s: ` sync` is typed into the header. A **thread** is stitched through the notches in written
  order: `println → write_text → return`. The pure nodes `encode` and `cosine` are not stitched.
  The effect nodes light up, and beads pass through them one after the other.

**Colour:** sand, thread.

**Narration:** "Nothing waits unless it depends on something. Effects run when they're needed — sync
threads them in order."

**Subtitle:** 不依赖，就不等待。效果在被需要时才执行——sync 把它们按顺序串起来。

**Code (final):**

```camel
func main(): int sync {
    let a = x->encode<wa>
    let b = x->encode<wb>
    println(cosine(a, b))
    'done'->write_text<'run.log'>
    return 0
}
```

**Out:** the camera dives into one `encode` node, and its inside is shot 3.

### Shot 3 · 0:22–0:30 · A layer is a function with its weights bound

**Idea:** `with` parameters are what a function is configured with, so a model reads as a pipeline.

**Picture:**

- The frame is full width. As each `->` is typed, its operator is traced, left to right:
  `x → linear → relu → linear → ring`.
- The weights `w1, b1` and `w2, b2` descend and **dock on the top** of their `linear` nodes. They are
  not connected by data edges, because they are configuration, not data.
- A bead runs the length of the pipeline. Under each edge the dim shape is written:
  `f32[b, 784] → f32[b, 256] → f32[b, 10]`.

**Colour:** chalk and sand only. The idea needs no accent.

**Narration:** "A layer is a function with its weights bound. The model reads like the pipeline it is."

**Subtitle:** 一层网络，就是绑定了权重的函数。模型读起来，就是它本来的那条管道。

**Code:**

```camel
with <m: Mlp>
func mlp(x: f32[b, 784]): f32[b, 10] {
    x->linear<m.w1, m.b1>
        ->relu
        ->linear<m.w2, m.b2>
}
```

**Out:** the pipeline folds into a `loss` frame that rises to the upper half of the screen.

### Shot 4 · 0:30–0:39 · A gradient is a reflection; a pass is a sweep

**Idea:** differentiation and optimization are functions from graph to graph. The derivative is an
ordinary graph, so the optimizer sees all of it.

**Picture:**

- A strip along the bottom reads like the command line: `std::macro · std::opt · std::jit`.
- 0–3 s, `with_grad`: a horizontal baseline is drawn under `loss`, and the graph is **reflected**
  beneath it in violet, every edge reversed: the reverse pass is the mirror image.
- 3–4 s: beside the baseline, the label flicks through `grad`, `vjp` and `jvp`. For `jvp`, a violet
  twin flashes *above* the line, running the same way. Then it settles on `with_grad`.
- 4–7.5 s: a cyan **sweep line** (`std::opt`) crosses both graphs. Behind it, nodes slide in
  straight lines to their new places. Duplicates shared by the forward graph and its mirror merge,
  and a constant chain folds. A dim counter reads `nodes 46 → 29` (take the real figure from
  `std::stats`).
- 7.5–9 s: a second sweep (`std::jit`), and beads run through both halves as one program.

**Colour:** violet, cyan, sand.

**Narration:** "The gradient is a graph transform, and its result is a graph — so the optimizer sees all
of it."

**Subtitle:** 求导是对图的变换，结果仍是一张图——所以优化器能看到它的全部。

**Code:**

```camel
let step = with_grad(loss)
let (l, g) = step<m>(x, y)
```

**Strip:**

```bash
camel train.cml std::macro std::opt std::jit
```

**Out:** a rose region begins to draw around both graphs.

### Shot 5 · 0:39–0:47 · Say where; ship anywhere

**Idea:** placement is declared and transfers are derived. The same graph goes to other runtimes with
the same meaning.

**Picture:**

- 0–4 s: `#gpu` is typed above the function. A rose region is traced around its nodes. Where edges
  cross the region's boundary, small ↑ and ↓ marks are drawn **by the compiler**: no code line
  lights up for them. Beads inside the region move at twice the speed. The strip shows
  `camel --devices 0 train.cml std::macro tiles::compile std::jit`.
- 4–8 s: the camera pulls back. The graph folds into a bundle and fans into cards (`model.onnx`,
  `model.cpp`), each holding the same drawing. One mint tick spans both cards.

**Colour:** rose, mint.

**Narration:** "Say where it runs; the compiler moves the data. Or ship the same graph as ONNX or C++."

**Subtitle:** 说明在哪里运行，数据搬运由编译器推出来。或者，把同一张图导出成 ONNX 或 C++。

**Code:**

```camel
#gpu
with <m: Mlp>
func forward(x: f32[b, 784]): f32[b, 10] {
    x->mlp<m>->softmax
}
```

**Strip (cards):**

```bash
camel model.cml std::macro std::opt onnx::export out=model.onnx
camel model.cml std::macro std::opt std::cpp
```

**Out:** the cards fold back into one graph. Its input stretches left into a pipe.

### Shot 6 · 0:47–0:52 · Data is a stream

**Idea:** a stream `T..` is a value, an element is computed only when pulled, and a generator
`yield`s it.

**Picture:**

- The training `loop` (a frame with a return arc) **pulls a short thread** from the pipe's mouth.
  One bead comes out, then another.
- `batch(64)` gathers 64 beads into one array badge. Between pulls, nothing in the pipe moves.
- A ghost second consumer reaches for the pipe. Its thread snaps in coral, because a stream is
  consumed once.

**Colour:** sand, one coral snap.

**Narration:** "Data is a stream: generated on demand, one batch per pull."

**Subtitle:** 数据是流：按需生成，拉一次，来一批。

**Code (supporting size):**

```camel
func epochs(xs: f32[?, 784], n: int): f32[784].. {
    for _ in 0..n do for x in xs do yield x
}

let data = xs->epochs(10)->batch(64)
let trained = loop p = p0 for b in data do p->sgd(b)
```

**Out:** the pipe straightens into a rail.

## Act III · When things go wrong

### Shot 7 · 0:52–0:59 · The railway

**Idea:** failure is a value. `->` carries null and errors past the steps that need a value, and the
failure is handled once.

**Picture:**

- Full width. A chalk rail runs through the switch points `read_text`, `decode_as`, `validate` and
  `!!`. A coral side track runs beneath it.
- First run: the bead rides straight through, and `cfg` is written in mint.
- Second run: `read_text` fails. The bead turns coral and **switches onto the side track**. It
  passes under `decode_as` and `validate`, which stay dark, and rejoins at `!!`, where it turns sand
  and carries `defaults`.
- In the code, only `!! defaults` is underlined.

**Colour:** chalk, coral, sand, mint.

**Narration:** "Failure is a value. The arrow carries it aside: write the happy path, handle it once."

**Subtitle:** 失败是一个值。箭头把它带上旁路：只写正常路径，在一处处理。

**Code:**

```camel
let cfg = path
    ->read_text
    ->json::decode_as::<Config>
    ->validate !! defaults
```

**Out:** the rail curves into a `map` that fans out over rows.

### Shot 8 · 0:59–1:04 · A fault, and a fence

**Idea:** a bug is a fault, not an error. It stops the program with a location, unless `attempt(f)`
draws a function boundary around it.

**Picture:**

- 0–2 s: `map` fans out to six `parse_row` calls. Call 4 **blots** coral. Every stroke freezes and
  dims, and a location is stamped beside the blot: `rows.cml:3:31 index out of range`.
- 2–2.4 s: a short reverse scrub. `attempt(` is typed in, and a thin coral **fence** is drawn
  around each call.
- 2.4–5 s: replay. Call 4 blots inside its fence. At the fence the blot becomes a coral bead on a
  small side track, while the other five finish in sand. The result row shows five mint values and
  one coral `!`.

**Colour:** coral, sand, mint.

**Narration:** "A bug is a fault — unless attempt fences it in."

**Subtitle:** bug 是故障，会带着位置停下——除非 attempt 把它隔在函数边界里。

**Code:**

```camel
let rows = lines->map<attempt(parse_row)>
```

**Out:** one fence widens into the frame of shot 9.

### Shot 9 · 1:04–1:09 · The seal

**Idea:** resources are part of the function type. `pure` and `uses` are promises the compiler
checks.

**Picture:**

- A mint **seal** is drawn closed around the frame of `normalize`, with a small `pure` tab.
- `->println` is typed at the end of the body. Its node is traced, and a line labelled `stdout` runs
  from it outward, **piercing the seal**. The seal tears a coral notch at that point, and
  `UsesPromiseViolated` is stamped there.
- Hold.

**Colour:** mint, coral.

**Narration:** "Promises are checked. Say pure, and the compiler holds you to it."

**Subtitle:** 承诺会被检查。写下 pure，编译器就让你兑现。

**Code:**

```camel
func normalize(x: f32[n]): f32[n] pure {
    (x / sum(x))->println
}
```

## Coda

### Shot 10 · 1:09–1:15 · The camel

**Idea:** it was one graph all along.

**Picture:**

- 0–2.5 s: the camera pulls far back. Every graph drawn since shot 1 is still on the canvas, joined
  by the match cuts. From this distance, their outer strokes **lie on the outline of the Camel
  logo**: the frames of shots 3–5 form the body, the rail of shot 7 the neck, and the circle closes
  through the stream pipe.
- 2.5–3.7 s: the inner graphs fade, and the logo outline is re-traced in chalk.
- 3.7 s: `Let the graph speak.` appears and its sand underline is drawn.
- 4.5 s: `pip install camel-lang`.

**Narration:** "Camel. Let the graph speak."

**Subtitle:** Camel。让图说话。

---

# Part 2: "Your library can teach the compiler" (90 s)

The same pen and the same ground, and one story: a library's custom operator, `rms_norm`, followed
from a black box to a teacher.

## Act I · The black box

### L1 · 0:00–0:08 · Opaque

**Idea:** in most stacks, a library's operator is opaque to the compiler.

**Picture:**

- A solid hatched box labelled `custom_op` sits in a user's graph.
- A cyan sweep line crosses the graph. It redraws everything around the box but passes over the box
  without changing it.
- The sweep's strip shows an empty slot labelled `rules: [ ]`.

**Narration:** "Most compilers can't see into your library. In Camel, your library teaches the
compiler."

**Subtitle:** 多数编译器看不进你的库。在 Camel 里，你的库可以教会编译器。

**Out:** the hatching clears line by line, which starts L2.

## Act II · Teach it

### L2 · 0:08–0:19 · Glass

**Idea:** an operation written in Camel is a subgraph the compiler sees through. Differentiation,
fusion and export apply without a rule written for each.

**Picture:**

- As the definition is typed, the box turns to **glass**: a dashed frame with its graph visible
  inside (`×`, `mean`, `+`, `rsqrt`, `×`). `eps` is docked on top.
- The reflection baseline is drawn, and the violet mirror forms beneath **without any rule card
  appearing**.
- A `model.onnx` card prints off with a mint tick.

**Narration:** "Write the operation in Camel, and the box turns to glass: grad, fusion and export see
straight through."

**Subtitle:** 用 Camel 写这个算子，黑盒就变成了玻璃：求导、融合、导出都能直接看穿它。

**Code:**

```camel
with <eps: f32 = 1e-6>
export func rms_norm(x: f32[n, d]): f32[n, d] {
    x * rsqrt(mean(x * x, -1, true) + eps)
}
```

### L3 · 0:19–0:31 · Glass and steel

**Idea:** a Camel definition can carry a native kernel under `#impl`. The body stays the meaning, a
differential test keeps the two in agreement, and only passes that need to look inside open it.

**Picture:**

- `#impl(rms_norm_kernel)` and the body-less kernel declaration are typed.
- The frame is drawn twice, one above the other: glass (the Camel body) and steel (hatched, labelled
  `mylib.cmo`). An `≡` between them pulses with each test sample, and a mint counter reads
  `agree 1000 / 1000`.
- The two collapse into one steel block that rides through the `std::opt` and `std::jit` sweeps as
  a single node.
- The reflection baseline approaches, and only then does the block turn to glass, so the mirror can
  form.

**Narration:** "Need speed? Attach a native kernel. The Camel body stays the meaning, a differential test
keeps them equal, and passes that must look inside still can."

**Subtitle:** 要速度，就挂上原生内核。Camel 函数体仍是语义，差分测试保证两者一致；需要看进去的遍照样看得见。

**Code (supporting size):**

```camel
#impl(rms_norm_kernel)
with <eps: f32 = 1e-6>
export func rms_norm(x: f32[n, d]): f32[n, d] {
    x * rsqrt(mean(x * x, -1, true) + eps)
}

with <eps: f32>
func rms_norm_kernel(x: f32[n, d]): f32[n, d]   // native, in mylib.cmo
```

### L4 · 0:31–0:44 · The stencil

**Idea:** a `rule` is a value of type `Rule`, and a rule set is an ordinary `Rule[]`. Where a user's
graph matches, the library's operation takes its place.

**Picture:**

- A *user's* graph shows the long form of RMS normalization written inline: five nodes.
- A **stencil** card is laid down. Its cut-out is the long form, and its replacement is
  `rms_norm<eps>(x)`. Square brackets close around it and a ghosted second stencil, `[ rms_fuse, … ]`.
- The stencil slides over the user's graph and **glows cyan where it fits**. The sweep line redraws
  that spot: the five nodes contract along straight lines into one steel `rms_norm` block.

**Narration:** "Your expertise ships as rules — values, in an array. Where users write the long form,
your fast op takes its place."

**Subtitle:** 你的专长以规则发布——规则是值，规则集是数组。用户写了长形式的地方，你的快算子就接替它。

**Code:**

```camel
rule rms_fuse(x: f32[n, d], const eps: f32) {
    x * rsqrt(mean(x * x, -1, true) + eps) =>
        rms_norm<eps>(x)
}

export let rules: Rule[] = [rms_fuse, swiglu_fuse]
```

### L5 · 0:44–0:59 · The jam

**Idea:** the compiler is not taught a false law. Every rule passes `check` before admission, a
refuted rule cannot stay in a library, and the refutation carries its counter-example.

**Picture:**

- A new stencil is laid down: `(a + b) + c ⇒ a + (b + c)` on `f32`. Random inputs fall through it as
  small beads.
- 4 s: one bead **jams** the stencil. A coral cross and a felt thud follow. The card flips to show
  two tiny graphs evaluating `1e16, -1e16, 1`: one side gives `1`, the other `0`.
- 9 s: `#requires(reassociate, no_infs)` is typed above the rule. The stencil re-enters, and its
  proposition label changes to `real equality`. Beads flow freely, a mint tick appears, and the
  verdict reads `sampled`.

**Narration:** "And the compiler won't learn a false law. check tries to break every rule — and shows
the input that does. Name the permission it needs, and it passes."

**Subtitle:** 编译器也不会学到错误的定律。check 会试着推翻每一条规则，并给出推翻它的输入。写明它需要的数值许可，它才通过。

**Code:**

```camel
rule regroup(a: f32, b: f32, c: f32) {
    (a + b) + c => a + (b + c)
}

println(check([regroup]))
```

**Report, in the form `check` prints:**

```text
refuted regroup: proposition equality, evidence counterexample,
  the two sides differ at (1e+16, -1e+16, 1): 1 and 0
```

**After the tag:**

```text
sampled regroup: proposition real equality + IEEE on special values, evidence samples 200
```

## Act III · Ship it

### L6 · 0:59–1:08 · Handed over by name

**Idea:** importing a module or writing a rule changes no pass. The user hands the rule set to the
optimizer as a value.

**Picture:**

- `import` is typed, and nothing happens: the `rules: [ ]` slot on the strip stays empty.
- `std::opt<rules: mylib::rules>` is typed, and the fanned stencils slide into the slot.
- The next sweep carries them.

**Narration:** "Importing changes nothing. Users hand your rules to the optimizer, by name."

**Subtitle:** 导入什么也不改变。用户按名字，把你的规则显式地交给优化器。

**Code:**

```camel
let fast = model->std::opt<rules: mylib::rules>
```

**Strip:**

```bash
camel model.cml std::opt rules=mylib::rules std::jit
```

### L7 · 1:08–1:20 · A pass you can open

**Idea:** a pass is a function transformation, `(f: F) => F!`. A library ships a whole pipeline as
one function, used by name on the command line, with errors carried along the same arrow.

**Picture:**

- One sweep is labelled `mylib::compile`. The camera dives into its label, and the sweep opens like
  a function frame. Inside are three smaller sweeps, `std::macro → std::opt<rules> → tiles::compile`,
  with the railway's thin coral side track under them.
- Pull back to the strip.

**Narration:** "And a pass is just a function — from a function to a function. Ship a whole pipeline
under your library's name."

**Subtitle:** 而遍不过是一个函数——从函数到函数。把一整条流水线，以你的库的名字发布。

**Code:**

```camel
export func compile::<F>(f: F): F!
    when is_function::<F>()
{
    f->std::macro
        ->std::opt<rules: rules>
        ->tiles::compile
}
```

**Strip:**

```bash
camel model.cml mylib::compile std::jit
```

### L8 · 1:20–1:30 · Coda

**Picture:**

- Pull back over the glass box, the steel kernel, the stencils and the open pass, joined into one
  drawing that lies on the logo outline.
- The end card follows, as in the main film.

**Narration:** "Types, rules and passes: one language, all the way down. Let the graph speak."

**Subtitle:** 类型、规则和遍：自始至终，同一种语言。让图说话。

---

## Claims and sources

DP = `docs/design-principles.cn.md`; the English translation, `design-principles.en.md`, has the
same section numbers.

| Shot | Claim on screen | Source |
|---|---|---|
| 0 | A graph recovered from code by tracing breaks apart and loses what effects meant | `docs/why.en.md`; README "Why Camel" |
| 1 | What you write is the graph; nothing is traced | README; DP 1 ("the graph is native, not a product") |
| 1 | Equal pure computations become one node | DP 3.2 (pure computation is deterministic; common-subexpression merging relies on it); tutorial ch. 18 (`x * 2 + x * 2`) |
| 2 | Independent work runs in parallel without being asked | README Highlights; DP 2.1 row 1; DP 3.3 (no `async`/`await`) |
| 2 | An effect nothing uses, outside `sync`, does not run (`EffectNotExecuted`) | DP 3.1; `docs/reference/camel-for-ai.md` |
| 2 | `sync` orders effects in written order; different resources have no order without it; pure nodes are off the chain | DP 3.3 (last item), 3.4, 3.1 (edges) |
| 3 | A layer is a function with weights bound; `x->linear<w, b>->relu` | `docs/stdlib-conventions.cn.md` §4; DP 4.4 |
| 4 | `grad`, `with_grad`, `vjp`, `jvp` are function transforms; `grad` differentiates the first `with` parameter | DP 9.9; `docs/handover/stdlib/autodiff.signatures.cml` |
| 4 | The derivative is a graph rewritten at compile time, so later passes optimize it | README "A taste"; `docs/cli.md` (`std::macro` before `grad`) |
| 4 | Passes are graph-to-graph functions, composed explicitly on the command line | DP 9.1, 9.2; ZEN 5 |
| 5 | `#gpu` declares placement; the compiler derives transfers; the device is chosen at run time | DP 13.1, 13.3, 13.4; `docs/cli.md` `--devices` |
| 5 | The same program exports to ONNX and standalone C++ with the same meaning | README; DP 9.8, 10.6 |
| 6 | A stream is a value; an element is computed only when pulled; a stream is consumed once; generators `yield`; `batch` gives `T[k]..` | DP 5.4 |
| 7 | `->` propagates null and errors; handle once with `!!` | DP 3.9; stdlib conventions §4 |
| 8 | A fault terminates with a location; `attempt(f)` turns a fault into an error value at the function boundary; `map<attempt(parse_row)>` | DP 3.9 |
| 9 | `pure` and `uses` are checked promises; violation is `UsesPromiseViolated` | DP 3.3; DP 1, red line 8 |
| 10, L8 | "Let the graph speak."; "One language, all the way down"; `pip install camel-lang` | `ZEN` 1 and 7; README Quick start |
| 10 | The logo is a graph | `assets/logo.svg` (brand asset) |
| L2 | A Camel definition is visible to the compiler: fusion, differentiation, export and rules apply without per-op rules | DP 10.4 |
| L3 | `#impl(f)` names a native kernel; not expanded by default; expanded on demand; a differential test keeps the two in agreement | DP 10.4; `stdlib/array.cml` (`#impl(sum_kernel)`) |
| L4 | A `rule` is a `Rule` value; a rule set is a `Rule[]` | DP 9.11; `stdlib/rules.cml` header |
| L5 | Verdicts `proved`, `refuted`, `sampled`; a refuted rule may not stay in a library; refutations give a counter-example | DP 9.11; DP 1, red line 4; the report form is from `test/plans/feat/modules/rules.plan.toml` |
| L5 | Under `reassociate`, the proposition is real equality on finite values | DP 9.11, 7 |
| L6 | Rule sets are handed to passes explicitly; importing changes nothing; `std::opt<rules: …>` | DP 9.3, 9.11 |
| L7 | A pass is `(f: F) => F!`; a pipeline is function composition used by name; module passes carry the module name | DP 9.1, 9.2, 9.3 |
| L7 | `F!` passing into a pass that requires `is_function::<F>()` carries the error on | DP 3.9 |

## Production notes

- **Real output:** the node count (shot 4), the `check` lines (L5) and the fault location (shot 8)
  are in the form the tools print. Replace them with real output at render time.
- **Timing:** narration is budgeted at 2.3 words per second. If the voice runs slow, shot 4 and L3
  are the first to stretch. Take the time back from the hold in shot 10, never from shot 1.
- **The logo coda needs layout planning:** the graphs of the main film are placed on the canvas so
  that their outer frames trace the logo's outline (`assets/logo.svg`, viewBox 121×121). Lay out the
  whole canvas once, from the logo backwards, before animating any shot after shot 1.
