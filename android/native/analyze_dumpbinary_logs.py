#!/usr/bin/env python3
"""
Parse fastbot1.log (before fix) and fastbot2.log (after fix) for dumpBinary lines,
output stats and 4/10-segment trends for comparison.
"""
import re
import sys
from pathlib import Path

def parse_log(path: Path):
    """Returns list of (ms, is_fail) in order."""
    data = []
    pat = re.compile(r'\[Fastbot\]:\s+// dumpBinary:\s+(\d+)\s+ms(\s+\(buffer full or fail\))?')
    text = path.read_text(errors='replace')
    for m in pat.finditer(text):
        ms = int(m.group(1))
        is_fail = m.group(2) is not None
        data.append((ms, is_fail))
    return data

def percentile(sorted_arr, p):
    if not sorted_arr:
        return 0
    k = (len(sorted_arr) - 1) * p / 100
    f = int(k)
    c = f + 1 if f + 1 < len(sorted_arr) else f
    return sorted_arr[f] + (k - f) * (sorted_arr[c] - sorted_arr[f]) if c > f else sorted_arr[f]

def stats(data, name):
    if not data:
        return None
    ms_list = [x[0] for x in data]
    fails = sum(1 for x in data if x[1])
    n = len(ms_list)
    s = sorted(ms_list)
    return {
        'name': name,
        'n': n,
        'min': min(ms_list),
        'max': max(ms_list),
        'mean': sum(ms_list) / n,
        'p50': percentile(s, 50),
        'p90': percentile(s, 90),
        'p95': percentile(s, 95),
        'p99': percentile(s, 99),
        'fails': fails,
        'fail_rate': 100.0 * fails / n if n else 0,
    }

def segment_avgs(data, num_segments):
    if not data:
        return []
    n = len(data)
    ms_list = [x[0] for x in data]
    fail_list = [x[1] for x in data]
    seg_size = n / num_segments
    result = []
    for i in range(num_segments):
        start = int(i * seg_size)
        end = int((i + 1) * seg_size) if i < num_segments - 1 else n
        if start >= end:
            continue
        seg_ms = ms_list[start:end]
        seg_fail = fail_list[start:end]
        avg_ms = sum(seg_ms) / len(seg_ms)
        fail_count = sum(seg_fail)
        result.append((i + 1, start + 1, end, len(seg_ms), avg_ms, fail_count, 100.0 * fail_count / len(seg_ms)))
    return result

def main():
    base = Path(__file__).resolve().parent
    log1 = base / "fastbot1.log"
    log2 = base / "fastbot2.log"
    if not log1.exists():
        print("Missing fastbot1.log", file=sys.stderr)
        sys.exit(1)
    if not log2.exists():
        print("Missing fastbot2.log", file=sys.stderr)
        sys.exit(2)

    d1 = parse_log(log1)
    d2 = parse_log(log2)
    s1 = stats(d1, "fastbot1 (修复前)")
    s2 = stats(d2, "fastbot2 (修复后)")

    print("=" * 60)
    print("dumpBinary 耗时对比：fastbot1.log (修复前) vs fastbot2.log (修复后)")
    print("=" * 60)
    print()
    print("一、总体统计")
    print("-" * 60)
    for s in (s1, s2):
        if s is None:
            continue
        print(f"  【{s['name']}】")
        print(f"    样本数: {s['n']}")
        print(f"    最小值: {s['min']} ms  最大值: {s['max']} ms")
        print(f"    平均值: {s['mean']:.1f} ms")
        print(f"    P50: {s['p50']:.0f} ms  P90: {s['p90']:.0f} ms  P95: {s['p95']:.0f} ms  P99: {s['p99']:.0f} ms")
        print(f"    失败数: {s['fails']}  失败率: {s['fail_rate']:.1f}%")
        print()
    print()
    print("二、对比摘要")
    print("-" * 60)
    if s1 and s2:
        print(f"  样本数:    fastbot1 {s1['n']}  →  fastbot2 {s2['n']}  (差异 {s2['n'] - s1['n']})")
        print(f"  平均值:    {s1['mean']:.1f} ms  →  {s2['mean']:.1f} ms  变化 {(s2['mean'] - s1['mean']) / s1['mean'] * 100:+.1f}%")
        print(f"  最大值:    {s1['max']} ms  →  {s2['max']} ms")
        print(f"  P99:       {s1['p99']:.0f} ms  →  {s2['p99']:.0f} ms")
        print(f"  失败率:    {s1['fail_rate']:.1f}%  →  {s2['fail_rate']:.1f}%")
    print()
    print("三、按步数顺序 4 段平均（越跑越慢趋势）")
    print("-" * 60)
    for name, data in [("fastbot1 (修复前)", d1), ("fastbot2 (修复后)", d2)]:
        print(f"  【{name}】")
        for seg in segment_avgs(data, 4):
            print(f"    段{seg[0]} (步 {seg[1]}-{seg[2]}): 条数={seg[3]}  平均={seg[4]:.0f} ms  失败={seg[5]} ({seg[6]:.1f}%)")
        print()
    print("四、按步数顺序 10 段平均")
    print("-" * 60)
    for name, data in [("fastbot1 (修复前)", d1), ("fastbot2 (修复后)", d2)]:
        print(f"  【{name}】")
        segs = segment_avgs(data, 10)
        line = "    " + "  ".join(f"段{i}:{segs[i-1][4]:.0f}ms" for i in range(1, 11) if i <= len(segs))
        print(line)
        print()
    return 0

if __name__ == "__main__":
    sys.exit(main())
