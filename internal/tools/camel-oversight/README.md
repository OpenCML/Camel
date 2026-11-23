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
running, lanes running/total, develop's age, unacknowledged dispatches, the build host's load, open alerts;
click it for each alert, the recent reminders and the lanes. Toasts at the top right for every reminder,
acknowledgement and coordinator message; an urgent reminder stays until clicked.

## Setup on the machine that runs DSH

1. Install as a bundle (the profile's `bundles` list and a `link:` dependency), mounted in one place.
2. Machine-local values in `%USERPROFILE%\.camel-oversight\config.json`:
   `{"repo": "D:\\Projects\\Camel", "buildHost": "<ssh target>"}`; `repo` turns the watch on, `buildHost`
   (key-based ssh) adds the build host's load. Optional: `logFile`, `stateDir`, `watchSeconds`, `limits`.
3. Keep the web server on loopback and publish it on the tailnet with `tailscale serve --bg <port>`. The
   routes are outside the GUI's login; the tailnet is the gate.
4. After a code change, reload the DSH process; a session sees new tools only once it starts afresh.

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
