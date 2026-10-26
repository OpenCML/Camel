# Camel promo

A series of short films for AI researchers and engineers. Each film makes one point, with its own
tension and reveal, drawn in real depth and light; text and music carry it.

## The series

1. **The graph was always there** (about 50 s, in production). AI compilers trace Python to recover
   a graph the language threw away, and the trace tears; in Camel the code is the graph, and the
   `if` that tore stands whole and runs. [script.md](script.md) has the beats and the claims with
   their sources, [style.md](style.md) the look and sound, and `poc/` the proof of concept
   (beats 1–3).
2. **The gradient is a graph**: `grad` is a function that takes a graph and returns one, which the
   compiler then optimizes like any other.
3. **Parallel for free**: work that does not depend on other work runs at the same time, with no
   threads or `async` written by hand.
4. **A pass is a function**: the compiler's passes are functions from a function to a function,
   declared in the standard library and composed by name.
5. **Errors are values**: a failure travels beside the happy path and is handled once; a bug is
   fenced at a function boundary.
6. **Your library can teach the compiler**: a library ships its operations, native kernels and
   checked rewrite rules, and users hand those rules to the optimizer by name.

| Path | What it holds |
|---|---|
| `poc/` | film 1's renderer: three.js scenes drawn frame by frame as a function of time (`src/film.js`), headless rendering (`render.cjs`) and the synthesised score (`score.py`) |
| `motion/` | the earlier feature-tour film (Motion Canvas), superseded; its bundled code font `src/fonts/` (Promo Mono NL, SIL OFL) is still used |
| `film/` | `cut.py` and `voice.py` from the earlier film, and its `NOTES.md` |
| `logo/` | the logo work: the faithful trace, the vector rebuild and its animation, and the redesign studies |

The shipped mark itself is in `assets/brand/`.

## Render the proof of concept

Renders are not committed. Frames, the score and the MP4 go to `renders/poc/`.

Rendering needs:

- Node 18 or later;
- ffmpeg;
- Python 3 with numpy and scipy;
- the Noto Sans and Noto Sans CJK SC fonts.

The WebGL runs in software: Chromium's ANGLE over Mesa llvmpipe (see the flags in `render.cjs`).

```sh
cd promo/poc
npm install
npm run serve &                           # http://localhost:9100/?play plays it in real time
npm run cues                              # ../renders/poc/cues.json: the beat grid the score follows
npm run render                            # ../renders/poc/frames/000000.png ... (60 fps, one page)
python3 score.py ../renders/poc/cues.json ../renders/poc/score.wav
ffmpeg -framerate 60 -i ../renders/poc/frames/%06d.png -i ../renders/poc/score.wav \
  -c:v libx264 -preset slow -crf 16 -pix_fmt yuv420p -c:a aac -b:a 256k -shortest \
  -movflags +faststart ../renders/poc/poc-v1.mp4
```

To render single frames, use `node render.cjs --out DIR --times 5.6,10.0,24.4`.

`npm run render` uses one page. Rendering several pages at once (`--workers N`) needs a machine
with a GPU. Without one, WebGL runs in software and parallel pages run out of memory: they
crashed the Docker VM. Render full films on your own machine, not in the build container.
