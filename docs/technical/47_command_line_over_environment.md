# 47. The command line, not the environment

Behaviour comes from the command line: a pass parameter, a pass option, a build option. The
environment is for what a run *is* (where the installation is, where the toolchain is), never for
what it *does*. This page is the account of the collector's and the collection tree's move, and of
what the move cost.

## 1. The 21 knobs, and where each one lives now

Twenty-one names, twenty-one knobs, no aliases. Only the accepted values have synonyms:
`--gc-check verify=before` also takes `pre`, and `--gc-stress kind=both` also takes
`minor+major` and `all`.

| Environment name | Option | Owner: who sets it, and when |
|---|---|---|
| `CAMEL_GC_NURSERY_MB` | `--gc-nursery-mb` | Someone running a big program, and the capacity plans; the birth space of the generational collector |
| `CAMEL_GC_PROMOTION_AGE` | `--gc-promotion-age` | Operator authors and the generational tests; 1 promotes at the first collection, which is what shows a missing write barrier |
| `CAMEL_GC_LARGE_THRESHOLD_KB` | `--gc-large-threshold-kb` | Someone sizing the large-object space; a small threshold makes ordinary arrays be born old, which is what the tests of such objects need |
| `CAMEL_GC_LARGE_BUDGET_MB` | `--gc-large-budget-mb` | The same; large-object bytes between two collections |
| `CAMEL_GC_GROWTH_PERCENT` | `--gc-growth-percent` | The same; how much the large-object space grows per cycle |
| `CAMEL_GC_HEAP_LIMIT_MB` | `--gc-heap-limit-mb` | The capacity plans and `docs/technical/23`, `24`; turns "out of memory" into a located diagnostic instead of a host OOM |
| `CAMEL_SPACE_SEGMENT_KB` | `--space-segment-kb` | Test control: small segments make a small program grow the spaces, so growth is exercised under verification and stress |
| `CAMEL_META_SPACE_LIMIT_MB` | `--meta-space-limit-mb` | The capacity plans; the space of constants and function objects being compiled |
| `CAMEL_GRAPH_SPACE_LIMIT_MB` | `--graph-space-limit-mb` | The same; the space of runtime graphs |
| `CAMEL_PERM_SPACE_LIMIT_MB` | `--perm-space-limit-mb` | The same; the space of types and layouts |
| `CAMEL_GC_STRESS_MODE` | `--gc-stress kind=minor\|major\|both` | Operator authors (`docs/technical/36`, `24`) and the `feat/mm`, `gpu` plans |
| `CAMEL_GC_STRESS_ALLOC` | `--gc-stress alloc=` | The same; collect every N allocations |
| `CAMEL_GC_STRESS_SAFEPOINT` | `--gc-stress safepoint=` | The same; collect every N safepoints |
| `CAMEL_GC_LOG_MOVES` | `--gc-check moves` | The same; one record per relocated object |
| `CAMEL_GC_PRINT_CONFIG` | `--gc-check config` | The same; the effective configuration, printed once at start-up |
| `CAMEL_GC_POISON` | `--gc-check poison` | `docs/technical/36` section 8, and `mm.gc_poison_sweep`, which runs the other plans poison-forced |
| `CAMEL_GC_VERIFY` | `--gc-check verify=before\|after\|both` | `docs/technical/24`, and the plans that read a heap under stress |
| `CAMEL_COLL_FANOUT` | `--coll fanout=` | `docs/technical/31` section 9.3, and the compile-shape plans |
| `CAMEL_COLL_LEAF_CAPACITY` | `--coll leaf=` | The same |
| `CAMEL_COLL_PAD_NODES` | `--coll pad=on\|off` | The same |
| `CAMEL_COLL_VALIDATE` | `--coll validate=on\|off` | The same; checks the invariants of every table an operation returns |

`CAMEL_GC_ENABLE_YOUNG_COPYING` is not in the table because it was never a knob: five plans set
it, nothing read it, and the docs already said it changed nothing. Those settings were deleted
rather than migrated.

`CAMEL_TILES_NVRTC` stays in the environment: it says where the CUDA toolchain is installed,
which is configuration of the host, not behaviour of the program. `CAMEL_HOME` needs no line at
all: it names the installation the run was started from.

## 2. The account, in code

`test/tools/check-env-switches.mjs` scans `src`, `modules` and `include` and holds every reading
that is left, by call site -- `file:function`, never by variable name, so a renamed helper cannot
quietly allow a new site. Six lines remain: the three `include/` readings (`CAMEL_ERROR_FORMAT` in
a generated program, the one `getEnvIfSet` that reads the process environment for the platform's
names, and the `PATH` walk of the library search path), and the three of `modules/tiles`. The
target is one line, `CAMEL_TILES_NVRTC`.

## 3. What the move cost

The cheap tests to migrate are the ones that run a program. The expensive ones are the tests that
do not: nothing in them fails when the words change, they are simply no longer covered.

`mm.gc_poison_sweep` is the case in point. Its copies are made by a `[[sweeps]]` entry, which used
to add an environment to every copied test and now adds `args = ["--gc-check", "poison"]` in front
of the entry file of each one. `skip_args_prefix = "--gc-"` keeps a copy from naming the option
twice. But a copy can only take the words if it drives a run: `check`, `inspect` and `--version`
have no entry file to put them in front of, and the first attempt failed 49 such copies with
`camel inspect` usage text before the rule was written down.

The count, therefore: the poison sweep covers 3508 tests from the environment it used to add, and
3054 from the words. The 454 that are no longer in it never touched a GC option -- they are the
tests that never run a program -- but the sweep does not cover them any more, and that belongs in
the account rather than in a report nobody reads later.

Three failures are older than this move: `stream.tee_report`,
`stream.tee_report_element_size_unknown`, and `rules.every_exported_rule_enters_the_check` (115 s
under poisoning). The same three fail at `e566ff707`, the commit this migration starts from, and
they are slow-tier copies, outside the fast gate.

A harness case that fails prints dozens of lines at once. Read the command line on the first
one, not the last one: the first line of the collfuzz failure said
`invalid command line: '--coll' 'validate=on'`, and the interesting half was that the run had
already been given one `--coll`, so this was a second one and the option refuses it.

The spelling is two arguments, `--coll leaf=0,validate=on`. Option and value in one argument
(`"--coll leaf=0,validate=on"`) is refused with `Invalid --coll key: ' leaf'`, and a second
`--coll` in the same run is refused with `invalid command line`. A harness that wants to add a
width to a shape it already passes therefore merges it into the one spec, which is what
collfuzz does with `validate=on`.

The rule to carry forward: a migration's cost hides in the cases that do not run the program.
They do not turn red, the new parameters do not reach them, and they leave the statistics
quietly.