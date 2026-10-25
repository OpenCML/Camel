#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""How much of a set of programs std::jit compiles, and why it leaves the rest.

    jitcov.py [--corpus NAME=GLOB ...] [--progen 1-300] [--progen-size 1] [--jobs 4]
              [--timeout 20] [--out DIR] [--pipelines plain,inline,opt] [--camel PATH]

Run from the repository root (the install `--camel <path>` names, else `CAMEL_HOME`, else
out/latest). Every program is run twice per pipeline, under std::fvm and under std::jit, with
stats="file" given (see
src/passes/sched/fastvm/fastvm_stats.cpp): the VM then writes, for every graph, whether the
JIT compiled it, every construct that keeps it from being compiled, and how many of its
bytecodes the interpreter ran.

Reported per corpus and pipeline:
- graphs, and how many were compiled (entered as native code; as frameless and as framed code),
  compilable (the back end takes them, they never became hot) or rejected;
- the share of executed bytecodes that did not run in the interpreter: 1 - interpreted(std::jit)
  / interpreted(std::fvm) over the programs both runs finished;
- graphs by construct that needs the runtime: for a rejected graph the constructs that block
  it, for a graph compiled as framed code the constructs that make it framed (a record written
  before framed code existed has only the first). `any` counts a graph under every kind it
  contains, `sole` under the kind that is its only one; `weight` is the share of all executed
  bytecodes (of the std::fvm run) spent in those graphs;
- the operators (OPER) of those graphs, by graphs and by weight.

Without --corpus the default corpora are test/cases, examples, docs/tutorial/examples and
benchmarks. A program that does not compile, needs input it does not get, or runs longer than
the timeout yields no record for that run and is counted as such; faults and non-zero exits
still yield records (the statistics are written however the run ends).
"""
import argparse
import collections
import concurrent.futures
import glob
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PIPELINES = {
    'plain': [],
    'inline': ['std::opt::inline'],
    'opt': ['std::opt'],
}
DEFAULT_CORPORA = [
    ('test/cases', 'test/cases/**/*.cml'),
    ('examples', 'examples/**/*.cml'),
    ('tutorial', 'docs/tutorial/examples/**/*.cml'),
    ('benchmarks', 'benchmarks/**/*.cml'),
]


sys.path.insert(0, str(ROOT / 'test' / 'tools'))
from camel_arg import camel_argument  # noqa: E402


# The install this run measures: `--camel <path>` names it, else `$CAMEL_HOME/bin/camel`, else this
# tree's `out/latest/bin/camel`. One rule, in one place: test/tools/camel_arg.py.
CAMEL, REST = camel_argument(sys.argv, ROOT)


def camel_exe():
    return CAMEL


def seeds_of(text):
    out = []
    for part in text.split(','):
        if '-' in part:
            a, b = part.split('-')
            out.extend(range(int(a), int(b) + 1))
        elif part:
            out.append(int(part))
    return out


def run_one(job):
    """One program under one pipeline: the stats records of the fvm and the jit run."""
    path, env_extra, pipeline, timeout, workdir = job
    result = {}
    for backend in ('fvm', 'jit'):
        fd, stats = tempfile.mkstemp(suffix='.json', dir=workdir)
        os.close(fd)
        os.unlink(stats)
        env = dict(os.environ)
        env.update(env_extra)
        cmd = [camel_exe(), path] + PIPELINES[pipeline] + [f'std::{backend}(stats="{Path(stats).as_posix()}")']
        outcome = 'ok'
        try:
            p = subprocess.run(cmd, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=timeout, cwd=str(ROOT))
            if p.returncode < 0:
                outcome = f'signal {-p.returncode}'
        except subprocess.TimeoutExpired:
            outcome = 'timeout'
        record = None
        if os.path.exists(stats):
            try:
                with open(stats) as f:
                    lines = [line for line in f if line.strip()]
                # One line per VM run; a program that runs the VM several times (std::macro
                # evaluation does not) is merged below by taking every line.
                record = [json.loads(line) for line in lines]
            except (OSError, ValueError):
                record = None
            os.unlink(stats)
        result[backend] = {'outcome': outcome, 'runs': record}
    return path, pipeline, result


class Tally:
    def __init__(self):
        self.programs = 0
        self.no_record = 0
        self.crashed = []
        self.graphs = 0
        self.status = collections.Counter()
        self.mode = collections.Counter()
        self.mode_weight = collections.Counter()
        self.ref_slots = collections.Counter()
        self.any_kind = collections.Counter()
        self.sole_kind = collections.Counter()
        self.any_weight = collections.Counter()
        self.sole_weight = collections.Counter()
        self.oper_graphs = collections.Counter()
        self.oper_weight = collections.Counter()
        self.sole_oper = collections.Counter()
        self.sole_oper_weight = collections.Counter()
        self.status_weight = collections.Counter()
        self.fvm_executed = 0
        self.jit_interpreted = 0
        self.needing = 0

    def add(self, path, result):
        self.programs += 1
        for backend in ('fvm', 'jit'):
            if result[backend]['outcome'].startswith('signal'):
                self.crashed.append((path, backend, result[backend]['outcome']))
        fvm, jit = result['fvm']['runs'], result['jit']['runs']
        if not fvm or not jit or len(fvm) != len(jit):
            self.no_record += 1
            return
        for fvm_run, jit_run in zip(fvm, jit):
            weight_of = {(g['name'], g['pc']): g['executed'] for g in fvm_run['graphs']}
            self.fvm_executed += sum(g['executed'] for g in fvm_run['graphs'])
            self.jit_interpreted += sum(g['executed'] for g in jit_run['graphs'])
            for g in jit_run['graphs']:
                weight = weight_of.get((g['name'], g['pc']), 0)
                self.graphs += 1
                self.status[g['status']] += 1
                self.status_weight[g['status']] += weight
                if g['refSlots']:
                    self.ref_slots[g['status']] += 1
                if g['status'] == 'compiled':
                    mode = g.get('mode') or 'frameless'
                    self.mode[mode] += 1
                    self.mode_weight[mode] += weight
                # What needs the runtime: the blockers of a rejected graph, the constructs that
                # make a compiled or compilable one framed.
                needs = g['blockers'] if g['status'] == 'rejected' else g.get('runtime', [])
                if not needs:
                    continue
                self.needing += 1
                kinds = sorted({b['kind'] for b in needs})
                for kind in kinds:
                    self.any_kind[kind] += 1
                    self.any_weight[kind] += weight
                if len(kinds) == 1:
                    self.sole_kind[kinds[0]] += 1
                    self.sole_weight[kinds[0]] += weight
                opers = sorted({b['detail'] for b in needs if b['kind'] == 'OPER'})
                for oper in opers:
                    self.oper_graphs[oper] += 1
                    self.oper_weight[oper] += weight
                if kinds == ['OPER'] and len(opers) == 1:
                    self.sole_oper[opers[0]] += 1
                    self.sole_oper_weight[opers[0]] += weight

    def summary(self):
        native = 0.0
        if self.fvm_executed:
            native = max(0.0, 1.0 - self.jit_interpreted / self.fvm_executed)
        return {
            'programs': self.programs,
            'programs_without_record': self.no_record,
            'crashed': self.crashed,
            'graphs': self.graphs,
            'status': dict(self.status),
            'status_weight': dict(self.status_weight),
            'compiled_by_mode': dict(self.mode),
            'compiled_by_mode_weight': dict(self.mode_weight),
            'graphs_needing_runtime': self.needing,
            'graphs_with_reference_slots': dict(self.ref_slots),
            'bytecodes_fvm': self.fvm_executed,
            'bytecodes_interpreted_under_jit': self.jit_interpreted,
            'native_share': native,
            'blockers_any': dict(self.any_kind),
            'blockers_sole': dict(self.sole_kind),
            'blockers_any_weight': dict(self.any_weight),
            'blockers_sole_weight': dict(self.sole_weight),
            'operators': dict(self.oper_graphs),
            'operators_weight': dict(self.oper_weight),
            'operators_sole': dict(self.sole_oper),
            'operators_sole_weight': dict(self.sole_oper_weight),
        }


def pct(part, whole):
    return f'{100.0 * part / whole:.1f}%' if whole else 'n/a'


def render(summaries, pipelines):
    lines = []
    lines.append('## Graphs compiled')
    lines.append('')
    lines.append('| corpus | pipeline | programs (no record) | graphs | compiled (frameless + framed) '
                 '| compilable, cold | rejected | bytecodes not interpreted |')
    lines.append('|---|---|---|---|---|---|---|---|')
    for (corpus, pipeline), s in summaries.items():
        st = s['status']
        by_mode = s['compiled_by_mode']
        lines.append(
            f"| {corpus} | {pipeline} | {s['programs']} ({s['programs_without_record']}) "
            f"| {s['graphs']} | {st.get('compiled', 0)} ({pct(st.get('compiled', 0), s['graphs'])}; "
            f"{by_mode.get('frameless', 0)} + {by_mode.get('framed', 0)}) "
            f"| {st.get('compilable', 0)} | {st.get('rejected', 0)} "
            f"| {pct(s['bytecodes_fvm'] - s['bytecodes_interpreted_under_jit'], s['bytecodes_fvm'])} "
            f"of {s['bytecodes_fvm']} |")
    for (corpus, pipeline), s in summaries.items():
        total = s['bytecodes_fvm']
        rejected = s['graphs_needing_runtime']
        lines.append('')
        lines.append(f'## Graphs by construct that needs the runtime: {corpus}, {pipeline}')
        lines.append('')
        lines.append(f"{rejected} graphs need the runtime ({s['status'].get('rejected', 0)} of them "
                     'rejected).')
        lines.append('')
        lines.append('| construct | graphs containing it | of those graphs | weight | graphs where '
                     'it is the only one | weight |')
        lines.append('|---|---|---|---|---|---|')
        for kind, count in sorted(s['blockers_any'].items(), key=lambda kv: -kv[1]):
            lines.append(
                f"| {kind} | {count} | {pct(count, rejected)} "
                f"| {pct(s['blockers_any_weight'].get(kind, 0), total)} "
                f"| {s['blockers_sole'].get(kind, 0)} "
                f"| {pct(s['blockers_sole_weight'].get(kind, 0), total)} |")
        lines.append('')
        lines.append('| operator (OPER) | graphs containing it | weight | graphs where it is the '
                     'only such construct | weight |')
        lines.append('|---|---|---|---|---|')
        top = sorted(s['operators'].items(), key=lambda kv: -kv[1])[:20]
        for oper, count in top:
            lines.append(
                f"| `{oper}` | {count} | {pct(s['operators_weight'].get(oper, 0), total)} "
                f"| {s['operators_sole'].get(oper, 0)} "
                f"| {pct(s['operators_sole_weight'].get(oper, 0), total)} |")
        ref = s['graphs_with_reference_slots']
        lines.append('')
        lines.append(
            'Graphs whose frame holds collector-traced references: '
            + ', '.join(f"{k} {v} of {s['status'].get(k, 0)}" for k, v in sorted(ref.items()))
            + '.')
        if s['crashed']:
            lines.append('')
            lines.append('Ended by a signal: ' + '; '.join(f'{p} ({b}: {o})' for p, b, o in s['crashed']))
    return '\n'.join(lines) + '\n'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--corpus', action='append', default=[], help='NAME=GLOB (relative to the repository root)')
    ap.add_argument('--no-default-corpora', action='store_true')
    ap.add_argument('--progen', help='seeds of the random program generator, e.g. 1-300')
    ap.add_argument('--progen-size', type=int, default=1)
    ap.add_argument('--pipelines', default='plain,inline,opt')
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--timeout', type=float, default=20)
    ap.add_argument('--limit', type=int, default=0, help='at most this many programs per corpus')
    ap.add_argument('--out', help='directory for summary.json and report.md')
    args = ap.parse_args(REST)
    args.jobs = min(args.jobs, 4)

    pipelines = [p for p in args.pipelines.split(',') if p]
    corpora = []
    if args.corpus or args.no_default_corpora:
        for spec in args.corpus:
            name, _, pattern = spec.partition('=')
            corpora.append((name, pattern))
    elif not args.progen:
        corpora = list(DEFAULT_CORPORA)

    workdir = tempfile.mkdtemp(prefix='jitcov_')
    jobs = []
    corpus_of = {}
    for name, pattern in corpora:
        files = sorted(glob.glob(str(ROOT / pattern), recursive=True))
        if args.limit:
            files = files[:args.limit]
        for f in files:
            corpus_of[f] = name
            for pipeline in pipelines:
                jobs.append((f, {}, pipeline, args.timeout, workdir))
    if args.progen:
        sys.path.insert(0, str(ROOT / 'test' / 'tools' / 'progen'))
        from generate import generate, DEFAULT_FEATURES  # noqa: E402
        from model import emit  # noqa: E402
        name = f'progen(size {args.progen_size})'
        for seed in seeds_of(args.progen):
            prog, _expected, _attempts = generate(seed, DEFAULT_FEATURES, size=args.progen_size)
            f = os.path.join(workdir, f'progen_{seed}.cml')
            with open(f, 'w', encoding='utf-8', newline='\n') as out:
                out.write(emit(prog))
            env = {str(k): str(v) for k, v in dict(prog.a.get('env') or {}).items()}
            corpus_of[f] = name
            for pipeline in pipelines:
                jobs.append((f, env, pipeline, args.timeout, workdir))

    tallies = collections.OrderedDict()
    done = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for path, pipeline, result in pool.map(run_one, jobs):
            key = (corpus_of[path], pipeline)
            tallies.setdefault(key, Tally()).add(os.path.relpath(path, ROOT) if path.startswith(str(ROOT)) else os.path.basename(path), result)
            done += 1
            if done % 100 == 0:
                print(f'... {done}/{len(jobs)} runs', file=sys.stderr, flush=True)

    summaries = collections.OrderedDict((key, t.summary()) for key, t in tallies.items())
    report = render(summaries, pipelines)
    print(report)
    if args.out:
        out = Path(args.out)
        out.mkdir(parents=True, exist_ok=True)
        (out / 'report.md').write_text(report, encoding='utf-8')
        (out / 'summary.json').write_text(
            json.dumps({f'{c}|{p}': s for (c, p), s in summaries.items()}, indent=1), encoding='utf-8')
    return 0


if __name__ == '__main__':
    sys.exit(main())
