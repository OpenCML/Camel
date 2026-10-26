# The graph was always there

Film 1 of the series ([README.md](README.md)): one idea, about 50 seconds, room to breathe.

AI compilers try to recover a program's graph by tracing Python, and the trace keeps tearing. Camel
never throws that graph away.

The proof is one payoff. The `if` that tore the traced graph stands whole in Camel's graph as a
branch region, and light runs through it: one arm, then the other, in the same graph, with nothing
traced again. Other ideas (gradients, parallelism, passes, errors) are films of their own.

The audience is AI researchers and engineers. Most have watched `torch.compile` break a graph or
`jax.jit` reject a Python `if`. The film starts from that experience, so the messages on screen are
the real ones.

There is one hard rule: **nothing on screen may be false about Camel or about the tools it is set
against.** Every message, graph and value is checked; see "Claims and sources".

The look and sound are in [style.md](style.md). Beats 1–3 are built as the proof of concept in
[poc/](poc/); its render is `renders/poc/poc-v1.mp4`, which is not committed.

## The beat grid

The film runs at 96 BPM (one beat is 0.625 s). Every hit, tear, cut and run of light falls on the
grid, and the picture and the score are built from the same cue sheet (`poc/render.cjs --cues`).

| Beat | Time | Bars | Act |
|---|---|---|---|
| 1 | 0:00–0:12.5 | 1–5 | Cold open: the trace tears |
| 2 | 0:12.5–0:17.5 | 6–7 | The turn: silence |
| 3 | 0:17.5–0:30 | 8–12 | The reveal and the payoff: code is graph, and the whole branch runs |
| 4 | 0:30–0:40 | 13–16 | Pull back: the camel of light |
| 5 | 0:40–0:50 | 17–20 | End card |

## 1. Cold open: the trace tears (0:00–0:12.5)

**Picture.**

- A familiar piece of PyTorch types itself on the left, in dim code colours:

  ```python
  @torch.compile
  def loss_fn(xs, best):
      l = 0
      for x in xs:
          l = l + model(x).mean()
      if l > best:
          l = l * 0.5
      print("loss", l.item())
      return l
  ```

- A trace line walks down the code (3.2 s). Behind it, in depth on the right, the framework rebuilds
  the graph it is tracing in thin grey-white lines. The Python loop unrolls into three rows. The
  camera holds still.
- At each construct the trace cannot follow, the graph breaks:
  - the offending code turns the break colour;
  - the edge parts cleanly in two, and the failing piece drifts a little;
  - the compiler's own message types into a log under the code, calm and legible.

  | Beat | Code | `torch.compile` (2.14.1) | `jax.jit` (0.12) |
  |---|---|---|---|
  | 5.6 s | `if l > best:` | `Graph Break Reason: Data-dependent branching` | `TracerBoolConversionError` |
  | 7.5 s | `l.item()` | `Graph break from` `` `Tensor.item()` `` | `ConcretizationTypeError` |
  | 8.75 s | `print(...)` | `Failed to trace builtin operator` | prints `JitTracer(float32[])`, once, at trace time |
  | 10.0 s | the second call, with five batches | `Recompiling function loss_fn`, guard failure `len(xs) == 3` | traced again for the new input structure |

- **Splitting:** after the first tear, what was caught closes into a box labelled `graph 1`. After
  the second, `graph 2` closes around `l * 0.5`. A graph break splits the program into separate
  graphs with Python between them, and the picture shows exactly that.
- **The recompile:** at 10.0 s the whole traced graph comes apart: a few dozen fragments (each node,
  edge and box) drift slowly away and hang in the dark.
- **The line (11.25 s):** *AI compilers trace Python to recover a graph the language threw away.*
  Its subtitle is 「AI 编译器追踪 Python，想找回语言丢掉的那张图。」

**Sound.**

- A quiet low drone.
- Each break is a soft low knock with a dry tick. The recompile is a deeper knock with a short wash.
- Everything is cut dead on the downbeat of bar 6.

## 2. The turn (0:12.5–0:17.5)

**Picture.**

- Silence. The fragments of the broken graph hang and drift, and the camera drifts a little.
- At 13.75 s: *What if the language never threw it away?* Its subtitle is 「如果语言从未丢掉它呢？」
- At 17.5 s the fragments fall together into one point of light, which becomes the caret of the
  Camel code.

**Sound.** Silence, real silence, until a faint swell rises into the caret.

## 3. The reveal: code is graph (0:17.5–0:30)

**Picture.**

- The Camel code types at the lower left:

  ```camel
  with <m: Mlp>
  func loss(x: float[..], best: float): float {
      let h = relu(x @ m.w1 + m.b1)
      let l = float(mean(h @ m.w2 + m.b2))
      if l > best then l * 0.5 else l
  }
  ```

- **Code and graph are one object.** As each token is typed, its characters lift off the line and
  fly into the graph, where they become the node's label while its outline is traced around them.
  Every node starts from the source span the compiler records for it, so code and graph are the
  same thing seen two ways.
- **The function:** `func loss` draws the frame of the function.
- **The branch:** the `if` that tore the trace now draws a whole branch region. It has a door
  (the branch), both arms (`then` through `* 0.5`, `else` straight on) and the join. A thin break
  line crosses it once and heals.
- **The camera** holds a still wide shot of the whole graph while it is written, then moves in
  slowly to the branch for the payoff.
- **Data runs as light** (24.4 s): an amber pulse runs from the inputs through every edge, hop by
  hop on the beat.
  - With `best = 0.5`, the comparison is `true`: the `then` arm lights, and the result reads
    `0.91875`.
  - With `best = 9.0`, the light takes `else`, and the result reads `1.8375`.

  Both arms live in the one graph: nothing tears, nothing is traced again.
- **The line (28.1 s):** *The graph was always there.* Its subtitle is 「那张图，一直都在。」

**Sound.**

- A quiet bell for every node the code folds into, rising through A major pentatonic.
- A soft chord opens when the branch region closes whole.
- A chime marks each run reaching its result, and the last chord resolves under the line.

## 4. Pull back: the camel of light (0:30–0:40, planned)

**Picture.**

- The light settles in the graph, and the camera keeps pulling out, slowly.
- The graph's strokes ease onto the strokes of the Camel mark (`assets/brand/camel-mark.svg`): a
  graph itself, made of nodes and edges. At distance, the whole thing is the camel, made of light.
- Nothing new is claimed here. The mark is the brand, and the motion is a transition, not a
  statement about the program's graph.

**Sound.** The last chord blooms. One bell lands as the mark resolves.

## 5. End card (0:40–0:50, planned)

*Let the graph speak.* Then **Camel**, then `pip install camel-lang`. Hold, and let the music ring
out.

## Claims and sources

| Beat | Claim on screen | Source |
|---|---|---|
| 1 | The four `torch.compile` messages, for this exact `loss_fn` | Run in `camel-lanes` with torch 2.14.1+cpu, with `torch._logging.set_logs(graph_breaks=True, recompiles=True)`; the second call passes 5 batches instead of 3 |
| 1 | The `jax.jit` names: `TracerBoolConversionError` (a Python `if` on a traced value), `ConcretizationTypeError` (`.item()`), `print` showing `JitTracer(float32[])` once at trace time, a retrace for a new input | Run with jax 0.12.0.1 on the same constructs |
| 1 | A graph break splits the program into separate compiled graphs with Python in between | The same torch run (resume functions `torch_dynamo_resume_in_loss_fn_at_…`) |
| 1 | "AI compilers trace Python to recover a graph the language threw away" | `torch.compile` (TorchDynamo) and `jax.jit` both build their graphs by tracing Python execution |
| 3 | The Camel code compiles and runs; with `best = 0.5` it prints `0.91875`, with `9.0` `1.8375` | `camel` run of the same function in `camel-lanes` (`/work/promo/camel/reveal.cml`) |
| 3 | The graph's nodes, edges and branch: ports `m`, `x`, `best`; accesses `.w1` … `.b2`; `@`, `+`, `relu`, `mean`, `float`, `>`; BRCH, the `then` arm (`* 0.5`), the `else` arm, JOIN | `camel reveal.cml std::gir`: the GIR of `loss` |
| 3 | Each node comes from a source span | The `span` attribute every node carries in that GIR |
| 3 | Both arms of the `if` are in one graph; a second call takes the other arm without a retrace | The same GIR (one BRCH/JOIN region), and the two runs |

## Production notes

- **Truth over punch:** the earlier brief's line "every AI framework is trying to recover a graph"
  became "AI compilers trace Python to recover a graph". Not every framework traces, and the
  narrower claim is the one that is true.
- **Narration:** none. Text and music carry the film. If a voice is wanted later, it reads only the
  three lines.
- **Rendering:** the picture is WebGL (three.js) with bloom, rendered frame by frame in headless
  Chromium in the `camel-lanes` container (`poc/render.cjs`). The score is synthesised from the cue
  sheet (`poc/score.py`). See `README.md`.
