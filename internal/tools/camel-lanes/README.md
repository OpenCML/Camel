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
| `inboxBehind` | inbox commits on `fsh-dev` that are not on `develop` | 0 |
| `claimedNotDeleted` | items marked `doing` in the backlog that are still in the inbox | 0 |

The first four are branch hygiene: crossing one stops new branches being opened, because a branch a hundred
commits behind cannot be merged with a conflict surface anyone can reason about, and its own run cannot
produce a real red.

The last two are about the task list. An item is claimed by deleting it from `internal/inbox.md` and entering
it in `internal/backlog.md` under the same number — both halves, because the inbox is what nobody has taken
and the backlog is what someone is doing. A number in both is an item that was taken and not removed, and the
inbox then reports it as waiting. And an inbox commit that is not on `develop` means the list being dispatched
from is a copy someone read, not the one in the tree: that is not visible from inside the branch, because a
file read from `origin/fsh-dev` looks exactly like a file in the tree.

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