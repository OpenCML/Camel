# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Fail a macOS wheel whose binaries load something the user's Mac does not have, or are unsigned.

`scripts/check_wheel_tags.py` decides whether the *name* of a wheel is publishable. This decides
whether the *content* is: every binary the wheel carries has to be able to start on a Mac that has
nothing but this wheel installed, and on arm64 a Mach-O whose bytes changed after the linker signed
it does not start at all (`Killed: 9`).

Two checks, both against the unpacked wheel:

1. **Dependencies.** For every Mach-O file, `otool -L` has to name only

   - a system library (`/usr/lib`, `/System`), which every Mac has, or
   - a file inside the wheel, reached through `@loader_path` / an `@rpath` that stays inside it.

   Anything else is a dependency on the build machine: the build tree's `libcamel.dylib` (the
   recorded install name is the absolute build path, because `CMakeLists.txt` sets
   `CMAKE_MACOSX_RPATH OFF`), a Conan cache copy, or a framework under `/Library` that the user may
   not have. `auditwheel` does this job on Linux and has no opinion about a wheel it never saw.

2. **Signatures.** `codesign --verify --strict` has to pass for every Mach-O. `delocate-wheel`
   rewrites install names with `install_name_tool`-equivalent calls, which invalidates the linker's
   ad-hoc signature; delocate 0.13.0 re-signs the files it rewrites (`delocate.tools.set_install_name`
   and `set_install_id` -> `replace_signature(..., "-")`, and `_delete_rpaths` for the rpath
   sanitizing), and this is what proves it on the artifact rather than in its source. If a later
   delocate stops signing, this check fails rather than the user's `camel --version`.

What this does *not* do: judge whether the deployment target in the binaries is 13.0. `delocate`'s
own name update does that (`MACOSX_DEPLOYMENT_TARGET=13.0` makes the tag `macosx_13_0_arm64`, and a
library that needs a newer SDK raises instead), and `check_wheel_tags.py --expect` then checks the
name.

Runs on macOS only: `otool` and `codesign` are there.

    python3 scripts/check_macos_wheel.py wheelhouse/*.whl
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

# `otool -L` prints one line per dependency as
#     \t@loader_path/../libs/libcamel.dylib (compatibility version 0.0.0, current version 0.0.0)
# and a leading `path/to/file:` header line.
DEP = re.compile(r"^\s+(.+?) \(compatibility version")

# The prefixes delocate itself refuses to copy (delocate.delocating.filter_system_libs). A
# dependency under either one is on every macOS install.
SYSTEM_PREFIXES = ("/usr/lib", "/System")

# Mach-O, thin and fat, both byte orders.
MACHO_MAGICS = {
    b"\xfe\xed\xfa\xce",
    b"\xce\xfa\xed\xfe",
    b"\xfe\xed\xfa\xcf",
    b"\xcf\xfa\xed\xfe",
    b"\xca\xfe\xba\xbe",
    b"\xbe\xba\xfe\xca",
    b"\xca\xfe\xba\xbf",
    b"\xbf\xba\xfe\xca",
}


def run(argv):
    """Run a command and return `(returncode, stdout, stderr)` without raising."""
    result = subprocess.run(argv, capture_output=True, text=True)
    return result.returncode, result.stdout, result.stderr


def is_macho(path):
    try:
        with open(path, "rb") as stream:
            return stream.read(4) in MACHO_MAGICS
    except OSError:
        return False


def macho_files(root):
    """Every Mach-O under `root`, as paths relative to it."""
    found = []
    for dirpath, _dirnames, filenames in os.walk(root):
        for name in filenames:
            path = os.path.join(dirpath, name)
            if os.path.islink(path) or not is_macho(path):
                continue
            found.append(os.path.relpath(path, root))
    return sorted(found)


def otool_dependencies(path):
    """The load commands' dependency paths, first line (the header) dropped."""
    code, out, err = run(["otool", "-L", path])
    if code != 0:
        raise RuntimeError("otool -L failed: %s" % (err.strip() or out.strip()))
    deps = []
    for line in out.splitlines():
        match = DEP.match(line)
        if match:
            deps.append(match.group(1).strip())
    return deps


def otool_install_id(path):
    """The LC_ID_DYLIB name, or None for an executable or a bundle (they have none)."""
    code, out, _err = run(["otool", "-D", path])
    if code != 0:
        return None
    lines = [line for line in out.splitlines() if line.strip()]
    if len(lines) < 2:
        return None
    return lines[1].strip()


def otool_rpaths(path):
    """The LC_RPATH entries, in file order."""
    code, out, _err = run(["otool", "-l", path])
    if code != 0:
        return []
    rpaths = []
    pending = False
    for line in out.splitlines():
        stripped = line.strip()
        if stripped.startswith("cmd LC_RPATH"):
            pending = True
            continue
        if pending and stripped.startswith("path "):
            # `path @loader_path/../libs (offset 12)`
            rpaths.append(stripped[len("path ") :].split(" (offset")[0].strip())
            pending = False
    return rpaths


def inside(root, candidate):
    """True when `candidate` is a real file under `root` (both absolute, symlinks resolved)."""
    try:
        resolved = os.path.realpath(candidate)
    except OSError:
        return False
    root = os.path.realpath(root)
    return resolved == root or resolved.startswith(root + os.sep)


def resolve_dependency(name, binary, root):
    """Return `(ok, detail)`: where the dependency points, and whether the user's machine can find it.

    `binary` is the absolute path of the file that records the dependency; `@loader_path` and
    `@executable_path` are relative to it for the tools and the bundles the wheel carries.
    """
    if name.startswith(SYSTEM_PREFIXES):
        return True, "system"
    if name.startswith("@loader_path/"):
        target = os.path.join(os.path.dirname(binary), name[len("@loader_path/") :])
        if not os.path.exists(target):
            return False, "@loader_path resolves to %s, which is not in the wheel" % target
        if not inside(root, target):
            return False, "@loader_path resolves outside the wheel: %s" % target
        return True, "in-wheel %s" % os.path.relpath(target, root)
    if name.startswith("@rpath/"):
        tail = name[len("@rpath/") :]
        for entry in otool_rpaths(binary):
            if entry.startswith("@loader_path/"):
                target = os.path.join(os.path.dirname(binary), entry[len("@loader_path/") :], tail)
            elif entry.startswith("@executable_path/"):
                target = os.path.join(
                    os.path.dirname(binary), entry[len("@executable_path/") :], tail
                )
            elif entry.startswith("/"):
                target = os.path.join(entry, tail)
            else:
                continue
            if os.path.exists(target) and inside(root, target):
                return True, "in-wheel %s (via @rpath %s)" % (os.path.relpath(target, root), entry)
        return False, "@rpath/%s matches no rpath that resolves inside the wheel (%s)" % (
            tail,
            ", ".join(otool_rpaths(binary)) or "no LC_RPATH",
        )
    if name.startswith("@executable_path/"):
        return False, (
            "@executable_path is the process that started, not this wheel: %r cannot be honoured "
            "by an installed package" % name
        )
    if name.startswith("/"):
        return False, "absolute path outside the wheel: %s" % name
    return False, "not an absolute, @loader_path or @rpath reference: %r" % name


def signature(path):
    """`(ok, kind, detail)` from codesign; `kind` is what `codesign -dv` says it is signed with."""
    code, out, err = run(["codesign", "--verify", "--strict", path])
    if code != 0:
        return False, "invalid", (err.strip() or out.strip() or "codesign exited %d" % code)
    _code, _out, info = run(["codesign", "-dv", path])
    kind = "signed"
    for line in info.splitlines():
        if line.startswith("Signature="):
            kind = line[len("Signature=") :].strip() or "signed"
    return True, kind, ""


def check_wheel(wheel, verbose):
    """Return `(problems, notes)` for one wheel."""
    problems = []
    notes = []
    with tempfile.TemporaryDirectory() as tmpdir:
        with zipfile.ZipFile(wheel) as archive:
            archive.extractall(tmpdir)

        binaries = macho_files(tmpdir)
        if not binaries:
            problems.append("no Mach-O file in the wheel: a native distribution has to carry some")
            return problems, notes

        referenced = set()
        signed_with = {}
        for relative in binaries:
            path = os.path.join(tmpdir, relative)
            install_id = otool_install_id(path)
            try:
                deps = otool_dependencies(path)
            except RuntimeError as error:
                problems.append("%s: %s" % (relative, error))
                continue
            for dep in deps:
                # A dylib lists its own install name first; it is not a dependency of itself.
                if install_id is not None and dep == install_id:
                    continue
                ok, detail = resolve_dependency(dep, path, tmpdir)
                if ok:
                    if detail.startswith("in-wheel "):
                        referenced.add(os.path.normpath(detail[len("in-wheel ") :].split(" (via")[0]))
                    continue
                problems.append("%s depends on %s (%s)" % (relative, dep, detail))

            ok, kind, detail = signature(path)
            if not ok:
                problems.append("%s is not signed as it ships: %s" % (relative, detail))
            else:
                signed_with[relative] = kind
            if verbose:
                print("     %-58s %-6s %s" % (relative, kind, install_id or ""))

        # Library files nothing references: they are weight in every download, and a second copy of
        # a library that something *does* reference is the defect the Linux build documents
        # (build-pypi.yml, `--exclude libcamel.so`). On macOS the duplicate is expected in the
        # shape this repository currently builds: libcamel's install name is the build-tree path,
        # so `delocate-wheel` copies that file into `camel/.dylibs/` and rewrites every reference
        # to point there -- the copy the wheel already carried at `camel/.cmlenv/libs/` is then
        # referenced by nothing. This is a note and not a failure, because it does not stop the
        # wheel from working; it names the file and, when the same name sits elsewhere in the
        # wheel, the other copy, so the duplication is measured rather than assumed.
        by_basename = {}
        for binary in binaries:
            by_basename.setdefault(os.path.basename(binary), []).append(binary)
        libraries = [b for b in binaries if b.endswith((".dylib", ".so"))]
        for library in libraries:
            if os.path.normpath(library) in referenced:
                continue
            size = os.path.getsize(os.path.join(tmpdir, library))
            others = [
                other for other in by_basename.get(os.path.basename(library), []) if other != library
            ]
            if others:
                notes.append(
                    "%s carries no load command from anything in the wheel (%d bytes) while %s has "
                    "the same name: this copy is not the one that loads, and every download pays "
                    "for it" % (library, size, ", ".join(others))
                )
            else:
                notes.append(
                    "%s carries no load command from anything in the wheel (%d bytes): it is either "
                    "opened by name at run time or weight every download pays for" % (library, size)
                )

        if signed_with:
            kinds = sorted(set(signed_with.values()))
            print(
                "     %d Mach-O files, %d signed: %s"
                % (len(binaries), len(signed_with), ", ".join(kinds))
            )
    return problems, notes


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("wheels", nargs="+")
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="list every Mach-O with its signature kind and install name",
    )
    args = parser.parse_args()

    if sys.platform != "darwin":
        print("::error::this check needs macOS (otool, codesign); refusing to report a pass", file=sys.stderr)
        return 1

    failed = False
    for wheel in args.wheels:
        if not os.path.isfile(wheel):
            failed = True
            print("FAIL %s: no such file" % wheel, file=sys.stderr)
            continue
        print("checking %s" % wheel)
        problems, notes = check_wheel(wheel, args.verbose)
        for note in notes:
            print("NOTE %s: %s" % (wheel, note))
        if problems:
            failed = True
            for problem in problems:
                print("::error file=%s::%s" % (wheel, problem))
                print("FAIL %s: %s" % (wheel, problem), file=sys.stderr)
        else:
            print("ok   %s" % wheel)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())