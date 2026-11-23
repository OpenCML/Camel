# Two defects seen on first load, 2026-11-23 21:20

Both were observed on the running process after the plugin was merged and the process restarted. Neither
stops the channel from working; the first makes one tool unreadable and the second leaves one field of
`/oversee/state` empty.

## 1. `oversight_claim` returns a value the runtime refuses to render

```
Error: tool "oversight_claim" returned invalid output: output.render returned non-lossless JSON
```

The call's **effect happens**: `/oversee/state` afterwards reports `target` as the calling session with
`claimed: true`. What fails is the value the tool hands back to the model -- "non-lossless JSON" means
the render output could not be turned back into the tool's result without losing something, so the
runtime rejects it rather than showing a partial answer.

So this is not "the claim did not happen"; it is "the claim happened and the tool cannot say so". The
distinction matters because the failure message reads like a refusal, and a claim that silently worked
while reporting an error invites a second claim.

What to look at: the returned value probably carries something a JSON round-trip drops -- a function, an
`undefined` member, a `Map`, a `Date`, a class instance, or a value with a `toJSON` that is lossy. The
`render` wrapper is the place to compare what goes in with what comes out.

## 2. `build` is unconfigured even though `buildHost` is written twice

`/oversee/state` answers `"build": {"unconfigured": true}` after the restart, which means `config.buildHost`
was empty. The value is present in **two** layers:

- this package's own `cordis.patch.yml` (the file that carried it successfully before the restart:
  `/oversee/state` then reported `load`, `cores` and `testProcesses` read over ssh);
- the profile's `cordis.patch.yml`, as an `insert` of the same `id`.

Two `insert`s of one id is the likely cause: one of them mounts the plugin and the other is discarded,
and which one wins is not the one carrying the config. Before the restart the same arrangement did
report load, so the layering is at least order-dependent rather than uniformly broken.

The fix is to mount from one place only, with values, and keep the other as a documented example.