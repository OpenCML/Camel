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
