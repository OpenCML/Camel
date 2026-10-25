# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Set the executable bit on the tool binaries a wheel carries under `camel/.cmlenv/bin/`.

`pip` installs the mode the wheel's zip entries record, not the mode the file had in the build
tree. setuptools writes every package-data file as `0o644` whatever the source mode is, so
`chmod +x` before the build changes nothing in the artifact -- measured on the host: a `0o755`
source file comes out of `python -m pip wheel` as `external_attr >> 16 == 0o100644`, and a
`0o644` one comes out the same.

The console entry points (`camel`, `camel-ls`, `camel-db`, `camel-format`) `exec` those binaries
(`pypi/camel-lang/src/camel_cli.py`), so a wheel without the bit installs commands that fail on
first use with `PermissionError: [Errno 13] Permission denied`. Nothing else in the pipeline
can set it, which is why this rewrites the archive the way `scripts/wheel_retag.py` does: a
wheel is a zip file and `zipfile` is enough, so a machine with no package index still produces
the same artifact.

    python3 scripts/wheel_modes.py dist/pypi/*.whl            # fix, print what changed
    python3 scripts/wheel_modes.py --check dist/pypi/*.whl    # verify only, non-zero if wrong
"""

import argparse
import os
import sys
import zipfile

# Where the entry points look for the binaries (`_BIN` in src/camel_cli.py), and the mode the
# POSIX entry points need. Windows has no executable bit; the `.exe` files work as they are.
BUNDLED_BIN = "/.cmlenv/bin/"
EXECUTABLE = 0o100755


def is_bundled_binary(name):
    return BUNDLED_BIN in name and not name.endswith("/")


def modes(archive):
    """`{entry name: mode}` for the bundled binaries; the mode from the zip's external attr."""
    return {
        info.filename: (info.external_attr >> 16) & 0o7777
        for info in archive.infolist()
        if is_bundled_binary(info.filename)
    }


def check(wheel):
    """Return the bundled binaries that are not executable."""
    with zipfile.ZipFile(wheel) as archive:
        found = modes(archive)
    return [name for name, mode in sorted(found.items()) if not mode & 0o111], len(found)


def fix(wheel):
    """Rewrite `wheel` in place with the executable bit on its bundled binaries."""
    rewritten = wheel + ".modes"
    changed = []
    with zipfile.ZipFile(wheel) as source:
        with zipfile.ZipFile(rewritten, "w", zipfile.ZIP_DEFLATED) as target:
            for info in source.infolist():
                data = source.read(info.filename)
                if is_bundled_binary(info.filename) and ((info.external_attr >> 16) & 0o7777) != EXECUTABLE:
                    info.external_attr = EXECUTABLE << 16
                    changed.append(info.filename)
                target.writestr(info, data)
    os.replace(rewritten, wheel)
    return changed


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("wheels", nargs="+")
    parser.add_argument("--check", action="store_true", help="verify only; do not rewrite")
    args = parser.parse_args()

    failed = False
    for wheel in args.wheels:
        if args.check:
            not_executable, total = check(wheel)
            if total == 0:
                print("::error file=%s::no bundled binary (.cmlenv/bin/) in this wheel" % wheel)
                print("FAIL %s: no bundled binary" % wheel, file=sys.stderr)
                failed = True
            elif not_executable:
                for name in not_executable:
                    print(
                        "::error file=%s::%s is not executable in the wheel; the console entry "
                        "point that execs it would fail with PermissionError" % (wheel, name)
                    )
                    print("FAIL %s: %s not executable" % (wheel, name), file=sys.stderr)
                failed = True
            else:
                print("ok   %s (%d binaries executable)" % (wheel, total))
        else:
            changed = fix(wheel)
            print(
                "%s: %d binar%s made executable%s"
                % (
                    wheel,
                    len(changed),
                    "y" if len(changed) == 1 else "ies",
                    "" if changed else " (already were)",
                )
            )
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())