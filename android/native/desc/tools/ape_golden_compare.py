#!/usr/bin/env python3
import argparse
import json
import re
import sys
from collections import defaultdict


KEY_RE = re.compile(
    r"ape-key: activityRaw=(?P<raw>\S*) activityKey=(?P<key>\S*) "
    r"stateHash=(?P<state>\d+) stateKeyHash=(?P<keyhash>\d+) graphSize=(?P<graph>\d+) names=(?P<names>\d+)"
)
SAMPLE_RE = re.compile(r"ape-key: sample=(?P<sample>.*)$")
REFINE_RE = re.compile(r"ape naming: refine activity=(?P<activity>\S+)")


def parse_log(path: str):
    rows = []
    refines = defaultdict(int)
    pending = None
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            m = KEY_RE.search(line)
            if m:
                pending = {
                    "activityRaw": m.group("raw"),
                    "activityKey": m.group("key"),
                    "stateHash": int(m.group("state")),
                    "stateKeyHash": int(m.group("keyhash")),
                    "graphSize": int(m.group("graph")),
                    "names": int(m.group("names")),
                    "sample": "",
                }
                rows.append(pending)
                continue
            m = SAMPLE_RE.search(line)
            if m and pending is not None:
                pending["sample"] = m.group("sample")
                continue
            m = REFINE_RE.search(line)
            if m:
                refines[m.group("activity")] += 1
    return rows, refines


def summarize(rows, refines):
    out = {
        "stepCount": len(rows),
        "activityKeyCount": len({r["activityKey"] for r in rows}),
        "stateKeyHashCount": len({r["stateKeyHash"] for r in rows}),
        "avgNames": (sum(r["names"] for r in rows) / len(rows)) if rows else 0.0,
        "refineCountByActivity": dict(sorted(refines.items())),
    }
    return out


def diff(native, java):
    d = {}
    for k in ("stepCount", "activityKeyCount", "stateKeyHashCount"):
        d[k] = {"native": native[k], "java": java[k], "delta": native[k] - java[k]}
    d["avgNames"] = {
        "native": round(native["avgNames"], 4),
        "java": round(java["avgNames"], 4),
        "delta": round(native["avgNames"] - java["avgNames"], 4),
    }
    acts = sorted(set(native["refineCountByActivity"]) | set(java["refineCountByActivity"]))
    d["refineCountByActivity"] = {
        a: {
            "native": native["refineCountByActivity"].get(a, 0),
            "java": java["refineCountByActivity"].get(a, 0),
            "delta": native["refineCountByActivity"].get(a, 0)
            - java["refineCountByActivity"].get(a, 0),
        }
        for a in acts
    }
    return d


def main():
    ap = argparse.ArgumentParser(description="Compare APE golden logs (native vs java baseline).")
    ap.add_argument("--native-log", required=True)
    ap.add_argument("--java-log", required=True)
    ap.add_argument("--json-out", default="")
    ap.add_argument("--max-stepcount-delta", type=int, default=-1)
    ap.add_argument("--max-activitykeycount-delta", type=int, default=-1)
    ap.add_argument("--max-statekeyhashcount-delta", type=int, default=-1)
    ap.add_argument("--max-avg-names-delta", type=float, default=-1.0)
    ap.add_argument("--fail-on-refine-activity-delta", action="store_true")
    args = ap.parse_args()

    n_rows, n_ref = parse_log(args.native_log)
    j_rows, j_ref = parse_log(args.java_log)
    n_sum = summarize(n_rows, n_ref)
    j_sum = summarize(j_rows, j_ref)
    out = {
        "native": n_sum,
        "java": j_sum,
        "diff": diff(n_sum, j_sum),
    }
    checks = []
    failed = False

    def add_check(name: str, delta_value, max_allowed):
        nonlocal failed
        if max_allowed < 0:
            checks.append(
                {
                    "name": name,
                    "enabled": False,
                    "delta": delta_value,
                    "maxAllowed": max_allowed,
                    "pass": True,
                }
            )
            return
        passed = abs(delta_value) <= max_allowed
        if not passed:
            failed = True
        checks.append(
            {
                "name": name,
                "enabled": True,
                "delta": delta_value,
                "maxAllowed": max_allowed,
                "pass": passed,
            }
        )

    add_check("stepCount", out["diff"]["stepCount"]["delta"], args.max_stepcount_delta)
    add_check(
        "activityKeyCount",
        out["diff"]["activityKeyCount"]["delta"],
        args.max_activitykeycount_delta,
    )
    add_check(
        "stateKeyHashCount",
        out["diff"]["stateKeyHashCount"]["delta"],
        args.max_statekeyhashcount_delta,
    )
    add_check("avgNames", out["diff"]["avgNames"]["delta"], args.max_avg_names_delta)

    refine_activity_failures = []
    if args.fail_on_refine_activity_delta:
        for act, item in out["diff"]["refineCountByActivity"].items():
            if item["delta"] != 0:
                failed = True
                refine_activity_failures.append(
                    {
                        "activity": act,
                        "native": item["native"],
                        "java": item["java"],
                        "delta": item["delta"],
                    }
                )
    out["gate"] = {
        "checks": checks,
        "failOnRefineActivityDelta": args.fail_on_refine_activity_delta,
        "refineActivityFailures": refine_activity_failures,
        "passed": not failed,
    }
    text = json.dumps(out, ensure_ascii=False, indent=2)
    print(text)
    if args.json_out:
        with open(args.json_out, "w", encoding="utf-8") as f:
            f.write(text + "\n")
    if failed:
        sys.exit(2)


if __name__ == "__main__":
    main()

