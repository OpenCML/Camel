# camel-oversight

The collaboration engine between the decision-maker (another machine) and the coordinator (a session in
this DSH process). How the two use it is in `internal/collab/SKILL.md`; this page is about the plugin.

## Three parts

**The channel.** The decision-maker reads state and sends dispatches; the coordinator acknowledges and
speaks back, and both are pushed as they happen.

| Route | Method | Answers |
|---|---|---|
| `/oversee/state` | GET | the claimed session (idle/running, since when), its live lanes (subagents), open alerts, recent reminders, the latest measurements, every root session, recent turns, unacknowledged dispatches |
| `/oversee/tail?n=50` | GET | the last n session events, one line each |
| `/oversee/say` | POST | `{"id": "D-1123-43", "text": "...", "mode": "queue"}` queues a dispatch prefixed `[decision-maker · D-1123-43]`; `mode: "steer"` delivers at the nearest step; a repeated id is dropped |
| `/oversee/acks` | GET | acknowledgements and the coordinator's own messages |
| `/oversee/events` | GET | server-sent events: acknowledgements, `oversight_tell`, reminders, the claimed session ending, and a `beat` after each measurement |
| `/oversee/reload` | POST | sync the plugin directory out of `origin/develop` into the live directory, reload this one plugin, and answer with the commit it came from; a tree that fails a check is refused with `file:line` and the live directory is left alone |

Model tools: `oversight_claim`, `oversight_ack`, `oversight_tell`, `oversight_readings`.

**The watch.** Every `watchSeconds` (300) the plugin measures the repository (develop's age, fsh-dev not
yet merged, the main checkout, lane branches ahead and behind, merged branches not deleted), the backlog,
the lanes and the build host (`measure.js`), and runs the rules (`rules.js`). A rule that finds nothing
says nothing, so a healthy hour costs the coordinator no context. A finding becomes a reminder once it has
held for the rule's `hold` and was not delivered within its `cooldown`; all reminders due in one beat go
out as one message prefixed `[oversight · 提醒]`, at the loudest level among them:

| Level | Delivery |
|---|---|
| quiet | `agent.inject`: context at the coordinator's next step; never wakes it |
| wake | the same while it runs; `agent.followup` (a new turn) when it is idle |
| urgent | `agent.steer`: the nearest step |

Once an hour, while the coordinator is at work, the watch also re-states the standing rules from
`internal/collab/rules.md` (read fresh each time), so a long context does not drift from them. This
replaces the coordinator's scheduled prompts; the thresholds live in `rules.js` and nowhere else.

**The panel** (`client.js`). Above the composer of the claimed session, a band: the coordinator idle or
running, lanes running/total, develop's age, unacknowledged dispatches, the build host's load, open alerts,
the plugin's version, and a **reload button** (see below). Click the band for each alert, the recent
reminders and the lanes. Toasts at the top right for every reminder, acknowledgement and coordinator
message; an urgent reminder stays until clicked.

## Setup on the machine that runs DSH

1. Install as a bundle (the profile's `bundles` list and a `link:` dependency), mounted in one place.
2. Machine-local values in `%USERPROFILE%\.camel-oversight\config.json`:
   `{"repo": "D:\\Projects\\Camel", "buildHost": "<ssh target>"}`; `repo` turns the watch on, `buildHost`
   (key-based ssh) adds the build host's load. Optional: `logFile`, `stateDir`, `watchSeconds`, `limits`,
   `liveDir`, `syncScript`, `syncRef`.
3. Keep the web server on loopback and publish it on the tailnet with `tailscale serve --bg <port>`. The
   routes are outside the GUI's login; the tailnet is the gate.
4. What needs a DSH restart and what does not: `rules.js` (the rules, their texts and default limits) and `measure.js` (the readings) are reloaded at the next beat after they change on `origin/integration` (or `origin/develop` where there is no integration branch: the pushed, gated content -- never the working tree, which holds a merge in progress), and a file that fails to load leaves the version in use in place (reported as a `hot-reload` event and under `hotLoaded` in `/oversee/state`); the limits in `~/.camel-oversight/config.json` and `internal/collab/rules.md` (also read from that line) are read afresh too. `client.js` (the band) is a browser module and needs a page refresh, and a session sees new tools only once it starts afresh; `index.js` (routes, tools, the session wiring) is replaced by the reload route below.
5. Point the plugin's `link:` dependency at the live directory, not at the repository (next section). That
   is the one change that needs a DSH restart, and the only one.

## Reloading it without restarting DSH

Restarting DSH to change a plugin stops every agent at work, so the owner chose a button (2026-11-24,
`internal/plans/2026-11-24-oversight-live-reload.cn.md`). The plugin is loaded from a directory that holds
only pushed content -- `D:\Projects\camel-oversight-live\` by default -- because the main checkout holds
merges in flight and lane half-work, and DSH's replacement does not fall back to the code it replaced.

**The button.** The band's `重载插件` calls `POST /oversee/reload`, which:

1. runs `internal/tools/sync-oversight-live.mjs`: it reads the plugin directory out of `origin/develop`
   (`git ls-tree` + `git cat-file`, no worktree, no `stash`), writes it to a temporary directory beside the
   live one, and gates every `.js` with `node --check`, then imports the staged `index.js` and `client.js` in
   a child process. A file that fails stops everything: the temporary directory is removed, the live
   directory is not touched, and the answer carries `file` and `line`;
2. reloads this one plugin through DSH's profile reconciliation (`ctx.hmr.reconcileProfile`, which is what
   the Plugin Manager's own private `reload` calls -- it has no public method for this);
3. answers with the commit sha the directory came from, which the band shows.

Content decides, not timestamps: when the live directory already holds that commit's bytes, the route
answers `changed: false` and reloads nothing, so a merge that does not touch the plugin never hiccups a
running coordinator.

**What it cannot promise.** DSH's replacement is stop-then-start, and a plugin whose replacement fails to
start does not keep running: its previous code is gone and the route that would report the failure is
inside it. Both gates exist to make that unreachable rather than to report it -- a tree that does not
parse, or whose `index.js` throws while it is evaluated, is refused before the live directory moves. What
is left is `apply()` itself throwing against a real context; when that happens the plugin is down until
DSH restarts, the log line `reload-failed` records why, and `D:\Projects\camel-oversight-live.previous\`
holds the tree that was running before, for a manual restore. The answer is written before the restart
starts, so the wire always carries the commit that was being tried.

**`client.js` still needs a page refresh.** It is a browser module, built and served separately from the
host half: reloading the plugin replaces `index.js` and its routes, not the module the page already holds.
A change to the band -- including this button -- appears after the owner refreshes the page, and a session
sees changed model tools only once it starts afresh.

DSH conventions this plugin depends on: a tool's `render(args, value)` returns content blocks
(`[{ type: 'text', text }]`); `agent.inject` / `followup` / `steer` take a user message
(`{ id, role: 'user', source: { kind: 'user' }, content }`); client components register into slots with
`ctx.slots.inject(name, () => ctx.slots.register({ name, id }, Component))`.

## Keeping the coordinator from drifting

A long session forgets its skill. Three layers keep the standing rules (`internal/collab/rules.md`) in
front of it, each measurable: the rules are part of the claimed session's prompt context at every step
(`promptReach` in `/oversee/state` counts whom the prompt was assembled for); they are injected every
`rulesRefreshMinutes` (30) and right after a claim; and on sight -- a call to a question tool, or a turn
that ends by asking the owner, is answered with a reminder at once; a tool call that blocks the claimed
session is reported at `blockWarnMinutes` (10) and its turn cancelled at `blockCancelMinutes` (30), queued
messages kept (`blockExempt` lists tool names that may run long).

## When a side drops

Every acknowledgement, message and reminder is kept (and rebuilt from the log at load), so a reader that
lost the stream reconnects with `/oversee/events?since=<ms>` and receives what it missed. The decision-maker's
client adds `client=dm` to its requests; after `dmOfflineMinutes` (45) without one, the coordinator is told
to carry on without waiting. With no claim, a session working in the repository is told in its prompt that
the coordinator claims first. The full contingency table is in `internal/collab/SKILL.md`.

## Back-pressure

A backlog worked through in order makes anything newer or more urgent wait behind stale messages, so each
direction has a limit and past it the sender reorganizes. `/oversee/say` refuses a dispatch with `429` once
`queueLimit` (3) unread dispatches sit in the coordinator's queue; a dispatch with `"supersedes": [ids]`
takes those still unread out of the queue (`agent.inbox.remove`) before it is delivered, and reports which
it withdrew and which were already read. `/oversee/state` lists `queuedDispatches`. The plugin's own
messages replace their unread predecessor of the same kind. A reader back after more than five missed
events gets one `missed` summary instead of a replay.
