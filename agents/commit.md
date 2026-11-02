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

## Citations In Messages And Documents
- A message or page that points at code cites a **symbol**, not a line: `modules/graph/module.cpp#neKernel`, never `modules/graph/module.cpp:110`. The `docs.links.citations` check refuses the line form, and it is right to: a line number is a reading of one tree, and the next commit that touches the file makes it false.
- A line number belongs only in a sentence that quotes a measurement whose instrument counts lines, and that sentence has to say which tree it was measured on.
