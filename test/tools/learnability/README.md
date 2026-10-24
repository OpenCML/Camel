# The AI learnability evaluation (H254)

Does a model that has never seen Camel write correct Camel from one file —
`docs/reference/camel-for-ai.md` — alone? This directory is the measurement: a task set with
decidable acceptance, a fixture that enforces the isolation, and a runner that reports numbers
instead of impressions.

Nothing here grades an answer by reading it. Every task's acceptance is the compiler's own
reading: an exit code, the program's stdout, and for one task `camel check`.

## What the fixture guarantees

Isolation is a property of the run, not a promise made to the subject:

- each task gets its own sandbox holding **exactly one file**, `camel-for-ai.md`, copied there by
  the runner; no repository path is passed to the subject and no task's answer is ever written
  into the repository;
- the `bl` solver receives that text inside the request. It is a model endpoint with no file
  system, so it cannot read `docs/language/**`, `stdlib/**`, `test/**` or this directory even if
  it tried;
- the `cmd` solver is spawned with the sandbox as its working directory;
- the `provided` solver takes code produced elsewhere. That arm's isolation rests on the
  instructions given to whoever produced it, so it is **weaker than `bl`**, and the report of a
  `provided` run has to say so.

## Running it

The compiler is not built on the Windows checkout this is driven from (`AGENTS.md`: do not build
there). The runner ships each round to the build host and compiles it there with
`remote-compile.sh`, against one recorded revision.

```bash
# Control: the verified golden solutions must all pass. Proves the fixture, not the reference.
node test/tools/learnability/run.mjs --solver provided --provided <dir-of-<id>.cml> \
     --archive <archive-dir> --rounds 1

# Paid arm: a model endpoint that has never seen Camel, up to three compile-fix rounds.
node test/tools/learnability/run.mjs --solver bl --model qwen3.8-max \
     --tasks L05,L12,L14 --rounds 3 --archive <archive-dir> --token-budget 1500000

# Any other agent CLI: {request} is a JSON messages file, {answer} is where it writes its reply.
node test/tools/learnability/run.mjs --solver cmd \
     --cmd-template 'my-agent --request {request} --out {answer}' \
     --tasks L01 --archive <archive-dir>

# Recompile an archived run without spending tokens.
node test/tools/learnability/run.mjs --solver replay --archive <archive-dir> --rounds 3
```

Options: `--camel-host` (default `88zjwei`), `--camel-bin`, `--camel-bin`'s `CAMEL_HOME` and
`LD_LIBRARY_PATH` come from the host environment, `--token-budget` stops a paid run before it
overspends, `--keep-sandbox` keeps the temporary sandboxes for inspection.

## The archive is the evidence

A run writes, under `--archive`:

- `report.json` / `report.md` — the numbers, per task: pass round, every round's exit code, stdout,
  stderr, the diagnostics read, and why a round did not pass;
- `<id>/round<N>.response.txt` — the subject's reply, verbatim, before any parsing;
- `<id>/round<N>.cml` — the extracted file that was compiled;
- `rounds/round-N/` — the sources shipped to the compiler, plus `remote.txt`, the compiler's raw
  answer in the delimited form `remote-compile.sh` prints.

`report.json` records the reference's sha256 and character count, so a before/after pair can be
shown to differ in the reference and not in anything else.

## The task set

`tasks.json` holds one entry per task: `id`, `area` (the five areas H254 names — values and types,
effects and sync, streams, rules, training), the `prompt` handed to the subject, and `acceptance`.
The prompt states only observable behaviour — never a Camel spelling — so a task cannot be made
easier by knowing the language, and the measurement stays honest.

`area` covers: values and types (L01–L05, L14, L15), effects and sync (L06, L07, L11), streams
(L08), rules (L09, L10), training (L12, L13).