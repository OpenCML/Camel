# Visual language: "Light in the dark"

## Register (the whole series keeps it)

This is a well-made technical film, not a game trailer: restraint and precision.

- **Lines:** nodes and edges are crisp, thin lines. Bloom only where meaning is (light running
  through a graph, a region at its reveal), and subtle there.
- **Ground:** plain dark, with at most a very slight vignette. No grain, no chromatic fringing, no
  dust or starfield, no haze.
- **Failure:** a failure is a clean break in a line plus the real message, calm and legible. A
  shatter is a few dozen fragments drifting apart slowly. No glitch effects.
- **Camera:** it holds still while code is read and moves only when the meaning moves, slowly and
  without tilting.
- **Sound:** quiet and sparse. A silence is real silence.

## 0. The idea

The film argues one thing: the graph is not reconstructed, it is the program. The graph is the
subject, so it is the clearest thing on screen.

- **Two graphs.** The traced graph (Python, beat 1) is cool grey-white; it breaks, and its pieces
  hang. The Camel graph (beat 3) is a faintly warmer white with a teal region, and it never breaks.
  Data runs through it as amber light, the one thing that glows.
- **Code becomes graph.** The characters of a token travel from the line into the node they create
  and become its label while the outline is drawn around them.

## 1. Palette (linear; only values above 1 bloom)

| Role | Value | Meaning |
|---|---|---|
| ground | about `#0B0D14` | the dark |
| code | `#A8BAE0`, keywords `#C7A8FF`, numbers `#8CFFC7`, strings `#FFC26B` | text, crisp |
| traced graph | (0.74, 0.8, 0.92) | what a tracer recovers |
| break | (0.92, 0.36, 0.36) | a graph break, a recompile; the offending code, the message |
| Camel graph | (0.78, 0.92, 0.9) | what was never thrown away |
| region | (0.5, 0.9, 0.82), a short bloom as it closes, a faint glass fill | a whole branch: both arms in one graph |
| data | (2.2, 1.35, 0.45) | a value running through an edge (blooms) |
| result | `#80FFC2` | a computed value |

## 2. Light, depth, camera

- **Bloom:** strength 0.42, radius 0.3, threshold 1.02: only the running light and the region's
  moment cross it.
- **Depth:** the graph is laid out in 3D (parameters and weight accesses sit behind the main chain).
  The camera is perspective (35° vertical FOV) with no handheld drift.
  - **Beat 1:** a still frame.
  - **Beat 2:** a slow drift.
  - **Beat 3:**
    - a still wide shot while the code is written;
    - one slow move in to the branch for the payoff.

## 3. Motion

- **Drawing:** outlines are traced (0.22–0.3 s) and edges stroke from source to target.
- **A tear:** the broken edge parts in two, the failing piece drifts a few pixels and turns the
  break colour, and the message types beneath the code.
- **The recompile:** every node, edge and box is one rigid fragment drifting slowly away, and
  labels fade where they stand. At the gather, the fragments fall into the caret.
- **A run:** a small amber head with a short trail, one hop per fifth of a beat.
- **Typing:** Python at about 60 characters per second, Camel at about 35. Messages type at a
  readable pace.

## 4. Text

- **Statements:** Noto Sans Light, 40–54 px, centred, no glow. The Chinese line sits below in Noto
  Sans CJK SC at 60 % size and 70 % opacity. Each is held for at least 1.5 s.
- **Code and labels:** Promo Mono NL (JetBrains Mono without ligatures).
- **Compiler messages:** verbatim, Promo Mono NL at 19 px in the break colour, under the code.

## 5. Sound

- **Beat grid:** 96 BPM, with one cue sheet for picture and score.
- **The cold open:**
  - a quiet low drone;
  - each tear a soft low knock with a dry tick;
  - the recompile a deeper knock with a short wash;
  - cut dead at the turn.
- **The turn:** silence until the fragments gather.
- **The reveal:**
  - a faint swell into the caret;
  - a quiet bell per node;
  - a soft chord as the region closes;
  - a chime when each run reaches its result;
  - a resolving chord under the last line.
- **Level:** peaks at about -6 dBFS, with no compression.

## 6. Format

- 1920×1080 at 60 fps, H.264, AAC 48 kHz stereo.
- A safe margin of 60 px for text.
