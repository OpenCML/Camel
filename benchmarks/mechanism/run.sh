#!/usr/bin/env bash
# T10 mechanism counts -- one-key entry point.
# Regenerates counts.csv + details.csv next to this script.
# Overrides: CAMEL_REPO, T10_SSH_HOST, T10_SERVER_DIR (see run.py --help).
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python "$DIR/run.py" --out-dir "$DIR" "$@"
