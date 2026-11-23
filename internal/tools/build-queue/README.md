# Build Queue panel for DSH Web

A small, self-contained DSH client plugin that shows the Camel build queue in the
Web GUI: the current build, the waiters, a timeline of recent builds, and the
machine's CPU/memory load.

- Source: this directory (`@local/dsh-build-queue`)
- Host half: `index.js` (reads the state file, samples CPU/memory, serves two
  authenticated routes)
- Client half: `client.js` (a prebuilt browser module, no build step)
- Bundle row: `cordis.patch.yml` inserts `build-queue` with `config.statePath`

## Panel states

The panel has three states. They are independent: hiding never rewrites the
geometry or the collapsed choice.

| State | What you see | Stored in | Survives reload |
| --- | --- | --- | --- |
| **Expanded** | Full panel, header + body | — | — |
| **Collapsed** | A 220×32 chip at the panel's top-right corner, showing the current build or `空闲` | `localStorage["dsh.buildQueue.collapsed"]` = `1` | yes |
| **Hidden** | Only a 6×44 px tab on the right edge, at the panel's height | `sessionStorage["dsh.buildQueue.hidden"]` = `1` (informational only) | **no** — a reload brings the panel back |

Geometry (position and size) lives in
`localStorage["dsh.buildQueue.geom"]` as `{ "x", "y", "w", "h" }`, where `h` may
be `null` for automatic content height. The chip and the hidden tab never use
`w`/`h`; they clamp themselves inside the viewport.

## Interaction

- **Drag** the header (or the chip) to move the panel; a click under 4 px still
  toggles collapse.
- **Resize** from the bottom-right grip; minimum 320×240, maximum viewport − 24.
  Widening near the right edge pushes the left edge left, so the grip stays
  reachable.
- **Collapse** by clicking the header (chevron).
- **Hide** with the dash button next to the chevron. While hidden, polling stops
  completely and the panel can be restored from the right-edge tab.
- **Alt+Shift+B** (ignored while typing in the composer or any editable field):
  - panel hidden → restore it;
  - otherwise → reset the geometry to the default top-right position, expand,
    and clear `dsh.buildQueue.geom`.

## Recovery

If the panel is ever unreachable, run this in the DevTools console and reload:

```js
localStorage.removeItem('dsh.buildQueue.geom');
localStorage.removeItem('dsh.buildQueue.collapsed');
location.reload();
```

Then press **Alt+Shift+B** for the same reset without DevTools.

## Host routes

Both are registered on the Connection service and require the normal session
cookie:

- `GET /api/build-queue.state` → `{ ok, now, state }` where `state` is the
  normalized `build-state.json` (or `null` while it is missing/mid-write).
- `GET /api/build-queue.resources` → `{ ok, now, cpu: { percent, cores, sampled },
  memory: { total, used, free, percent }, load }`. CPU is the whole machine,
  derived from per-core busy deltas since the previous call (the first call is
  unknown); `load` is `null` on Windows, where `os.loadavg()` is meaningless.

The panel polls both routes once per second, and only while the page is visible
and the panel is not hidden.

## Notes

- No build step: `client.js` is the artifact served to the browser.
- HMR re-serves the bundle automatically after an edit; the open page needs a
  refresh for the new module.
- Theme colours come from `--dsw-alias-*` tokens only.