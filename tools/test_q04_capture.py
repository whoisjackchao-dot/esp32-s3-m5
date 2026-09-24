#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Q04 采集编排器纯逻辑单测（先红后绿；无硬件、无第三方、不写 captures/）。

覆盖：占位符判定、样本选择、文件 SHA256、WAV 时长（wave 模块造临时文件）、
apply_capture 纯字段填写（不就地改）、run_capture/decode 的 argv 注入与失败上报、
草稿读写往返、距离解析。
运行：python3 tools/test_q04_capture.py
"""
import argparse
import copy
import hashlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
import wave
from contextlib import redirect_stdout
from pathlib import Path

import q04_capture as q

HERE = Path(__file__).resolve().parent


def make_wav(path, nframes, framerate=48000):
    """用 wave 模块造一个真实的临时 WAV（单声道 16bit）。"""
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(framerate)
        w.writeframes(b"\x00\x00" * nframes)


def make_sample():
    return {
        "id": "q4-01",
        "_组": "中文基础",
        "wav": "captures/q04/q4-01.wav",
        "wav_sha256": "<待填：录音SHA256>",
        "speaker_authorized": True,
        "duration_s": 0,
        "ambient_noise": "<待填>",
        "device_distance_m": 0,
        "reference_text": "请把用户登录函数重构成三个小函数",
        "audio_environment": "<待填，如 安静室内工位>",
    }


class IsPlaceholderTest(unittest.TestCase):
    def test_is_placeholder_when_none_or_blank_returns_true(self):
        # Given 各类空值 When 判定 Then 均视为占位
        for v in (None, "", "   ", "\t"):
            with self.subTest(value=v):
                self.assertTrue(q.is_placeholder(v))

    def test_is_placeholder_when_daitian_style_returns_true(self):
        # Given 草稿中实际出现的 <待填…> 形态 When 判定 Then 均视为占位
        for v in ("<待填>", "<待填：录音SHA256>", "<待填，如 安静室内工位>"):
            with self.subTest(value=v):
                self.assertTrue(q.is_placeholder(v))

    def test_is_placeholder_when_real_value_returns_false(self):
        # Given 真实数据（含 0/False/空列表 等合法值）When 判定 Then 不是占位
        for v in ("安静室内工位", 0, 0.5, False, True, [], {}, ["useState"]):
            with self.subTest(value=v):
                self.assertFalse(q.is_placeholder(v))


class SelectSamplesTest(unittest.TestCase):
    def test_select_samples_when_no_only_returns_all(self):
        # Given 3 条样本且 only=None When 选择 Then 返回全部
        samples = [{"id": "a"}, {"id": "b"}, {"id": "c"}]
        self.assertEqual(q.select_samples(samples, None), samples)

    def test_select_samples_when_only_selects_one_based_index(self):
        # Given only=2 When 选择 Then 返回第 2 条（1 基）单元素列表
        samples = [{"id": "a"}, {"id": "b"}, {"id": "c"}]
        self.assertEqual(q.select_samples(samples, 2), [{"id": "b"}])

    def test_select_samples_when_out_of_range_raises_value_error(self):
        # Given only 越界（0 或 len+1）When 选择 Then 抛 ValueError
        samples = [{"id": "a"}, {"id": "b"}]
        for bad in (0, 3, -1):
            with self.subTest(only=bad):
                with self.assertRaises(ValueError):
                    q.select_samples(samples, bad)


class Sha256OfFileTest(unittest.TestCase):
    def test_sha256_of_file_when_small_file_matches_hashlib(self):
        # Given 已知字节的临时文件 When 计算摘要 Then 与 hashlib 对字节直接求值一致
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "a.bin"
            data = "hello 语料\n".encode("utf-8")
            p.write_bytes(data)
            self.assertEqual(q.sha256_of_file(p), hashlib.sha256(data).hexdigest())

    def test_sha256_of_file_when_large_file_chunked_matches(self):
        # Given 大于单块(64KB)的文件 When 分块读取 Then 摘要仍与整体一致
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "big.bin"
            data = bytes(range(256)) * 1000  # 256000 字节
            p.write_bytes(data)
            self.assertEqual(q.sha256_of_file(p), hashlib.sha256(data).hexdigest())


class DurationOfWavTest(unittest.TestCase):
    def test_duration_of_wav_when_48k_mono_returns_nframes_over_framerate(self):
        # Given 48kHz、24000 帧的临时 WAV When 求时长 Then 等于 0.5s
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "a.wav"
            make_wav(p, 24000, 48000)
            self.assertAlmostEqual(q.duration_of_wav(p), 0.5)

    def test_duration_of_wav_when_other_framerate_uses_that_framerate(self):
        # Given 8kHz、8000 帧 When 求时长 Then 等于 1.0s（按实际帧率计算）
        with tempfile.TemporaryDirectory() as td:
            p = Path(td) / "b.wav"
            make_wav(p, 8000, 8000)
            self.assertAlmostEqual(q.duration_of_wav(p), 1.0)


class ApplyCaptureTest(unittest.TestCase):
    def test_apply_capture_when_called_returns_new_dict_without_mutating_input(self):
        # Given 原始占位样本 When 填写采集结果 Then 返回新 dict、原 dict 不变、字段正确
        sample = make_sample()
        frozen = copy.deepcopy(sample)
        updated = q.apply_capture(
            sample,
            "captures/q04/q4-01.wav", "captures/q04/q4-01.s3pcap",
            "ab" * 32, 1.5, "安静室内", 0.5,
        )
        self.assertEqual(sample, frozen)
        self.assertIsNot(updated, sample)
        self.assertEqual(updated["wav"], "captures/q04/q4-01.wav")
        self.assertEqual(updated["wav_sha256"], "ab" * 32)
        self.assertEqual(updated["duration_s"], 1.5)
        self.assertEqual(updated["ambient_noise"], "安静室内")
        self.assertEqual(updated["device_distance_m"], 0.5)
        self.assertEqual(updated["audio_environment"], "ambient=安静室内; distance=0.5m")
        self.assertTrue(updated["speaker_authorized"])


class RunCaptureTest(unittest.TestCase):
    def test_run_capture_when_zero_exit_returns_ok_and_exact_argv(self):
        # Given 假 runner 返回 0 When 采集 Then argv 精确等于注入约定且报成功
        calls = []

        def fake_run(argv):
            calls.append(argv)
            return subprocess.CompletedProcess(argv, 0)

        with tempfile.TemporaryDirectory() as td:
            outdir = Path(td)
            ok, cap = q.run_capture({"id": "q4-07"}, outdir, 120, 45, run=fake_run)
            self.assertTrue(ok)
            self.assertEqual(cap, outdir / "q4-07.s3pcap")
            self.assertEqual(calls, [[
                sys.executable, str(HERE / "button_hil.py"),
                "120", "45", str(outdir / "q4-07.s3pcap"),
            ]])

    def test_run_capture_when_nonzero_exit_reports_failure_without_raising(self):
        # Given 假 runner 返回 2（button_hil 失败码）When 采集 Then 返回失败且不抛异常
        def fake_run(argv):
            return subprocess.CompletedProcess(argv, 2)

        with tempfile.TemporaryDirectory() as td:
            ok, cap = q.run_capture({"id": "q4-07"}, Path(td), 120, 45, run=fake_run)
            self.assertFalse(ok)
            self.assertEqual(cap, Path(td) / "q4-07.s3pcap")


class DecodeTest(unittest.TestCase):
    def test_decode_when_zero_exit_returns_true_and_exact_argv(self):
        # Given 假 runner 返回 0 When 解码 Then argv 精确等于 [python, decode_capture.py, cap, wav]
        calls = []

        def fake_run(argv):
            calls.append(argv)
            return subprocess.CompletedProcess(argv, 0)

        cap = Path("/tmp/x.s3pcap")
        wav = Path("/tmp/x.wav")
        self.assertTrue(q.decode(cap, wav, run=fake_run))
        self.assertEqual(calls, [[
            sys.executable, str(HERE / "decode_capture.py"), str(cap), str(wav),
        ]])

    def test_decode_when_nonzero_exit_returns_false_without_raising(self):
        # Given 假 runner 返回 2 When 解码 Then 返回 False 且不抛异常
        def fake_run(argv):
            return subprocess.CompletedProcess(argv, 2)

        self.assertFalse(q.decode(Path("/tmp/x.s3pcap"), Path("/tmp/x.wav"), run=fake_run))


class DraftIoTest(unittest.TestCase):
    def test_load_and_save_draft_roundtrip_keeps_chinese_raw(self):
        # Given 草稿 dict When 保存后重新加载 Then 数据一致且文件为非转义 UTF-8 中文
        doc = {"samples": [make_sample()]}
        with tempfile.TemporaryDirectory() as td:
            out = Path(td) / "out.json"
            q.save_draft(out, doc)
            raw = out.read_bytes()
            self.assertIn("请把用户登录函数".encode("utf-8"), raw)
            self.assertNotIn(b"\\u", raw)  # ensure_ascii=False：不许出现 \uXXXX 转义
            self.assertTrue(raw.startswith(b'{\n  "samples"'))  # indent=2
            loaded = q.load_draft(out)
            self.assertEqual(loaded, doc)

    def test_load_completed_or_initial_prefers_existing_out(self):
        # Given 完成稿已存在 When 选择基底 Then 用完成稿（重做时保留其余已采结果）
        with tempfile.TemporaryDirectory() as td:
            draft = Path(td) / "draft.json"
            out = Path(td) / "out.json"
            q.save_draft(draft, {"samples": [{"id": "draft"}]})
            q.save_draft(out, {"samples": [{"id": "completed"}]})
            args = argparse.Namespace(draft=draft, out=out)
            self.assertEqual(q.load_completed_or_initial(args)["samples"][0]["id"], "completed")

    def test_load_completed_or_initial_falls_back_to_draft(self):
        # Given 完成稿不存在 When 选择基底 Then 用原始草稿
        with tempfile.TemporaryDirectory() as td:
            draft = Path(td) / "draft.json"
            out = Path(td) / "out.json"
            q.save_draft(draft, {"samples": [{"id": "draft"}]})
            args = argparse.Namespace(draft=draft, out=out)
            self.assertEqual(q.load_completed_or_initial(args)["samples"][0]["id"], "draft")


class ParseDistanceTest(unittest.TestCase):
    def test_parse_distance_when_valid_float_returns_float(self):
        # Given 合法数字字符串 When 解析 Then 得到 float
        self.assertEqual(q.parse_distance("0.5"), 0.5)
        self.assertEqual(q.parse_distance(" 1 "), 1.0)

    def test_parse_distance_when_invalid_raises_value_error(self):
        # Given 非数字 When 解析 Then 抛 ValueError
        with self.assertRaises(ValueError):
            q.parse_distance("abc")


class PromptMissingTest(unittest.TestCase):
    def test_prompt_missing_when_both_given_never_asks(self):
        # Given 两项都已由 CLI 提供 When 补全 Then 不发生任何交互
        def ask(_prompt):
            raise AssertionError("不应发生交互")

        self.assertEqual(q.prompt_missing("安静", 0.5, ask=ask), ("安静", 0.5))

    def test_prompt_missing_when_ambient_absent_asks_once(self):
        # Given 仅缺 ambient When 补全 Then 只询问一次并保留已有 distance
        asks = []

        def ask(prompt):
            asks.append(prompt)
            return "安静室内工位"

        self.assertEqual(q.prompt_missing(None, 0.5, ask=ask), ("安静室内工位", 0.5))
        self.assertEqual(len(asks), 1)

    def test_prompt_missing_when_distance_invalid_then_valid_retries(self):
        # Given distance 输入先非法后合法 When 补全 Then 重试并返回解析值
        answers = iter(["abc", " 0.8 "])
        asks = []

        def ask(prompt):
            asks.append(prompt)
            return next(answers)

        buf = io.StringIO()
        with redirect_stdout(buf):
            result = q.prompt_missing("安静", None, ask=ask)
        self.assertEqual(result, ("安静", 0.8))
        self.assertEqual(len(asks), 2)


if __name__ == "__main__":
    unittest.main()
