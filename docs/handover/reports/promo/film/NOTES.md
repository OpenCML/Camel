# Main film, cut v1

`main-cut-v1.mp4` is shots 00–10 of `../script.md` (part 1, "The program is the graph"), in order,
at 1920×1080, 60 fps, H.264. It runs **75.95 s**; the script's budget is 75 s. It is assembled by
`cut.py`, which runs in the `camel-lanes` container (`/work/promo/cut`) next to the shot MP4s and
`script.md`.

| Shot | Starts | Length | Words in the narration | Words per second |
|---|---|---|---|---|
| 0 Recovered | 0:00.0 | 6.35 s | 15 | 2.5 |
| 1 One ink | 0:05.9 | 7.07 s | 11 | 1.7 |
| 2 `sync` is a thread | 0:12.6 | 9.85 s | 19 | 2.0 |
| 3 A layer | 0:22.1 | 7.80 s | 17 | 2.3 |
| 4 Reflection and sweep | 0:29.5 | 9.55 s | 20 | 2.2 |
| 5 Say where | 0:38.6 | 7.22 s | 18 | 2.6 |
| 6 Stream | 0:45.4 | 6.85 s | 11 | 1.7 |
| 7 Railway | 0:51.9 | 7.52 s | 16 | 2.2 |
| 8 Fault and fence | 0:59.0 | 6.13 s | 11 | 1.9 |
| 9 Seal | 1:04.7 | 4.75 s | 12 | 2.8 |
| 10 The camel | 1:09.1 | 6.87 s | 5 | 0.8 |

The pace counts each shot's length minus the 0.4 s overlap. Style §7 caps narration at 2.3 words per
second. Shots 0, 5 and 9 are over that cap and need either a longer hold or a shorter line. Shot 10
has the room to give.

## What is still placeholder

- **Transitions.** Style §5.4 calls for match cuts, where the last shape of a shot is the first
  shape of the next. The shots were rendered one at a time on an empty ground, so each join is a
  0.4 s dissolve through the ground. Real match cuts need the end of each shot and the start of the
  next laid out together. Shot 0's crumple point is also not yet placed on shot 1's caret.
- **Voice.** There is none yet. The English narration is burned in as a small chalk-dim caption
  under the Chinese subtitle; both come from `script.md`.
- **Score and sound effects** (style §7) are not made.
- **End card.** Shot 10 sets the motto in JetBrains Mono at 64 px, where the style asks for weight
  700 at 88 px.
- **The coda.** The script originally had the camera pull back over every earlier graph lying on
  the logo's outline. The shots were never laid out on one canvas, so shot 10 draws the logo's own
  strokes as graph pieces instead; `script.md` now describes it that way.
- **Real output.** On-screen diagnostics (`EffectNotExecuted`, `StreamConsumedTwice`,
  `rows.cml:3:31  IndexOutOfRange`, `UsesPromiseViolated`) use the registry's names, not captured
  compiler output.
- **Frame rate.** Style §6 asks for 30 fps for review and 60 fps for the master. This cut is
  60 fps throughout.

## Voice

A synthetic scratch voice is worth trying, because the per-shot timing above can only be checked
against a voice.

- **Recommended:** Kokoro-82M. It is small, runs on the CPU, and has clear English voices.
  Install it with `pip` in the `camel-lanes` container under `/work/promo`, not on the host.
- **Lighter fallback:** Piper (one ONNX voice of about 60 MB), if Kokoro's dependencies are too
  heavy.
- **Script:** render one WAV per shot from the narration lines, then let `cut.py` place each at its
  shot's start plus 0.3 s.

The final voice should be a person; the synthetic one is for timing only.

## Sequel plan

Part 2, "Your library can teach the compiler" (L1–L8, about 90 s), uses the same project, the same
`lib/plotter.tsx` and the same cut script. It needs new pieces in the helper library:

1. **Box materials.** A hatched opaque box, a glass frame (dashed, graph visible inside) and a
   steel block (hatched, labelled `mylib.cmo`). These carry L1–L3.
2. **Stencil card.** A cut-out pattern and its replacement, with bracketed rule arrays. It glows
   cyan where it fits and contracts the five nodes into one. This carries L4 and L6.
3. **The jam (L5).** Beads falling through a stencil, a coral jam, a card flip, and two error bars.
   The numbers (`7.76e-8`, `1.91e-6`, the vector `[-1e-16, 27.004, -33.563]`) must be re-measured
   from a real `check` run before rendering, per the production notes.
4. **An opening pass (L7).** A sweep that opens like a function frame onto three smaller sweeps,
   reusing shot 7's coral side track.
5. **Coda (L8).** The same treatment as shot 10, then the end card.

Order of work:

1. Verify the sequel's code against the current design: `#impl`, the `rule` syntax, `Rule[]`,
   `std::opt<rules: …>`, and `compile::<F>(f: F): F! when …`.
2. Render L1–L4.
3. Render L5 once real `check` output exists.
4. Render L6–L8.
5. Make a second cut of both films, with the voice if the scratch voice works out.
