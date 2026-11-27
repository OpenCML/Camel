# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""The corrected arm of one include edit: does the enum forward declaration compile if it names its
underlying type through `<cstdint>` instead of through the header it replaced?

An `enum class SemanticDiag : uint32_t;` names `uint32_t`, which the replaced header happened to
bring into scope; the corrected arm includes `<cstdint>` itself and spells the type `std::uint32_t`.
The overlay directory goes first on the `-I` path, so the edited copy is the one that is opened, and
every translation unit of the compile-commands database is compiled with `-fsyntax-only`.  The tree
is never touched.

This answers one arm, so it prints one line -- and a second arm is what makes it readable: with
`--baseline` the no-overlay arm is printed first, and the corrected arm is then the difference
against it.  Without that, a tree that already fails for reasons of its own fails identically here,
and the arm's count says nothing about the edit.

Run it from the repository root of the tree whose build wrote the database:

    python3 internal/tools/header-weight-edits-fix.py --baseline

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

PROG = 'header-weight-edits-fix.py'
ANSI = re.compile(r'\x1b\[[0-9;]*m')

# The arm: (header relative to --include-root, needle, replacement).
HEADER = 'camel/core/type/resolver.h'
NEEDLE = '#include "camel/core/error/diagnostics/messages/semantic.h"'
REPLACEMENT = ('#include <cstdint>\n'
               'namespace camel::core::error {\n'
               'enum class SemanticDiag : std::uint32_t;\n'
               '}')

EPILOG = """\
Traps this tool is shaped around.  Each is marked in the source at the line that avoids it:

  underlying  an enum forward declaration still has to name its underlying type, and the type it
              may only name through an include it no longer has.  The fix is to include what the
              declaration uses, not to hope the removed header's `<cstdint>` stays reachable.
  baseline    one arm alone is unreadable.  A TU that was already failing fails the same way with
              this overlay, so `--baseline` prints the no-overlay arm first; the corrected arm is
              then a difference, and a difference of zero is a claim that the edit is clean.
  overlay     the edited header must come first on the `-I` path.  An overlay placed after the
              tree's own include directories is read by nothing, and the arm then reports the
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
    def_overlay = os.path.join(tempfile.gettempdir(), 'camel-header-weight', 'edits-fix')
    p = argparse.ArgumentParser(
        prog=PROG,
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=EPILOG,
    )
    p.add_argument('--root', default=os.getcwd(),
                   help='the tree the database belongs to, and the tree the edited header is read '
                        'from. Default: the current directory, so the tool is meant to be run from '
                        'the repository root.')
    p.add_argument('--db', default=None,
                   help='the compile-commands database: it names the translation units and their '
                        'compile flags. Default: <root>/build/compile_commands.json, which is '
                        'where the build writes it.')
    p.add_argument('--include-root', default='src/kernel/include',
                   help='the directory the edited header is named from -- the one on the `-I` '
                        'path. Default: %(default)s.')
    p.add_argument('--overlay-dir', default=def_overlay,
                   help='where the overlay is written: the edited header at its include name, so '
                        'it wins over the real one on `-I`. Default: %(default)s (a scratch path '
                        'under the system temp directory).')
    p.add_argument('--out-dir', default=None,
                   help='where the JSON reading is written. Default: --overlay-dir.')
    p.add_argument('--baseline', action='store_true',
                   help='also run the no-overlay arm and print it first, so this arm is read as a '
                        'difference against it. Off by default, which prints the corrected arm '
                        'alone -- a reading that says nothing about whether the tree already '
                        'failed before the overlay went on.')
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
    real = os.path.join(opts.root, opts.include_root, HEADER)

    if not os.path.isfile(real):
        die('the arm edits %s, which does not exist: run from the repository root, or name the '
            'tree with --root (and the header with --include-root)' % real)
    if not os.path.isfile(db_path):
        die('no compile-commands database at %s: the database names the translation units and '
            'their flags, so nothing can be compiled without it. Build the tree first, or name '
            'the database with --db' % db_path)
    by_src, db = load_db(db_path)
    warn_if_foreign(db[0], db_path)
    os.makedirs(opts.out_dir, exist_ok=True)

    src = open(real, encoding='utf-8').read()
    if NEEDLE not in src:
        die('the arm replaces %r, which %s no longer contains: the header changed under this arm, '
            'so the edit it describes is not the edit that would be measured' % (NEEDLE, real))
    os.makedirs(os.path.join(opts.overlay_dir, os.path.dirname(HEADER)), exist_ok=True)
    with open(os.path.join(opts.overlay_dir, HEADER), 'w', encoding='utf-8') as f:
        f.write(src.replace(NEEDLE, REPLACEMENT))

    print('=== host=%s root=%s db=%s jobs=%d' % (opts.host, opts.root, db_path, opts.jobs))
    sources = list(by_src)
    readings = []

    def run_all(overlay):
        def one(src_):
            e = by_src[src_]
            c = strip_args(e['command'])
            # The overlay goes before every other `-I`: after them, the real header is found first
            # and the overlay is read by nothing.
            c = c[:1] + ['-I', overlay] + c[1:] + ['-fsyntax-only']
            r = subprocess.run(c, cwd=e['directory'], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
            err = ANSI.sub('', r.stderr.decode(errors='replace'))
            if r.returncode == 0:
                return None
            return [l.split(': error:')[-1].strip()[:80] for l in err.splitlines()
                    if ': error:' in l]
        started = time.time()
        with ThreadPoolExecutor(max_workers=opts.jobs) as ex:
            res = list(ex.map(one, sources))
        return res, time.time() - started

    def report(tag, res, elapsed):
        bad = [(s, e) for s, e in zip(sources, res) if e]
        c = collections.Counter()
        for s, e in bad:
            c[e[0] if e else '(none)'] += 1
        readings.append({'tag': tag, 'fails': len(bad), 'total': len(res),
                         'elapsed_s': round(elapsed, 1),
                         'messages': [[k, v] for k, v in c.most_common()],
                         'files': [os.path.relpath(s, opts.root) for s, e in bad]})
        return bad, c

    if opts.baseline:
        # Trap: the corrected arm alone is unreadable.  A TU that was already failing fails here
        # for its own reason and cannot be told from one this overlay broke, so the no-overlay arm
        # is printed first and the arm below is read as a difference against it.
        res, elapsed = run_all(None)
        bad, c = report('baseline (no overlay)', res, elapsed)
        print('%-28s fails: %3d / %d' % ('baseline (no overlay)', len(bad), len(res)))
        for k, v in c.most_common(6):
            print('   %3d  %s' % (v, k))
        for s, e in bad[:8]:
            print('      ', os.path.relpath(s, opts.root))
        print()

    res, elapsed = run_all(opts.overlay_dir)
    bad, c = report('e1b resolver.h with <cstdint> + std::uint32_t', res, elapsed)
    print('e1b resolver.h with <cstdint> + std::uint32_t: fails %d / %d' % (len(bad), len(res)))
    for k, v in c.most_common(6):
        print('   %3d  %s' % (v, k))
    for s, e in bad[:8]:
        print('      ', os.path.relpath(s, opts.root))

    json.dump({'host': opts.host, 'root': opts.root, 'db': db_path, 'jobs': opts.jobs,
               'header': HEADER, 'replacement': REPLACEMENT, 'arms': readings},
              open(os.path.join(opts.out_dir, 'edits-fix.json'), 'w', encoding='utf-8'), indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())