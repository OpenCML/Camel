# Shot 01 prototype (Motion Canvas)

This prototype is shot 1 of the main film ([../script.md](../script.md)). It runs for 7 s at
1920×1080:

1. `func f(x: int): int { x * 2 + x * 2 }` is on the left. The body is typed while its graph grows
   inside the dashed frame of `f` on the right.
2. The two equal halves get a sand mark, then collapse into one `×` node (and one constant `2`).
   Two parallel edges are left going into `+`, which shows one node used twice.
3. x = 3 flows through: 6, then 12, and the result ring turns mint.
4. A Chinese subtitle is shown for preview. To hide it, set `SHOW_SUBTITLE = false` at the top of
   the scene.

The beats and their times are listed at the top of `src/scenes/shot01.tsx`. Colours and motion rules
come from [../style.md](../style.md).

## Status

| Check | Result |
|---|---|
| Type-check against `@motion-canvas/core` and `@motion-canvas/2d` 3.17.2 | Passes (`tsc`, no errors) |
| Render or preview | Not done: rendering on this Windows host would need vite, the editor UI and ffmpeg, which were deliberately not installed |

Expect small layout tuning on the first preview: the spacing of the typed glyphs, and the position
of the `=` mark.

## Preview

```bash
cd docs/handover/reports/promo/shot01
npm install
npm run serve        # opens the editor at http://localhost:9000; press Space to play
npm run check        # type-check only
```

## Render to video

1. In the editor, open **Video Settings** and set 1920×1080 at 30 fps (60 fps for the master).
2. On the **Rendering** tab, choose the exporter **Video (FFmpeg)**, then click **Render**.

The file lands in `output/`. `@motion-canvas/ffmpeg` brings its own ffmpeg binary. For a PNG
sequence, choose the **Image sequence** exporter instead.

## Fonts

The scene uses three fonts, with system fallbacks:

- JetBrains Mono, for code and node glyphs
- Inter, for labels
- Noto Sans SC, for the subtitle

Install them, or add `@font-face` rules and import the CSS from `src/project.ts`, so that the preview
matches `style.md`. The code underlines are laid out from the measured width of the text, so they
stay aligned whichever font is used.

## Extending to later shots

Each later shot should be its own scene in `src/scenes/`, listed in `src/project.ts` in order. To
make the match cuts work, a scene should start from the final state of the previous one: shot 1's
result ring becomes shot 2's input `x`. The palette constants and the `addNode`, `edge`, `pop` and
`travel` helpers are the pieces to move into a shared `src/lib/` once there is a second scene.
