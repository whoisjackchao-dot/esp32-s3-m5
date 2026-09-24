#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Q04D-2 适配器：冻结语料 key_slots → 评分器 slots（纯 stdlib，无第三方依赖）。

规范依据：contracts/M3-Q04准确率验收契约.md §3.3、§5、§7 决策 #19
（Q04D-2 = A 建适配器，2026-09-24 批准）。冻结工具（q04_corpus.py）逐条记录
key_slots = {"negation","action_object","number","file_path"}（自由文本），
而评分器（q04_grader.py）的 slots 参数期望 {名: 必须出现在假设中的期望子串}；
不建适配器时"声明槽位"（尤其操作对象）不生效，仅自动兜底生效。

键名映射（corpus key → grader slot 名）：
  negation → negation、action_object → object、number → number、
  file_path → path；其余键原样透传（用自己的名字）。
哨兵值（表示"无此槽位"，跳过不产出）：空白串，以及去空白后不区分大小写的
  "无"/"none"/"-"/"n/a"/"na"/"null"。

用法：
  python q04_adapter.py <frozen.json> <hypotheses.json>

假设文件接受 {"id": 文本} 或 [{"id":..., "hypothesis":...}, ...]；
空字符串假设合法（评分 FAIL(EMPTY)、保留分母，不剔除）。
退出码：0 = 达标；1 = 不达标；2 = 输入/用法错误（文件缺失、JSON 解析失败、
冻结清单被篡改 VerifyFailed、假设缺失 id）。
"""
import json
import sys
from pathlib import Path

import q04_corpus
import q04_grader

# corpus key_slots 键 → grader slots 键（对应原因码 KEY_SLOT_<名>）。
SLOT_KEY_MAP = {
    "negation": "negation",
    "action_object": "object",
    "number": "number",
    "file_path": "path",
}
# 表示"无此槽位"的哨兵值（trim + 小写比较；"无"不受大小写影响）。
SENTINEL_VALUES = frozenset({"无", "none", "-", "n/a", "na", "null"})


def _is_sentinel(value):
    if not isinstance(value, str):
        return False
    trimmed = value.strip()
    return not trimmed or trimmed.lower() in SENTINEL_VALUES


def slots_from_key_slots(key_slots):
    """冻结 key_slots → 评分器 slots；哨兵值跳过，未知键原样透传。"""
    slots = {}
    for key, value in key_slots.items():
        if _is_sentinel(value):
            continue
        slots[SLOT_KEY_MAP.get(key, key)] = value
    return slots


def load_hypotheses(path):
    """读取假设文件：{id: 文本} 或 [{"id":..., "hypothesis":...}, ...] → {id: 文本}。"""
    try:
        text = Path(path).read_text(encoding="utf-8")
    except OSError as exc:
        raise ValueError("无法读取 {}: {}".format(path, exc))
    try:
        data = json.loads(text)
    except json.JSONDecodeError as exc:
        raise ValueError("假设 JSON 解析失败 {}: {}".format(path, exc))
    if isinstance(data, dict):
        return data
    if isinstance(data, list):
        hyp = {}
        for pos, item in enumerate(data):
            if (not isinstance(item, dict) or "id" not in item
                    or "hypothesis" not in item):
                raise ValueError("假设 #{} 缺少 id/hypothesis 字段".format(pos))
            hyp[item["id"]] = item["hypothesis"]
        return hyp
    raise ValueError("假设文件须为 {{id: 文本}} 或 [{{id, hypothesis}}]: {}".format(
        path))


def build_samples(frozen_samples, hypotheses):
    """冻结样本 + 假设文本 → 评分器样本；任一 id 缺失抛 ValueError（列全缺失 id）。"""
    missing = [s["id"] for s in frozen_samples if s["id"] not in hypotheses]
    if missing:
        raise ValueError("假设文件缺少样本 id: {}".format(", ".join(missing)))
    return [{"id": s["id"],
             "reference": s["reference_text"],
             "hypothesis": hypotheses[s["id"]],
             "slots": slots_from_key_slots(s["key_slots"])}
            for s in frozen_samples]


def run_q04(frozen_path, hypotheses_path):
    """先校验冻结清单内容哈希（篡改抛 VerifyFailed），再加载假设并整卷评分。"""
    q04_corpus.verify_manifest(frozen_path)
    hypotheses = load_hypotheses(hypotheses_path)
    frozen = q04_corpus.load_frozen(frozen_path)
    return q04_grader.grade_corpus(build_samples(frozen["samples"], hypotheses))


def main(argv):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if hasattr(sys.stderr, "reconfigure"):
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    if len(argv) < 3:
        print("用法：python q04_adapter.py <frozen.json> <hypotheses.json>",
              file=sys.stderr)
        return 2
    try:
        report = run_q04(argv[1], argv[2])
    except (ValueError, q04_corpus.VerifyFailed) as exc:
        print("[FAIL] {}".format(exc), file=sys.stderr)
        return 2
    print(q04_grader.format_report(report))
    return 0 if report.overall_pass else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
