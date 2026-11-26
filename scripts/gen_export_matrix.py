# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""M6: generate the ONNX export capability matrix from the lowering registries.

Scans the Camel source for:
  * every OpDef registration under src/stdlib/<ns>/**.cpp (`.name = "op"`) -> the
    operator universe, namespaced as <ns>:<op>,
  * the forward lowering table  src/stdlib/onnx/lowering.cpp        add("uri", fn[, minOpset])
  * the gradient lowering table src/stdlib/onnx/lowering_grad.cpp   add("uri", fn, minOpset)

and emits a matrix: operator x (forward lowering? min opset) x (gradient
lowering? min opset). Numbers in the opset column are the minimum ONNX opset
version of the emitted op(s), as registered at the add() call.

Those three paths are constants below, and `--check` treats each of them as an
input it must be able to read: the layout has moved twice under this script
(`modules/onnx/` -> `lib/onnx/` -> `src/stdlib/onnx/`), and the second move left
`--check` raising FileNotFoundError before it compared anything, which made
`-Check all`'s `export-matrix` row red on the base tree instead of red on a
finding.

Outputs (paper appendix material; the tree does not keep a copy -- see `--check`):
  export_matrix.csv  (full table)
  export_matrix.md   (markdown, grouped: both / forward-only / no lowering)

    python3 scripts/gen_export_matrix.py           regenerate both files
    python3 scripts/gen_export_matrix.py --check   the reading this tree can give:
                                                  both lowering tables and the operator root are
                                                  readable, none of the three scans is empty, and
                                                  -- when the tree does keep a copy of the two
                                                  files at its root -- that copy is what this
                                                  generator produces, byte for byte (LF):
                                                  `.gitattributes` pins `* text=auto eol=lf`
                                                  for this tree, so a CRLF file is a mismatch
                                                  here, not an equivalent spelling

The OpDef scan is textual (`.name = "..."` literals); operators registered via
variables instead of literals are missed and counted in the summary line only.
"""

from __future__ import annotations

import difflib
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]

# The three trees this generator reads, at the paths this layout has them
# (`docs/design/architecture/layout-map.cn.md`; `src/stdlib/onnx/CMakeLists.txt` compiles both
# tables, so these are the files the exporter itself is built from and not a copy of them). They
# are constants because a stale spelling in a scan is not a wrong reading, it is no reading at
# all: `--check` died on the old root before comparing anything.
FORWARD_TABLE = REPO / "src" / "stdlib" / "onnx" / "lowering.cpp"
GRADIENT_TABLE = REPO / "src" / "stdlib" / "onnx" / "lowering_grad.cpp"
OPDEF_ROOT = REPO / "src" / "stdlib"
# The one module root the operator scan skips: the onnx module's own registrations are the two
# lowering tables, and counting them as Camel operators would put this generator's subject in the
# universe it is a table of.
SKIPPED_MODULE = "onnx"

ADD_RE = re.compile(r'add\(\s*"([^"]+)"\s*,\s*(.+?)\s*(?:,\s*(\d+)\s*)?\)\s*;')
NAME_RE = re.compile(r'\.name\s*=\s*"([a-zA-Z0-9_]+)"')


def rel(path: Path) -> str:
    """The path as this tree spells it, so a message names something a reader can open."""
    return path.relative_to(REPO).as_posix()


def read_source(path: Path) -> str:
    """The file's text, or exit 1 naming the path that is not there.

    A layout move that leaves one of these lines behind used to surface as a FileNotFoundError
    traceback, which reads like a bug in the checker and buries the one fact worth having: which
    path is missing.
    """
    try:
        return path.read_text(encoding="utf-8")
    except FileNotFoundError:
        print(
            f"{rel(path)} is not in this tree: the export matrix reads it as an input, so the "
            f"layout moved and this line did not",
            file=sys.stderr,
        )
        sys.exit(1)


def scan_lowerings(path: Path) -> dict[str, int | None]:
    out = {}
    for uri, _fn, ver in ADD_RE.findall(read_source(path)):
        out[uri] = int(ver) if ver else None
    return out


def scan_opdefs() -> set[str]:
    uris = set()
    if not OPDEF_ROOT.is_dir():
        print(
            f"{rel(OPDEF_ROOT)}/ is not a directory in this tree: the operator scan reads the "
            f"module roots there",
            file=sys.stderr,
        )
        sys.exit(1)
    for cpp in OPDEF_ROOT.rglob("*.cpp"):
        if SKIPPED_MODULE in cpp.parts:
            continue
        ns = cpp.relative_to(OPDEF_ROOT).parts[0]
        for name in NAME_RE.findall(cpp.read_text(encoding="utf-8", errors="ignore")):
            uris.add(f"{ns}:{name}")
    return uris


def collect():
    """The matrix rows, and the three readings they were built from.

    The readings travel with the rows because `--check` has to say which path it read and what it
    found there: a scan over a root that is not where the registrations are finds nothing and
    exits 0, and that green is the silent empty scan the reader inventory
    (`internal/reports/2026-11-22-second-move-reader-inventory.cn.md`, section 4) warns about.
    """
    fwd = scan_lowerings(FORWARD_TABLE)
    grad = scan_lowerings(GRADIENT_TABLE)
    opdefs = scan_opdefs()
    universe = opdefs | set(fwd) | set(grad)

    rows = []
    for uri in sorted(universe):
        rows.append((uri, fwd.get(uri) if uri in fwd else "absent", grad.get(uri) if uri in grad else "absent"))
    return rows, (fwd, grad, opdefs)


def summarize(rows):
    """(forward lowerings, gradient lowerings, the URIs with neither)."""
    n_fwd = sum(1 for _, f_, _ in rows if f_ != "absent")
    n_grad = sum(1 for _, _, g in rows if g != "absent")
    none = [u for u, f_, g in rows if f_ == "absent" and g == "absent"]
    return n_fwd, n_grad, none


def render_csv(rows) -> str:
    lines = ["operator,forward_lowering_min_opset,gradient_lowering_min_opset"]
    for uri, f_, g in rows:
        show = lambda v: "" if v == "absent" else ("1" if v is None else str(v))
        lines.append(f"{uri},{show(f_)},{show(g)}")
    return "\n".join(lines) + "\n"


def render_md(rows) -> str:
    n_fwd, n_grad, none = summarize(rows)

    def fv(v):
        if v == "absent":
            return "✗"
        return "✓" if v is None else f"✓ ≥{v}"

    md = ["# ONNX export capability matrix (generated by scripts/gen_export_matrix.py)", ""]
    md.append(
        f"{len(rows)} operator URIs (textual scan; see script docstring). "
        f"**{n_fwd} forward operators** and **{n_grad} gradient operators** have ONNX lowerings; "
        f"{len(none)} have none (unsupported boundary, e.g. no VJP or no standard-op form). "
        "Gradient lowerings live on the gradient operators the autodiff emits (matmul_grad_lhs, "
        "conv2d_input_grad, layer_norm_grad, ...), so the forward/gradient columns are disjoint by construction. "
        "Opset column = minimum ONNX opset of the emitted op(s) as registered at the add() call."
    )
    md.append("")
    md.append("| operator | forward | gradient |")
    md.append("|---|---|---|")
    for uri, f_, g in rows:
        md.append(f"| `{uri}` | {fv(f_)} | {fv(g)} |")
    return "\n".join(md) + "\n"


# `* text=auto eol=lf` is the tree's line-ending rule, so both files are written with `\n` on
# every platform and `--check` compares the bytes: a CRLF file is a mismatch, not an equivalent.
LF = {"encoding": "utf-8", "newline": "\n"}
SNAPSHOT_NAMES = ("export_matrix.csv", "export_matrix.md")


def main() -> int:
    rows, (fwd, grad, opdefs) = collect()
    (REPO / SNAPSHOT_NAMES[0]).write_text(render_csv(rows), **LF)
    (REPO / SNAPSHOT_NAMES[1]).write_text(render_md(rows), **LF)
    n_fwd, n_grad, none = summarize(rows)
    print(f"wrote {REPO / SNAPSHOT_NAMES[0]} ({len(rows)} rows) and {REPO / SNAPSHOT_NAMES[1]}")
    print(f"  read {len(fwd)} forward lowerings from {rel(FORWARD_TABLE)}, {len(grad)} gradient "
          f"lowerings from {rel(GRADIENT_TABLE)}, {len(opdefs)} operator URIs under "
          f"{rel(OPDEF_ROOT)}/")
    print(f"  forward lowerings: {n_fwd}, gradient lowerings: {n_grad}, none: {len(none)}")
    return 0


def check() -> int:
    """The reading this tree can give the export matrix, as a wall. Three things have to hold, and
    each one names the path it read when it does not:

      * both lowering tables are readable. A layout move that leaves one of these lines behind is
        red here *by the name of the path* -- the state `-Check all`'s `export-matrix` row was in
        from the move of 2026-11-22 until this file learned the new root.
      * none of the three scans is empty. A scan over a root that is not where the registrations
        are reads nothing and exits 0, and that green is the silent empty scan
        `internal/reports/2026-11-22-second-move-reader-inventory.cn.md` warns about: the same
        layout move that broke the two table paths also moved the operator root, and only the
        FileNotFoundError made the second half visible.
      * a tree that keeps `export_matrix.csv` / `export_matrix.md` at its root still gets the
        byte-for-byte comparison those files were added for. This tree does not keep them
        (`2de5b196f8` took them out; `internal/plans/2026-11-22-root-cleanup.cn.md` keeps them
        out, generated into `out/`), so the line says which of the two states it read rather than
        leaving a missing file to look like agreement.
    """
    rows, (fwd, grad, opdefs) = collect()

    empty = []
    if not fwd:
        empty.append(f"{rel(FORWARD_TABLE)} registers no lowering")
    if not grad:
        empty.append(f"{rel(GRADIENT_TABLE)} registers no lowering")
    if not opdefs:
        empty.append(f"no `.name = \"...\"` registration is under {rel(OPDEF_ROOT)}/")
    if empty:
        for line in empty:
            print(f"{line}: this generator scans it and read nothing", file=sys.stderr)
        return 1

    produced = {"export_matrix.csv": render_csv(rows), "export_matrix.md": render_md(rows)}
    present = [name for name in SNAPSHOT_NAMES if (REPO / name).is_file()]
    mismatched = 0
    for name in present:
        actual = (REPO / name).read_bytes()
        if actual == produced[name].encode("utf-8"):
            continue
        mismatched += 1
        print(f"{name} is not what this generator produces; run "
              f"`python3 scripts/gen_export_matrix.py`", file=sys.stderr)
        diff = list(difflib.unified_diff(actual.decode("utf-8").splitlines(),
                                         produced[name].splitlines(),
                                         f"../{name}", "generated", lineterm=""))
        if diff:
            for line in diff:
                print(line, file=sys.stderr)
        else:
            print(f"  every line agrees but the bytes do not: {name} is not written with this "
                  f"tree's line endings (LF; .gitattributes `* text=auto eol=lf`, "
                  f"{len(actual)} bytes against {len(produced[name].encode('utf-8'))})",
                  file=sys.stderr)
    if mismatched:
        return 1

    n_fwd, n_grad, none = summarize(rows)
    print(f"export matrix reads this tree: {len(fwd)} forward lowerings in "
          f"{rel(FORWARD_TABLE)}, {len(grad)} gradient lowerings in {rel(GRADIENT_TABLE)}, "
          f"{len(opdefs)} operator URIs under {rel(OPDEF_ROOT)}/ -- {len(rows)} rows, "
          f"{n_fwd} forward, {n_grad} gradient, {len(none)} none")
    if present:
        print(f"  {', '.join(present)}: what this generator produces, byte for byte")
    else:
        print(f"  no {' or '.join(SNAPSHOT_NAMES)} at the root to compare with: this tree does "
              f"not keep a copy (it is generated into out/)")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--check"]:
        sys.exit(check())
    elif sys.argv[1:]:
        print("usage: python3 scripts/gen_export_matrix.py [--check]", file=sys.stderr)
        sys.exit(2)
    sys.exit(main())
