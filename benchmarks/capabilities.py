"""Write the ONNX backend's operator capability table.

Runs ``tools/onnx_capabilities.cml`` (which prints ``onnx.supported_operators()``)
and writes ``<out>/onnx_capabilities.md``: every Camel operator URI the exporter
can lower at the default opset, grouped by provider.

Usage:  python capabilities.py [--out results/report]
"""

from __future__ import annotations

import argparse
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


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default="results/report")
    args = ap.parse_args()
    out = Path(args.out)
    if not out.is_absolute():
        out = BENCH_ROOT / out
    out.mkdir(parents=True, exist_ok=True)
    uris = supported_operators()
    (out / "onnx_capabilities.md").write_text(render(uris))
    print(f"wrote {out / 'onnx_capabilities.md'} ({len(uris)} operators)")


if __name__ == "__main__":
    main()
