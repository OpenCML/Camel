"""Write the ONNX backend's operator capability table.

Runs ``tools/onnx_capabilities.cml`` (which prints ``onnx.supported_operators()``)
and writes ``<out>/onnx_capabilities.md``: every Camel operator URI the exporter
can lower at the default opset, grouped by provider, followed by what the
export target runs: the ONNX operator x element type x opset matrix of ONNX
Runtime's CPU provider from ``modules/onnx/capabilities/onnxruntime-cpu.json``
(the data the exporter checks against; regenerate it with
``tools/probe_ort_capabilities.py``).

Usage:  python capabilities.py [--out results/report]
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
from collections import defaultdict
from pathlib import Path

from common.configs import REPO_ROOT, camel_binary

BENCH_ROOT = Path(__file__).resolve().parent

GROUPS = [
    ("tensor", "Tensor operators (`tensor:`)"),
    ("nn", "Neural-network layers (`nn:`)"),
    ("builtin", "Builtin scalar operators (`:op/`), used by input-dependent shapes and conditions"),
    ("math", "Math module (`math:`)"),
]


def supported_operators() -> list[str]:
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    proc = subprocess.run(
        [str(camel_binary()), str(BENCH_ROOT / "tools" / "onnx_capabilities.cml")],
        capture_output=True,
        text=True,
        env=env,
        check=True,
    )
    return [line.split(" ", 1)[1] for line in proc.stdout.splitlines() if line.startswith("CAPABILITY ")]


def group_of(uri: str) -> str:
    if uri.startswith(":op/"):
        return "builtin"
    return uri.split(":", 1)[0]


def render(uris: list[str]) -> str:
    grouped: dict[str, list[str]] = defaultdict(list)
    for uri in uris:
        grouped[group_of(uri)].append(uri)
    lines = [
        "# ONNX export: operator capabilities",
        "",
        f"{len(uris)} Camel operators can be lowered to ONNX (default opset).",
        "",
        "| Group | Count | Operators |",
        "|---|---|---|",
    ]
    for key, title in GROUPS:
        names = sorted(grouped.pop(key, []))
        if names:
            shown = ", ".join(f"`{n.split(':', 1)[1] if key != 'builtin' else n[4:]}`" for n in names)
            lines.append(f"| {title} | {len(names)} | {shown} |")
    for key, names in sorted(grouped.items()):
        lines.append(f"| `{key}:` | {len(names)} | {', '.join(f'`{n}`' for n in sorted(names))} |")
    return "\n".join(lines) + "\n"


CAPABILITY_DATA = REPO_ROOT / "modules" / "onnx" / "capabilities" / "onnxruntime-cpu.json"


def render_target(data: dict) -> str:
    """ONNX operator x element type: the opsets the provider runs it at, and where it has no kernel."""

    def ranges(opsets: list[int]) -> str:
        out, start = [], None
        for i, o in enumerate(opsets):
            start = o if start is None else start
            if i + 1 == len(opsets) or opsets[i + 1] != o + 1:
                out.append(str(start) if start == o else f"{start}-{o}")
                start = None
        return ", ".join(out)

    dtypes = ["float32", "int64", "bool"]
    lines = [
        "",
        f"## Target: {data['runtime']} {data['runtime_version']} ({data['provider']})",
        "",
        f"Opsets probed: {data['opsets'][0]}-{data['opsets'][-1]}. Each cell lists the opsets at which the "
        "provider runs the ONNX operator on that element type; **no kernel** marks valid ONNX the provider "
        "cannot load (the exporter rejects these at export time); blank means ONNX does not define it.",
        "",
        "| ONNX operator | " + " | ".join(dtypes) + " |",
        "|---|" + "---|" * len(dtypes),
    ]
    for op, entry in sorted(data["ops"].items()):
        cells = []
        for dtype in dtypes:
            parts = []
            if dtype in entry.get("supported", {}):
                parts.append(ranges(entry["supported"][dtype]))
            if dtype in entry.get("no_kernel", {}):
                parts.append(f"**no kernel** {ranges(entry['no_kernel'][dtype])}")
            cells.append("; ".join(parts))
        lines.append(f"| `{op}` | " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="results/report")
    args = ap.parse_args()
    out = Path(args.out)
    if not out.is_absolute():
        out = BENCH_ROOT / out
    out.mkdir(parents=True, exist_ok=True)
    uris = supported_operators()
    data = json.loads(CAPABILITY_DATA.read_text())
    (out / "onnx_capabilities.md").write_text(render(uris) + render_target(data))
    print(f"wrote {out / 'onnx_capabilities.md'} ({len(uris)} operators)")


if __name__ == "__main__":
    main()
