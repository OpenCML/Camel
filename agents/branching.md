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
2. Read `git log --oneline origin/develop..<lane>` and `git diff --stat origin/develop...<lane>`. Identify unique content and conflicts rather than relying on the branch name. **Then read the bodies, not only the subjects.** A commit whose message says not to merge it has not been overtaken by the fact that merging it is now convenient. On 2026-11-16 `3773eed5c` carried `DO NOT MERGE THIS BRANCH YET ... This branch waits until the three families are fixed`, reached `origin/develop` anyway, and left twenty-one tests red in three families -- one of them a write to a captured name that produces no node and no diagnostic, so the program compiles, runs, prints its value and says nothing. The merger had checked content, diff shape and blob identity, and **none of those three asks what the author of the commit intended**; the statement was in the body the whole time. To merge such a commit, first delete the sentence in a new commit and say there why the reason no longer holds -- the message is the only place that intention is recorded, so a merge that ignores it deletes the record without replacing it. The author's ruling of that day also requires the twenty-one to be listed one by one in the baseline as brought in by that commit and pending, so that `new red = 0` keeps meaning what it says.
3. Use `git merge-tree --write-tree origin/develop <lane>` to preview the merge result and conflicts. Patch-equivalence alone is not proof that content is present or missing.
4. Check whether the branch is already integrated or superseded. `git branch --merged`, ancestry and the resulting tree answer different questions; use all of them as appropriate.
5. Run the relevant tests and the fast acceptance layer on the actual merge result. A lane may be red; `develop` may not.

A lane with more than ten unique commits is taken as a whole with `--no-ff`, not split by file or cherry-picked piecemeal. Do not incorporate WIP pairs whose second commit merely undoes the first. The lane owner may collapse such work before integration and force-push only that lane with `--force-with-lease`; the merger does not rewrite it.

## Merge and verification

- Merge sequentially, checking branch and worktree state after every operation. If a command fails, inspect the graph, status and recent commits before proceeding; do not repeat it blindly.
- Resolve conflicts by meaning. Documentation retains both applicable current statements; code retains every valid case without reviving an outdated implementation.
- Run conflict-marker, diagnostics and documentation checks, relevant plans, and the fast layer. Name the tested commit in the report.
- **Push each merge in the same step that makes it.** The shape to avoid is not "pushing late", it is merging faster than pushing: a local run's readings carry no `ls-remote` backing, and a gate result on an unpushed commit cannot be quoted. A merge is finished when it is pushed -- `git rev-list --count origin/develop..HEAD` above 2 means an earlier merge was not.
- **Never rewrite a pushed commit on `develop` -- no amend, no rebase, and no `--force-with-lease` either**, unless the owner asks for exactly that. A published sha is a name other things refer to: hand-written ledgers, `git notes` bodies, another lane's recorded base, a report someone already quoted. Rewriting replaces the name and leaves every reference pointing at an object nothing contains -- and the object is unreachable from then on, so it does not merely become old, it stops existing. **The rule follows from what a message can express, not from how bad the mistake was: anything an amended message can say, a new commit can say better**, because the new commit keeps both steps visible -- what was written first, and what replaced it. The precedent is a question page whose original advice the author's ruling overtook: it was restored, corrected in place and given a new commit, so a reader sees the road that was abandoned beside the road taken. Amending would have shown only the second.
- **Name the tree the reading came from, and only trust what can name it.** The gate's own `[provenance] tip <sha7> | tier <tier> | jobs <n>` line is the evidence; `out/latest/build-info.json` in the *main* checkout is not, because the main checkout is usually not the tree that was built -- on 2026-11-15 it held `revision=b72d59535e52, builtAt=2026-11-12` while the gate was measuring `b55283c43` on another host. The same file is fresh in a lane's worktree, where the build happened. **Same filename, two different levels of trust: quote the stamp only from the tree that was built, and quote `[provenance]` for every gate reading.**
- **Nothing writes to the main checkout while a gate is measuring it.** On 2026-11-15 a documentation commit landed in the main checkout while the merger's `--changed` was starting; the gate's own guard refused with `1 dirty file(s)` and exit 4, which was correct -- the coordinator's timing was not. Documentation commits by anyone else wait for a gap between two readings, because `camel-remote.ps1` transmits the working tree, not `HEAD`.
- **A lane's worktree belongs to the lane, and to nobody else, coordinator included.** On 2026-11-15 the coordinator made three writes into state another agent owned: a documentation commit into the main checkout mid-gate, a `git branch -d` on a branch the merger was finishing, and a commit pushed into a measurement lane's worktree while that lane was still running. Each time the other agent reported the write as an unexplained concurrent actor, and each time the guard it blamed had in fact been right. **The cost is not the lost keystroke: it is that a correct guard gets recorded as broken, and the next reader stops believing it.** Whoever owns a path is the only writer; if the coordinator needs a page from a lane's tree, the lane commits it.
- **Run git in a registered worktree, or read about another repository.** A directory under the worktree root with no `.git` of its own does not stop git: it walks up and answers about whatever repository is above it. On 2026-11-15 a lane whose directory had been deregistered reported `branch=master`, `HEAD=a44c193`, `M camel-remote.ps1` -- none of which exist in this project; the parent directory happened to be its own one-commit repository with a copy of `camel-remote.ps1` in it. **The reading looked exactly like this project's**, complete with a dirty file, and it was about a different repository. Check `git rev-parse --show-toplevel` before trusting any status reading, and treat a missing `.git` as "this is not my tree" rather than as a clean tree.
- Verify ancestry with `git merge-base --is-ancestor <integrated-tip> develop`. Check `origin/develop` separately when reporting what has or has not been pushed. **"Merged" is a verdict about a base, so say which base.** `git branch -d` judges against `HEAD`, not against `origin/develop`, and the two disagree: on 2026-11-16 a lane deleted a measurement branch whose tip was an earlier commit of its own branch, and `-d` took it -- "deleted (was `bb5591de2`)" -- because from `HEAD` that commit is reachable, while against `develop` it was not merged at all. The coordinator had predicted a refusal and told the lane to report which kind, and there was no refusal to report. **A branch that is safe to delete under one base is not safe under the other**, so name the base before the word, and neither `-d` nor `-D` announces which one it used.
- Synchronize a feature branch by merging the current `origin/develop` into it, not by rebasing a pushed lane.
- **Never `git stash`: the stash is one stack for the whole repository, shared by every worktree.** On 2026-11-15 two lanes working in different worktrees each ran `git stash push` / `pop`; each popped the other's entry, and one lane's source changes were briefly replaced by the other's files in the other's worktree. Nothing was lost, and that was luck: a `pop` takes the top entry, so whoever runs it next gets the most recent stranger's work. To set work aside, write `git diff > <file>.patch` (and say where it is). An entry someone else left in the stack -- on 2026-11-15 a 74-file, 1985-line WIP -- is not yours to pop, drop or reorder.
- Do not push `main`, `develop`, a release branch or a tag without the user's authorization. Local integration and remote publication are different actions.

## Who guards a plan

"Is it guarded?" has three answers, and each one has to be able to name itself; **the hole is the thing none of the three names.**

- **The selection rules** (`test/plan-selection.json`): `--changed` reaches it because a rule ties the changed file to this plan. Naming the rule is part of knowing it is guarded.
- **Direct calls in the gate** (`internal/tools/camel-remote.ps1`): some checks are in no plan at all -- `check-plan-fields`, `check-doc-links`, the generated-reference checks -- because the gate itself runs them on every measurement. **"Not in the `--changed` selection" does not mean "not guarded"**; it means the guard is somewhere else, and that place has to be named.
- **Named runs on a schedule** (see "Plans no change selects" below): the plans no rule selects and the gate does not call, which exist only because someone runs them by name.

A green `--changed` run says the selected plans passed, not that the tree is good. On 2026-11-15 a scheduler change passed its selection at 1356/1352 while nine tests were red on the pushed `develop`, because no rule tied that file to a plan whose objects die differently -- **that is exactly the shape of a thing none of the three layers names**, and it is why each layer has to be able to say what it covers instead of being assumed to cover everything.

> 三层都说不出名字的那个东西，就是拿 `1356/1352` 当「树是好的」那一刻的真实情形。 —— the owner, 2026-11-15

## Plans no change selects

Some plans are deliberately unreachable from `--changed` (`test/plans/perf/**`, the GC-stress plans; see `test/plan-selection.json` and `docs/test-plans.md`, "Selecting Plans From A Change"). That is allowed, and it has one condition: **something must name them on a schedule, or the ledger cannot tell "deliberate" from "forgotten".** A plan no change selects is a plan whose red is invisible until someone goes looking, and `perf/showcase_c2` sat red that way.

- Run the unselectable plans **once each time the merge queue empties**, and report the reading with the commit it was taken against. It does not need to be every night, and it does not gate a merge; it needs to happen often enough that a change in its message is noticed within a day or two.
- Read them on the build host, not on a workstation: the same case measured 300 s and 600 s (both timeouts) locally and 1.87 s / 2.07 s on the build host. **A difference of two orders of magnitude means "hangs on that platform", not "slower"**, and a timeout there is a platform finding rather than a defect until proved otherwise.
- Report `killed by a signal` and `timed out` beside `fail` whenever they are non-zero; both are absent from `[SUMMARY]` when zero, so "the suffix did not appear" is the reading.

## Closing and preserving branches

- Delete only branches whose content is integrated or demonstrably superseded. First create `archive/<name>` pointing at the old tip.
- Use normal deletion for merged branches. Do not use `-D` to force deletion merely because a branch is unmerged; investigate the remaining content first.
- **`-d` answers two different questions depending on a remote.** For a local branch with a same-named branch on `origin`, `-d` compares against that remote, not against `develop`: a lane branch that was pushed but never merged is reported as `merged to refs/remotes/origin/<name>, but not yet merged to HEAD` and then **deleted, exit 0**. On 2026-11-16 this deleted `lane-capturewrite`, whose four commits were still queued for merge; it was rebuilt from `308129030` and nothing was lost, but only because the tip sha had been recorded beforehand. So `git branch --merged origin/develop` is not the check that protects you here, and neither is the exit status: read `git rev-list --count origin/develop..<branch>` before deleting, and record the tip sha beside the name so a surprise deletion is reversible. A guard that reports success while removing unmerged work is worse than a guard that refuses.
- Do not delete probes, owner-only refs or a branch still used by another worktree. `hold/fsh-dev` and `release` require an explicit owner decision.
- A branch with no new content does not need a merge commit. Archive it instead of reintroducing old code or old question pages.
- **A carrier branch says who consumes it and when it dies.** Some branches are not "in flight" and not "merged": part of the work must not land (a rework whose rule the owner then withdrew) while part of it must (the cases that become the acceptance criteria of the change that replaced it). Keep it under `hold/<name>`, excluding it from the in-flight count `U` and counting it separately as `H`. Its tip must say, in a place a reader will find -- a note on the branch tip or the commit it points at -- **which item consumes it and that it is deleted when that item lands**. Without that sentence it may not exist. `H > 3`, or a `hold/` branch behind `origin/develop` by more than 50 commits, is a decision point: take the content, or discard it and record one sentence naming what replaced it. **Parking is allowed; parking without an expiry date is how a parked branch becomes permanent junk** -- and the hygiene numbers cannot tell the two apart, because `U`, `maxBehind` and `branch --merged` all assume a branch is either merged or in flight.

## Release assembly

`release` is the only branch used to assemble a version. It is cut from `develop` and lives for that version.

1. Merge the intended `develop` boundary into `release` with `--no-ff`.
2. Before a `develop` to `main` release merge, explicitly confirm the version to publish, the next development version and the `changes.log` release-note content with the user.
3. Push `release` before tagging so CI can build and test it. Fix build/test-only issues there; semantic fixes belong in `develop` and are cherry-picked with `-x` onto `release`.
4. After a green run, create the tag on `release`, then move `main` to that commit. Do not tag `main` separately.
5. Merge `release` back into `develop` with `--no-ff` and verify `git merge-base --is-ancestor release develop`. Apply the next development-version bump and return to a feature branch.

Do not guess release versions, rewrite release history, or publish from an unverified commit. A red release run blocks the tag.
