#!/usr/bin/env python3
"""Parse checker output and report the five normalized contest score terms."""

import argparse
import math
import re
import sys
from pathlib import Path

NUMBER = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
ROW_RE = re.compile(
    rf"^\s*(SS|FF)\s*\|\s*({NUMBER})\s*\|\s*({NUMBER})\s*\|",
    re.IGNORECASE | re.MULTILINE,
)
ORI_AREA_RE = re.compile(rf"ori\s*TotalBufArea\s*\([^)]*\)\s*=\s*({NUMBER})", re.I)
NEW_AREA_RE = re.compile(rf"new\s*TotalBufArea\s*\([^)]*\)\s*=\s*({NUMBER})", re.I)
TERM_NAMES = ("SS_TNS_term", "SS_WNS_term", "FF_TNS_term", "FF_WNS_term", "AREA_term")


def parse_checker_output(text):
    rows = [(m.group(1).upper(), float(m.group(2)), float(m.group(3))) for m in ROW_RE.finditer(text)]
    corners = {"SS": [], "FF": []}
    for corner, wns, tns in rows:
        corners[corner].append((wns, tns))
    if len(corners["SS"]) < 2 or len(corners["FF"]) < 2:
        raise ValueError("could not find both Original and Modified SS/FF checker tables")
    ori_area = ORI_AREA_RE.search(text)
    new_area = NEW_AREA_RE.search(text)
    if not ori_area or not new_area:
        raise ValueError("could not find original and modified area in checker output")
    return {
        "original": {"SS": corners["SS"][0], "FF": corners["FF"][0], "area": float(ori_area.group(1))},
        "modified": {"SS": corners["SS"][1], "FF": corners["FF"][1], "area": float(new_area.group(1))},
    }


def normalized_term(modified, original):
    return None if math.isclose(original, 0.0, abs_tol=1e-15) else 1.0 - modified / original


def compute_terms(metrics):
    ori, new = metrics["original"], metrics["modified"]
    return {
        "SS_TNS_term": normalized_term(new["SS"][1], ori["SS"][1]),
        "SS_WNS_term": normalized_term(new["SS"][0], ori["SS"][0]),
        "FF_TNS_term": normalized_term(new["FF"][1], ori["FF"][1]),
        "FF_WNS_term": normalized_term(new["FF"][0], ori["FF"][0]),
        "AREA_term": normalized_term(new["area"], ori["area"]),
    }


def fmt(value):
    return "N/A" if value is None else f"{value:.4f}"


def add(values):
    return None if any(v is None for v in values) else sum(values)


def optimization_bottleneck(terms):
    if terms["SS_WNS_term"] is not None and terms["SS_WNS_term"] < 0.80:
        return "setup WNS"
    if terms["SS_TNS_term"] is not None and terms["SS_TNS_term"] < 0.90:
        return "setup TNS"
    if ((terms["FF_WNS_term"] is not None and terms["FF_WNS_term"] < 0.95) or
            (terms["FF_TNS_term"] is not None and terms["FF_TNS_term"] < 0.95)):
        return "hold"
    if terms["AREA_term"] is not None and terms["AREA_term"] < 0.30:
        return "area"
    return "balanced cleanup"


def print_analysis(label, terms):
    setup = add([terms["SS_TNS_term"], terms["SS_WNS_term"]])
    hold = add([terms["FF_TNS_term"], terms["FF_WNS_term"]])
    bottleneck = optimization_bottleneck(terms)
    print(f"\n========== Score Term Analysis: {label} ==========")
    for name in TERM_NAMES:
        print(f"{name:<13} = {fmt(terms[name]):<9}  closer to 1 is better")
    print("----------------------------------------------\n")
    print(f"Setup sum = {fmt(setup)}")
    print(f"Hold sum  = {fmt(hold)}")
    print(f"Area term = {fmt(terms['AREA_term'])}")
    print(f"Current bottleneck = {bottleneck}")
    print(f"Optimization hint = prioritize {bottleneck}")
    print("=========================================================")


def print_summary(results):
    averages = {}
    for name in TERM_NAMES:
        values = [terms[name] for terms in results if terms[name] is not None]
        averages[name] = sum(values) / len(values) if values else None
    setup = add([averages["SS_TNS_term"], averages["SS_WNS_term"]])
    hold = add([averages["FF_TNS_term"], averages["FF_WNS_term"]])
    bottleneck = optimization_bottleneck(averages)
    print("\n========== Overall Score Term Summary ==========")
    for name in TERM_NAMES:
        print(f"average {name:<13} = {fmt(averages[name])}")
    print(f"average setup sum   = {fmt(setup)}")
    print(f"average hold sum    = {fmt(hold)}")
    print(f"current average bottleneck = {bottleneck}")
    print("================================================")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("outputs", nargs="+", help="checker output file(s), or - for stdin")
    parser.add_argument("--label", help="label used when one output is analyzed")
    args = parser.parse_args()
    results = []
    for output in args.outputs:
        text = sys.stdin.read() if output == "-" else Path(output).read_text(errors="replace")
        label = args.label if args.label and len(args.outputs) == 1 else Path(output).stem
        try:
            terms = compute_terms(parse_checker_output(text))
        except ValueError as exc:
            print(f"[score-term analysis] {label}: {exc}", file=sys.stderr)
            return 2
        print_analysis(label, terms)
        results.append(terms)
    print_summary(results)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
