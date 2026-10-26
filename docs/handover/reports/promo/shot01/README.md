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

## Status: rendered

`out/shot01.mp4` is the render: 1920×1080, 60 fps, 7.07 s, H.264. Three stills sit beside it:

| File | Moment |
|---|---|
| `out/still-1-start.png` | the start (1.0 s) |
| `out/still-2-merge.png` | the merge (4.1 s) |
| `out/still-3-end.png` | the end |

**How it was rendered.** In the Docker container `camel-lanes` (Ubuntu 24.04), under
`/work/promo/shot01`:

1. Install the fonts with `apt`: `fonts-jetbrains-mono` and `fonts-noto-cjk`.
2. Start the editor with `npx vite --port 9000`.
3. Drive the editor's built-in **Image sequence** exporter with headless Chromium through Puppeteer
   (`/work/promo/render.cjs`). This produces 424 PNG frames.
4. Encode with
   `ffmpeg -framerate 60 -i %06d.png -c:v libx264 -crf 16 -pix_fmt yuv420p out/shot01.mp4`.

`npx tsc --noEmit --skipLibCheck` passes. The `--skipLibCheck` flag is only needed in that container,
because installing Puppeteer there also installs a newer `@types/node`.

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
