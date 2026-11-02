# Branching And Merge Rules

This repository uses a strict `main` / `develop` / feature-branch workflow. Follow these rules whenever you perform merges, release preparation, or branch synchronization.

## Core Policy

- Do not develop directly on `main` or `develop`.
- End every release or merge workflow back on a feature branch, not on `main` or `develop`.
- Use merge commits, not fast-forward merges, for both feature integration and release promotion.
- Treat merge and release workflows as strictly sequential operations on shared git state.
- Never run git write operations in parallel during merge or release work.

## Feature Branch To `develop`

- Merge feature branches into `develop` with `git merge --no-ff`.
- Keep the merge commit explicit.
- The merge commit message must summarize the integrated work itself, not merely the merge action.
- Do not use vague subjects such as `merge: integrate xxx`, `merge branch`, or `release: merge develop`.
- A valid merge commit subject should describe the shared product change, feature set, refactor theme, or release content that the merge introduces.
- After the merge completes, rebase the merged feature branch so it stays synchronized with the updated `develop`.
- After the merge, verify branch ancestry before proceeding.

### A lane may not accumulate: the ten-commit rule

A lane's age is measured in `develop`'s API changes, not in days. A lane holding forty unmerged
commits is a fuse: every change `develop` makes in the code the lane also edits is a merge that has
not happened yet, and every commit taken out of the lane by diff is a chance to miss one.

The rule, and who counts what when:

- **The lane owner**, before starting a new piece of work, runs
  `git rev-list --count origin/develop..HEAD`. If it is **more than 10**, the lane merges into
  `develop` **first** and only then continues.
- **The lane owner**, after every merge of `develop` into the lane, runs the same count and records
  it in the merge commit. A lane that merges `develop` in and then adds ten more commits without
  going back has already crossed the line again.
- **The coordinator (or the merge lane)**, when a lane is handed over, runs the same count before
  deciding how to take it. **A lane over 10 is taken whole (`git merge --no-ff`), not by diff** —
  taking a commit out of a long lane is how a dependency gets missed (the include that `01ff08254`
  needed is the example on record).
- **After any merge, the person who merged runs `git rev-list --count origin/develop..HEAD`, which
  must be 0 — or 1 when the merge commit itself has not been pushed yet.** Anything else means a
  commit is still outside the door, which has happened twice and was caught only by this count.

A lane may be red; `develop` may not. When a lane's tip is red, take its **last commit that was
green** and say so in the merge message, so the red's cause lands with the next commit instead of
sitting on `develop`.

### A lane squashes its own wip before it merges

The author's ruling of 2026-11-02 13:30, after a whole-lane merge carried a lane's work-in-progress
states into `develop`'s history: **"add then take back" commits are squashed by the lane into one
complete commit before the lane merges into `develop`; force-pushing one's own lane branch is
allowed; the merging side does not squash for the lane.**

The instance on record: `796f5ae07` ("The tangent bit exists but does not yet reach the resolver: the
closed shape is asked for and never given") and `2850834ab` ("The closed half's type answers
`(ct) => (cotangents)`: the marker split puts on `lin`, and the red it makes") were written as
steps inside one piece of work, and both are now on `origin/develop` (and on `fsh-dev`,
`lane-casemig`) because the lane handed over a history that still contained them. Neither is a state
anyone would want to bisect to or revert to.

Executable form, in this order:

- The **lane**, before handing over: `git log --oneline origin/develop..HEAD` and read it as a
  history, not as a to-do list. A commit whose message describes the state *between* two finished
  ones ("does not yet reach", "still red", "wip", "try"), or whose successor reverts part of it, is
  folded into the commit it belongs to (`git rebase -i origin/develop`, `git reset --soft` to the
  base, then commit once) and the lane is pushed again with
  `git push --force-with-lease origin lane-xxx`.
- The **merging side**, before merging: the same `git log --oneline origin/develop..origin/lane-xxx`
  goes into the merge message, one line per commit. If a wip pair is still there, the lane is
  returned to its owner to squash. The merger does not squash it: rewriting a lane's history is the
  owner's act, and a merger that does it silently leaves the owner's branch and `develop` disagreeing
  about the same work.

The rule does not license rewriting history that is already on `develop`. It is about the state of
the lane **before** the door.

### A reading carries the tree, the sha, the build time and the clock

A number in a report is only a number until the four facts around it are written down: **which tree**
it was measured in (a worktree path, not "the repo"), **the sha `origin/develop` was at when it was
fetched** (not the sha someone quoted an hour earlier), **when the binary that produced it was
built** (the runner prints it as `binary built <timestamp>`), and **when the measurement was taken**.

The instance on record: a page was priced against `95d6ab4fc` while `origin/develop` had already
moved to `8529eb9c4`, so the "current" figure was two merges old and nobody could tell from the page
itself. `git rev-parse --short origin/develop` **after** the fetch, pasted next to the number, is
what settles it.

### A lane's reading is not develop's number

The same numbers measured on a lane tree and written as `develop`'s are wrong for every reader who
arrives after the next merge, and they are wrong silently, because the shape of the number is
unchanged. A lane that quotes line counts, test totals, timings or file lists says **which tree** it
read them in; a page that needs a `develop` figure measures it on `develop` (`git show
<develop-sha>:<path> | wc -l`, not a file in the lane's working directory) or states the lane and the
merge base it belongs to.

The instance on record: the closure page
`docs/handover/reviews/questions-2026-11-01-closures-as-values-block-the-transposed-backward-half.md`
carried six line counts measured in a lane tree and read as `develop`'s; they were off by up to 34
lines, and one path named a file by a name two files share. The page now carries the command and the
sha beside the numbers, which is what makes the next reader able to re-measure instead of trust.

### "Green" is only green on today's `develop`

A lane that reports a green suite reports it about **the `develop` it last merged in**. Five times in
one night a lane was green and still not mergeable, because the tree it measured predated a change
its own work would have to live with. Two checks before merging a green lane, both required:

- `git merge-base --is-ancestor origin/develop <the commit to merge>` — **must succeed**. If it
  fails, the green belongs to an older world: the lane merges `origin/develop` in, re-runs, and
  reports again.
- `git rev-list --count origin/develop..<the commit to merge>` — the lane's own commits, a different
  number from how far behind it is, and both are worth knowing.

One exception: a commit behind by **documentation only**, with each intervening commit named and its
file list shown in the merge message. Documents cannot make code green or red, so the reading holds.
Being behind by a commit that touches code is not an exception — it is the thing the check exists
to catch.

### When a ruling and a reading disagree

A ruling is a decision about what the language should be; a reading is what the tree does today. They
can disagree, and when they do, **do neither of the two easy things**: do not execute the ruling as
if the reading were noise, and do not stop and wait.

- **Take the default the page itself wrote down.** If the page that asked the question names a
  default and a criterion for abandoning it ("if the suite drops, something calls it directly and it
  is not a candidate"), the measurement is what applies that criterion. A reading of 508 failing
  cases is the criterion answering.
- **Write the conflict as a question page** and put it in `questions/`, with the reading, what was
  tried, and the paths: the ruling stands as written in the record, and the person who ruled gets to
  rule again knowing the number.
- **Say in the merge or fix commit that the ruling was not executed, and why.** Otherwise the next
  reader finds a ruling on `develop` and assumes it was carried out.

The example on record: the ruling said the eleven `:op/ne_*` rows are a second implementation of
`==` and should go. Deleting them took the suite from `8390/8374/0` to `8390/7866/508`, because
`modules/graph/module.cpp#neKernel` builds a `__neq__` group for the types that have no `==` to fall back
on. The rows stayed, a question page carried the reading, and the ruling's own text stayed in the
record untouched.

**The conflict ends when the ruling comes back with a path.** If the person who ruled responds with
how to do it — change the call sites to an equivalent form first, then delete — the state is no
longer "ruling vs reading": it is a migration with an order, and the lane does it. Move the page to
`reviews/` at that point, because a ruled page left in `questions/` turns the index into a list of
questions that have been answered. The example above ended exactly this way: the author answered with
"grep for the URI, read `==` and negate, then delete", and the page went to `reviews/`.

## Coordinator Workflow: Handover Branches And `wzj-dev`

The coordinator's own work follows a two-step integration so the remote stays quiet and the task book's readers still see progress.

- A `handover/Hxx-...` branch is a **local staging branch**: do not push it to the remote, and do not create a remote branch for it. The only remote branch that represents the coordinator's development is `wzj-dev`.
- Merge each finished handover branch into `wzj-dev` first (`git merge --no-ff`), and promote `wzj-dev` into `develop` in batches — at least every half hour, so an AI reading `develop` sees the progress quickly. Pushing `develop` and `wzj-dev` is what makes the work visible.
- As soon as a handover branch is merged into `wzj-dev` and its worktree is removed, delete the branch. If an older remote `handover/...` branch still exists from before this rule, delete it too; the commits are in `develop`.
- The rules above still hold: merge commits only, never rebase `develop`, never force-push, never touch `main`.

## The `release` Branch: Where A Version Is Assembled

- `release` is the branch a version is assembled on and the only branch a tag is made from; `main`
  is moved to the tagged commit afterwards. It is cut from `develop` and lives one version.
- Assemble a version there with `git merge --no-ff <develop boundary>` -- the boundary is the last
  `develop` commit that version ships -- then write its `changes.log` section, tag, and run
  `node scripts/setver.js <next>` for the following development version.
- Push `release` **before** creating the tag. `.github/workflows/ci.yml` and `build-pypi.yml` run on
  that push (about 40 minutes, in parallel), so the tag, `main` and the published wheel only move
  once the run is green. A red run on `release` costs a push; a red run on the tag costs a
  published version that cannot be taken back.
- A fix that run asks for is made on `release` when it is a build, test or timing bound. A fix that
  changes meaning is made on `develop` and `git cherry-pick -x`-ed onto `release`, so both sides
  hold the same content and the later merge is a no-op.
- After the tag, merge `release` back into `develop` with `git merge --no-ff release`, which is how
  the notes, the version bump and any release-branch fix reach the next version.
  `git merge-base --is-ancestor release develop` must be true afterwards.
- `develop` is not a trigger of either workflow: it takes a merge every few minutes, and a leg that
  started on it would spend 40 minutes measuring a tree nobody releases.

## `develop` To `main`

- Only merge `develop` into `main` when intentionally releasing a new version.
- Merge `develop` into `main` with `git merge --no-ff`.
- The release merge commit on `main` must summarize the released content, not just state that `develop` was merged.
- Before merging `develop` into `main`, explicitly ask the user to confirm:
  - the release version number to publish now
  - the next development version number after release
- The usual version progression is to increment the trailing component by 1, for example `v0.1.22 -> v0.1.23`, but you must still ask the user to confirm.
- Before merging `develop` into `main`, check whether `changes.log` already contains the release notes for the pending release.
- If `changes.log` is missing the release notes, summarize them from the relevant git commit history and prepare a `changes.log` update on `develop`.
- Before committing that `changes.log` update on `develop`, ask the user to confirm that the proposed release-note content is appropriate.
- The version tag is created on the `release` commit that carries the notes, once that commit's run
  is green; `main` is then moved to that same commit, so the tag, `main` and `release` name one
  tree. `main` is an ancestor of it (every earlier release is in `release`'s history), so the move
  is a fast-forward; when it is not, stop and read the graph rather than forcing it.
- The release merge commit on `main` should name the key released changes; avoid redundant action words such as `publish`, `merge`, or the branch name unless they are necessary to keep the subject clear.

## Post-Release Version Bump

After tagging the release on `main`:

1. Switch to `develop`.
2. Rebase `develop`.
3. Run `npm run setver x.x.x`.
4. Run `npm i`.
5. Commit the version bump immediately with:
   - `chore: update to version vx.x.x`

## Final Branch State

After the post-release version bump commit on `develop`:

1. Switch back to the active feature branch.
2. Rebase that feature branch.
3. Confirm the working branch is the feature branch before continuing development.

## Failure Handling

- If any merge, rebase, checkout, tag, stash, or commit step fails, stop the workflow immediately.
- Before continuing after a failure, re-check:
  - the current branch
  - working tree cleanliness
  - recent commit graph for `main`, `develop`, and the active feature branch
  - whether the intended tag points to the intended commit
- Do not make forward progress in a release workflow based on assumptions about repository state.

## Required Verification Commands

For merge or release work, run and inspect these kinds of checks at the relevant points:

- Current branch and worktree:
  - `git branch --show-current`
  - `git status --short`
- Branch ancestry:
  - `git merge-base --is-ancestor main develop`
  - `git merge-base --is-ancestor develop <feature-branch>`
  - `git merge-base --is-ancestor main <feature-branch>`
- Short graph confirmation:
  - `git log --graph --decorate --oneline main develop <feature-branch> -n 12`
- Release tag confirmation:
  - `git show --no-patch --pretty=oneline <tag>`

Agents should prefer explicit verification over visual guesswork from partial history.

## Practical Constraints For AI Agents

- Never leave the repository on `main` after a release workflow.
- Never leave the repository on `develop` after a release workflow unless the user explicitly says to stop there.
- If a task involves merges or releases, read this file first and follow it exactly.
- Do not guess release version numbers without explicit user confirmation.
- Do not merge `develop` into `main` until both version confirmation and `changes.log` confirmation have been completed with the user.
- Never write a merge commit subject that only describes the mechanical act of merging.
- Never run `checkout`, `merge`, `rebase`, `commit`, `tag`, `reset`, or `stash` in parallel with another git write operation.

## Collapsing a Lane
A lane that must land as one commit is collapsed against the base it will land on, and the measurement
that says "only my content is in it" is the **two-dot** diff, `git diff <base>..HEAD`.

- **`...` (three dots) hides what you reverted.** `git diff <base>...HEAD` is the difference from the
  *merge-base*, so a file the base changed after your merge-base appears as **your** change -- and if
  your branch still holds the older copy, what it shows as "your edit" is in fact you putting someone
  else's work back. Wrong: collapse a lane on a three-dot stat and push it. Right: `git diff <base>..HEAD`
  after `git reset --hard <base>` and re-applying only the paths you own. This has bitten this branch:
  a three-dot stat read "10 files, mine" while `reset --soft` revealed `agents/test.md`,
  `src/execute/pass.cpp` and `src/passes/test/fault.cpp` staged as reverts of the base's newer work.
- **Collapse by resetting to the base and re-taking your paths**, not by rewording history one commit at
  a time: `git reset --hard <base>; git checkout <old-tip> -- <paths>`, then one commit. The tree is what
  lands; the base's newer files are untouched because they were never in your paths.
- **Force-push a shared lane with `--force-with-lease` only.** Another lane may have written to it, and
  the lease is what turns "I am replacing what I saw" into a refusal when that is no longer true.

## The Main Checkout Is A Read-Only Coordination Surface
The checkout at the repository root is where merges are inspected and where a reader looks; it is not
anyone's work tree. Before a lane starts work, `git -C <main checkout> rev-parse --abbrev-ref HEAD` must
answer `develop`. A lane that finds itself editing a file inside that checkout stops, stashes what it
wrote, and redoes the change in its own worktree -- a stash is not a rescue, it is a way to keep the
work while the checkout goes back to being readable. Six times in one night this surface was written
into from somewhere else: a reformatted header, a broken replacement in `graph_read.cpp`, a duplicated
probe diagnostic, a zero-byte `cml.cpp`, and once eight files plus a branch other than `develop`. Each
time the diff had to be recovered from the wreckage before anyone could say whether it was work or a
repeat of work that had already landed.