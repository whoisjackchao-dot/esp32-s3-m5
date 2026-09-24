#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Q04 语料冻结工具单测（先红后绿的回归护栏）。

覆盖：freeze+verify 正常路径、各类篡改检出（改参考文本/改wav哈希/增删/重排样本）、
样本数 !=15 拒绝冻结、缺必填字段拒绝冻结（逐样本错误清单）、拒绝覆盖已有冻结清单。
运行：python3 tools/test_q04_corpus.py
"""
import copy
import hashlib
import io
import json
import os
import shutil
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

import q04_corpus as q


def make_sample(i, **over):
    """合成第 i 条样本（纯测试占位值，非真实语料；只写临时目录）。"""
    s = {
        "id": "q4-{:02d}".format(i),
        "wav": "captures/q04/q4-{:02d}.wav".format(i),
        "wav_sha256": hashlib.sha256(
            ("placeholder-wav-{:02d}".format(i)).encode("utf-8")).hexdigest(),
        "speaker_authorized": True,
        "language": "zh-CN",
        "technical_words": ["热词{}".format(i)] if i % 2 else [],
        "duration_s": 5.0 + 0.1 * i,
        "ambient_noise": "安静室内",
        "device_distance_m": 0.5,
        "reference_text": "参考文本{}".format(i),
        "key_slots": {
            "negation": "无",
            "action_object": "对象{}".format(i),
            "number": "数字{}".format(i),
            "file_path": "路径{}".format(i),
        },
        "acceptable_equivalents": ["等价形式{}".format(i)],
        "audio_environment": "quiet-indoor",
    }
    s.update(over)
    return s


def make_draft(n=15):
    return {"samples": [make_sample(i + 1) for i in range(n)]}


class TempDirCase(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="q04test-")
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def _write_json(self, name, obj):
        path = os.path.join(self.tmp, name)
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(obj, fh, ensure_ascii=False)
        return path

    def _load(self, path):
        with open(path, "r", encoding="utf-8") as fh:
            return json.load(fh)

    def _dump(self, path, obj):
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(obj, fh, ensure_ascii=False, indent=2, sort_keys=True)

    def _freeze_ok(self, draft=None, name="frozen.json"):
        draft = make_draft() if draft is None else draft
        draft_path = self._write_json("draft.json", draft)
        out_path = os.path.join(self.tmp, name)
        rc = q.main(["q04_corpus.py", "freeze", draft_path, out_path])
        self.assertEqual(rc, 0)
        return out_path


class TestCanonicalHash(unittest.TestCase):
    def test_sorted_keys_deterministic(self):
        a = {"b": 1, "a": {"y": 2, "x": 3}}
        b = {"a": {"x": 3, "y": 2}, "b": 1}
        self.assertEqual(q.content_hash(a), q.content_hash(b))

    def test_content_hash_field_excluded(self):
        m1 = {"samples": [1, 2], "content_hash": "old"}
        m2 = {"samples": [1, 2], "content_hash": "new"}
        self.assertEqual(q.content_hash(m1), q.content_hash(m2))

    def test_any_content_change_changes_hash(self):
        base = {"samples": [{"id": "a", "reference_text": "x"}]}
        other = {"samples": [{"id": "a", "reference_text": "y"}]}
        self.assertNotEqual(q.content_hash(base), q.content_hash(other))

    def test_sample_order_participates_in_hash(self):
        s1 = {"id": "a"}
        s2 = {"id": "b"}
        self.assertNotEqual(q.content_hash({"samples": [s1, s2]}),
                            q.content_hash({"samples": [s2, s1]}))


class TestValidateDraft(TempDirCase):
    def _errors(self, draft):
        return q.validate_draft(draft)

    def test_valid_15_no_errors(self):
        self.assertEqual(self._errors(make_draft()), [])

    def test_14_samples_refused(self):
        errs = self._errors(make_draft(14))
        self.assertTrue(errs)
        self.assertIn("15", errs[0])

    def test_16_samples_refused(self):
        errs = self._errors(make_draft(16))
        self.assertTrue(errs)
        self.assertIn("15", errs[0])

    def test_missing_reference_text(self):
        draft = make_draft()
        del draft["samples"][4]["reference_text"]
        errs = self._errors(draft)
        self.assertTrue(any("reference_text" in e for e in errs))

    def test_blank_reference_text(self):
        draft = make_draft()
        draft["samples"][0]["reference_text"] = "   "
        errs = self._errors(draft)
        self.assertTrue(any("reference_text" in e for e in errs))

    def test_missing_wav_sha256(self):
        draft = make_draft()
        del draft["samples"][2]["wav_sha256"]
        self.assertTrue(any("wav_sha256" in e for e in self._errors(draft)))

    def test_wav_sha256_bad_format(self):
        draft = make_draft()
        draft["samples"][2]["wav_sha256"] = "abc123"
        self.assertTrue(any("wav_sha256" in e for e in self._errors(draft)))

    def test_speaker_authorized_false_refused(self):
        draft = make_draft()
        draft["samples"][7]["speaker_authorized"] = False
        errs = self._errors(draft)
        self.assertTrue(any("speaker_authorized" in e for e in errs))

    def test_speaker_authorized_missing_refused(self):
        draft = make_draft()
        del draft["samples"][7]["speaker_authorized"]
        self.assertTrue(any("speaker_authorized" in e for e in self._errors(draft)))

    def test_speaker_authorized_string_true_refused(self):
        draft = make_draft()
        draft["samples"][7]["speaker_authorized"] = "true"
        self.assertTrue(any("speaker_authorized" in e for e in self._errors(draft)))

    def test_key_slots_missing_refused(self):
        draft = make_draft()
        del draft["samples"][0]["key_slots"]
        self.assertTrue(any("key_slots" in e for e in self._errors(draft)))

    def test_key_slots_missing_slot_key_refused(self):
        draft = make_draft()
        del draft["samples"][0]["key_slots"]["negation"]
        errs = self._errors(draft)
        self.assertTrue(any("negation" in e for e in errs))

    def test_key_slots_empty_value_refused(self):
        draft = make_draft()
        draft["samples"][0]["key_slots"]["number"] = ""
        self.assertTrue(any("number" in e for e in self._errors(draft)))

    def test_duplicate_ids_refused(self):
        draft = make_draft()
        draft["samples"][9]["id"] = draft["samples"][0]["id"]
        errs = self._errors(draft)
        self.assertTrue(any("重复" in e for e in errs))

    def test_unknown_top_level_key_refused(self):
        draft = make_draft()
        draft["notes"] = "x"
        self.assertTrue(self._errors(draft))

    def test_missing_samples_key_refused(self):
        self.assertTrue(self._errors({}))

    def test_error_names_sample_and_field(self):
        draft = make_draft()
        del draft["samples"][11]["ambient_noise"]
        errs = self._errors(draft)
        self.assertEqual(len(errs), 1)
        self.assertIn("ambient_noise", errs[0])
        self.assertIn("q4-12", errs[0])

    def test_errors_aggregate_across_samples(self):
        draft = make_draft()
        del draft["samples"][0]["reference_text"]
        draft["samples"][1]["speaker_authorized"] = False
        errs = self._errors(draft)
        self.assertEqual(len(errs), 2)
        joined = "\n".join(errs)
        self.assertIn("q4-01", joined)
        self.assertIn("q4-02", joined)


class TestFreeze(TempDirCase):
    def test_happy_freeze_writes_frozen(self):
        out = self._freeze_ok()
        frozen = self._load(out)
        self.assertEqual(frozen["schema"], q.SCHEMA)
        self.assertEqual(len(frozen["samples"]), 15)
        self.assertTrue(frozen["frozen_at"])
        self.assertRegex(frozen["content_hash"], r"^[0-9a-f]{64}$")
        self.assertEqual(frozen["samples"][0]["id"], "q4-01")
        self.assertEqual(frozen["samples"][0]["reference_text"], "参考文本1")
        self.assertEqual(frozen["samples"][0]["key_slots"]["file_path"], "路径1")

    def test_frozen_hash_matches_stored_content(self):
        out = self._freeze_ok()
        frozen = self._load(out)
        payload = {k: v for k, v in frozen.items() if k != "content_hash"}
        self.assertEqual(q.content_hash(payload), frozen["content_hash"])

    def test_refuses_overwrite_and_keeps_original_bytes(self):
        out = self._freeze_ok()
        with open(out, "rb") as fh:
            before = fh.read()
        with self.assertRaises(q.FreezeRefused):
            q.freeze_manifest(make_draft(), out)
        with open(out, "rb") as fh:
            self.assertEqual(fh.read(), before)

    def test_validation_failure_writes_nothing(self):
        draft_path = self._write_json("draft.json", make_draft(14))
        out_path = os.path.join(self.tmp, "frozen.json")
        rc = q.main(["q04_corpus.py", "freeze", draft_path, out_path])
        self.assertEqual(rc, 1)
        self.assertFalse(os.path.exists(out_path))

    def test_frozen_at_explicit_is_used(self):
        frozen = q.freeze_manifest(make_draft(), os.path.join(self.tmp, "f.json"),
                                   frozen_at="2026-09-24T00:00:00+00:00")
        self.assertEqual(frozen["frozen_at"], "2026-09-24T00:00:00+00:00")


class TestVerify(TempDirCase):
    def _tampered(self, mutate):
        out = self._freeze_ok()
        frozen = self._load(out)
        mutate(frozen)
        self._dump(out, frozen)
        return out

    def test_happy_verify(self):
        out = self._freeze_ok()
        frozen = self._load(out)
        self.assertEqual(q.verify_manifest(out), frozen["content_hash"])

    def test_reference_text_edit_detected(self):
        out = self._tampered(lambda f: f["samples"][3].update(
            reference_text="看输出后改的参考"))
        with self.assertRaises(q.VerifyFailed):
            q.verify_manifest(out)

    def test_wav_hash_change_detected(self):
        out = self._tampered(lambda f: f["samples"][3].update(wav_sha256="0" * 64))
        with self.assertRaises(q.VerifyFailed):
            q.verify_manifest(out)

    def test_sample_removed_detected(self):
        out = self._tampered(lambda f: f["samples"].pop())
        with self.assertRaises(q.VerifyFailed):
            q.verify_manifest(out)

    def test_sample_added_detected(self):
        out = self._tampered(lambda f: f["samples"].append(make_sample(99)))
        with self.assertRaises(q.VerifyFailed):
            q.verify_manifest(out)

    def test_sample_reordered_detected(self):
        def swap(f):
            f["samples"][0], f["samples"][1] = f["samples"][1], f["samples"][0]
        out = self._tampered(swap)
        with self.assertRaises(q.VerifyFailed):
            q.verify_manifest(out)

    def test_other_field_edit_detected(self):
        out = self._tampered(lambda f: f["samples"][5].update(ambient_noise="临街"))
        with self.assertRaises(q.VerifyFailed):
            q.verify_manifest(out)

    def test_key_slot_edit_detected(self):
        out = self._tampered(
            lambda f: f["samples"][2]["key_slots"].update(number="15"))
        with self.assertRaises(q.VerifyFailed):
            q.verify_manifest(out)

    def test_content_hash_removed_flagged(self):
        out = self._tampered(lambda f: f.pop("content_hash"))
        with self.assertRaises(ValueError):
            q.verify_manifest(out)

    def test_non_frozen_json_rejected(self):
        path = self._write_json("draft.json", make_draft())
        with self.assertRaises(ValueError):
            q.verify_manifest(path)

    def test_missing_file_rejected(self):
        with self.assertRaises(ValueError):
            q.verify_manifest(os.path.join(self.tmp, "nope.json"))


class TestCLI(TempDirCase):
    def test_cli_freeze_then_verify_ok(self):
        draft_path = self._write_json("draft.json", make_draft())
        out_path = os.path.join(self.tmp, "frozen.json")
        out_buf, err_buf = io.StringIO(), io.StringIO()
        with redirect_stdout(out_buf), redirect_stderr(err_buf):
            self.assertEqual(
                q.main(["q04_corpus.py", "freeze", draft_path, out_path]), 0)
        out_buf, err_buf = io.StringIO(), io.StringIO()
        with redirect_stdout(out_buf), redirect_stderr(err_buf):
            self.assertEqual(
                q.main(["q04_corpus.py", "verify", out_path]), 0)
        self.assertIn("[OK]", out_buf.getvalue())

    def test_cli_verify_tampered_exits_1_with_message(self):
        out_path = self._freeze_ok()
        frozen = self._load(out_path)
        frozen["samples"][0]["reference_text"] = "被改过的参考"
        self._dump(out_path, frozen)
        out_buf, err_buf = io.StringIO(), io.StringIO()
        with redirect_stdout(out_buf), redirect_stderr(err_buf):
            self.assertEqual(
                q.main(["q04_corpus.py", "verify", out_path]), 1)
        self.assertIn("不一致", err_buf.getvalue())

    def test_cli_freeze_short_draft_refused(self):
        draft_path = self._write_json("draft.json", make_draft(14))
        out_path = os.path.join(self.tmp, "frozen.json")
        out_buf, err_buf = io.StringIO(), io.StringIO()
        with redirect_stdout(out_buf), redirect_stderr(err_buf):
            self.assertEqual(
                q.main(["q04_corpus.py", "freeze", draft_path, out_path]), 1)
        self.assertIn("[REFUSE]", err_buf.getvalue())
        self.assertFalse(os.path.exists(out_path))

    def test_cli_freeze_overwrite_refused(self):
        out_path = self._freeze_ok()
        draft_path = self._write_json("draft2.json", make_draft())
        out_buf, err_buf = io.StringIO(), io.StringIO()
        with redirect_stdout(out_buf), redirect_stderr(err_buf):
            self.assertEqual(
                q.main(["q04_corpus.py", "freeze", draft_path, out_path]), 1)
        self.assertIn("[REFUSE]", err_buf.getvalue())

    def test_cli_missing_draft_file_exits_2(self):
        out_path = os.path.join(self.tmp, "frozen.json")
        self.assertEqual(
            q.main(["q04_corpus.py", "freeze",
                    os.path.join(self.tmp, "nope.json"), out_path]), 2)

    def test_cli_bad_json_exits_2(self):
        path = os.path.join(self.tmp, "bad.json")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write("{not json")
        self.assertEqual(
            q.main(["q04_corpus.py", "freeze", path,
                    os.path.join(self.tmp, "frozen.json")]), 2)

    def test_cli_verify_non_frozen_exits_2(self):
        path = self._write_json("draft.json", make_draft())
        self.assertEqual(q.main(["q04_corpus.py", "verify", path]), 2)

    def test_cli_no_subcommand_exits_2(self):
        self.assertEqual(q.main(["q04_corpus.py"]), 2)


class TestEndToEndTamperRoundTrip(TempDirCase):
    """规范要求的演示路径：冻结 -> 篡改 -> 校验必须失败（非零退出码）。"""

    def test_freeze_tamper_verify_roundtrip(self):
        draft_path = self._write_json("draft.json", make_draft())
        out_path = os.path.join(self.tmp, "frozen.json")
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            self.assertEqual(
                q.main(["q04_corpus.py", "freeze", draft_path, out_path]), 0)
            self.assertEqual(q.main(["q04_corpus.py", "verify", out_path]), 0)
            frozen = self._load(out_path)
            frozen["samples"][9]["reference_text"] = "看到模型输出后改写"
            frozen["samples"][9]["key_slots"]["negation"] = "改成肯定"
            self._dump(out_path, frozen)
            self.assertEqual(q.main(["q04_corpus.py", "verify", out_path]), 1)


class TestConstants(unittest.TestCase):
    def test_exactly_15_required(self):
        self.assertEqual(q.REQUIRED_SAMPLES, 15)

    def test_required_slot_keys_match_spec(self):
        self.assertEqual(q.REQUIRED_SLOT_KEYS,
                         ("negation", "action_object", "number", "file_path"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
