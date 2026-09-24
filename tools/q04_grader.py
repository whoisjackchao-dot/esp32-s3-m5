#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Q04 ASR 准确率评分核心与报告（纯 stdlib、确定性规则，无 LLM/网络）。
依据：StickS3_OpenCode_测试用例与追踪矩阵.md §ASR-10、
contracts/M3-ASR验收冻结清单.md §1（准确率门槛行，Q04 2026-09-15 已批准）。
已批准判据：固定 15 条编程口述，至少 14 条"无需修改或 <=2 处局部修正且不重写整句"；
同一词或连续片段的一次修正计 1 处；标点/空格单独计数、不占额度；
否定反转/操作对象/关键数字/文件路径错误直接不合格（即使只错 1 处）；
归一化不得删除路径分隔符、小数点、负号、版本点或代码标识（"1.5"≠"15"、"a/b"≠"ab"）；
大段 diff/整句替换不算 1 处轻微修正：整句 → FAIL(REWRITE)，大段未定 → NEEDS_REVIEW(LARGE_DIFF)；
歧义标 NEEDS_REVIEW 不自动放行、不计 PASS；失败/空输出记不合格、仍在分母。
API：grade_sample(reference, hypothesis, slots=None) -> Verdict；
     grade_corpus(samples) -> Report（overall_pass 当且仅当 pass_count >= 14/15）。
slots：名 → 必须出现在假设中的期望子串（值必须 str）→ 原因码 KEY_SLOT_<名>；
未声明时自动兜底：否定词增减、数字序列、含路径分隔符词元。
CLI：python q04_grader.py samples.json（JSON 数组 / {"samples":[...]} / JSONL）；
退出码：0 = 达标；1 = 不达标；2 = 输入不合法。
"""
import difflib
import json
import re
import sys
import unicodedata
from dataclasses import dataclass
from enum import Enum

REQUIRED_SAMPLES = 15
MIN_PASS = 14
MAX_LOCAL_CORRECTIONS = 2
# 相似度低于该值、或整体改动占比超过该值，且差异由大块连续替换主导 → 整句重写。
REWRITE_SIMILARITY = 0.5
REWRITE_CHANGED_FRACTION = 0.5
# 单个差异块占句长比例超过该值视为"大块"（重写判定需大块主导）。
REVIEW_BLOCK_FRACTION = 0.25
# 单个差异块跨度（词元数，两侧取大）达到该值即"大段 diff"，标 NEEDS_REVIEW。
LARGE_DIFF_SPAN = 4
# 否定词元：出现集合在引用/假设间变化 → 疑似否定反转（从严，宁误报不漏放）。
NEGATION_CHARS = "不没别无勿莫非未"

_CODE_START = frozenset(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")
# code=路径/数字/代码标识（保护 . / \ : + = - 与版本点）；cjk=单个汉字；其余单字符=punct。
_TOKEN_RE = re.compile(
    r"[A-Za-z0-9_][A-Za-z0-9_./\\:+=-]*"
    r"|[\u3400-\u9fff]"
    r"|.",
    re.DOTALL)
_NUMERIC_RE = re.compile(r"-?\d+(?:\.\d+)*")


class Status(Enum):
    """样本级判定：PASS / FAIL / NEEDS_REVIEW（复核不计 PASS）。"""

    PASS = "PASS"
    FAIL = "FAIL"
    NEEDS_REVIEW = "NEEDS_REVIEW"


def normalize(text):
    """NFKC 归一化：全角数字/标点折叠为半角；不改汉字；不删任何分隔符或代码标识。"""
    return unicodedata.normalize("NFKC", text)


def tokenize(text):
    """切成 (kind, value) 序列：kind ∈ {code, cjk, punct}。"""
    out = []
    for m in _TOKEN_RE.finditer(text):
        tok = m.group(0)
        if tok[0] in _CODE_START:
            kind = "code"
        elif "\u3400" <= tok[0] <= "\u9fff":
            kind = "cjk"
        elif tok[0].isspace():
            continue
        else:
            kind = "punct"
        out.append((kind, tok))
    return out


def content_tokens(text):
    """参与对齐与相似度计算的内容词元：代码/路径/数字词元 + 单个汉字。"""
    return [tok for kind, tok in tokenize(text) if kind in ("code", "cjk")]


def punct_stream(text):
    """标点流（与内容对齐分离，仅用于单独计数的差异报告）。"""
    return [tok for kind, tok in tokenize(text) if kind == "punct"]


def ws_pattern(text):
    """空白模式：每个连续空白段折叠为 '#'（宽度归一化，位置差异单独计数）。"""
    return ["#" for _ in re.findall(r"\s+", text)]


def numeric_tokens(text):
    """数字词元（含小数点与负号）：序列不同 → 关键数字错误。"""
    return _NUMERIC_RE.findall(text)


def path_tokens(text):
    """含路径分隔符的代码词元：序列不同 → 文件路径错误。"""
    return [tok for kind, tok in tokenize(text)
            if kind == "code" and ("/" in tok or "\\" in tok)]


def negation_counts(text):
    """否定词元出现计数（如 {"不": 1}）；引用/假设不一致 → 疑似否定反转。"""
    return {ch: text.count(ch) for ch in NEGATION_CHARS if ch in text}


def key_violations(ref_text, hyp_text, slots):
    """关键槽位违规（有序去重）：声明槽位子串检查 + 否定/数字/路径自动兜底。"""
    codes = []
    for name, expected in (slots or {}).items():
        if not isinstance(expected, str):
            raise ValueError("slot {!r} 的期望值必须是 str".format(name))
        if normalize(expected) not in hyp_text:
            codes.append("KEY_SLOT_" + name.upper())
    if negation_counts(ref_text) != negation_counts(hyp_text):
        codes.append("KEY_SLOT_NEGATION")
    if numeric_tokens(ref_text) != numeric_tokens(hyp_text):
        codes.append("KEY_SLOT_NUMBER")
    if path_tokens(ref_text) != path_tokens(hyp_text):
        codes.append("KEY_SLOT_PATH")
    return list(dict.fromkeys(codes))


def _diff_blocks(a, b):
    """两个序列的差异块数（非 equal opcode 数）。"""
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    return sum(1 for op in sm.get_opcodes() if op[0] != "equal")


@dataclass(frozen=True)
class Verdict:
    """单条样本判定结果。

    reasons 为机器可读原因码：EMPTY / KEY_SLOT_<NAME> / REWRITE /
    TOO_MANY_CORRECTIONS / REORDER / LARGE_DIFF / PUNCT_SPACE_DIFF。
    数值计数：local_corrections=局部修正处数；is_rewrite=0/1；
    key_slot_violations=关键槽位违规数；punct_space_diff=标点/空格差异块数
    （单独计数，永不影响 status）。similarity/changed_fraction 为对齐指标。
    """
    status: Status
    reasons: tuple
    local_corrections: int
    is_rewrite: int
    key_slot_violations: int
    punct_space_diff: int
    similarity: float
    changed_fraction: float


def grade_sample(reference, hypothesis, slots=None):
    """按已批准 Q04 规则判定单条样本（规则详见模块 docstring）。"""
    if slots is not None and not isinstance(slots, dict):
        raise ValueError("slots 必须是 dict 或 None")
    ref_text = normalize(reference or "")
    hyp_text = normalize(hypothesis) if hypothesis else ""
    if not hyp_text.strip():
        return Verdict(Status.FAIL, ("EMPTY",), 0, 0, 0, 0, 0.0, 0.0)
    violations = key_violations(ref_text, hyp_text, slots)
    ref_tok = content_tokens(ref_text)
    hyp_tok = content_tokens(hyp_text)
    sm = difflib.SequenceMatcher(None, ref_tok, hyp_tok, autojunk=False)
    blocks = [op for op in sm.get_opcodes() if op[0] != "equal"]
    local_corrections = len(blocks)
    similarity = sm.ratio()
    ref_len = max(len(ref_tok), 1)
    hyp_len = max(len(hyp_tok), 1)
    changed = sum(op[2] - op[1] for op in blocks) + sum(op[4] - op[3] for op in blocks)
    changed_fraction = changed / (len(ref_tok) + len(hyp_tok)) if changed else 0.0
    span_fracs = [max((op[2] - op[1]) / ref_len, (op[4] - op[3]) / hyp_len)
                  for op in blocks]
    max_span = max((max(op[2] - op[1], op[4] - op[3]) for op in blocks), default=0)
    # 整句重写：总体差异大 且 差异由大块连续替换主导（分散小修正不算重写）。
    bulk_dominated = any(f > REVIEW_BLOCK_FRACTION for f in span_fracs)
    is_rewrite = int((similarity < REWRITE_SIMILARITY
                      or changed_fraction > REWRITE_CHANGED_FRACTION)
                     and bulk_dominated)
    if violations:
        status, reasons = Status.FAIL, tuple(violations)
    elif is_rewrite:
        status, reasons = Status.FAIL, ("REWRITE",)
    elif local_corrections > MAX_LOCAL_CORRECTIONS:
        status, reasons = Status.FAIL, ("TOO_MANY_CORRECTIONS",)
    else:
        review = []
        if blocks and sorted(ref_tok) == sorted(hyp_tok):
            review.append("REORDER")
        if max_span >= LARGE_DIFF_SPAN:
            review.append("LARGE_DIFF")
        if review:
            status, reasons = Status.NEEDS_REVIEW, tuple(review)
        else:
            status, reasons = Status.PASS, ()
    pdiff = (_diff_blocks(punct_stream(ref_text), punct_stream(hyp_text))
             + _diff_blocks(ws_pattern(ref_text), ws_pattern(hyp_text)))
    if pdiff:
        reasons = reasons + ("PUNCT_SPACE_DIFF",)
    return Verdict(status, reasons, local_corrections, is_rewrite,
                   len(violations), pdiff, similarity, changed_fraction)


@dataclass(frozen=True)
class Report:
    """整卷判定：逐样本 Verdict 列表 + 汇总。overall_pass 当且仅当 pass_count >= 14/15。"""
    results: tuple

    @property
    def total(self):
        return len(self.results)

    @property
    def pass_count(self):
        return sum(1 for v in self.results if v.status is Status.PASS)

    @property
    def fail_count(self):
        return sum(1 for v in self.results if v.status is Status.FAIL)

    @property
    def review_count(self):
        return sum(1 for v in self.results if v.status is Status.NEEDS_REVIEW)

    @property
    def overall_pass(self):
        return self.pass_count >= MIN_PASS


def grade_corpus(samples):
    """对样本列表逐条评分。样本为 {"reference": str, "hypothesis": str, "slots": dict?}。"""
    if not isinstance(samples, list):
        raise ValueError("samples 必须是 list")
    results = []
    for pos, sample in enumerate(samples):
        if not isinstance(sample, dict):
            raise ValueError("样本 #{} 不是 dict".format(pos))
        missing = [k for k in ("reference", "hypothesis") if k not in sample]
        if missing:
            raise ValueError("样本 #{} 缺少字段: {}".format(pos, ",".join(missing)))
        results.append(grade_sample(sample["reference"], sample["hypothesis"],
                                    sample.get("slots")))
    return Report(tuple(results))


def format_report(report):
    """渲染中文 Markdown 报告（表逐样本 + 汇总 + 复核/分母警告）。"""
    lines = ["Q04 ASR 准确率验收报告（固定 {} 条编程口述；合格 = 无需修改或 <={} 处"
             "局部修正且不重写整句；门槛 >={} 条）".format(
                 REQUIRED_SAMPLES, MAX_LOCAL_CORRECTIONS, MIN_PASS), ""]
    lines.append("| # | 状态 | 修正数 | 关键槽位违规 | 重写 | 相似度 | 原因 |")
    lines.append("| --- | --- | --- | --- | --- | --- | --- |")
    for i, v in enumerate(report.results, 1):
        lines.append("| {} | {} | {} | {} | {} | {:.2f} | {} |".format(
            i, v.status.name, v.local_corrections, v.key_slot_violations,
            v.is_rewrite, v.similarity, ",".join(v.reasons) or "-"))
    verdict = "PASS" if report.overall_pass else "FAIL"
    lines.append("")
    lines.append("合格 {}/{}（固定分母 {}；NEEDS_REVIEW {} 条不计 PASS）→ 验收 {}".format(
        report.pass_count, report.total, REQUIRED_SAMPLES, report.review_count,
        verdict))
    if report.review_count:
        lines.append("⚠ {} 条需人工复核（歧义不自动放行）".format(report.review_count))
    if report.total != REQUIRED_SAMPLES:
        lines.append("⚠ 实际输入 {} 条 ≠ 固定分母 {}，不能作为 Q04 验收依据".format(
            report.total, REQUIRED_SAMPLES))
    return "\n".join(lines)


def load_samples(path):
    """读取样本文件：JSON 数组、{"samples":[...]}、单对象或 JSONL（逐行 JSON）。"""
    with open(path, "r", encoding="utf-8") as fh:
        text = fh.read().strip()
    if not text:
        return []
    try:
        data = json.loads(text)
    except json.JSONDecodeError:
        return [json.loads(line) for line in text.splitlines() if line.strip()]
    if isinstance(data, dict):
        return data["samples"] if "samples" in data else [data]
    return data


def main(argv):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    if len(argv) < 2:
        print("用法：python q04_grader.py samples.json", file=sys.stderr)
        return 2
    try:
        report = grade_corpus(load_samples(argv[1]))
    except (ValueError, OSError) as exc:
        print("[FAIL] {}".format(exc), file=sys.stderr)
        return 2
    print(format_report(report))
    return 0 if report.overall_pass else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
