#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Q04 语料预登记/冻结工具（ASR-10 自动化评分与证据流程第 1 条的机械执行）。

规范依据：
- StickS3_OpenCode_测试用例与追踪矩阵.md §8「ASR-10自动化评分与证据流程」第 1 条：
  运行前冻结 15 条 fixture 清单、录音来源/哈希、人工参考文本、关键语义槽位
  （否定/动作对象/数字/文件路径）、可接受等价形式和音频环境；
  不在看到模型输出后改参考以让它通过。
- contracts/M3-ASR验收冻结清单.md §2 第 1 条（15 条语料清单：说话人授权、语言、
  技术词、时长、环境噪声、设备距离、人工参考文本）与第 7 条（fixture 持久化范围）。

本工具是模板与执行器：不包含、也不生成任何真实语料内容。
草稿清单（JSON，顶层仅 {"samples": [...]}）经校验后冻结为带内容哈希的清单文件；
冻结后任何内容改动都会被 verify 检出——这就是"不得看到模型输出后改参考"的机械执行。

哈希方案（有意选择：不做样本顺序无关的规范化）：
  content_hash = sha256( canonical_json(冻结内容) )，其中 canonical_json =
  json.dumps(obj, ensure_ascii=False, sort_keys=True, separators=(",", ":")) 的 UTF-8 字节；
  content_hash 字段本身不参与哈希。样本顺序参与哈希（清单顺序即清单语义的一部分），
  因此任何内容变更——改参考文本、改 wav 哈希、增/删/重排样本、改任一字段、
  加顶层键——都会改变规范化字节从而被 verify 检出。键序与空白排版差异不影响验证
  （哈希锚定内容而非文件排版），这是有意为之：verify 对同一内容的重复调用是幂等的。

冻结校验（任一不满足即拒绝冻结，并给出逐样本错误清单）：
- 恰好 15 条样本（REQUIRED_SAMPLES）；id 不得重复；
- 每条样本必填：
  * 非空白字符串：id / wav / language / reference_text / ambient_noise / audio_environment
  * 布尔 true：speaker_authorized（说话人授权）
  * 64 位小写十六进制：wav_sha256
  * 正数：duration_s / device_distance_m
  * 字符串数组（元素非空白，可为空列表）：technical_words / acceptable_equivalents
  * 对象 key_slots：必须含 negation / action_object / number / file_path 四个非空槽位
    （允许携带额外槽位；额外槽位同样被哈希覆盖）。

用法：
  python q04_corpus.py freeze <draft.json> <frozen_out.json>
  python q04_corpus.py verify <frozen.json>

退出码：0 = 冻结成功 / 验证一致；
        1 = 拒绝冻结（清单不合法或目标已存在）或验证不一致（清单疑似被改动）；
        2 = 用法 / 输入文件错误（文件不存在、JSON 解析失败、文件不是冻结清单结构）。
"""
import argparse
import copy
import hashlib
import json
import re
import sys
from datetime import datetime, timezone
from pathlib import Path

REQUIRED_SAMPLES = 15
SCHEMA = "q04-corpus-frozen"
SCHEMA_VERSION = 1
REQUIRED_TEXT_FIELDS = ("id", "wav", "language", "reference_text",
                        "ambient_noise", "audio_environment")
REQUIRED_NUMBER_FIELDS = ("duration_s", "device_distance_m")
REQUIRED_LIST_FIELDS = ("technical_words", "acceptable_equivalents")
REQUIRED_SLOT_KEYS = ("negation", "action_object", "number", "file_path")
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


class FreezeRefused(Exception):
    """冻结被拒绝（清单不合法或目标已存在）；errors 为逐条错误说明。"""

    def __init__(self, errors):
        super().__init__("; ".join(errors))
        self.errors = list(errors)


class VerifyFailed(Exception):
    """冻结清单内容哈希与存储值不一致（疑似被改动）。"""

    def __init__(self, detail):
        super().__init__(detail)
        self.detail = detail


def canonical_bytes(obj):
    """规范化序列化：sort_keys + 紧凑分隔符 + ensure_ascii=False 的 UTF-8 字节。"""
    return json.dumps(obj, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":")).encode("utf-8")


def content_hash(payload):
    """冻结内容的内容哈希；content_hash 字段本身不参与（若在则剔除）。"""
    payload = {k: v for k, v in payload.items() if k != "content_hash"}
    return hashlib.sha256(canonical_bytes(payload)).hexdigest()


def _is_blank(v):
    return isinstance(v, str) and not v.strip()


def _sample_tag(pos, s):
    if isinstance(s, dict):
        sid = s.get("id")
        if isinstance(sid, str) and sid.strip():
            return "samples[{}] (id={})".format(pos, sid)
    return "samples[{}]".format(pos)


def _validate_sample(pos, s, seen_ids):
    tag = _sample_tag(pos, s)
    errs = []
    if not isinstance(s, dict):
        return ["{}: 样本必须是 JSON 对象".format(tag)]
    for f in REQUIRED_TEXT_FIELDS:
        if not isinstance(s.get(f), str) or _is_blank(s.get(f)):
            errs.append("{}: 缺少必填文本字段 {}（须为非空白字符串）".format(tag, f))
    for f in REQUIRED_NUMBER_FIELDS:
        v = s.get(f)
        if isinstance(v, bool) or not isinstance(v, (int, float)) or v <= 0:
            errs.append("{}: 字段 {} 必须为正数".format(tag, f))
    for f in REQUIRED_LIST_FIELDS:
        v = s.get(f)
        if not isinstance(v, list) or any(
                not isinstance(x, str) or _is_blank(x) for x in v):
            errs.append("{}: 字段 {} 必须为字符串数组（元素非空白，可为空列表）".format(tag, f))
    if s.get("speaker_authorized") is not True:
        errs.append("{}: speaker_authorized 必须为布尔 true（说话人授权）".format(tag))
    if not isinstance(s.get("wav_sha256"), str) or not SHA256_RE.match(s["wav_sha256"]):
        errs.append("{}: wav_sha256 必须为 64 位小写十六进制".format(tag))
    slots = s.get("key_slots")
    if not isinstance(slots, dict):
        errs.append("{}: key_slots 必须为对象".format(tag))
    else:
        for k in REQUIRED_SLOT_KEYS:
            if not isinstance(slots.get(k), str) or _is_blank(slots.get(k)):
                errs.append("{}: key_slots.{} 缺失或为空".format(tag, k))
    sid = s.get("id")
    if isinstance(sid, str) and sid.strip():
        if sid in seen_ids:
            errs.append("{}: id 与 samples[{}] 重复: {}".format(
                tag, seen_ids[sid], sid))
        else:
            seen_ids[sid] = pos
    return errs


def validate_draft(draft):
    """校验草稿清单，返回错误字符串列表；空列表 = 可冻结。"""
    if not isinstance(draft, dict):
        return ["草稿顶层必须是 JSON 对象"]
    errors = []
    extra = sorted(set(draft) - {"samples"})
    if extra:
        errors.append("草稿顶层仅允许 samples，出现多余键: {}".format(", ".join(extra)))
    if "samples" not in draft:
        errors.append("草稿缺少 samples")
        return errors
    samples = draft["samples"]
    if not isinstance(samples, list):
        errors.append("samples 必须是数组")
        return errors
    if len(samples) != REQUIRED_SAMPLES:
        errors.append("样本数必须恰好 {} 条，实际 {} 条".format(
            REQUIRED_SAMPLES, len(samples)))
    seen_ids = {}
    for pos, s in enumerate(samples):
        errors.extend(_validate_sample(pos, s, seen_ids))
    return errors


def build_frozen(draft, frozen_at=None):
    """校验草稿并构造冻结清单（不含 content_hash）；不合法抛 FreezeRefused。"""
    errors = validate_draft(draft)
    if errors:
        raise FreezeRefused(errors)
    if frozen_at is None:
        frozen_at = datetime.now(timezone.utc).isoformat(timespec="seconds")
    return {
        "schema": SCHEMA,
        "schema_version": SCHEMA_VERSION,
        "frozen_at": frozen_at,
        "samples": copy.deepcopy(draft["samples"]),
    }


def freeze_manifest(draft, out_path, frozen_at=None):
    """校验草稿并写出冻结清单；目标已存在时拒绝覆盖（防篡改/幂等）。

    成功返回带 content_hash 的冻结清单 dict；被拒绝抛 FreezeRefused（不产生输出文件）。
    """
    frozen = build_frozen(draft, frozen_at=frozen_at)
    frozen["content_hash"] = content_hash(frozen)
    path = Path(out_path)
    if path.exists():
        raise FreezeRefused(["目标已存在，拒绝覆盖（防篡改/幂等）: {}".format(path)])
    try:
        fh = open(path, "x", encoding="utf-8")
    except FileExistsError:
        raise FreezeRefused(["目标已存在，拒绝覆盖（防篡改/幂等）: {}".format(path)])
    with fh:
        json.dump(frozen, fh, ensure_ascii=False, indent=2, sort_keys=True)
        fh.write("\n")
    return frozen


def load_frozen(path):
    """读取并做结构校验；不是冻结清单结构时抛 ValueError（CLI → 退出码 2）。"""
    try:
        text = Path(path).read_text(encoding="utf-8")
    except OSError as exc:
        raise ValueError("无法读取 {}: {}".format(path, exc))
    try:
        data = json.loads(text)
    except json.JSONDecodeError as exc:
        raise ValueError("JSON 解析失败 {}: {}".format(path, exc))
    if not isinstance(data, dict) or data.get("schema") != SCHEMA:
        raise ValueError("{} 不是冻结清单（schema 必须为 {}）".format(path, SCHEMA))
    if not isinstance(data.get("samples"), list):
        raise ValueError("{} 缺少 samples 数组".format(path))
    if not isinstance(data.get("content_hash"), str):
        raise ValueError("{} 缺少 content_hash".format(path))
    return data


def verify_manifest(path):
    """重算冻结清单内容哈希并与存储值比对。

    一致返回存储哈希；不一致（清单被改动）抛 VerifyFailed；
    文件缺失/解析失败/结构不是冻结清单抛 ValueError。
    """
    data = load_frozen(path)
    stored = data["content_hash"]
    recomputed = content_hash(data)
    if recomputed != stored:
        raise VerifyFailed(
            "内容哈希不一致：冻结清单已被改动。stored={} recomputed={}".format(
                stored, recomputed))
    return stored


def main(argv):
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(
        prog="q04_corpus.py",
        description="Q04 语料预登记/冻结工具（运行前冻结；不在看到模型输出后改参考）")
    sub = parser.add_subparsers(dest="cmd")
    p_freeze = sub.add_parser(
        "freeze", help="校验草稿清单并写出冻结清单（拒绝覆盖已存在的目标）")
    p_freeze.add_argument("draft", help="草稿清单 JSON（顶层仅 samples）")
    p_freeze.add_argument("out", help="冻结清单输出路径（不得已存在）")
    p_verify = sub.add_parser("verify", help="重算冻结清单内容哈希并比对存储值")
    p_verify.add_argument("frozen", help="冻结清单 JSON")
    args = parser.parse_args(argv[1:])
    if args.cmd is None:
        parser.print_usage(sys.stderr)
        return 2
    if args.cmd == "freeze":
        try:
            draft = json.loads(Path(args.draft).read_text(encoding="utf-8"))
        except OSError as exc:
            print("[FAIL] 无法读取草稿: {}".format(exc), file=sys.stderr)
            return 2
        except json.JSONDecodeError as exc:
            print("[FAIL] 草稿 JSON 解析失败: {}".format(exc), file=sys.stderr)
            return 2
        try:
            frozen = freeze_manifest(draft, args.out)
        except FreezeRefused as exc:
            print("[REFUSE] 拒绝冻结：", file=sys.stderr)
            for e in exc.errors:
                print("  - {}".format(e), file=sys.stderr)
            return 1
        print("[OK] 已冻结 {} 条样本 -> {} (content_hash={})".format(
            len(frozen["samples"]), args.out, frozen["content_hash"]))
        return 0
    try:
        verify_manifest(args.frozen)
    except VerifyFailed as exc:
        print("[FAIL] 冻结清单校验不通过：{}".format(exc.detail), file=sys.stderr)
        return 1
    except ValueError as exc:
        print("[FAIL] {}".format(exc), file=sys.stderr)
        return 2
    print("[OK] 冻结清单内容哈希一致（content_hash 校验通过）")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
