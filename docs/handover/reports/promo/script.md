# Promo scripts

Two films:

- **Main film, "The program is the graph"**: 75 s, for AI researchers and engineers.
- **Sequel, "Your library can teach the compiler"**: 90 s, for people who might write a Camel
  library.

Both have English narration and Chinese subtitles. The visual vocabulary is in [style.md](style.md),
and the opening shot is prototyped in [shot01/](shot01/).

## Creative rationale

Camel's claim is that the program *is* the graph. So the film never leaves the graph: it is one
continuous canvas, and every shot is a match cut from the last shape of the previous one. The closing
zoom-out shows that all the shots were parts of one picture. Each idea is shown as something
happening to the graph, not described next to it:

| Idea | What the viewer sees |
|---|---|
| Typing | grows nodes |
| Equality | merges them |
| Independence | lets two value dots run at once |
| `sync` | the only place dashed order lines appear |
| A pass | a gate the graph passes through and comes out of rearranged, not replaced |
| An error | a value that rolls onto a side track and rejoins at one `!!` |
| A fault | freezes the frame, unless an `attempt` boundary catches it |
| A promise (`pure`) | a shield that cracks |

Colour is reserved for meaning (sand = the value now, cyan = passes, violet = derivatives, coral =
failure, cream = order, rose = device, mint = right and computed), so after the first minute the
viewer reads the frame without narration. The narration says less than the picture: one plain
sentence per idea, about 2.3 words per second. That leaves room to look, which is what this audience
wants.

The sequel reuses the same canvas and shapes, now from the library author's side. The opaque
third-party box of other frameworks becomes, in Camel, a definition the compiler sees through, a
kernel attached to it, rules that are values, a checker that answers with a counter-example, and a
pass that is just a function.

**About the code on screen:** all code is written to the language as designed (`docs/design-principles.cn.md`),
not to what runs today. Numbers shown as results of a pass (node counts, report lines) are given in
the form the tools print. Before final render, they should be replaced with the real output of
`std::stats` and `check` once those paths exist.

---

# Part 1: "The program is the graph" (75 s)

Canvas 1920×1080. The left 40 % is the code panel and the right 60 % is the graph, unless a shot says
otherwise.

### Shot 1 · 0:00.0–0:07.0 · The program is the graph (prototype: `shot01/`)

**Idea:** what you type is the graph; one value is one node.

**Layout and motion:**

- 0.0 s: the panel fades in. Its header line `func f(x: int): int {` and closing `}` are already
  there. The function frame `f` (dashed steel) fades in on the right, and its port `x` pops on the
  frame's left edge while the result ring pops on its right edge.
- 0.5–2.3 s: the body `x * 2 + x * 2` is typed. Each finished token pops its node (`x` lights the
  port, `*` pops a `×`, `2` pops a constant), and each edge draws after its node.
- 2.5 s: both `x * 2` get a sand underline in the code, and their nodes take a sand outline, with
  an `=` between them.
- 3.0–4.1 s: the lower half slides onto the upper half (easeInOutCubic) and fades. The `2`s merge
  too. One `×` is left, and it re-centres.
- 4.1 s: the two edges into `+` separate into a parallel pair (one node, used twice). The label
  `one value, one node` appears in dim.
- 4.4–6.2 s: x = 3 flows through as sand dots: `×` shows 6, `+` shows 12, and the result ring turns
  mint with 12.
- 6.2–7.0 s: hold, with a slow 3 % push-in.

**Colour:** steel graph on ink, sand for equality and the travelling value, mint for computed values.

**Narration:** "In Camel, the program is the graph. Write a value twice — the compiler sees it once."

**Subtitle:** 在 Camel 里，程序就是图。同一个值写两遍，编译器只看到一次。

**Code:**

```camel
func f(x: int): int {
    x * 2 + x * 2
}
```

**Match cut out:** the mint result ring slides left and becomes the input `x` of shot 2. The panel
text cross-types to the next snippet: old lines erase backwards, new lines type.

### Shot 2 · 0:07–0:13 · Parallel without being asked

**Idea:** order comes from dependencies, so independent work runs in parallel without being asked.

**Layout and motion:**

- The input `x` (an array badge, `f32[784]`) fans out to two `encode` operators. Each has a small
  weight circle docked on top (`wa`, `wb`). Both feed `cosine`, then the result ring.
- Two sand dots leave `x` in the same frame and arrive at the two `encode` nodes together, at
  exactly the same speed. They then converge on `cosine`.
- No label says "parallel". A faint dim timeline under the graph shows two bars overlapping in time,
  drawn as the dots move.

**Colour:** steel and sand; weights are small panel circles with dim glyphs.

**Narration:** "Nothing here says which comes first. Independent work runs side by side."

**Subtitle:** 这里没有写谁先谁后。互不依赖的计算，自己就会并行。

**Code:**

```camel
func similar(x: f32[784]): f32 {
    let a = x->encode<wa>
    let b = x->encode<wb>
    cosine(a, b)
}
```

**Match cut out:** the `cosine` result becomes the `report` value in shot 3. The camera pans left a
little.

### Shot 3 · 0:13–0:21 · Effects run when needed; `sync` orders them

**Idea:** an effect is part of the graph and runs when it is needed. Order is written only where it
matters.

**Layout and motion:**

- The panel shows the code below but **without** `sync` in the header.
- On the graph, `evaluate → report` is lit. `write_text` and `println` (effect nodes with a cream
  corner notch) sit dim at 40 %, and nothing reaches them. A dim label beside them reads
  `EffectNotExecuted`.
- 3.0 s: ` sync` types into the header. A pale cream bracket encloses the body, and the dashed cream
  order edges draw in written order: `write_text ⇢ println ⇢ return`. The effect nodes light up.
- Dots run: `report` feeds `write_text`, and then `println` fires after it.
- `evaluate` (pure) stays outside the dashed chain, with no dashes on it.

**Colour:** cream for order, dim for not-run, steel and sand otherwise.

**Narration:** "Effects run when something needs them. Where order matters, write sync — once, exactly
there."

**Subtitle:** 效果在被需要时才执行。需要顺序的地方写 sync——只写一次，只写在那里。

**Code (final state):**

```camel
func main(): int sync {
    let report = evaluate(model, data)
    report->write_text<'eval.txt'>
    println('saved')
    return 0
}
```

**Match cut out:** the camera dives into the `evaluate` call node. It opens (a frame zoom) and the
`mlp` inside becomes shot 4.

### Shot 4 · 0:21–0:29 · A layer is a function with its weights bound

**Idea:** `with` parameters are what a function is configured with. A model is a pipeline of
configured functions.

**Layout and motion:**

- A full-width horizontal pipeline: `x` (badge `f32[b, 784]`) → `linear` → `relu` → `linear` →
  result (`f32[b, 10]`).
- As each `->` is typed, its operator slides in from the right. The weight circles `w1, b1` and
  `w2, b2` drop from above and dock onto the top edges of the `linear` nodes, with no data edge.
- A sand dot runs the whole pipeline once, left to right.
- The dim shape labels under the edges update: `f32[b, 784] → f32[b, 256] → f32[b, 10]`.

**Colour:** steel and sand; weights are panel circles. No accent colour, so the shape of the idea
carries it.

**Narration:** "A layer is a function with its weights bound. A model reads as the pipeline it is."

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

**Match cut out:** the pipeline shrinks into the `loss` frame, which slides toward a cyan gate.

### Shot 5 · 0:29–0:39 · Passes are functions over the graph

**Idea:** differentiation and optimization are graph-to-graph functions. The optimizer sees the
derivative as an ordinary graph.

**Layout and motion:**

- The left panel shows the two-line code. At the bottom, a cyan rule labelled as the command line
  carries three gates: `std::macro → std::opt → std::jit`.
- 0–3 s: the `loss` graph enters the `grad` gate (inside `std::macro`). It exits with a violet
  mirror image below it, edges reversed, joined by faint violet ticks.
- 3–4.5 s: the gate's label cycles once through `grad`, `vjp` and `jvp`. For `jvp`, the violet twin
  runs forward instead. Then it settles back on `with_grad`.
- 4.5–8 s: both graphs pass through `std::opt`. Duplicate nodes (shared between forward and reverse)
  merge, and a constant chain folds. Nodes move along straight lines to their new places. A dim
  counter in the corner reads `nodes 46 → 29` (to be taken from `std::stats` at render time).
- 8–10 s: they enter `std::jit`, and a sand dot runs through both graphs as one program.

**Colour:** cyan gates, violet derivative, sand value.

**Narration:** "Gradients are graph transforms: grad, vjp, jvp. So is the optimizer — and it sees the
whole derivative, not a trace."

**Subtitle:** 求导是图变换：grad、vjp、jvp。优化器也是——它看到的是完整的导数图，而不是一段追踪记录。

**Code:**

```camel
let step = with_grad(loss)
let (l, g) = step<m>(x, y)
```

**Command line (bottom strip):**

```bash
camel train.cml std::macro std::opt std::jit
```

**Match cut out:** a rose region draws around the forward and reverse graphs, which leads into
shot 6.

### Shot 6 · 0:39–0:47 · One program, many destinations

**Idea:** placement is declared and the transfers are derived. The same graph exports to other
runtimes with the same meaning.

**Layout and motion:**

- 0–4 s: `#gpu` types above the function header in the panel. A rose region draws around the
  function's nodes. Where edges cross the boundary, small dim ↑ (upload) and ↓ (download) markers
  appear: the compiler put them there. Dots inside the region move at twice the speed. A dim strip
  at the bottom shows `camel --devices 0 train.cml std::macro tiles::compile std::jit`.
- 4–8 s: the camera pulls back. The graph folds into a thin bundle and fans out into three cards,
  each holding the same graph thumbnail: `model.onnx`, `model.cpp` (`std::cpp`) and `jit`. One mint
  tick spans all three. Two more command lines appear under the cards.

**Colour:** rose region, dim markers, mint tick.

**Narration:** "Mark it for the GPU, and the compiler works out the transfers. The same graph ships as
ONNX or C++."

**Subtitle:** 标上 #gpu，数据搬运由编译器推出来。同一张图，可以导出成 ONNX，也可以是 C++。

**Code:**

```camel
#gpu
with <m: Mlp>
func forward(x: f32[b, 784]): f32[b, 10] {
    x->mlp<m>->softmax
}
```

**Command lines (bottom strip):**

```bash
camel model.cml std::macro std::opt onnx::export out=model.onnx
camel model.cml std::macro std::opt std::cpp
```

**Match cut out:** the cards fold back into one graph. Its input port stretches left into a pipe,
the stream of shot 7.

### Shot 7 · 0:47–0:53 · Streams and generators

**Idea:** a stream `T..` is a value, and an element is computed only when pulled.

**Layout and motion:**

- A horizontal pipe enters from the left. Inside it sit the generator `epochs` (a small step block)
  and its state dot.
- The training `loop` (a frame with a return arrow) pulls once per round. Each pull draws one sand
  element out of the pipe, through `batch(64)`, which groups 64 dots into one array badge, and into
  the loop.
- Between pulls, nothing inside the pipe moves.
- A ghost second consumer reaches for the same pipe. A coral cross appears and the ghost fades,
  because a stream is consumed once.

**Colour:** sand elements, steel pipe, one coral cross.

**Narration:** "Streams are values. A generator yields one element per pull — and only when pulled."

**Subtitle:** 流也是值。生成器每被拉取一次，才产出一个元素。

**Code:**

```camel
func epochs(xs: f32[?, 784], n: int): f32[784].. {
    for _ in 0..n do for x in xs do yield x
}

let data = xs->epochs(10)->batch(64)
let trained = loop p = p0 for b in data do p->sgd(b)
```

**Match cut out:** the pipe straightens into the steel rail of shot 8.

### Shot 8 · 0:53–1:00 · Failure is a value: the railway

**Idea:** `->` propagates null and errors. You write only the happy path and handle the failure once.

**Layout and motion:**

- Full width. A steel main rail carries the steps `read_text`, `decode_as`, `validate` and then
  `!!`, each drawn as a switch point. A thinner coral side track runs parallel below.
- First run: a sand dot travels the main rail from end to end, and the result is a mint `cfg`.
- Second run: `read_text` flashes coral (the file is missing). The dot turns coral and slides onto
  the side track. `decode_as` and `validate` dim to 40 % as it passes beneath them without entering.
  At `!!` the side track merges back, the dot turns sand, and the result is `defaults` in mint.
- The code panel underlines the single `!! defaults` in sand.

**Colour:** steel, coral side track, sand, mint.

**Narration:** "Failure is a value. The arrow carries it aside — write the happy path, handle it once."

**Subtitle:** 失败是一个值。箭头把它送上旁路——你只写正常路径，在末尾处理一次。

**Code:**

```camel
let cfg = path
    ->read_text
    ->json::decode_as::<Config>
    ->validate !! defaults
```

**Match cut out:** the rail curves into a `map` node over many rows, which leads into shot 9.

### Shot 9 · 1:00–1:06 · Faults vs errors; `attempt` at a function boundary

**Idea:** a bug, such as an index out of range, is a fault and not an error. It stops the program
with a location. `attempt(f)` turns a fault into an error value at the function boundary.

**Layout and motion:**

- 0–2.5 s: a `map` fans out to 6 element calls of `parse_row`. Element 4 flashes coral. The canvas
  freezes, everything else dims to 25 %, and a coral monospace label appears beside it:
  `rows.cml:3:31  index 3 out of range`.
- 2.5 s: the frame rewinds (a short reverse scrub, 0.4 s). `attempt(` types into the code. A thin
  coral frame draws around each element's call.
- 3–6 s: replay. Element 4 flashes inside its frame, and at the frame edge the flash becomes a
  coral dot on a tiny side track. The other five finish in sand, and the result row shows five mint
  values and one coral `!`.

**Colour:** coral fault, sand values, mint results.

**Narration:** "A bug is a fault — it stops, with a location. Unless attempt draws a boundary."

**Subtitle:** bug 是故障：程序停下，并给出位置——除非用 attempt 在函数边界把它隔离。

**Code:**

```camel
let rows = lines->map<attempt(parse_row)>
```

**Match cut out:** one element's coral frame grows and becomes the function frame of shot 10.

### Shot 10 · 1:06–1:11 · Promises the compiler checks

**Idea:** resources are part of the function type. `pure` and `uses` are promises the compiler
checks.

**Layout and motion:**

- The function frame `normalize` carries a mint shield on top labelled `pure`.
- `->println` types at the end of the body line. The `println` node pops, and its outline turns
  coral. A coral line runs from it to the shield, which cracks.
- The label `UsesPromiseViolated · println uses stdout` appears in coral monospace beside it.
- Hold 1 s.

**Colour:** mint shield, coral violation.

**Narration:** "Promises are checked: say pure, and the compiler holds you to it."

**Subtitle:** 承诺会被检查：写下 pure，编译器就让你兑现。

**Code:**

```camel
func normalize(x: f32[n]): f32[n] pure {
    (x / sum(x))->println
}
```

### Shot 11 · 1:11–1:15 · End

**Idea:** it was one graph all along.

**Layout and motion:**

- 0–2 s: the camera pulls far back. Every graph from shots 1–10 is still on the canvas, connected
  by the match cuts, as one constellation. Their colours are the film's vocabulary, all visible at
  once.
- 2 s: the constellation dims to 15 %. **Let the graph speak.** appears (Inter Semibold 96 px)
  with its sand underline drawn left to right.
- 3.2 s: `pip install camel-lang` appears (monospace 44 px, dim).

**Narration:** "Camel. Let the graph speak."

**Subtitle:** Camel。让图说话。

---

# Part 2: "Your library can teach the compiler" (90 s)

The same canvas and the same vocabulary, seen by a library author. The running example is one
operation, `rms_norm`, followed from definition to kernel to rule to pass.

### L1 · 0:00–0:08 · The hook

**Idea:** in most stacks, a library's knowledge is invisible to the compiler. In Camel it is code the
compiler reads.

**Layout and motion:**

- An opaque grey box labelled `custom_op` drifts through a cyan `std::opt` gate. Nothing changes:
  the gate cannot see in.
- The gate turns toward the camera, showing an empty slot on its side labelled `rules: [ ]`.
- The grey box dissolves into a dashed frame (a Camel function), the start of L2.

**Colour:** grey (dim), cyan.

**Narration:** "Most compilers can't learn what your library knows. In Camel, your library can teach
it."

**Subtitle:** 大多数编译器学不会你的库所知道的东西。在 Camel 里，你的库可以教会它。

### L2 · 0:08–0:19 · Write the operation in Camel

**Idea:** a definition written in Camel is a subgraph the compiler sees through. Differentiation,
fusion and export apply without a rule written for each.

**Layout and motion:**

- The code types on the left. On the right, the `rms_norm` frame fills with its graph: `×`, `mean`,
  `+ eps`, `rsqrt`, `×`. `eps` is a weight-style circle docked on top, because it is a `with`
  parameter.
- 5 s: the frame passes through a `grad` gate and comes out with its violet mirror. No rule card
  appears, because none was needed.
- 8 s: a small `model.onnx` card is printed off the graph, with a mint tick.

**Colour:** steel, violet, mint.

**Narration:** "Write your operation in Camel. The compiler sees through it, so grad, fusion and export
just work — no extra rules."

**Subtitle:** 用 Camel 写你的算子。编译器看得穿它，所以求导、融合、导出直接可用——不必另写规则。

**Code:**

```camel
with <eps: f32 = 1e-6>
export func rms_norm(x: f32[n, d]): f32[n, d] {
    x * rsqrt(mean(x * x, -1, true) + eps)
}
```

### L3 · 0:19–0:31 · A native kernel under `#impl`, kept in agreement

**Idea:** a definition can be backed by a native kernel. The Camel body stays the meaning, a
differential test keeps the two equal, and the definition is expanded only for passes that need to
look inside.

**Layout and motion:**

- `#impl(rms_norm_kernel)` types above the definition, and the body-less kernel declaration types
  below it.
- The graph splits into two lanes. The top lane is the open Camel body. The bottom lane is a solid
  panel block with a chip glyph, labelled `mylib.cmo`. An `≡` between them pulses with each test
  sample, and a mint counter ticks: `agree 1000 / 1000`.
- 6 s: the lanes close into a single solid `rms_norm` block that rides through `std::opt` and
  `std::jit` as one node, fast.
- 9 s: a `grad` gate approaches. The block opens on demand to reveal the Camel body again, and the
  violet mirror forms.

**Colour:** steel, panel block, mint counter, violet.

**Narration:** "When speed matters, attach a native kernel. The Camel body stays the meaning; a
differential test keeps the two in agreement. Passes that need to look inside still can."

**Subtitle:** 需要速度时，挂上原生内核。Camel 函数体仍是语义本身，差分测试让两者保持一致；需要看进去的遍照样能看。

**Code:**

```camel
#impl(rms_norm_kernel)
with <eps: f32 = 1e-6>
export func rms_norm(x: f32[n, d]): f32[n, d] {
    x * rsqrt(mean(x * x, -1, true) + eps)
}

with <eps: f32>
func rms_norm_kernel(x: f32[n, d]): f32[n, d]   // native, in mylib.cmo
```

### L4 · 0:31–0:45 · Rules are values a library ships

**Idea:** a `rule` is a value of type `Rule`, and a rule set is an ordinary `Rule[]` a module
exports. Where a user's graph matches the pattern, the library's operation takes its place.

**Layout and motion:**

- The right side shows a *user's* model graph in which the long form of RMS normalization is
  written out inline (5 nodes).
- A cyan rule card is dealt in: mini-pattern `x * rsqrt(mean(x*x)+eps)` ⇒ `rms_norm<eps>(x)`.
  Square brackets draw around it and a second, ghosted card (`…`): `[ rms_fuse, … ]`, labelled
  `mylib::rules : Rule[]`.
- 7 s: the card's pattern finds its match in the user graph and rings it in cyan for 0.3 s. The
  five nodes contract along straight lines into one solid `rms_norm` block (with its kernel chip).

**Colour:** cyan card and match, steel graph, sand on the replaced block for 0.5 s.

**Narration:** "Your knowledge ships as rules. A rule is a value; a rule set is an array. Where users
write the long form, your fast one takes its place."

**Subtitle:** 你的知识以规则的形式发布。规则是值，规则集就是数组。用户写了长形式的地方，换成你的快形式。

**Code:**

```camel
rule rms_fuse(x: f32[n, d], const eps: f32) {
    x * rsqrt(mean(x * x, -1, true) + eps) =>
        rms_norm<eps>(x)
}

export let rules: Rule[] = [rms_fuse, swiglu_fuse]
```

### L5 · 0:45–0:59 · The checker refuses a wrong law, with a counter-example

**Idea:** every rule passes `check` before it is admitted, and a refuted rule cannot stay in a
library. Verdicts are `proved`, `refuted` and `sampled`, and a refutation carries its
counter-example.

**Layout and motion:**

- A new card is dealt: `(a + b) + c ⇒ a + (b + c)` for `f32`. The card runs through a narrow mint
  frame labelled `check`, with dice-like sample dots streaming through.
- 4 s: one sample stops the stream. A coral cross lands on the card, a low thud sounds, and the card
  flips to show the counter-example in coral monospace. The two sides are drawn as two tiny graphs
  evaluating `1e16`, `-1e16`, `1` to `1` and to `0`.
- 9 s: `#requires(reassociate, no_infs)` types above the rule. The card re-enters `check`, the
  proposition label changes to `real equality`, and it exits with a mint tick and `sampled`.

**Colour:** coral refusal, mint pass, cyan card.

**Narration:** "Before a law ships, check tries to break it. This one is refuted — with the input that
breaks it. Name the permission it needs, and it passes."

**Subtitle:** 规则发布之前，check 会先试着推翻它。这一条被驳回——附上推翻它的输入。写明它需要的数值许可，它才通过。

**Code:**

```camel
rule regroup(a: f32, b: f32, c: f32) {
    (a + b) + c => a + (b + c)
}

println(check([regroup]))
```

**On-screen report (the form `check` prints):**

```text
refuted regroup: proposition equality, evidence counterexample,
  the two sides differ at (1e+16, -1e+16, 1): 1 and 0
```

**Then, after adding the permission tag:**

```text
sampled regroup: proposition real equality + IEEE on special values, evidence samples 200
```

### L6 · 0:59–1:09 · Handed over explicitly

**Idea:** importing a module or writing a rule changes no pass. The user hands the rule set to the
optimizer as a value.

**Layout and motion:**

- A user's file imports `mylib`, and nothing happens: the `std::opt` gate's `rules:` slot stays
  empty, and a dim note reads `import changes nothing`.
- Then `std::opt<rules: mylib::rules>` types. The bracketed row of cards slides into the slot, and
  the gate's outline brightens.
- The command-line form appears beneath it in dim.

**Colour:** cyan, dim.

**Narration:** "Importing changes nothing. Users hand your rules to the optimizer, explicitly — so
every rewrite has a name."

**Subtitle:** 导入不会改变任何东西。用户显式地把你的规则交给优化器——每一次重写都有名字可查。

**Code:**

```camel
let fast = model->std::opt<rules: mylib::rules>
```

**Command line:**

```bash
camel model.cml std::opt rules=mylib::rules std::jit
```

### L7 · 1:09–1:21 · A pass is a function; ship a pipeline

**Idea:** a pass is a function transformation, `(f: F) => F!`. A library can ship a whole pipeline
as one function, used by name on the command line. Errors ride the same `->`.

**Layout and motion:**

- One gate labelled `mylib::compile` stands on the cyan rule.
- The camera pushes into it. It opens like a function frame, and inside are three smaller gates:
  `std::macro → std::opt<rules> → tiles::compile`.
- A thin coral side track runs under them, the railway again. If any stage fails, its result rides
  the side track out of the gate.
- Pull back. The single gate stands on the command line strip.

**Colour:** cyan, coral side track (thin, not activated).

**Narration:** "And a pass is just a function, from a function to a function. Ship a whole pipeline
under your library's name."

**Subtitle:** 而遍不过是一个函数：从函数到函数。把一整条流水线，以你的库的名字发布出去。

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

**Command line:**

```bash
camel model.cml mylib::compile std::jit
```

### L8 · 1:21–1:30 · Close

**Idea:** types, rules and passes are all written in Camel itself.

**Layout and motion:**

- Pull back across the sequel's canvas: the definition, the kernel lanes, the rule cards and the
  library gate, joined into one graph.
- Dim to 15 %. **Let the graph speak.**, its sand underline, then `pip install camel-lang`.

**Narration:** "Types, rules and passes — one language, all the way down. Let the graph speak."

**Subtitle:** 类型、规则和遍——自始至终，同一种语言。让图说话。

---

## Production notes

- **Timing budget:** narration is written at about 2.3 words per second with a 0.3 s breath per
  shot. If the voice runs slower, Shot 5 and L3 are the first to stretch (by 1 s each). The main film
  then becomes 77 s, so take the 2 s back from Shot 11's hold.
- **Real output:** these are reference only, and are rendered in the form the tools print:
  - the node counts in Shot 5, from `std::stats`;
  - the `check` lines in L5;
  - the location in Shot 9.

  Use the real values where the tools give them.
- **Code panels:** keep the lengths above, with no more than 6 lines at the subject size. L3 and L7
  run longer and use the 44–46 px supporting size.
