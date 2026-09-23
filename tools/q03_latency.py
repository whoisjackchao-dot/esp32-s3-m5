#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Q03 时延验收判定核心与报告（方案B；契约 contracts/M3-Q03时延测量契约.md）。

判据（Q03 已批准）：5/15/30s 各 20 次，每组 >=19 次"松键后 <=5s 成功填入"；
失败保留在分母；P50/P95 用 nearest-rank 附加报告。
输入：run record 列表（JSON），每条 {"group","latency_s"|null,"delta_ms","br04_status"}。
退出码：0 = 三组均达标；1 = 有组不达标；2 = 输入不合法。
"""
import json
import math
import sys

THRESHOLD_S = 5.0
EXPECTED_N = 20
MIN_PASS = 19
GROUPS = ("5s", "15s", "30s")

ANCHOR_EARLY = "BtnA 松开"
ANCHOR_HARDSTOP = "END recID="


def pick_anchor_ts(lines, group, t_begin_ms):
    """起点锚点：提前结束组取 'BtnA 松开'，30s 硬停组取 'END recID='。
    lines 为 (ts_ms, text) 升序序列；返回首个 ts>=t_begin_ms 的匹配 (ts_ms, text)，无则 None。"""
    key = ANCHOR_EARLY if group in ("5s", "15s") else ANCHOR_HARDSTOP
    for ts, text in lines:
        if ts >= t_begin_ms and key in text:
            return (ts, text)
    return None


def serial_latency_delta_ms(lines, t_ble_end_ms):
    """误差标定 Δ = 串口 END 行到达时刻 − BLE END 通知到达时刻（设备发射时刻相同）。"""
    for ts, text in lines:
        if ANCHOR_HARDSTOP in text:
            return ts - t_ble_end_ms
    return None


def build_record(group, index, t0_ms, t1_ms, br04_status, delta_ms=None,
                 end_frames=None, end_samples=None, summary_match=None):
    """latency_s 仅在 t0/t1 齐备且 BR-04 applied 时给出；否则 None（失败仍进分母）。"""
    ok = (t0_ms is not None and t1_ms is not None and br04_status == "applied"
          and summary_match is not False)
    latency_s = ((t1_ms - t0_ms) / 1000.0) if ok else None
    return {
        "group": group, "index": index,
        "latency_s": latency_s,
        "delta_ms": delta_ms,
        "br04_status": br04_status,
        "end_total_frames": end_frames,
        "end_samples": end_samples,
        "summary_match": summary_match,
        "ok": ok,
    }


def nearest_rank_percentile(values, p):
    """nearest-rank 百分位：升序排序后取第 ceil(p/100*n) 个（1-based）。空集返回 None。"""
    vals = sorted(v for v in values if v is not None)
    if not vals:
        return None
    rank = max(1, min(len(vals), int(math.ceil(p / 100.0 * len(vals)))))
    return vals[rank - 1]


def evaluate_group(latencies, threshold_s=THRESHOLD_S, expected_n=EXPECTED_N,
                   min_pass=MIN_PASS, deltas_s=None):
    """按 Q03 判据评估一组。latencies: 逐次点估计秒数或 None（失败）。"""
    n = len(latencies)
    complete = n >= expected_n
    successes = sum(1 for v in latencies if v is not None and v <= threshold_s)
    passed = complete and successes >= min_pass
    lats = [v for v in latencies if v is not None]
    grey = 0
    if deltas_s is not None:
        for v, d in zip(latencies, deltas_s):
            if v is not None and v <= threshold_s and v + (d or 0.0) > threshold_s:
                grey += 1
    return {
        "n": n, "complete": complete, "successes_le_threshold": successes,
        "threshold_s": threshold_s, "min_pass": min_pass,
        "rate": (successes / n) if n else 0.0,
        "p50_s": nearest_rank_percentile(lats, 50),
        "p95_s": nearest_rank_percentile(lats, 95),
        "failed_indices": [i for i, v in enumerate(latencies)
                           if v is None or v > threshold_s],
        "grey_zone": grey,
        "passed": passed,
    }


def evaluate(records, groups=GROUPS):
    """records: [{"group":str,"latency_s":float|None,"delta_ms":number?}]"""
    by_group = {g: [] for g in groups}
    deltas = {g: [] for g in groups}
    for r in records:
        g = r.get("group")
        if g not in by_group:
            raise ValueError("unknown group: {!r}".format(g))
        lat = r.get("latency_s")
        by_group[g].append(lat)
        deltas[g].append((r.get("delta_ms") or 0) / 1000.0)
    results = {}
    for g in groups:
        results[g] = evaluate_group(by_group[g], deltas_s=deltas[g])
    results["_all_passed"] = all(results[g]["passed"] for g in groups)
    results["_needs_review"] = any(results[g]["grey_zone"] > 0 for g in groups)
    return results


def format_report(results):
    lines = ["Q03 时延验收报告（阈值 {}s，每组 {}/{}）".format(
        THRESHOLD_S, MIN_PASS, EXPECTED_N), ""]
    lines.append("| 组 | 次数 | <=5s 成功 | 达标(>=19/20) | P50(s) | P95(s) | 灰区 | 失败序号 |")
    lines.append("| --- | --- | --- | --- | --- | --- | --- | --- |")
    for g in GROUPS:
        r = results[g]
        p50 = "{:.2f}".format(r["p50_s"]) if r["p50_s"] is not None else "-"
        p95 = "{:.2f}".format(r["p95_s"]) if r["p95_s"] is not None else "-"
        lines.append("| {} | {} | {} | {} | {} | {} | {} | {} |".format(
            g, r["n"], r["successes_le_threshold"],
            "PASS" if r["passed"] else "FAIL", p50, p95, r["grey_zone"],
            ",".join(str(i) for i in r["failed_indices"]) or "-"))
    lines.append("")
    lines.append("三组总体：{}".format("PASS" if results["_all_passed"] else "FAIL"))
    if results.get("_needs_review"):
        grey = sum(results[g]["grey_zone"] for g in GROUPS)
        lines.append("⚠ 灰区 {} 项：串口延迟不确定度可能致误判，需人工复核（不自动放行）".format(grey))
    return "\n".join(lines)


def load_records(path):
    """读取 run records：兼容 JSONL（q03_run 逐行追加）、JSON 数组、{"records":[...]}。"""
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read().strip()
    if not text:
        return []
    try:
        data = json.loads(text)
    except json.JSONDecodeError:
        return [json.loads(line) for line in text.splitlines() if line.strip()]
    if isinstance(data, dict):
        return data["records"] if "records" in data else [data]
    return data


def main(argv):
    if len(argv) < 2:
        print("用法：python q03_latency.py records.jsonl", file=sys.stderr)
        return 2
    records = load_records(argv[1])
    try:
        results = evaluate(records)
    except ValueError as exc:
        print("[FAIL] {}".format(exc), file=sys.stderr)
        return 2
    print(format_report(results))
    return 0 if results["_all_passed"] else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
