# Branching And Merge Rules

This repository uses `main` for releases, `develop` for integration and feature branches for work. Branch and merge operations are stateful and sequential: inspect the repository before each step and stop after an unexpected result.

## Core policy

- Do not develop directly on `main` or `develop` unless the user explicitly asks for integration there.
- Start feature branches from the current `origin/develop`. Do not create additional worktrees unless the user explicitly requests one.
- Integrate with `git merge --no-ff`; do not fast-forward, rebase shared history or force-push shared branches.
- Never run checkout, merge, rebase, commit, tag, reset or stash concurrently with another Git write operation.
- End integration work on a feature branch, not on `main` or `develop`.
- A merge commit describes the integrated content, not merely the act of merging. Do not use subjects such as `merge branch` or `update`.

## Before integrating a lane

1. Check `git status --short --branch` and confirm the intended base and clean worktree.
2. Read `git log --oneline origin/develop..<lane>` and `git diff --stat origin/develop...<lane>`. Identify unique content and conflicts rather than relying on the branch name.
3. Use `git merge-tree --write-tree origin/develop <lane>` to preview the merge result and conflicts. Patch-equivalence alone is not proof that content is present or missing.
4. Check whether the branch is already integrated or superseded. `git branch --merged`, ancestry and the resulting tree answer different questions; use all of them as appropriate.
5. Run the relevant tests and the fast acceptance layer on the actual merge result. A lane may be red; `develop` may not.

A lane with more than ten unique commits is taken as a whole with `--no-ff`, not split by file or cherry-picked piecemeal. Do not incorporate WIP pairs whose second commit merely undoes the first. The lane owner may collapse such work before integration and force-push only that lane with `--force-with-lease`; the merger does not rewrite it.

## Merge and verification

- Merge sequentially, checking branch and worktree state after every operation. If a command fails, inspect the graph, status and recent commits before proceeding; do not repeat it blindly.
- Resolve conflicts by meaning. Documentation retains both applicable current statements; code retains every valid case without reviving an outdated implementation.
- Run conflict-marker, diagnostics and documentation checks, relevant plans, and the fast layer. Name the tested commit in the report.
- Verify ancestry with `git merge-base --is-ancestor <integrated-tip> develop`. Check `origin/develop` separately when reporting what has or has not been pushed.
- Synchronize a feature branch by merging the current `origin/develop` into it, not by rebasing a pushed lane.
- Do not push `main`, `develop`, a release branch or a tag without the user's authorization. Local integration and remote publication are different actions.

## Closing and preserving branches

- Delete only branches whose content is integrated or demonstrably superseded. First create `archive/<name>` pointing at the old tip.
- Use normal deletion for merged branches. Do not use `-D` to force deletion merely because a branch is unmerged; investigate the remaining content first.
- Do not delete probes, owner-only refs or a branch still used by another worktree. `hold/fsh-dev` and `release` require an explicit owner decision.
- A branch with no new content does not need a merge commit. Archive it instead of reintroducing old code or old question pages.

## Release assembly

`release` is the only branch used to assemble a version. It is cut from `develop` and lives for that version.

1. Merge the intended `develop` boundary into `release` with `--no-ff`.
2. Before a `develop` to `main` release merge, explicitly confirm the version to publish, the next development version and the `changes.log` release-note content with the user.
3. Push `release` before tagging so CI can build and test it. Fix build/test-only issues there; semantic fixes belong in `develop` and are cherry-picked with `-x` onto `release`.
4. After a green run, create the tag on `release`, then move `main` to that commit. Do not tag `main` separately.
5. Merge `release` back into `develop` with `--no-ff` and verify `git merge-base --is-ancestor release develop`. Apply the next development-version bump and return to a feature branch.

Do not guess release versions, rewrite release history, or publish from an unverified commit. A red release run blocks the tag.
