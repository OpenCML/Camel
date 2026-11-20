# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Say, line by line, what a built wheel carries -- and refuse one that lost a file the install needs.

`scripts/check_wheel_tags.py` judges the *name* against the tag ruling: which wheel pip may install
where, and whether the file that claims to carry `_camel` does. It cannot see whether the copy of
`out/<tag>/` inside the wheel is complete, and that is the failure this repository has actually had:
`Camel` 0.2.44's install is `bin/`, `libs/`, `stdlib/`, `include/` and `python/` under
`camel/.cmlenv/`, and a pattern that misses one nesting level loses a whole directory silently --
the wheel still builds, still installs, and the first program that imports a module or compiles
against the headers is the one that finds out.

The expectation is not a list written here. It is the tree the wheel was packed from:
`out/<tag>/stdlib/*.cml` has to become `camel/.cmlenv/stdlib/*.cml`, `out/<tag>/include/**` has to
become `camel/.cmlenv/include/**`, and the binaries the `[project.scripts]` entry points exec have to
be under `camel/.cmlenv/bin/`. So a new standard-library file or a new header is covered by the
check the day it lands, and a packaging change that drops it fails here instead of after an upload.

Offline by construction: it reads the wheel with `zipfile` and the tree with `os.walk`. No index, no
network, and it writes nothing.

Usage (a rolled-up wheel set, and the tree it came from):

    python3 scripts/check_wheel_contents.py --out out/latest dist/pypi/*.whl
    python3 scripts/check_wheel_contents.py --list dist/pypi/*.whl   # the manifest only

Exit 0 when every wheel matches, 1 when one does not, 2 on a usage error.
"""

from __future__ import annotations

import argparse
import os
import sys
import zipfile

# The per-platform spelling of the runtime library, from the wheel's own platform tag: the check
# runs on the machine that built the wheel, but it must not assume that machine is the target.
LIBRARY_BY_PLATFORM = (
    ("win_", "libcamel.dll"),
    ("macosx_", "libcamel.dylib"),
    ("manylinux_", "libcamel.so"),
    ("musllinux_", "libcamel.so"),
    ("linux_", "libcamel.so"),
)

# `camel` and the four commands `[project.scripts]` declares (pypi/camel-lang/pyproject.toml). The
# rollup checks the same four against `camel/.cmlenv/bin/` before it packs; this checks the packed
# wheel, which is a different claim -- one is "collected", the other is "travelled".
DECLARED_COMMANDS = ("camel", "camel-ls", "camel-db", "camel-format")

EXTENSION_SUFFIXES = (".so", ".pyd", ".dylib")


def parse_wheel_name(path):
    stem = os.path.basename(path)
    if not stem.endswith(".whl"):
        raise ValueError("not a wheel: %s" % stem)
    parts = stem[: -len(".whl")].split("-")
    if len(parts) < 5:
        raise ValueError("not a wheel name (name-version-python-abi-platform): %s" % stem)
    return parts[0], parts[1], parts[-3], parts[-2], parts[-1]


def library_name(platform_tag):
    for prefix, name in LIBRARY_BY_PLATFORM:
        if platform_tag.startswith(prefix):
            return name
    return None


def tree_files(root):
    """Every file under `root`, as paths relative to it with `/` separators."""
    found = []
    for directory, _dirs, names in os.walk(root):
        for name in names:
            full = os.path.join(directory, name)
            found.append(os.path.relpath(full, root).replace(os.sep, "/"))
    return sorted(found)


def expectation(tree, out_dir):
    """What `camel/.cmlenv/` has to hold, from the tree and the collected install."""
    wanted = {}

    def add(prefix, rel_paths):
        for rel in rel_paths:
            wanted[prefix + rel] = True

    stdlib = os.path.join(tree, "stdlib")
    if os.path.isdir(stdlib):
        add(
            "camel/.cmlenv/stdlib/",
            [rel for rel in tree_files(stdlib) if not rel.endswith(".cmo")],
        )
    include = os.path.join(tree, "include")
    if os.path.isdir(include):
        add("camel/.cmlenv/include/", tree_files(include))
    collected_stdlib = os.path.join(out_dir, "stdlib")
    if os.path.isdir(collected_stdlib):
        # The `.cmo` files are build products: the install is the only place that knows which
        # modules this configure built (`scripts/collect-out.js` removes a stale one).
        add(
            "camel/.cmlenv/stdlib/",
            [rel for rel in tree_files(collected_stdlib) if rel.endswith(".cmo")],
        )
    return wanted


def check_one(path, tree, out_dir, expected, listing=False):
    """Return (problems, role) for one wheel; `role` is `interop` or `runtime`."""
    problems = []
    name, version, python_tag, abi_tag, platform_tag = parse_wheel_name(path)
    with zipfile.ZipFile(path) as archive:
        entries = [item for item in archive.namelist() if not item.endswith("/")]
    present = set(entries)

    # The two roles the ruling defines: `cp310-abi3` carries `_camel`, `py3-none` must not.
    has_extension = [e for e in entries if os.path.basename(e).startswith("_camel") and e.endswith(EXTENSION_SUFFIXES)]
    role = "interop" if python_tag != "py3" or abi_tag == "abi3" else "runtime"
    if role == "interop" and not has_extension:
        problems.append("the interop wheel carries no _camel extension")
    if role == "runtime" and has_extension:
        problems.append("the runtime-only wheel carries %s" % ", ".join(has_extension))
    if role == "interop" and not any(e.startswith("camel/.cmlenv/python/") for e in has_extension):
        problems.append("the interop wheel's _camel is not under camel/.cmlenv/python/")

    # Every file the tree says the install holds, at the path the package data has to put it.
    for wanted in sorted(expected):
        if wanted not in present:
            problems.append("missing from the wheel: %s" % wanted)

    # The binaries the entry points exec, and the runtime library they load.
    bin_dir = "camel/.cmlenv/bin/"
    binaries = {e[len(bin_dir):] for e in entries if e.startswith(bin_dir) and "/" not in e[len(bin_dir):]}
    for command in DECLARED_COMMANDS:
        if not any(b == command or b in (command + ".exe", command + ".real") for b in binaries):
            problems.append("no binary for the %r entry point under camel/.cmlenv/bin/" % command)
    if not binaries:
        problems.append("camel/.cmlenv/bin/ is empty")
    library = library_name(platform_tag)
    if library is None:
        problems.append("platform tag %r names no library convention this check knows" % platform_tag)
    elif "camel/.cmlenv/libs/" + library not in present:
        problems.append("missing camel/.cmlenv/libs/%s" % library)

    # The modules: every `.cmo` the install collected is a module this release promises. A listing
    # run has no install to compare with, so it does not judge this.
    collected = sorted(rel for rel in expected if rel.startswith("camel/.cmlenv/stdlib/") and rel.endswith(".cmo"))
    if not listing and out_dir and not collected:
        problems.append(
            "no .cmo in %s/stdlib/ -- either the build collected none or the install is not there; "
            "a wheel of commands with no modules is not this distribution" % out_dir
        )

    return problems, role, entries


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("wheels", nargs="+")
    parser.add_argument("--tree", default=".", help="the checkout the wheel was packed from (default: .)")
    parser.add_argument("--out", default="out/latest", help="the collected install (default: out/latest)")
    parser.add_argument("--list", action="store_true", help="print the manifest, check nothing")
    args = parser.parse_args()

    tree = os.path.abspath(args.tree)
    out_dir = os.path.abspath(args.out) if args.out else ""
    if not args.list and not os.path.isdir(tree):
        print("not a directory: %s" % tree, file=sys.stderr)
        return 2
    expected = {}
    if not args.list:
        expected = expectation(tree, out_dir)
        if not expected:
            print(
                "%s holds neither stdlib/ nor include/: this is not a checkout of the project, so "
                "there is nothing to compare the wheel with" % tree,
                file=sys.stderr,
            )
            return 2

    failed = False
    for path in args.wheels:
        if not os.path.isfile(path):
            print("FAIL %s: no such file" % path, file=sys.stderr)
            failed = True
            continue
        problems, role, entries = check_one(path, tree, out_dir, expected, args.list)
        name, version, python_tag, abi_tag, platform_tag = parse_wheel_name(path)
        print("== %s (%s-%s-%s-%s, %s)" % (os.path.basename(path), name, version, python_tag, abi_tag, platform_tag))
        print("   role: %s" % role)
        print("   files: %d" % len(entries))
        groups = {}
        for entry in entries:
            head = "/".join(entry.split("/")[:3]) if entry.startswith("camel/.cmlenv/") else entry.split("/")[0]
            groups[head] = groups.get(head, 0) + 1
        for head in sorted(groups):
            print("   %5d  %s" % (groups[head], head))
        # The manifest is printed whether or not the check passed: "which file is missing" is
        # answered by reading the list, and the list is what a reviewer compares line by line.
        for entry in sorted(entries):
            print("     %s" % entry)
        if problems:
            failed = True
            for problem in problems:
                print("FAIL %s: %s" % (os.path.basename(path), problem), file=sys.stderr)
        else:
            print("ok   %s" % os.path.basename(path))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
