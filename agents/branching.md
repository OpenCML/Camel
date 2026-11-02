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
on. The rows stayed, the question went back to `questions/`, and the ruling's own text stayed in the
record untouched.

## Coordinator Workflow: Handover Branches And `wzj-dev`

The coordinator's own work follows a two-step integration so the remote stays quiet and the task book's readers still see progress.

- A `handover/Hxx-...` branch is a **local staging branch**: do not push it to the remote, and do not create a remote branch for it. The only remote branch that represents the coordinator's development is `wzj-dev`.
- Merge each finished handover branch into `wzj-dev` first (`git merge --no-ff`), and promote `wzj-dev` into `develop` in batches — at least every half hour, so an AI reading `develop` sees the progress quickly. Pushing `develop` and `wzj-dev` is what makes the work visible.
- As soon as a handover branch is merged into `wzj-dev` and its worktree is removed, delete the branch. If an older remote `handover/...` branch still exists from before this rule, delete it too; the commits are in `develop`.
- The rules above still hold: merge commits only, never rebase `develop`, never force-push, never touch `main`.

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
- After the merge commit is created on `main`, immediately create a version tag in the form `vx.x.x`.
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
