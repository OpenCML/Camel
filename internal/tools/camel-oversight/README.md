# camel-oversight

A DSH plugin that lets the decision-maker, running on another machine, see what this session is doing and
leave it messages without waiting for a git round trip.

| Route | Method | Answers |
|---|---|---|
| `/oversee/state` | GET | the target session's status (`idle`/`running`) and since when, every live agent, one summary per recent turn (tools called, how it ended), unacknowledged message ids, the build host's load |
| `/oversee/tail?n=50` | GET | the last n session events, one line each (turn start/end, user and assistant text, tool calls and results) |
| `/oversee/say` | POST | `{"id": "D-1123-43", "text": "...", "mode": "queue"}` queues a user message prefixed `[decision-maker · D-1123-43]`; `mode: "steer"` delivers it at the nearest step instead of the next turn; neither cancels the running turn; a repeated id is dropped |
| `/oversee/acks` | GET | what the model answered for each message |

The model gets one tool, `oversight_ack(id, status, note)` with status `taken`, `declined` or `done`, and a
system-prompt paragraph that names the messages it has not acknowledged yet.

## Setup on the machine that runs DSH

1. Put a random token of at least 16 characters in `%USERPROFILE%\.camel-oversight\token` (or set
   `tokenFile`). Without it every route answers 503. The routes are raw web-server routes, outside the GUI's
   launch-token login, so the token is their only lock besides the network.
2. Add the plugin to the profile (this directory's `cordis.patch.yml`); set `buildHost` to the ssh target of
   the build server if its load should be reported (key-based ssh, no prompt).
3. Keep the web server on loopback and publish it on the tailnet: `tailscale serve --bg <port>`. Restrict the
   tailnet ACL so that only the decision-maker's machine reaches this one.
4. `camel-lanes` registers `/camel/say` without authentication; set its `http: false` once this plugin is in.

## Calling it

```sh
curl -s -H "Authorization: Bearer $TOKEN" https://<host>.ts.net/oversee/state
curl -s -H "Authorization: Bearer $TOKEN" -H 'content-type: application/json' \
     -d '{"id":"D-1123-43","text":"...","mode":"queue"}' https://<host>.ts.net/oversee/say
```
