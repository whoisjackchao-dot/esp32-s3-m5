#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Q04D-2 适配器单测（先红后绿的回归护栏）。

覆盖：四键映射与哨兵跳过、未知键透传、缺失 id 报错（列全 id）、空假设
FAIL(EMPTY) 仍计分母、冻结防篡改（verify 先行）、15 条端到端（声明 object
槽位实际导致 FAIL）。依据：契约 §3.3/§5/§7 决策 #19（Q04D-2）。
运行：python3 tools/test_q04_adapter.py
"""
import hashlib
import io
import json
import os
import shutil
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

import q04_corpus
import q04_grader
import q04_adapter as qa

# 端到端固定句对：替换操作对象后否定/数字/路径兜底均无差异，
# FAIL 只能来自声明的 object 槽位（KEY_SLOT_OBJECT）——这正是接线证明。
OBJECT_REF = "请打开 main.py 查看 3 个配置"
WRONG_OBJECT_HYP = "请打开 readme.md 查看 3 个配置"


def make_sample(i):
    """合成第 i 条冻结样本（纯测试占位值，只写临时目录）。

    s00 特制 action_object="main.py" 供端到端接线证明；其余引用含
    "单元测试"与"3"（与槽位值一致）；negation/file_path 用哨兵"无"。
    """
    if i == 0:
        ref = OBJECT_REF
        slots = {"negation": "无", "action_object": "main.py",
                 "number": "3", "file_path": "无"}
    else:
        ref = "样本{:02d}：运行全部 3 个单元测试".format(i)
        slots = {"negation": "无", "action_object": "单元测试",
                 "number": "3", "file_path": "无"}
    return {
        "id": "s{:02d}".format(i),
        "wav": "captures/q04/s{:02d}.wav".format(i),
        "wav_sha256": hashlib.sha256(
            ("placeholder-wav-{:02d}".format(i)).encode("utf-8")).hexdigest(),
        "speaker_authorized": True,
        "language": "zh-CN",
        "technical_words": [],
        "duration_s": 5.0 + 0.1 * i,
        "ambient_noise": "安静室内",
        "device_distance_m": 0.5,
        "reference_text": ref,
        "key_slots": slots,
        "acceptable_equivalents": [],
        "audio_environment": "quiet-indoor",
    }


def make_draft():
    return {"samples": [make_sample(i) for i in range(15)]}


def dict_hypotheses(draft):
    """全对假设（id→引用原文）：全部样本应 PASS。"""
    return {s["id"]: s["reference_text"] for s in draft["samples"]}


class TempDirCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="q04adapter-")
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def _write_json(self, name, obj):
        path = os.path.join(self.tmp, name)
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(obj, fh, ensure_ascii=False, indent=2)
        return path

    def _freeze_corpus(self):
        """在临时目录冻结 15 条合成语料，返回冻结文件路径。"""
        frozen_path = os.path.join(self.tmp, "frozen.json")
        q04_corpus.freeze_manifest(make_draft(), frozen_path)
        return frozen_path

    def _good_hyp_path(self):
        return self._write_json("hyp.json", dict_hypotheses(make_draft()))


class TestSlotsFromKeySlots(unittest.TestCase):
    """key_slots → slots 映射与哨兵跳过（契约 §3.3、任务 Q04D-2 键名表）。"""

    def test_all_four_keys_mapped(self):
        self.assertEqual(
            qa.slots_from_key_slots({
                "negation": "不要删除", "action_object": "main.py",
                "number": "3", "file_path": "src/a/b.py"}),
            {"negation": "不要删除", "object": "main.py",
             "number": "3", "path": "src/a/b.py"})

    def test_sentinels_skipped_for_every_documented_form(self):
        for sentinel in ("无", "none", "-", "n/a", "na", "null",
                         "  ", "", "None", "NULL", "N/A", "无 "):
            self.assertEqual(
                qa.slots_from_key_slots({"action_object": sentinel}),
                {}, "哨兵 {!r} 必须跳过".format(sentinel))

    def test_sentinel_skipped_only_for_that_slot(self):
        self.assertEqual(
            qa.slots_from_key_slots({"negation": "无", "number": "3"}),
            {"number": "3"})

    def test_unknown_key_passes_through_with_own_name(self):
        self.assertEqual(
            qa.slots_from_key_slots({"voice_command": "重启设备"}),
            {"voice_command": "重启设备"})

    def test_unknown_key_with_sentinel_value_skipped(self):
        self.assertEqual(qa.slots_from_key_slots({"voice_command": "-"}), {})

    def test_all_sentinels_yield_empty_slots(self):
        self.assertEqual(
            qa.slots_from_key_slots(
                {"negation": "无", "action_object": "none", "number": "-",
                 "file_path": "n/a"}),
            {})


class TestBuildSamples(unittest.TestCase):
    """冻结样本 + 假设 → 评分器样本（缺失 id 报错；空假设保留）。"""

    def test_maps_frozen_fields_to_grader_sample(self):
        draft = make_draft()["samples"][:1]
        samples = qa.build_samples(draft, {"s00": OBJECT_REF})
        self.assertEqual(samples, [{
            "id": "s00", "reference": OBJECT_REF, "hypothesis": OBJECT_REF,
            "slots": {"object": "main.py", "number": "3"}}])

    def test_missing_ids_raise_listing_all_of_them(self):
        draft = make_draft()["samples"]
        with self.assertRaises(ValueError) as ctx:
            qa.build_samples(draft, {"s00": "x"})
        self.assertIn("s03", str(ctx.exception))
        self.assertIn("s14", str(ctx.exception))

    def test_empty_string_hypothesis_graded_fail_empty_and_still_counted(self):
        draft = make_draft()["samples"][:2]
        samples = qa.build_samples(
            draft, {"s00": "", "s01": "样本01：运行全部 3 个单元测试"})
        report = q04_grader.grade_corpus(samples)
        self.assertEqual(report.total, 2)
        self.assertIs(report.results[0].status, q04_grader.Status.FAIL)
        self.assertIn("EMPTY", report.results[0].reasons)


class TestLoadHypotheses(TempDirCase):
    """假设文件两种格式 → {id: 文本}；坏输入抛 ValueError（CLI → 退出码 2）。"""

    def test_dict_format_passthrough(self):
        path = self._write_json("hyp.json", {"s00": "文本"})
        self.assertEqual(qa.load_hypotheses(path), {"s00": "文本"})

    def test_list_format_converted_to_dict(self):
        path = self._write_json(
            "hyp.json", [{"id": "s00", "hypothesis": "甲"},
                         {"id": "s01", "hypothesis": "乙"}])
        self.assertEqual(qa.load_hypotheses(path),
                         {"s00": "甲", "s01": "乙"})

    def test_bad_json_raises_valueerror(self):
        path = os.path.join(self.tmp, "broken.json")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write("{不是JSON")
        with self.assertRaises(ValueError):
            qa.load_hypotheses(path)

    def test_list_item_missing_field_raises(self):
        path = self._write_json("hyp.json", [{"id": "s00"}])
        with self.assertRaises(ValueError):
            qa.load_hypotheses(path)


class TestRunQ04(TempDirCase):
    """run_q04：先 verify_manifest（篡改拒绝），再整卷评分。"""

    def test_missing_hypothesis_id_raises_listing_id(self):
        frozen = self._freeze_corpus()
        hyp = self._write_json("hyp.json", {"s00": "文本"})
        with self.assertRaises(ValueError) as ctx:
            qa.run_q04(frozen, hyp)
        self.assertIn("s01", str(ctx.exception))

    def test_tampered_frozen_refused_with_verifyfailed(self):
        frozen = self._freeze_corpus()
        with open(frozen, "r", encoding="utf-8") as fh:
            data = json.load(fh)
        data["samples"][2]["reference_text"] = "看到模型输出后改参考"
        with open(frozen, "w", encoding="utf-8") as fh:
            json.dump(data, fh, ensure_ascii=False, indent=2)
        with self.assertRaises(q04_corpus.VerifyFailed):
            qa.run_q04(frozen, self._write_json("hyp.json", {}))

    def test_verify_runs_before_hypotheses_load(self):
        """冻结清单被篡改时先抛 VerifyFailed（即使假设文件也不存在）。"""
        frozen = self._freeze_corpus()
        with open(frozen, "r", encoding="utf-8") as fh:
            data = json.load(fh)
        data["samples"][0]["key_slots"]["number"] = "999"
        with open(frozen, "w", encoding="utf-8") as fh:
            json.dump(data, fh, ensure_ascii=False, indent=2)
        with self.assertRaises(q04_corpus.VerifyFailed):
            qa.run_q04(frozen, os.path.join(self.tmp, "不存在.json"))


class TestMain(TempDirCase):
    """CLI 退出码：0 达标 / 1 不达标 / 2 输入或用法错误。"""

    def test_exit_0_when_all_pass(self):
        frozen = self._freeze_corpus()
        buf_out, buf_err = io.StringIO(), io.StringIO()
        with redirect_stdout(buf_out), redirect_stderr(buf_err):
            code = qa.main(["q04_adapter.py", frozen, self._good_hyp_path()])
        self.assertEqual(code, 0)
        self.assertIn("验收 PASS", buf_out.getvalue())

    def test_exit_1_when_gate_not_met(self):
        """门槛 = >=14/15：挂 2 条（错误对象 + 空假设）→ 13/15 不达标 → 退出码 1。"""
        frozen = self._freeze_corpus()
        hyp = dict_hypotheses(make_draft())
        hyp["s00"] = WRONG_OBJECT_HYP
        hyp["s01"] = ""
        buf_out, buf_err = io.StringIO(), io.StringIO()
        with redirect_stdout(buf_out), redirect_stderr(buf_err):
            code = qa.main(["q04_adapter.py", frozen,
                            self._write_json("hyp.json", hyp)])
        self.assertEqual(code, 1)
        self.assertIn("KEY_SLOT_OBJECT", buf_out.getvalue())

    def test_exit_2_on_missing_frozen_file(self):
        hyp = self._good_hyp_path()
        buf_out, buf_err = io.StringIO(), io.StringIO()
        with redirect_stdout(buf_out), redirect_stderr(buf_err):
            code = qa.main(["q04_adapter.py",
                            os.path.join(self.tmp, "没有.json"), hyp])
        self.assertEqual(code, 2)

    def test_exit_2_on_bad_hypotheses_json(self):
        frozen = self._freeze_corpus()
        path = os.path.join(self.tmp, "broken.json")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write("[坏JSON")
        buf_out, buf_err = io.StringIO(), io.StringIO()
        with redirect_stdout(buf_out), redirect_stderr(buf_err):
            code = qa.main(["q04_adapter.py", frozen, path])
        self.assertEqual(code, 2)

    def test_exit_2_on_missing_hypothesis_id(self):
        frozen = self._freeze_corpus()
        buf_out, buf_err = io.StringIO(), io.StringIO()
        with redirect_stdout(buf_out), redirect_stderr(buf_err):
            code = qa.main(["q04_adapter.py", frozen,
                            self._write_json("hyp.json", {"s00": "文本"})])
        self.assertEqual(code, 2)

    def test_exit_2_on_usage_missing_argument(self):
        frozen = self._freeze_corpus()
        buf_out, buf_err = io.StringIO(), io.StringIO()
        with redirect_stdout(buf_out), redirect_stderr(buf_err):
            self.assertEqual(qa.main(["q04_adapter.py", frozen]), 2)
            self.assertEqual(qa.main(["q04_adapter.py"]), 2)


class TestEndToEnd(TempDirCase):
    """15 条真实冻结语料端到端：总数 15；声明 object 槽位实际导致 FAIL。"""

    def test_15_samples_all_pass_exit_0(self):
        frozen = self._freeze_corpus()
        report = qa.run_q04(frozen, self._good_hyp_path())
        self.assertEqual(report.total, 15)
        self.assertEqual(report.pass_count, 15)
        self.assertTrue(report.overall_pass)

    def test_declared_object_slot_wires_through_and_fails(self):
        """错误操作对象假设 → FAIL(KEY_SLOT_OBJECT)；空假设 FAIL(EMPTY) 仍在分母。"""
        frozen = self._freeze_corpus()
        hyp = dict_hypotheses(make_draft())
        hyp["s00"] = WRONG_OBJECT_HYP
        hyp["s01"] = ""
        hyp_path = self._write_json("hypotheses.json",
                                    [{"id": k, "hypothesis": v}
                                     for k, v in hyp.items()])
        report = qa.run_q04(frozen, hyp_path)
        self.assertEqual(report.total, 15)
        object_verdict = report.results[0]
        self.assertIs(object_verdict.status, q04_grader.Status.FAIL)
        self.assertEqual(object_verdict.reasons, ("KEY_SLOT_OBJECT",))
        self.assertIs(report.results[1].status, q04_grader.Status.FAIL)
        self.assertIn("EMPTY", report.results[1].reasons)
        self.assertEqual(report.pass_count, 13)
        self.assertFalse(report.overall_pass)


if __name__ == "__main__":
    unittest.main(verbosity=2)
