# Visual language: "Light in the dark"

The film argues one thing: the graph is not reconstructed, it is the program. Its look serves that
argument. The graph is the only bright thing on screen, and it lives in real depth, made of light.

## 0. The idea

- **The graph is light.** Strokes, nodes and data are emissive and bloom into the dark. Code is
  quiet text on the screen's surface; the graph hangs in space behind it, and the camera moves
  through that space.
- **Two graphs, two temperatures.**
  - **The traced graph** (Python, beat 1) is cold white-blue. It tears red and shatters into shards
    that hang.
  - **The Camel graph** (beat 3 on) is warmer white with teal glass. Data runs through it as amber
    light, and it never tears.
- **Code becomes graph physically.** The characters of a token lift off the line and fly into the
  node they create, landing as its label while the outline is drawn around them.
- **One idea per film.** This film proves one thing: the graph is the program. Its single payoff
  is the whole branch running in the graph that tore before, and then the pull-back to the mark.

## 1. Palette (linear HDR: values above 1 bloom)

| Role | Value | Meaning |
|---|---|---|
| background | `#03050C` → `#0A0F22`, vignetted | the dark; slow dust and soft out-of-focus lights give depth |
| code | `#A8BAE0`, keywords `#C7A8FF`, numbers `#8CFFC7`, strings `#FFC26B` | text; never blooms |
| traced graph | (1.25, 1.55, 2.1) | what a tracer recovers: cold |
| tear | (3.4, 0.3, 0.4) | a graph break, a recompile; red-hot, cooling |
| Camel graph | (1.2, 2.0, 2.1) | what was never thrown away |
| region | (0.45, 1.6, 1.4) with a glass fill | a whole branch: both arms in one graph |
| data | (3.6, 2.1, 0.7) | a value running through an edge |
| result | `#80FFC2` | a computed value |

## 2. Light, depth and camera

- **Bloom:** strength 1.05, radius 0.55, threshold 0.82. Only light blooms; text stays crisp.
- **Depth:** the graph is laid out in 3D. Parameters and weight accesses sit behind the main chain,
  and shards scatter in z. The camera is a moving perspective camera (35° vertical FOV) with a
  breath of handheld drift. It pushes in the cold open, drifts through the fragments in the turn,
  follows the graph as it is written, then pulls back and orbits.
- **Grade:**
  - an ACES tone map;
  - a soft vignette;
  - a little chromatic aberration at the edges;
  - film grain at 2 %.

  The glitch lives in the grade: slice displacement, RGB split and a brightness kick, for 0.1–0.5 s
  after each tear.

## 3. Motion

- **Drawn, not popped:** outlines are traced (0.22–0.3 s) and edges stroke from source to target.
- **Shards:**
  - **The break:** a shattered shape breaks into pieces of 30–60 px, which burst (strength 90–230),
    spin, cool from red-hot to dim white, and drift.
  - **Gathering:** at the gather they fall into one point.
- **Light:** a run is a comet, a bright head with a short trail, one hop per fifth of a beat; a
  node flashes as the light arrives.
- **Typing:** Python at about 60 characters per second (a familiar thing, quickly); Camel at about
  35 (read every token).

## 4. Text

- **Statements:** Noto Sans Light, 40–54 px, centred, with a soft blue glow. The Chinese line sits
  below in Noto Sans CJK SC at 60 % size and 70 % opacity. There are three statements in beats 1–3,
  each held for at least 1.5 s.
- **Code and labels:** Promo Mono NL (JetBrains Mono without ligatures, so `->` and `>=` show the
  characters typed).
- **Compiler messages:** Promo Mono NL at 19 px in tear red, typed fast and verbatim, under the code.

## 5. Sound

- **One beat grid:** 96 BPM. Picture and score share one cue sheet.
- **The cold open:**
  - a low drone whose filter opens as the trace goes wrong;
  - a soft pulse, hats under the trace, and a click per typed character;
  - a glitch hit per tear (a sub drop, a bit-crushed burst, a stutter);
  - the recompile as the largest hit;
  - a riser cut dead at the turn.
- **The turn:** silence, then one high shimmer.
- **The reveal:**
  - a reverse swell into the caret, then a bell per node, rising through A major pentatonic;
  - a chord when the branch closes whole;
  - an arpeggio along each run of light, a chime on each result;
  - a resolving chord under the last line.
- **No voice for now:** text and music carry it.

## 6. Format

- 1920×1080 at 60 fps, H.264 (CRF 16), AAC 48 kHz stereo.
- A safe margin of 60 px for text.
