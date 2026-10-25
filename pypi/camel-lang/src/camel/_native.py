# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Where the native extension module is, and how it is loaded (H253).

The extension is `_camel`, a CPython module that links `libcamel`. Two layouts are real:

* an install built from this repository -- `out/<tag>/python/_camel*.so` beside
  `out/<tag>/libs/libcamel.so`, which is what `CAMEL_HOME` names;
* a wheel -- `camel/.cmlenv/python/_camel*.so` beside `camel/.cmlenv/libs/libcamel.so`, because
  the wheel is the whole install under `camel/.cmlenv/` (scripts/pypi-rollup.js).

Both put the extension one directory below the file that sits beside `libs/`, which is the
runpath the build writes (`$ORIGIN/../libs`). The preload below is the second belt: on Linux a
library already open in the process is reused when the loader resolves `libcamel.so`, so naming
it explicitly first makes the extension import even when the runpath was lost -- a copy through
a build step that does not preserve it, or a wheel unpacked somewhere unusual.

Nothing here imports NumPy: the extension hands out DLPack capsules, and the wrapper in
`camel/__init__.py` turns them into arrays when NumPy is available.
"""

from __future__ import annotations

import ctypes
import glob
import importlib
import os
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_PACKAGE = _HERE.name


def _library_names() -> list[str]:
    if sys.platform == "win32":
        return ["camel.dll", "libcamel.dll"]
    if sys.platform == "darwin":
        return ["libcamel.dylib"]
    return ["libcamel.so"]


def _library_roots() -> list[Path]:
    """The directories a `libcamel` could be in, nearest layout first."""
    roots = [
        _HERE,  # a build that copied the library next to the package
        _HERE / ".cmlenv" / "libs",  # the wheel
        _HERE.parent / "libs",
    ]
    home = os.environ.get("CAMEL_HOME")
    if home:
        roots.append(Path(home) / "libs")
        roots.append(Path(home))
    return roots


def _preload_library() -> None:
    for root in _library_roots():
        for name in _library_names():
            candidate = root / name
            if candidate.is_file():
                try:
                    ctypes.CDLL(str(candidate), mode=ctypes.RTLD_GLOBAL)
                except OSError:
                    # A wrong-architecture or half-written copy is not fatal here: the loader
                    # below still gets its chance, and the error that says so is the one
                    # Python raises when the import fails.
                    pass
                else:
                    if sys.platform == "win32":
                        os.add_dll_directory(str(root))
                    return


def _extension_dirs() -> list[Path]:
    """The directories an `_camel*.so` / `_camel*.pyd` could be in, nearest layout first."""
    dirs = [_HERE]
    dirs.append(_HERE / ".cmlenv" / "python")  # the wheel
    home = os.environ.get("CAMEL_HOME")
    if home:
        dirs.append(Path(home) / "python")
    # A source checkout run by the repository's own tests: the install the build collected.
    for parent in _HERE.parents:
        candidate = parent / "out" / "latest" / "python"
        if candidate.is_dir():
            dirs.append(candidate)
    return dirs


def _adopt_bundled_install() -> None:
    """Point `CAMEL_HOME` at the install this package carries, unless the caller named one.

    The runtime reads the standard library and the module search roots from `CAMEL_HOME`, and a
    wheel carries the whole install under `camel/.cmlenv/`. A project that only ran `pip install`
    therefore has an install, and must not also have to be told where it is; `CAMEL_HOME` set by
    the caller still wins, which is what a checkout that was built but not installed wants.
    """
    bundled = _HERE / ".cmlenv"
    if os.environ.get("CAMEL_HOME"):
        return
    if (bundled / "libs").is_dir() or (bundled / "stdlib").is_dir():
        os.environ["CAMEL_HOME"] = str(bundled)


def _missing_message() -> str:
    """Why no extension was found -- and the two cases need different words.

    A wheel that carries the body only (`py3-none-<platform>`, which any Python 3 can install)
    has no extension on purpose, so its message names the interpreter the extension needs and
    says that the `camel` command is unaffected. A checkout that was never built has an
    extension that does not exist yet, and that message says how to build it.
    """
    bundled = _HERE / ".cmlenv"
    if (bundled / "bin").is_dir():
        return (
            "camel was installed without its Python interop extension: this wheel "
            "(py3-none-<platform>) carries the `camel` command only. Calling Camel from Python "
            "needs the interop wheel this release published for your platform, and its file name "
            "says which CPython it is for (cp311, cp313, ...): it is the interpreter the release "
            "was built with, and there is one per platform rather than one for every version. "
            "Install camel-lang with that interpreter and pip picks the wheel that has the "
            "extension; `camel` itself works as it is."
        )
    return (
        "the Camel extension module (_camel) was not found. It is built with the rest of the "
        "project (`npm run build`, which needs Python's development files) and installed as "
        "out/latest/python/_camel*.so; a wheel carries it at camel/.cmlenv/python/. Looked in: "
        + ", ".join(str(d) for d in _extension_dirs())
    )


def _load():
    _adopt_bundled_install()
    _preload_library()
    for directory in _extension_dirs():
        if not directory.is_dir():
            continue
        found = sorted(
            glob.glob(str(directory / "_camel*.so"))
            + glob.glob(str(directory / "_camel*.pyd"))
            + glob.glob(str(directory / "_camel*.dylib"))
        )
        if not found:
            continue
        if str(directory) not in sys.path:
            sys.path.insert(0, str(directory))
        return importlib.import_module("_camel")
    raise ImportError(_missing_message())


_camel = _load()

Session = _camel.Session
CamelError = _camel.CamelError
DiagnosticError = _camel.DiagnosticError
FaultError = _camel.FaultError