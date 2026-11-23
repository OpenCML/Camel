# camel-lanes

A DSH plugin. Every few minutes it reads this repository and puts the lane state in front of the agent: how
many lane branches are unmerged, how far behind the worst one is, how many worktrees there are, and — since
the two gates described below — whether the task list being worked from is the one in the tree.

Its purpose is narrow and worth stating, because a panel that only reports numbers is easy to ignore. The
coordinator has obligations that are not enforced by anything: claim an item from the inbox before working
on it, and get the author's inbox into `develop` before dispatching from it. Both are one command away from
being checkable, and both were missed on the same afternoon — so they are checks now, and the block the
model reads every step says so without being asked.

## The gates

| Key | Counts | Limit |
|---|---|---|
| `unmerged` | lane branches not yet in `develop` | 20 |
| `maxBehind` | commits the worst lane branch is behind | 100 |
| `mergedLocal` | local branches already merged and not deleted | 20 |
| `mergedRemote` | remote branches already merged and not deleted | 10 |

The first four are branch hygiene: crossing one stops new branches being opened, because a branch a hundred
commits behind cannot be merged with a conflict surface anyone can reason about, and its own run cannot
produce a real red.


The four that remain are branch hygiene: crossing one stops new branches being opened, because a branch
commits behind cannot be merged with a conflict surface anyone can reason about, and its own run will
produce a real red.

Two gates went with the inbox. `inboxBehind` and `claimedNotDeleted` counted halves of a claim, and a
claim is no longer made in a file: work is dispatched and followed on the oversight channel, and which
lanes are running is read from the live subagents rather than from a table somebody maintains. Counting
a file nobody writes would have gone on reporting zero, which is the reading a gate must never be able
to make falsely.
## Two routes, and what they cost

The plugin registers on the web server that already serves the GUI, so there is no second
listener and no second lifecycle to keep alive.

| Route | Method | Does |
|---|---|---|
| `/camel/state` | GET | the four gate readings, the branches and worktrees behind them, and what the build host is doing |
| `/camel/say` | POST | `{"text": "..."}` becomes a user message in the configured session |

`/camel/say` is not a new mechanism. It calls the same `sessionController.prompt` the composer
calls when a person types, so a message arriving over the network becomes an ordinary turn with an
ordinary user message. Nothing it can say is something the GUI could not, and the transcript has
the same shape either way.

What that costs belongs next to the route rather than in a later paragraph: **a caller who can
reach the port can start a turn, and a turn can ask for anything this session can do.** So the
routes are off with `http: false`, and the response names the caller by address, because an
address is the only identity a plain HTTP request carries.

### Reaching it from another machine

The web server listens on **one** address. Binding it to a loopback literal keeps every other
machine out; binding it to a concrete interface address serves that network over plain HTTP. The
unspecified address is rejected at load, on purpose.

That means a Tailnet peer cannot reach a loopback-bound server, and moving the bind to the
Tailscale address would take the GUI away from `127.0.0.1` — the two are the same listener. The
way to have both is to keep the bind on loopback and put `tailscale serve` in front of it:

```powershell
tailscale serve --bg 3080
```

The consequence is worth saying plainly: a request forwarded that way arrives from `127.0.0.1`,
so the server cannot tell it apart from a local client. The address in `/camel/say`'s response
will read `127.0.0.1` and `claimedBy` will carry the Tailscale login `tailscale serve` attaches.
That header is evidence only because something upstream put it there; the *address* is what proves
a route. Any decision that treated "arrived on loopback" as "is local" would be wrong exactly here,
which is why there is no such branch in the plugin.

## Layout

- `index.js` — the host half. Measures, renders the block, registers the `camel_lanes_silence` tool.
- `client.js` — the chips in the conversation header.
- `cordis.patch.yml` — the insertion, with `repo` and the refresh interval.
- `locale/*.json`, `icon.svg` — the panel's labels and mark.

## Installing it

The plugin is loaded from a DSH profile, not from here. Link it in rather than copying, so this file stays
the source:

```powershell
$profile = "$env:USERPROFILE\.dsh\profiles\web\node_modules\@local\camel-lanes"
New-Item -ItemType Junction -Path $profile -Target "D:\Projects\Camel\internal\tools\camel-lanes"
```

Then the profile's `cordis.patch.yml` inserts it. `repo` in this package's own `cordis.patch.yml` is the
repository it reads; a second checkout needs its own copy of that value, not a second copy of the code.

The host half is picked up when the profile reloads. The client half needs the web bundle rebuilt —
`pnpm run dev:web` from the DSH checkout rebuilds it as it changes, and without that watcher a change to
`client.js` appears only after the bundle is built again.

## The silence tool

Every gate can be quieted for a number of minutes, and the silence expires on its own. A gate that is
genuinely over its limit comes back without anyone bringing it back — which is the only version of this that
does not end in a gate nobody reads. Pass zero minutes to bring one back immediately.
