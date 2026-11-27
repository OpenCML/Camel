# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Do the proposed include edits still compile the whole tree?

Each arm overlays one edited header in front of the real one -- its directory goes first on the
`-I` path, so the edited copy is the one that is opened -- and compiles every translation unit of
the compile-commands database with `-fsyntax-only`.  The tree itself is never touched: an arm that
turns out wrong costs a directory under the temp root and nothing else.

The first arm is the baseline: the same translation units with no overlay at all.  It is not an
extra courtesy.  An arm's failure count says nothing on its own, because a tree that already fails
for reasons of its own fails identically with any overlay on top, and then every arm looks either
equally right or equally wrong.  The arms are read as differences against that baseline.

Run it from the repository root of the tree whose build wrote the database:

    python3 internal/tools/header-weight-edits.py

Every path it touches is a parameter; `--help` names each one and the default it takes.
"""
import argparse
import collections
import json
import os
import re
import shlex
import socket
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

PROG = 'header-weight-edits.py'
ANSI = re.compile(r'\x1b\[[0-9;]*m')

# The three edits, each as (key, tag, header relative to --include-root, needle, replacement).
# They are constants rather than arguments because the tool answers one fixed question -- whether
# these three include edits compile the tree -- and an arm whose needle silently stopped matching
# would otherwise print a comfortable zero.
ARMS = [
    ('e1',
     'e1 resolver.h opaque enum',
     'camel/core/type/resolver.h',
     '#include "camel/core/error/diagnostics/messages/semantic.h"',
     'namespace camel::core::error {\nenum class SemanticDiag : uint32_t;\n}'),
    ('e2',
     'e2 operator_traits.h -include',
     'camel/core/operator_traits.h',
     '#include "camel/core/operator_table.h"\n',
     ''),
    ('e3',
     'e3 frame.h fwd decl',
     'camel/core/context/frame.h',
     '#include "camel/runtime/graph.h"',
     'namespace camel::runtime {\nclass GCGraph;\n}'),
]

EPILOG = """\
Traps this tool is shaped around.  Each is marked in the source at the line that avoids it:

  baseline    every arm needs a baseline arm beside it.  Without one, a failure is unattributable:
              a TU that was already failing fails the same way under every overlay, and a TU that
              fails only under the overlay cannot be told from it.  The baseline is printed first
              and is the reading every other arm is a difference against.
  overlay     the edited header must come first on the `-I` path.  An overlay placed after the
              tree's own include directories is read by nothing, and every arm then reports the
              baseline's numbers while looking like it measured the edit.

Exit codes: 0 a reading was produced -- the counts are the answer, including a count of failing
TUs above the baseline; 2 the tool could not measure anything, and says what was missing.
"""


def die(message):
    sys.stderr.write('%s: %s\n' % (PROG, message))
    raise SystemExit(2)


def strip_args(cmd):
    """The compiler invocation with everything that writes a file removed, and `-O*` dropped.

    What is compiled does not change: `-c`, `-o`, `-MF` and friends only decide what is written,
    and an optimisation level cannot change whether a line of C++ is valid.  The caller appends
    `-fsyntax-only`.
    """
    a = shlex.split(cmd)
    rest, i = [], 1
    while i < len(a):
        if a[i] in ('-o', '-MF', '-MT', '-MQ', '-MJ'):
            i += 2
            continue
        if a[i] in ('-c', '-M', '-E', '-S', '-MD', '-MMD'):
            i += 1
            continue
        rest.append(a[i])
        i += 1
    return [a[0]] + [x for x in rest if not x.startswith('-O')]


def load_db(db_path):
    """`{source: entry}` with the first entry per source, plus the entry count."""
    try:
        with open(db_path, encoding='utf-8') as f:
            db = json.load(f)
    except (OSError, ValueError) as exc:
        die('cannot read the compile-commands database at %s: %s' % (db_path, exc))
    if not db:
        die('the compile-commands database at %s is empty: it names no translation unit, so there '
            'is nothing to compile' % db_path)
    if 'command' not in db[0]:
        die('the database entry for %s has no `command` field; this tool reads the `command` form '
            'only' % db[0].get('file', '<unknown>'))
    by_src = {}
    for e in db:
        by_src.setdefault(e['file'], e)
    return by_src, db


def warn_if_foreign(entry, db_path):
    """Say so when the database was written by a build whose tree this process cannot see."""
    directory = entry.get('directory', '')
    if directory and not os.path.isdir(directory):
        sys.stderr.write(
            '%s: warning: the database at %s names a build directory this machine cannot see '
            '(%s); it was written by a build elsewhere, or the tree moved.  The reading below '
            'describes the tree those paths name, not the one this process can see.\n'
            % (PROG, db_path, directory))


def parse_args(argv):
    def_overlay = os.path.join(tempfile.gettempdir(), 'camel-header-weight', 'edits')
    p = argparse.ArgumentParser(
        prog=PROG,
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=EPILOG,
    )
    p.add_argument('--root', default=os.getcwd(),
                   help='the tree the database belongs to, and the tree the edited headers are '
                        'read from. Default: the current directory, so the tool is meant to be run '
                        'from the repository root.')
    p.add_argument('--db', default=None,
                   help='the compile-commands database: it names the translation units and their '
                        'compile flags. Default: <root>/build/compile_commands.json, which is '
                        'where the build writes it.')
    p.add_argument('--include-root', default='src/kernel/include',
                   help='the directory the edited headers are named from -- the one on the `-I` '
                        'path. Default: %(default)s.')
    p.add_argument('--overlay-dir', default=def_overlay,
                   help='where the overlays are written, one subdirectory per arm (`e1`, `e2`, '
                        '`e3`), each holding its edited header at its include name. Default: '
                        '%(default)s (a scratch path under the system temp directory).')
    p.add_argument('--out-dir', default=None,
                   help='where the JSON reading is written. Default: --overlay-dir.')
    p.add_argument('--jobs', type=int, default=8,
                   help='how many translation units are compiled at once. Default: %(default)s.')
    p.add_argument('--host', default=socket.gethostname(),
                   help='the machine this reading describes; recorded in the output header line '
                        'and in the JSON so a number can be quoted with its source. Default: this '
                        "machine's hostname. It moves no work: the commands run here, so the "
                        'database must have been written by a build on this machine, or its '
                        'absolute `directory` paths must resolve here.')
    opts = p.parse_args(argv)
    if opts.jobs < 1:
        p.error('--jobs must be at least 1')
    return opts


def main(argv=None):
    opts = parse_args(argv)
    opts.root = os.path.abspath(opts.root)
    db_path = opts.db or os.path.join(opts.root, 'build', 'compile_commands.json')
    opts.overlay_dir = os.path.abspath(opts.overlay_dir)
    opts.out_dir = os.path.abspath(opts.out_dir or opts.overlay_dir)

    for key, tag, header, needle, repl in ARMS:
        real = os.path.join(opts.root, opts.include_root, header)
        if not os.path.isfile(real):
            die('arm %s edits %s, which does not exist: run from the repository root, or name the '
                'tree with --root (and the headers with --include-root)' % (key, real))
    if not os.path.isfile(db_path):
        die('no compile-commands database at %s: the database names the translation units and '
            'their flags, so nothing can be compiled without it. Build the tree first, or name '
            'the database with --db' % db_path)
    by_src, db = load_db(db_path)
    warn_if_foreign(db[0], db_path)
    os.makedirs(opts.out_dir, exist_ok=True)
    sources = list(by_src)
    print('=== host=%s root=%s db=%s jobs=%d' % (opts.host, opts.root, db_path, opts.jobs))
    print('TUs:', len(by_src))

    def run_all(overlay):
        def one(src):
            e = by_src[src]
            c = strip_args(e['command'])
            if overlay:
                # Before every other `-I`: after them, the real header is found first and the
                # overlay is read by nothing.
                c = c[:1] + ['-I', overlay] + c[1:]
            r = subprocess.run(c + ['-fsyntax-only'], cwd=e['directory'],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            err = ANSI.sub('', r.stderr.decode(errors='replace'))
            if r.returncode == 0:
                return src, None
            lines = [l.split(': error:')[-1].strip()[:80] for l in err.splitlines()
                     if ': error:' in l]
            return src, lines
        started = time.time()
        with ThreadPoolExecutor(max_workers=opts.jobs) as ex:
            res = list(ex.map(one, sources))
        return res, time.time() - started

    readings = []

    def report(tag, res, elapsed):
        bad = [(s, e) for s, e in res if e]
        print('%-28s fails: %3d / %d' % (tag, len(bad), len(res)))
        c = collections.Counter()
        for s, e in bad:
            c[(e[0] if e else '(no error line)')] += 1
        for k, v in c.most_common(6):
            print('      %3d  %s' % (v, k))
        for s, e in bad[:6]:
            print('         ', os.path.relpath(s, opts.root))
        readings.append({'tag': tag, 'fails': len(bad), 'total': len(res),
                         'elapsed_s': round(elapsed, 1),
                         'messages': [[k, v] for k, v in c.most_common()],
                         'files': [os.path.relpath(s, opts.root) for s, e in bad]})
        return bad

    # Trap: every arm needs a baseline arm beside it.  An overlay arm's failure count is read as a
    # difference from this one; on its own it cannot say whether a failing TU failed because of the
    # edit or because it was already failing, and an arm that measures nothing then looks like an
    # arm that measured no regression.
    res, elapsed = run_all(None)
    report('baseline (no overlay)', res, elapsed)
    print()

    for key, tag, header, needle, repl in ARMS:
        overlay = os.path.join(opts.overlay_dir, key)
        os.makedirs(os.path.join(overlay, os.path.dirname(header)), exist_ok=True)
        real = os.path.join(opts.root, opts.include_root, header)
        src = open(real, encoding='utf-8').read()
        if needle not in src:
            die('arm %s replaces %r, which %s no longer contains: the header changed under this '
                'arm, so the edit it describes is not the edit that would be measured'
                % (key, needle, real))
        with open(os.path.join(overlay, header), 'w', encoding='utf-8') as f:
            f.write(src.replace(needle, repl))
        res, elapsed = run_all(overlay)
        report(tag, res, elapsed)
        print()

    json.dump({'host': opts.host, 'root': opts.root, 'db': db_path, 'jobs': opts.jobs,
               'arms': readings},
              open(os.path.join(opts.out_dir, 'edits.json'), 'w', encoding='utf-8'), indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())