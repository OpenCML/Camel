# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""Camel from a Python project (H253).

A Python project calls the functions of a Camel module and the arrays cross through DLPack,
without a copy:

    import camel
    rules = camel.load("rules.cml")
    y = rules.smooth(x, 0.25)          # x and y are NumPy arrays; x is read in place

What is on this page:

* `camel.load(name, dirs=(), entry_dir=None)` -- a Camel module by name or by path;
* `camel.Module` -- the loaded module; its exported functions are its attributes;
* `camel.Session` -- one Camel runtime (a context, its search paths and its compiled libraries);
* `camel.CamelError`, `camel.DiagnosticError`, `camel.FaultError` -- what a failure raises, with
  `name`, `code`, `message`, `suggestion`, `file`, `line`, `column`, `stage` and the report the
  `camel` command line would have printed, in `camel_text`.

Only an exported, non-generic function is reachable, which is what the runtime's own door for a
native caller offers (include/camel/runtime/definitions.h, `libraryFunction`): a generic function
has no single graph to run, so this binding refuses it rather than choosing a type argument for
you. Only the numeric element types DLPack names cross -- f32, f64, i32, i64, bool, c64, c128 --
and only host memory that is contiguous and of one lane.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any, Iterable, Sequence

from ._native import (  # noqa: F401  (re-exported: `camel.CamelError` and friends)
    CamelError,
    DiagnosticError,
    FaultError,
    Session,
    _camel,
)

__version__ = "0.2.30"
__author__ = "Zhenjie Wei"
__author_email__ = "Kogler_S@outlook.com"
__description__ = "Camel: A Next-Generation Language for AI and High-Performance Computing"
__url__ = "https://github.com/OpenCML/Camel"

__all__ = [
    "load",
    "Module",
    "Session",
    "CamelError",
    "DiagnosticError",
    "FaultError",
    "__version__",
]


class _CapsuleArray:
    """A DLPack capsule dressed as the producer side of the protocol.

    The extension hands an array back as the capsule `core::array::toDLPack` built, which is what
    every consumer of the standard accepts; wrapping it costs nothing and means NumPy's
    `from_dlpack` is called the way its documentation describes, whatever its version.
    """

    __slots__ = ("_capsule",)

    def __init__(self, capsule: Any) -> None:
        self._capsule = capsule

    def __dlpack__(self, *, stream: Any = None, max_version: Any = None, copy: Any = None) -> Any:
        return self._capsule

    def __dlpack_device__(self) -> tuple:
        return (1, 0)  # kDLCPU, device 0: the only device the runtime carries today


def _as_python(value: Any) -> Any:
    """A returned capsule becomes an array when NumPy is importable, and stays a capsule when it
    is not: the extension itself does not need NumPy, only the convenience here does."""
    if value is None or not _looks_like_capsule(value):
        return value
    try:
        import numpy
    except ImportError:
        return value
    return numpy.from_dlpack(_CapsuleArray(value))


def _looks_like_capsule(value: Any) -> bool:
    return type(value).__name__ == "PyCapsule"


class Function:
    """An exported function of a loaded Camel module. Calling it runs it on the node VM."""

    __slots__ = ("_session", "_library", "_name")

    def __init__(self, session: Session, library: str, name: str) -> None:
        self._session = session
        self._library = library
        self._name = name

    @property
    def name(self) -> str:
        return self._name

    @property
    def library(self) -> str:
        return self._library

    def __call__(self, *args: Any) -> Any:
        return _as_python(self._session.call(self._library, self._name, args))

    def __repr__(self) -> str:
        return f"<camel function {self._library}::{self._name}>"


class Module:
    """A Camel module, loaded. Its exported functions are attributes of it:

        rules = camel.load("rules.cml")
        rules.smooth(x, 0.25)
        rules["smooth"](x, 0.25)
    """

    __slots__ = ("_session", "_library", "_path")

    def __init__(self, library: str, *, session: Session | None = None, path: Path | None = None):
        self._library = library
        self._path = path
        self._session = session if session is not None else Session()

    @property
    def name(self) -> str:
        return self._library

    @property
    def path(self) -> Path | None:
        return self._path

    @property
    def session(self) -> Session:
        return self._session

    def function(self, name: str) -> Function:
        return Function(self._session, self._library, name)

    def __getitem__(self, name: str) -> Function:
        if not isinstance(name, str):
            raise TypeError("a Camel function is named by a string")
        return self.function(name)

    def __getattr__(self, name: str) -> Function:
        if name.startswith("_"):
            raise AttributeError(name)
        return self.function(name)

    def __dir__(self) -> list:
        return sorted(set(super().__dir__()) | {"function"})

    def __repr__(self) -> str:
        where = f" from {self._path}" if self._path is not None else ""
        return f"<camel module {self._library}{where}>"


def load(
    name: str,
    *,
    dirs: Iterable[str] = (),
    entry_dir: str | None = None,
    session: Session | None = None,
) -> Module:
    """A Camel module by name (`"rules"`) or by path (`"rules.cml"`, `"some/dir/rules.cml"`).

    `dirs` are extra module roots, in order, and a path's own directory is one of them. They are
    read by the first `Session` the process makes: a Camel runtime compiles a library once and
    keeps it, so a later `load` reuses the context the first one built (a second context would
    compile the same module again and could not share the values its module level computed).
    """
    path: Path | None = None
    library = name
    roots = [str(d) for d in dirs]
    if name.endswith(".cml"):
        path = Path(name).expanduser().resolve()
        # A path that is not there is a mistake worth naming now: without this the caller gets a
        # `Module` that only says so at the first call, and the message would be about a library
        # the caller never named.
        if not path.is_file():
            raise FileNotFoundError(f"no Camel module at {path}")
        library = path.stem
        roots.insert(0, str(path.parent))
    if session is None:
        session = Session(entry_dir=entry_dir, search_dirs=roots)
    return Module(library, session=session, path=path)