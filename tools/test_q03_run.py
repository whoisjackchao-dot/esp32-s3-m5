#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""q03_run 编排的纯逻辑回归测试（不含 BLE/串口/进程）。运行：python -m unittest test_q03_run"""
import unittest

import q03_run as r


class TestClassifyAppendLine(unittest.TestCase):
    def test_br04_line_with_chinese_is_detected(self):
        br04, _ = r.classify_append_line("[3] 桥接 HTTP 200\n")
        self.assertTrue(br04)

    def test_br04_line_mojibake_still_detected(self):
        br04, _ = r.classify_append_line("[3] \u00e6\u00a1\u00a5\u00e6\u008e\u00a5 HTTP 200\n")
        self.assertTrue(br04)

    def test_applied_on_json_body_line(self):
        br04, status = r.classify_append_line(
            '{"protocolVersion":1,"tuiEpoch":"e","operationID":"op","status":"applied","reason":null}')
        self.assertFalse(br04)
        self.assertEqual(status, "applied")

    def test_uncertain_and_processing(self):
        self.assertEqual(r.classify_append_line('{"status":"uncertain"}')[1], "uncertain")
        self.assertEqual(r.classify_append_line('{"status":"processing"}')[1], "processing")

    def test_other_lines_yield_no_status(self):
        for line in ("[1] 音频 x.wav (1 字节)", "[2] ASR HTTP 200 class=ASR_OK",
                     "text: 你好", "[PASS] 已填入草稿（未提交，请手动发送）", ""):
            br04, status = r.classify_append_line(line)
            self.assertFalse(br04, line)
            self.assertIsNone(status, line)

    def test_asr_error_line_is_not_br04(self):
        br04, status = r.classify_append_line("[FAIL] ASR 传输: ASR_TIMEOUT (HTTP 0)")
        self.assertFalse(br04)
        self.assertIsNone(status)


if __name__ == "__main__":
    unittest.main(verbosity=2)
