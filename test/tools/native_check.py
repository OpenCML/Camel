# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Where a check executable built by a module is: the install the build wrote, never a build directory.

`tiles-plan-check`, `tiles-async-check` and `tiles-module-lease` are built by the tiles module and
collected into the install's `bin/`: each module writes the list of them at generate time
(`build/camel-test-executables-<...>-<config>.txt`, one `$<TARGET_FILE:...>` per line) and
`scripts/collect-out.js` copies exactly what that list names. A launcher asks here, so it never
spells a build directory -- a target's binary directory follows its source directory, and one layout
move already left three launchers reading a directory nothing writes any more.

The install is the one the plan runner measured, which it names as `CAMEL_HOME` for every child:
`out/latest`, `out/debug` under `--build debug`, or the root of a `--camel <path>`. Without that
variable (a case started by hand) it is this tree's `out/latest`, the same default every other tool
in `test/tools/` takes.

A file that is not there is a failure with its path in it, and not a skip:

    产物的缺失是一个读数(读不到),不是一个结论(不用测)。
    A missing product is a reading -- nothing could be read -- and not a conclusion -- nothing to
    test.

A skip and a pass are the same number in a run's summary, which is how an entry whose check body
never started stayed counted for as long as the spelling was wrong. The failure names the path, so
the next reader has the one fact that says whether the build stopped producing it or the launcher
started looking somewhere else.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path


def install_root(root) -> Path:
    """The install a case measures: `CAMEL_HOME`, else this tree's `out/latest`."""
    named = os.environ.get("CAMEL_HOME")
    if named:
        return Path(named)
    return Path(root) / "out" / "latest"


def resolve(root, name: str) -> Path:
    """The path of `name` in the install's `bin/`; exits 1 naming the path when it is not there."""
    binary = install_root(root) / "bin" / name
    if binary.is_file():
        return binary
    print(
        "the check executable this case runs was not built into the install: "
        f"{binary}\n"
        "a missing product is a reading (nothing could be read), not a conclusion (nothing to "
        "test): the case fails here rather than skipping, because a skip and a pass are the same "
        "number in a run's summary",
        file=sys.stderr,
    )
    sys.exit(1)


def run_path(root) -> str:
    """The install's `bin` and `libs`, which the check executables look their libraries up through."""
    install = install_root(root)
    return os.pathsep.join([str(install / "bin"), str(install / "libs")])


def environment(root) -> dict:
    """This process's environment, with the install ahead of it on both library search paths."""
    env = os.environ.copy()
    env["PATH"] = os.pathsep.join([run_path(root), env.get("PATH", "")])
    if os.name != "nt":
        env["LD_LIBRARY_PATH"] = os.pathsep.join(
            [str(install_root(root) / "libs"), env.get("LD_LIBRARY_PATH", "")]
        )
    return env