# Commit Rules

## Message Format
- Use Conventional Commits prefixes such as `feat:`, `fix:`, `refactor:`, `docs:`, or `chore:`.
- Keep the subject concise and specific to the primary change.
- Write the message in English: the public repository is English-first, and the history reads in one language.
- Write the subject against the whole staged set, not the most recent edit you happened to make.
- The subject should name the highest-level purpose shared by all staged files; if it only describes the last step, it is too narrow.
- Use `refactor:` for structural code changes.
- Use `chore:` for maintenance work such as warning cleanup, formatting-only updates, and non-behavioral repository hygiene.

## Scope Discipline
- Keep commits focused on one coherent change.
- Do not mix unrelated refactors, docs changes, and bug fixes unless they are part of the same structural change.
- If a tool or hook rewrites files after commit, inspect the result before creating the next commit.
- Before committing, skim the working tree and staged diff to understand all touched areas, not just one visible change.
- Before writing the commit message, explicitly identify the common purpose that explains every staged file.
- If two staged areas need different commit subjects to be described honestly, split them into separate commits.
- When staged changes clearly span multiple concerns, split them into multiple commits instead of writing a single broad summary.
- **`git commit -a` and `git commit --amend -a` are a bet on a tree nobody else is writing, and only place them when the bet is known to be safe.** Use them only when every change in the tree is yours. On 2026-11-16 a second session was working in the same worktree and merged `develop` into it while a lane was reading; the lane then ran `commit --amend -a` and committed the merge under the title of a two-line rules edit. The result was not mainly a wrong title: the commit had two parents, and the branch then appeared to **delete 2,285 lines** of `develop`'s content relative to `develop`, because a merge swallowed into someone's commit turns other people's work into that commit's diff. Every reviewer of the branch would have read those deletions as this lane's doing.
- **After repairing a history, measure the repair and not the intention.** The recovery's proof was `git diff develop...HEAD --stat` reading 11 files, `+352 -76` -- the lane's own change set and nothing else. "I redid it" is a claim; the three-dot diff is the reading that it was redone.

## Citations In Messages And Documents
- A message or page that points at code cites a **symbol**, not a line: `modules/graph/module.cpp#neKernel`, never the same path followed by a line number. The `docs.links.citations` check refuses the line form, and it is right to: a line number is a reading of one tree, and the next commit that touches the file makes it false.
- A line number belongs only in a sentence that quotes a measurement whose instrument counts lines, and that sentence has to say which tree it was measured on.

## Language
- **Commit messages are English.** The repository is public, and the author ruled it on 2026-11-02 12:10 after a batch of perf: commits arrived in Chinese: one language, English. This applies to every lane from that point on. History is not rewritten to comply -- no rebase, no amend -- so the Chinese subjects that already landed stay as they are, and a merge message does not translate them.
