# camel-oversight

A DSH plugin that lets the decision-maker, running on another machine, see what this session is doing and
leave it messages without waiting for a git round trip.

| Route | Method | Answers |
|---|---|---|
| `/oversee/state` | GET | the target session's status (`idle`/`running`) and since when, every live agent, one summary per recent turn (tools called, how it ended), unacknowledged message ids, the build host's load |
| `/oversee/tail?n=50` | GET | the last n session events, one line each (turn start/end, user and assistant text, tool calls and results) |
| `/oversee/say` | POST | `{"id": "D-1123-43", "text": "...", "mode": "queue"}` queues a user message prefixed `[decision-maker · D-1123-43]`; `mode: "steer"` delivers it at the nearest step instead of the next turn; neither cancels the running turn; a repeated id is dropped |
| `/oversee/acks` | GET | what the model answered for each message |

The model gets two tools: `oversight_claim()`, which makes the calling session the target, and
`oversight_ack(id, status, note)` with status `taken`, `declined` or `done`. The target session also gets a
system-prompt paragraph naming the messages it has not acknowledged yet.

## Which session is the target

A process may hold many sessions, and none is guessed at. The target is the session that last called
`oversight_claim` (the coordinator does so at the start of each session, as its skill
`internal/coordinator/SKILL.md` says), or the session a request names (`session` in the `say` body or the
`tail` query). `/oversee/state` lists every root session with its directory, status and last activity, and
marks the claimed one. With no claim, `say` refuses and lists the sessions instead of delivering to an
unrelated one.

## Setup on the machine that runs DSH

1. Add the plugin to the profile (this directory's `cordis.patch.yml`); set `buildHost` to the ssh target of
   the build server if its load should be reported (key-based ssh, no prompt).
2. Keep the web server on loopback and publish it on the tailnet: `tailscale serve --bg <port>`. The routes
   sit outside the GUI's login; the tailnet is the gate, so its ACL decides who reaches them.
3. `camel-lanes` also registers `/camel/state` and `/camel/say`; set its `http: false` once this plugin is in,
   so there is one channel.

## Calling it

```sh
curl -s https://<host>.ts.net/oversee/state
curl -s -H 'content-type: application/json' \
     -d '{"id":"D-1123-43","text":"...","mode":"queue"}' https://<host>.ts.net/oversee/say
```
