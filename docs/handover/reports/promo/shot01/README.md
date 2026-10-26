# Shot 1 prototype (Motion Canvas)

This is shot 1 of the main film ([../script.md](../script.md)), "One ink". It runs for 7.0 s at
1920×1080 and is drawn in the "Plotter on navy" style ([../style.md](../style.md)).

1. `func f(x: int): int { x * 2 + x * 2 }` is typed on the logo-navy ground, behind a gutter rule.
2. Each operator and constant lifts off the code line as a glyph, glides along the line and then
   curves into the frame of `f`. There it becomes a node, whose outline is traced around it (drawn,
   not popped), and each edge is one stroke.
3. The two equal halves are marked in sand and slide together. One `×` (and one `2`) remains, with
   two parallel strokes into `+`: one node, used twice.
4. x = 3 flows through as sand beads: 6, then 12. The result ring is re-traced in mint.
5. A Chinese subtitle is shown for preview. Set `SHOW_SUBTITLE = false` in the scene to hide it.

The beats and their times are listed at the top of `src/scenes/shot01.tsx`.

## Status

The scene was type-checked and rendered with Motion Canvas 3.17.2, in a scratch copy outside the
repository:

- `tsc` reports no errors.
- The editor's built-in **Image sequence** exporter rendered 424 PNG frames at 60 fps (7.07 s),
  driven through headless Edge.
- The frames were reviewed. One fix came out of that review: the glyph flight path now curves
  instead of crossing the header line.

No video file was encoded, because ffmpeg is not installed on this host. Fonts fell back to the
system faces during that render. With JetBrains Mono and Noto Sans SC installed, the code spacing
changes slightly, and the underlines follow it because they are laid out from the text's measured
width.

## Preview

```bash
cd docs/handover/reports/promo/shot01
npm install
npm run serve        # editor at http://localhost:9000; press Space to play
npm run check        # type-check only
```

## Render

1. In the editor, open **Video Settings** and set 1920×1080 at 60 fps.
2. On the **Rendering** tab, choose an exporter:
   - **Image sequence**, built in, writes PNGs to `output/`.
   - **Video (FFmpeg)** writes an mp4 directly; the `@motion-canvas/ffmpeg` package brings its own
     binary.
3. Click **Render**.

## Fonts

| Font | Used for |
|---|---|
| JetBrains Mono | everything on screen |
| Noto Sans SC | the subtitle |

Install them, or add `@font-face` rules imported from `src/project.ts`, so that the render matches
`style.md`.

## Extending to later shots

Give each shot its own scene in `src/scenes/`, listed in order in `src/project.ts`. Each scene starts
from the final state of the previous one: the mint ring of shot 1 becomes the input `x` of shot 2.
Once there is a second scene, move these pieces into a shared `src/lib/`:

- the palette constants;
- `addNode`, `edge`, `trace`, `fly` and `travel`.

Lay the whole canvas out once from the logo outline (`assets/logo.svg`) before animating shots 2–9,
so that the coda's pull-back lands on the camel (script, production notes).
