# Camel promo

Two short films for AI researchers and engineers. Each is narrated in English with Chinese
subtitles and drawn in the "Plotter on navy" style.

- **"The program is the graph"** (75 s, shots 0–10): what Camel is, from the graph you write to the
  logo. This film is rendered.
- **"Your library can teach the compiler"** (about 90 s, L1–L8): a sequel for library authors. It
  is scripted only.

| Path | What it holds |
|---|---|
| `script.md` | both scripts: picture, narration, subtitle and code per shot, plus the table of claims and their sources |
| `style.md` | the visual and sound style |
| `motion/` | the Motion Canvas project; one scene per shot in `src/scenes/`, shared drawing helpers in `src/lib/plotter.tsx` |
| `film/` | `cut.py` assembles the film, `voice.py` makes the scratch narration, and `NOTES.md` records timings, placeholders and the sequel plan |
| `logo/` | the logo work: the faithful trace, the vector rebuild and its animation, and the redesign studies |

The shipped mark itself is in `assets/brand/`.

## Render

Renders are not committed. Per-shot frames, MP4s and stills go to `renders/`, and cuts go to
`film/*.mp4`.

Rendering needs:

- Node 18 or later;
- ffmpeg;
- Chromium (through Puppeteer);
- the Noto Sans SC font, for the subtitles.

The code font, Promo Mono NL, is bundled in `motion/src/fonts/`. It is JetBrains Mono with its
ligatures removed and its family renamed (`derive.py`), so `->` shows as the two characters it is
typed with. It is licensed under the SIL Open Font License, Version 1.1 (`OFL.txt` beside it).

```sh
cd promo/motion
npm install
npm run check                          # type-check
npm run serve                          # the editor at http://localhost:9000 (Space plays)
```

To render one scene (or several) without the editor:

```sh
VITE_SCENES=shot06 npm run serve &     # load only these scenes
npm run render                         # headless: frames to output/camel-promo/shot06/
./encode.sh shot06                     # ../renders/shot06/shot06.mp4
```

Leave `VITE_SCENES` unset to load all eleven scenes. The beats and their times are listed at the
top of each scene file. Set `SHOW_SUBTITLE = false` in a scene to hide its subtitle.

To assemble the film:

```sh
cd promo/film
python3 voice.py af_heart 0.85         # scratch narration to ../renders/voice/ (Kokoro-82M; setup in the file)
python3 cut.py main-cut-v4.mp4         # shots, match cuts or dissolves, voice and captions
```

So far everything has been rendered inside the `camel-lanes` container, never on the host.
