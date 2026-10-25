# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""The console entry points the wheel installs: `camel`, `camel-ls`, `camel-db`, `camel-format`.

Each one hands the process over to the binary the wheel carries under `camel/.cmlenv/bin/`:

    pip install camel-lang
    camel hello.cml std::jit

The runtime finds its own install from the path of the executable it is running as
(`include/camel/utils/install_layout.h`, `resolveInstallRoot`: the parent of the `bin`
directory the executable sits in), so the standard library, `libs/` and the modules are found
without setting `CAMEL_HOME`, and no shell or `.cmd` wrapper has to guess a `site-packages`
layout.

This module is deliberately **not** inside the `camel` package. `import camel` needs the
native extension and reports a missing one (`camel/_native.py`) when the wheel carries the
body only (`py3-none-<platform>`), and the `camel` command has to keep working on exactly
those interpreters -- so importing it must not drag the package in.

On POSIX `os.execv` replaces this process, so the arguments, the exit code and the signals are
the binary's own. Windows has no `exec`; there the child's exit code becomes this process's.
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

# `camel_cli.py` is installed beside the `camel` package, so this names the wheel's own copy
# of the install without asking Python where site-packages is.
_BIN = Path(__file__).resolve().parent / "camel" / ".cmlenv" / "bin"


def _binary(tool: str) -> str:
    exe = tool + (".exe" if os.name == "nt" else "")
    path = _BIN / exe
    if not path.is_file():
        raise SystemExit(
            f"{tool}: the bundled binary is missing ({path}). Reinstall camel-lang; the wheel "
            f"carries its binaries under camel/.cmlenv/bin/, one per entry point."
        )
    return str(path)


def _run(tool: str) -> None:
    """Replace this process with the bundled binary, keeping its arguments and its exit status."""
    argv = [str(_binary(tool)), *sys.argv[1:]]
    if os.name == "nt":
        # No exec on Windows: run the binary as a child and take its exit code. A console
        # control event (Ctrl-C) goes to the whole process group, so the binary gets that too.
        raise SystemExit(subprocess.call(argv))
    os.execv(argv[0], argv)


def camel() -> None:
    """The Camel CLI: `camel program.cml [pass ...]`."""
    _run("camel")


def ls() -> None:
    """The Camel language server (`camel-ls`)."""
    _run("camel-ls")


def debugger() -> None:
    """The Camel debugger: DAP, REPL and native HTTP back end (`camel-db`)."""
    _run("camel-db")


def formatter() -> None:
    """The Camel source formatter (`camel-format`)."""
    _run("camel-format")