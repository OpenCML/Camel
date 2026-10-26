# Main film cuts

- **`main-cut-v2.mp4`** (current) is shots 00–10 of `../script.md` (part 1, "The program is the
  graph"), at 1920×1080, 60 fps, H.264, with AAC audio. It runs **81.45 s**: 6.5 s over the
  script's 75 s budget.
- **`main-cut-v1.mp4`** is the first cut: 75.95 s, dissolves at every join, and no voice.

Both cuts are assembled by `cut.py`, run in the `camel-lanes` container (`/work/promo/cut2`). It
needs the shot MP4s, `script.md` and `voice/` beside it:

```sh
python3 cut.py main-cut-v2.mp4
```

| Shot | Starts | Length | Narration | Words | Words per second | Voice | Slack after the voice | Join to the next |
|---|---|---|---|---|---|---|---|---|
| 0 Recovered | 0:00.0 | 6.35 s | Compilers need the graph. Most languages make them rebuild it — and it breaks. | 13 | 2.0 | 5.03 s | 1.0 s | match cut: the caret |
| 1 One ink | 0:06.4 | 7.37 s | (unchanged) | 11 | 1.5 | 3.78 s | 3.3 s | match cut: port `x` |
| 2 `sync` is a thread | 0:13.7 | 9.82 s | (unchanged) | 18 | 1.8 | 6.58 s | 2.9 s | match cut: `x` |
| 3 A layer | 0:23.5 | 8.68 s | (unchanged) | 17 | 2.0 | 5.36 s | 3.0 s | match cut: the frame of `mlp` folds into the node `mlp` |
| 4 Reflection and sweep | 0:32.2 | 10.03 s | (unchanged) | 19 | 1.9 | 6.36 s | 3.4 s | match cut: `mlp` and its weights |
| 5 Say where | 0:42.3 | 7.28 s | Say where it runs; the compiler moves the data. Or ship it as ONNX or C++. | 16 | 2.2 | 5.86 s | 1.1 s | dissolve |
| 6 Stream | 0:49.1 | 6.85 s | (unchanged) | 11 | 1.6 | 4.08 s | 2.5 s | match cut: the pipe straightens into the rail |
| 7 Railway | 0:56.0 | 8.10 s | (unchanged) | 16 | 2.0 | 5.70 s | 2.1 s | match cut: the shape of `path` becomes `lines` |
| 8 Fault and fence | 1:04.1 | 6.15 s | (unchanged) | 10 | 1.6 | 3.24 s | 2.6 s | match cut: the fault's fence widens into `normalize`'s frame |
| 9 Seal | 1:10.2 | 4.75 s | Promises are checked: say pure, and keep it. | 8 | 1.7 | 2.68 s | 1.8 s | dissolve |
| 10 The camel | 1:14.6 | 6.87 s | (unchanged) | 5 | 0.7 | 1.69 s | 4.9 s | end |

- **Narration trims.** Shots 0, 5 and 9 were cut by words, not sped up, to stay under style §7's
  cap of 2.3 words per second. Each claim still holds:
  - shot 5's "it" is the same graph;
  - shot 9 still says the promise is checked.
- **Subtitles.** Shot 9's Chinese subtitle follows the new line: 承诺会被检查：写下 pure，就得做到。
- **Slack.** The length is the shot length minus the 0.3 s breath and the voice. Every line fits
  its shot.

## Transitions

**Match cuts (style §5.4).** At a match cut, the outgoing shot fades everything but one shape in
its last 0.4 s (`handOff` in `lib/plotter.tsx`). The incoming shot starts with that shape, at the
same place and size, and moves it into its own role (`arrive`). The cut itself is hard. Frame
differences across each cut, outside the subtitle band, are at most a few dozen anti-aliased
pixels.

- **0 → 1, the caret:** shot 0 crumples to the point where shot 1's caret stands before the first
  glyph. There it turns sand and gives way to the code's own caret.
- **1 → 2 and 2 → 3, `x`:** the same input glides to its next place.
- **3 → 4, `mlp`:** the dashed frame of `mlp` shrinks into the node `mlp` of `loss`. This is the
  fold the script describes.
- **4 → 5, `mlp` and `m`:** carried into `forward`.
- **6 → 7, the pipe:** the stream's pipe flattens into the rail ("the pipe straightens into a rail").
- **7 → 8, `path` → `lines`:** the shape of the input carries over, and its name is written anew.
- **8 → 9, the fence:** the fault's coral fence widens into `normalize`'s frame ("one fence widens
  into the frame of shot 9").

**Dissolves kept (0.4 s through the ground):**

- **5 → 6:** shot 5 ends on two export cards, and shot 6 opens on a stream pipe. No shape holds
  both. The script's "cards fold back into one graph, whose input stretches into a pipe" would need
  shot 5's ending rebuilt.
- **9 → 10:** shot 9 ends on the seal, and shot 10 opens close inside the logo. Making the seal
  become a logo stroke needs the logo's geometry in shot 9's coordinates.

## Scratch voice

| | |
|---|---|
| Tool | Kokoro-82M through the `kokoro` package 0.9.4, on CPU PyTorch 2.14.1 |
| Where | a virtualenv at `/work/promo/tts` in `camel-lanes`; nothing is installed on the host |
| Script | `voice.py` (in this folder) |
| Voice and speed | `af_heart` (American English) at speed 0.85 |
| Output | one WAV per shot, trimmed of silence |
| Placement | each line starts 0.3 s after its shot starts |

Two lines are spelled as they are spoken: "C plus plus" and "onyx" for ONNX.

This voice is for timing only; the final narration should be recorded by a person.

## What is still placeholder

- **Length.** At 81.45 s, the cut is 6.5 s over the budget. The match cuts no longer overlap shots
  the way the dissolves did, and shots 1, 3, 4 and 7 gained their hand-off moves. The slack column
  above shows where to take time back: the holds in shots 1–4 and 10 each have 3 s or more after
  the voice.
- **Two dissolves:** 5 → 6 and 9 → 10, see above.
- **Score and sound effects** (style §7) are not made.
- **End card.** Shot 10 sets the motto in JetBrains Mono at 64 px, where the style asks for weight
  700 at 88 px.
- **The coda.** Shot 10 draws the logo's own strokes as graph pieces, rather than pulling back over
  every earlier graph laid out on one canvas.
- **Real output.** On-screen diagnostics (`EffectNotExecuted`, `StreamConsumedTwice`,
  `rows.cml:3:31  IndexOutOfRange`, `UsesPromiseViolated`) use the registry's names, not captured
  compiler output.
- **Frame rate.** The cut is 60 fps throughout; style §6 asks for 30 fps for review.
- **English captions** stay burned in under the Chinese subtitle until the final voice is
  recorded.

## Sequel plan

Part 2, "Your library can teach the compiler" (L1–L8, about 90 s), uses the same project, the same
`lib/plotter.tsx`, `cut.py` and `voice.py`. It needs new pieces in the helper library:

1. **Box materials.** A hatched opaque box, a glass frame (dashed, graph visible inside) and a
   steel block (hatched, labelled `mylib.cmo`). These carry L1–L3.
2. **Stencil card.** A cut-out pattern and its replacement, with bracketed rule arrays. It glows
   cyan where it fits and contracts the five nodes into one. This carries L4 and L6.
3. **The jam (L5).** Beads falling through a stencil, a coral jam, a card flip, and two error bars.
   The numbers (`7.76e-8`, `1.91e-6`, the vector `[-1e-16, 27.004, -33.563]`) must be re-measured
   from a real `check` run before rendering.
4. **An opening pass (L7).** A sweep that opens like a function frame onto three smaller sweeps,
   reusing shot 7's coral side track.
5. **Coda (L8).** The same treatment as shot 10, then the end card.

Plan the hand-off shape of every join before animating, so that the sequel is match-cut
throughout.

Order of work:

1. Verify the sequel's code against the current design: `#impl`, the `rule` syntax, `Rule[]`,
   `std::opt<rules: …>`, and `compile::<F>(f: F): F! when …`.
2. Render L1–L4.
3. Render L5 once real `check` output exists.
4. Render L6–L8.
5. Make a combined cut with the scratch voice.
