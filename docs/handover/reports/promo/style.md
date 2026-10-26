# Visual language: "Plotter on navy"

Used by both films. The audience is AI researchers and engineers, so the frames should look like a
technical drawing being made in front of them, not like a product ad.

## 0. The idea

Camel's logo is a camel drawn as a graph: monoline cells and joints inside a circle, on deep navy.
The film takes its whole look from that drawing.

- **One ink.** Code and graph are drawn with the same pen, on the same ground. There is no panel box
  around the code: it is written straight on the navy, with a thin gutter rule. When a token is
  typed, its glyph **lifts off the line and flies into the graph** to become a node. That is the
  claim, "the program is the graph", shown and not said.
- **Plotter, not pop.** Things are drawn, not spawned. A node's outline is traced by a pen (stroke
  0 → 100 %). An edge is one stroke from source to target. Nothing bounces.
- **The ending is the logo.** In the last shot the camera pulls back and every graph drawn in the
  film turns out to sit inside one outline: the camel. The logo is literally a graph, so the film
  closes on the brand without leaving its own vocabulary.

## 1. Palette

The ground comes from the logo. Every accent colour has exactly one meaning in both films.

| Role | Hex | Meaning, and only this |
|---|---|---|
| ground (top → bottom) | `#2D3968` → `#242946` | vertical gradient, the logo's navy; never changes |
| grid | `#FFFFFF` at 4 % | 48 px drafting grid; only in shots that need space |
| chalk | `#D8DEF5` | the pen: node outlines, data edges, code text |
| chalk-dim | `#8E9AC0` | gutter, labels, ghosted (not-running) nodes |
| keyword | `#AEB9DE` | keywords in code |
| **sand** | `#F2B35B` | the brand colour and "the value, now": the travelling value, equality, focus |
| **cyan** | `#5EE0F0` | passes and rewriting: the sweep line, rule stencils |
| **violet** | `#C3A6FF` | what derivative transforms make: the reflected (reverse) graph, tangents |
| **mint** | `#7BE8B0` | right and computed: results, a passed check, the `pure` seal |
| **coral** | `#FF7A70` | failure: the error side track, faults, a broken seal, a refuted law |
| **thread** | `#F7E3B5` | order: the `sync` thread and order edges |
| **rose** | `#FF9CC2`, region fill at 10 % | device placement (`#gpu`) |

Rules:

- At most two accents per shot, besides sand.
- Mint never means "placed" or "fast", only "right and computed". Coral always comes with a reason or
  a location.
- Contrast: chalk on the ground is about 9:1, and chalk-dim is at least 4.5:1.

## 2. Type

- **JetBrains Mono for everything on screen.** That covers code, node glyphs, labels, report lines
  and the end card. A film about a language set entirely in a monospace face reads as one voice.
  Weights: 400 for code, 500 for labels, and 700 for the end line only.
- **Sizes at 1080p:** subject code 52–56 px, supporting code 40–44 px, labels 26 px, end line 88 px.
  Code is at most 6 lines and 34 characters per line at subject size.
- **Chinese subtitles:** Noto Sans SC Medium, 40 px, centred 86 px above the bottom edge, in a band
  that never moves. At most two lines of at most 22 characters each. Code terms stay in English
  monospace.
- **English narration is never printed.** Only terms that label something on the graph appear.
- **Syntax colour, sparingly:**

  | Token | Colour |
  |---|---|
  | keywords | keyword |
  | operators and `->` | chalk-dim |
  | numbers | mint |
  | strings | sand |
  | `#gpu` | rose |
  | other tags | cyan |
  | everything else | chalk |

## 3. The pen: how the graph is drawn

All strokes are 4 px with round caps and joins, the same weight as the logo's lines. Fills are the
ground colour, so a node hides the edges beneath it but adds no colour.

### 3.1 Nodes

| Kind | Drawing |
|---|---|
| Value, constant, port | Circle, 72 px. The glyph inside it is the glyph that flew from the code. A port sits on its function's frame. |
| Operator | Rounded square, 72 px, radius 18; wide operators (`linear`, `rms_norm`) are 140 px. |
| Function | Rounded frame, dashed chalk-dim 2 px, name top-left. A call is one node. Opening a call is a camera dive through it. |
| Native kernel (`#impl`) | A frame drawn twice: glass (dashed, contents visible) and solid (hatched with 45° chalk-dim lines). |
| Effect | An operator with a small notch cut in its top-right corner, where the `sync` thread passes. |
| Branch and join | Diamond. The arm not taken fades to 25 %. |
| Loop | Frame with a return arc. The state is a bead on the arc. |
| Result | Hollow ring. When reached, it is re-traced in mint and the value is written beneath it. |

### 3.2 Edges

- **Data:** one chalk stroke with a small arrowhead, trimmed at node borders. A node used twice has
  two parallel strokes 18 px apart, never one thick one.
- **With-binding (weights, configuration):** a small circle that docks onto the top of its operator,
  with no edge. Data runs left to right; configuration sits on top. This is `x->linear<w, b>`
  drawn literally.

### 3.3 Values and parallelism

- **A value** is a sand bead (18 px, soft 22 px glow) moving along a stroke. Its number is written
  beneath the node in mint.
- **Parallelism:** two beads moving in the same frames. No label. The simultaneity is the claim.
- **Not run:** a node nothing reaches stays chalk-dim and never gets a bead.

## 4. Metaphors, one per idea

Each idea has one image, and the image never changes between shots or between films.

| Idea | Image |
|---|---|
| The program is the graph | glyphs lift off the code line and become nodes (0) |
| One value, one node | equal subgraphs slide together and become one; their uses become parallel strokes |
| Order comes from dependency | beads move only when their inputs have arrived; independent beads move together |
| `sync` | **a thread** (thread colour, dashed, slight sag) stitched through the notches of the effect nodes, in written order. Pure nodes are never stitched. |
| An effect nothing needs | an un-stitched effect node stays dim; a small `EffectNotExecuted` label in chalk-dim |
| A layer | an operator with its weights docked on top |
| A pass | **a sweep line**: a vertical cyan line crosses the graph left to right; behind it the graph is redrawn in its new form, and nodes slide straight to their new places. A pipeline is several sweeps in a row, named on a strip like the command line. |
| A derivative | **a reflection**: `grad` draws a horizontal baseline, and the graph is reflected beneath it in violet with every edge reversed (the reverse pass is the mirror). `jvp` draws its violet twin above the line, running the same way. |
| Placement | a rose region; where edges cross its edge, small ↑ ↓ marks appear, drawn by the compiler and not the user |
| Export | the graph folds into a bundle and fans out into cards (`model.onnx`, `model.cpp`) that hold the same drawing; one mint tick spans them all |
| A stream | a pipe. A bead leaves it only when the consumer **pulls** a short thread from the pipe's mouth. A second consumer's thread snaps (coral). |
| An error | **the railway**: the happy path is a chalk rail with switch points at each step that can fail; a coral side track runs beneath; a failed bead switches onto it and passes under every later step; it rejoins at the single `!!` and turns sand again |
| A fault | the pen jerks: a coral blot appears on the node, every other stroke freezes and dims to 25 %, and a location is stamped beside the blot |
| `attempt` | **a fence**: a thin coral frame around one call; a blot inside it stays inside, and at the fence it becomes a bead on a small side track; the rest keeps moving |
| `pure` and `uses` | **a seal**: a mint line drawn around the function frame, closed with a small `pure` tab; a resource line (labelled `stdout`) that pierces the seal tears a coral notch in it, and the diagnostic is stamped there. `uses fs` is a seal with one labelled gate in it. |
| A rule (sequel) | **a stencil**: a card with a cut-out pattern on the left and the replacement on the right; laid over the graph, it glows cyan where it fits, and the sweep redraws that spot |
| `check` (sequel) | random inputs fall through the stencil as small beads; a counter-example **jams** it (coral), and the card flips to show the inputs and the two values that differ |
| A rule set (sequel) | stencils fanned in square brackets, `[ … ]`, because a rule set is an ordinary array value; it is handed to a pass by sliding into the `rules:` slot on the sweep's strip |

## 5. Motion

1. **Every movement has a cause on screen.** A node is traced because its glyph just arrived. Nodes
   merge because they are equal. A bead moves because its inputs are ready.
2. **Drawing timings:**

   | Motion | Time and easing |
   |---|---|
   | node trace | 0.30 s, easeInOutCubic |
   | edge stroke | 0.25 s, easeOutCubic |
   | glyph flight | 0.40 s, easeInOutCubic, with a slight 12 px arc upward |
   | rearrangement under a sweep | 0.9 s along straight lines, easeInOutCubic |
   | merged nodes fading | 0.3 s, linear |

3. **Rhythm:** draw (0.3–0.6 s), then change (0.8–1.2 s), then hold (at least 0.6 s, enough to read
   the frame). One claim per shot.
4. **Continuity:** match cuts only. The last shape of each shot is the first shape of the next. The
   only hard cut is into the end card's black-out pause, and even that resolves back to the logo.
5. **Camera:** a slow push (3 % over a shot), pans and dives at 1.0 s easeInOutCubic. The camera
   never shakes, spins or rolls.
6. **What never moves:** the ground, the subtitle band, code text while it is being read, and the
   meaning of a colour (a sand bead stays sand through a cyan sweep).
7. **Density:** at most 24 nodes on screen. Beyond that, fold a part into a function frame showing
   only its ports.

## 6. Layout

- 1920×1080 with 96 px safe margins. 30 fps for review, 60 fps for the master.
- **Split shots:** code on the left, starting at x = 136 px, with a 1 px chalk-dim gutter rule at
  x = 112 and line numbers in chalk-dim. The graph is on the right 58 %.
- **Full-graph shots** use the whole frame. **Before/after shots** are separated by the reflection
  baseline itself.
- **End card:**
  1. The logo outline is traced in chalk (1.2 s).
  2. `Let the graph speak.` appears in JetBrains Mono 700, 88 px, and a sand underline is drawn
     beneath it.
  3. 0.8 s later, `pip install camel-lang` appears in 40 px chalk-dim.

## 7. Sound

- **Narration:** calm, unhurried, at most 2.3 words per second, with a 0.3 s breath per shot.
- **Score:** a soft low pulse that ticks at the tempo of the beads, plus one slow rising line that
  resolves on the logo.
- **Effects:** only five, each aligned to its motion within ±1 frame.

  | Sound | When |
  |---|---|
  | a dry pen scratch | a node is traced |
  | a falling minor third | a merge |
  | a low felt thud | a fault, a jam, or a torn seal |
  | a soft pull-click | a stream pull |
  | a single bell | on the logo |

- **Subtitles:** they fade in and out over 0.15 s.
