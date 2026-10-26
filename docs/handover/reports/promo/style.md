# Visual language

Used by both films. The audience is AI researchers and engineers, so the frames should look like a
technical figure drawn with care, not like a product ad. That means a dark ground, few colours, and
motion that always shows a cause. The graph carries the film, the code supports it, and the subtitles
come third.

**The one idea behind every choice:** in Camel the program *is* the graph. So the film never leaves
the graph. There are no hard cuts and no cutaways to logos or stock footage. Every shot continues the
canvas of the shot before it (see 6.3, match cuts). The code panel is a window onto that same canvas,
not a separate slide.

---

## 1. Palette (dark ground)

Each colour means one thing, and that meaning holds for both films.

| Role | Hex | Meaning, and only this |
|---|---|---|
| ink | `#0D1117` | background, flat (no gradients; a 3 % dot grid when depth is needed, 7.4) |
| panel | `#141B26` | code panel, node fill, cards |
| line | `#2A3445` | panel borders, grid, separators |
| text | `#E6EDF3` | code, glyphs inside nodes |
| dim | `#7D8CA3` | labels, secondary text, ghosted (not-running) nodes |
| steel | `#6C86B0` (lit `#8FB3FF`) | data edges and ordinary node outlines |
| **sand** | `#E0A94B` | the brand colour and "look here": the current value travelling, equality, focus |
| **cyan** | `#4DD0E1` | passes (graph → graph functions) and rewriting in progress; rule cards |
| **violet** | `#B388FF` | everything a derivative transform makes (`grad`, `vjp`, `jvp`): reverse graph, tangents |
| **mint** | `#6EE7A8` | computed results, `pure`, a check that passed |
| **coral** | `#FF6B6B` | failure: error values on the side track, faults, a broken promise, a refuted law |
| **cream** | `#F5E6C8` (dashed) | order: `sync` edges and order edges between effects |
| **rose** | `#F28FB0`, region fill at 8 % | device placement (`#gpu`) |

Rules:

- A shot uses at most two accent colours besides sand.
- Mint means only "right, and computed". Coral means only "failed", and it always comes with a
  location or a reason on screen.
- Contrast: text on ink is above 12:1, and dim on ink is at least 4.5:1.
- Rose (device) is deliberately far from mint (success), so that placement is never read as "passed".

## 2. Type

- **Code and node glyphs:** JetBrains Mono (fallbacks Cascadia Mono, Consolas). 56–64 px at 1080p for
  code that is the subject of the shot, 44–48 px for supporting code, with line height 1.35. A code
  line has at most 34 characters, and a panel holds at most 6 lines.
- **Titles and labels:** Inter (fallbacks Segoe UI, Noto Sans). Titles are 72–96 px Semibold with
  −1 % tracking. Labels are 26–30 px Regular in dim.
- **Chinese subtitles:** Noto Sans SC Medium, 40 px, centred 8 % above the bottom edge. At most two
  lines, and at most 22 characters per line. Code terms (`grad`, `->`, `sync`) stay in English in
  the monospace face.
- **English narration is not printed on screen.** Viewers hear it. Only key terms appear, as labels on
  the graph.
- **Syntax colour** (used sparingly, and only when the code is the subject):

  | Token | Colour |
  |---|---|
  | keywords | `#B7C4D8` |
  | operators and `->` | dim |
  | numbers | mint |
  | strings | sand |
  | tags (`#gpu`, `#impl`) | rose for placement, cyan otherwise |
  | everything else | text |

  Glyphs inside nodes are never coloured.

## 3. How the graph is drawn

There is one vocabulary for the whole film. By its third appearance, a shape needs no explanation.

### 3.1 Nodes

Shape gives the kind of node, never its size.

| Kind | Shape |
|---|---|
| Value, constant, port | Circle, 72 px, panel fill, steel outline 4 px, one glyph (`x`, `2`). A port sits on the frame edge of its function. |
| Operator | Rounded square, 72 px, radius 16, with one glyph (`×`, `+`, `relu`). Wide operators (`linear`, `matmul`) are 132 px wide. |
| Function or subgraph | Rounded frame, dashed steel 2 px, name top-left in dim. A call is one node. Opening a call means the frame grows and its inside fades in (a continuous zoom, never a cut). |
| Native kernel (`#impl`) | The same frame, filled solid panel with a small chip glyph in the corner. It is closed by default and opens only when a pass needs to see inside. |
| Branch and join | Diamond. The arm not taken fades to 25 %. |
| Loop | Frame with a return arrow. Its state is a small dot riding the arrow. |
| Result | Hollow ring. When a value arrives, the outline turns mint and the value is printed below it. |

### 3.2 Edges

- **Data edge:** steel, 5 px, solid, with a small arrowhead. It is trimmed at node borders and never
  enters a node. A node used twice gets two parallel edges 18 px apart, not one thick line. This is
  how "one node, used twice" reads.
- **Order edge (`sync` and effects):** cream, 3 px, dashed, with the dashes drifting slowly toward the
  target. **Pure computation never has a dashed edge.** That contrast is the whole argument for
  "order comes from dependencies", so a dashed line never appears in a pure graph.
- **`with` binding (weights, configuration):** a small value circle that docks onto the *top* edge of
  its operator, with no edge drawn. Data flows left to right; configuration sits on top. This is
  `x->linear<w, b>` drawn literally.

### 3.3 Values, evaluation and parallelism

- **Evaluation:** a sand dot, 20 px with a 24 px glow, moves along a data edge. The node's value
  appears 34 px below it in mint monospace.
- **"Runs only if needed":** a node that nothing reaches stays dim at 40 % and never receives a dot.
- **Parallelism:** dots advance on independent branches at the same time. There is no label and no
  thread icon. The simultaneity is the claim.
- **Arrays:** a small 3×3 dot badge at the top-right of the node. A shape label such as `f32[b, 784]`
  is shown in dim, and only when the shape is the point of the shot.
- **Streams `T..`:** a horizontal pipe holding a small step-function block and a state dot. An element
  (a sand dot) leaves the pipe only when the consumer pulls. There is no gush of elements. A second
  consumer of the same stream gets a coral cross (it is a compile-time error).

## 4. The special things

### 4.1 Passes, the cyan gate

A pass is a vertical rounded gate, taller than the graph, outlined in cyan, with its name on top
(`std::opt`). The graph slides through it. On the far side the nodes rearrange (see 6.2): the graph
is visibly the *same graph changing*, not a new picture. A pipeline is several gates on one
horizontal cyan rule, labelled like the command line: `std::macro → std::opt → std::jit`. Gates hold
no gears and no sparkles. If one is opened, a graph is inside, because a pass is a function and a
function is a graph.

### 4.2 Derivatives

`grad(f)` is a gate whose output is the original graph (steel) plus a mirror image below it. The
mirror has its edges reversed and its nodes violet, with faint violet ticks joining corresponding
nodes. Every later gate acts on both graphs at once. `vjp` and `jvp` are the same gate with a
different label: `vjp` adds a pullback (the reverse graph), and `jvp` adds a forward tangent (a
violet twin running in the *same* direction).

### 4.3 Rules and checking

A rule is a small card: panel fill, cyan outline, a mini-pattern on the left, `⇒`, and a
mini-pattern on the right. Applying it rings the match in the big graph in cyan for 0.3 s, then
swaps the subgraph. Under `check`:

- A passed law gets a mint tick on the card's corner with the verdict word (`sampled` or `proved`).
- A refuted law gets a coral cross, and the card turns over to show the counter-example in monospace:
  the inputs, and the two values that differ.

Saturation shows one value splitting into a short column of equivalent writings. The chosen one turns
sand.

### 4.4 Effects and order

An effect node is a normal operator with a small cream corner notch. In a non-`sync` body, an effect
nothing needs stays dim, and the `EffectNotExecuted` label appears in dim. When `sync` is typed, a
pale cream bracket encloses the block, and dashed cream edges link the effect statements in written
order. Pure nodes inside the bracket stay unlinked.

### 4.5 Errors: the railway

Errors are values, so they travel on a second track.

- **Main track:** the pipeline is drawn as a straight steel rail. Each step that can fail (`T!`) is a
  switch point on that rail.
- **Side track:** a thinner coral rail runs parallel, below the main one.
- **Propagation with `->`:** when a step fails, the value dot (now coral) slides onto the side track.
  It rides past every later step, which dims to 40 % and is not entered.
- **Handling with `!!` and `??`:** the side track merges back into the main rail. The dot turns sand
  again and takes the fallback value, which appears in mint.
- One `!!` at the end, and one merge. That is the visual of "write the happy path, handle once".

### 4.6 Faults and `attempt`

- **A fault (a bug, not an error)** is a coral flash on the node. The whole canvas then freezes:
  dots stop and every other node dims to 25 %. A location label appears next to the node in coral
  monospace (`data.cml:12:9`). No side track is involved: a fault is not a value.
- **`attempt(f)`** draws a thin coral frame around the call of `f`. A fault inside flashes, but at the
  frame boundary it becomes a dot on the side track: the fault has become an error value. The rest
  of the canvas keeps running. Inside `map<attempt(parse_row)>`, each element has its own small frame.

### 4.7 Resources and promises

- **`pure`:** a small mint shield on top of the function frame.
- **`uses fs`:** a cream tag with the resource name on top of the frame.
- **A broken promise:** the shield turns coral and splits. The offending node's outline turns coral,
  and a thin coral line runs from it to the shield, with the real diagnostic name
  `UsesPromiseViolated` beside it.

### 4.8 Placement and destinations

- **`#gpu`** is a rose rounded region (8 % fill, 2 px outline, `gpu` label top-left) around the
  nodes of the tagged function. Where an edge crosses the boundary, a small upload (↑) or download
  (↓) marker appears in dim, because the compiler derived it and the user did not write it. Dots
  inside the region travel twice as fast.
- **Export:** the graph folds into a thin bundle, then fans out into cards (`model.onnx`,
  `model.cpp`, `jit`). Each card holds a static thumbnail of the same graph, and one mint tick spans
  all the cards: same meaning. No third-party logos.

## 5. Library-author film additions

- **Native kernel beside its definition:** two stacked lanes. The top lane is the Camel body (an open
  graph) and the bottom lane is the solid `#impl` block. Between them, an `≡` sign pulses each time a
  differential test sample passes, as a small mint tick counter.
- **A rule set:** a short row of rule cards with array brackets drawn around it (`[ … ]`), because a
  rule set is a `Rule[]`, an ordinary value. It is handed to the gate by sliding into a slot in the
  gate labelled `rules:`. It never enters on its own.
- **A library pass:** a gate whose label is the library's name (`mylib::compile`). Opening it
  reveals the smaller gates it composes.

## 6. Motion

1. **Every movement answers a "because".** A node appears because its token was just typed. Nodes
   merge because they are equal. A dot advances because its inputs are ready. No movement is
   decorative.
2. **Rewriting is rearrangement.** Under a pass, each node moves in a straight line from its old place
   to its new place over 1.0 s with easeInOutCubic. Merged nodes fade at the end of the move
   (0.3 s, linear). A replaced subgraph glows cyan for 0.3 s before it is swapped.
3. **Match cuts only.** The last shape of a shot is the first shape of the next one (for example, the
   result ring of shot 1 becomes the `x` input of shot 2). Camera moves carry the eye from one to
   the next. There are no cross-dissolves to unrelated imagery and no hard cuts except to the end
   card.
4. **Rhythm inside a shot:** appear (0.3–0.5 s), then change (0.8–1.2 s), then hold (at least 0.5 s,
   so the frame can be read). One claim per shot.
5. **Easing:**

   | Motion | Easing |
   |---|---|
   | pops | easeOutBack (a slight overshoot, pops only) |
   | moves and re-layouts | easeInOutCubic |
   | fades | linear |
   | glows and pulses | sine |

   No bounces, shakes, spins, flying-in text or camera wobble.
6. **Camera:** it does two things only. It pushes in slowly (3–5 % over 5 s), and it pans or zooms
   along the graph to the next point of interest (1.0 s, easeInOutCubic). Zoom is how a call opens
   into its subgraph.
7. **Code and graph are linked.** Typing a token pops its node from the code side of the frame (scale
   0 → 1, 0.35 s), and the edge follows (0.3 s). Highlighting a node underlines its token in sand.
8. **What never moves:** the ink background, the subtitle band, the position of the code panel within
   a shot, and the meaning of a colour. A sand dot stays sand when it passes through a cyan gate.
   Text never moves while it is being read.
9. **Density:** at most 24 nodes visible at once. Beyond that, collapse a part into a function frame
   that shows only its ports.

## 7. Layout

- Canvas 1920×1080, 30 fps (60 fps for the final master), with 96 px safe margins.
- **Split shots:** code on the left 40 % (panel 780 px wide, centred at x = −520) and the graph on
  the right 60 %.
- **Full-graph shots:** the whole frame. **Before/after shots:** top and bottom halves.
- **Code panel:** panel fill, radius 22, 2 px line border, file name top-left in dim 22 px, 56 px
  inner padding.
- **Background:** flat ink. When depth is wanted, a 3 % dot grid at 48 px pitch with 0.5× parallax.
- **Title and end card:** ink, one line of Inter Semibold at 96 px, with a 6 px sand underline drawn
  left to right over 0.5 s. End card: **Let the graph speak.**, and 0.8 s later
  `pip install camel-lang` (monospace 44 px, dim). Nothing else on the card.

## 8. Sound

- **Narration:** calm and slightly slow, about 2.3 English words per second, leaving a 0.3 s breath
  between shots.
- **Score:** a minimal low pulse with a slow ascending figure, kept under the voice.
- **Effects:** only three, each aligned to its motion within ±1 frame.

  | Sound | When |
  |---|---|
  | a very soft tick | node pop |
  | a falling minor third | merge |
  | a short low thud | fault or refutation |

- **Subtitles:** they fade in and out over 0.15 s and follow the narration line by line.
