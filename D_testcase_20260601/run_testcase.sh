#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
TC="${1:?usage: run_testcase.sh testcaseN}"
CHECKER_OUTPUT="$(mktemp "${TMPDIR:-/tmp}/${TC}.checker.XXXXXX")"
trap 'rm -f "$CHECKER_OUTPUT"' EXIT

cd "$ROOT"
make clean
make
./cadd0016 "./$TC" "./$TC/modified_clk_tree.structure"
./checker "$ROOT/$TC/clk_tree.structure" "$ROOT/$TC/modified_clk_tree.structure" \
  "$ROOT/$TC/buf.lib" "$ROOT/$TC/FF_delay.rpt" "$ROOT/$TC/SS_delay.rpt" | tee "$CHECKER_OUTPUT"
python3 "$ROOT/analyze_score_terms.py" --label "$TC" "$CHECKER_OUTPUT"
