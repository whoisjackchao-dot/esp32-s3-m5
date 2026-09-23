#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Q03 判定核心单测（先红后绿的回归护栏）。运行：python -m unittest test_q03_latency"""
import json
import os
import tempfile
import unittest

import q03_latency as q


class TestNearestRank(unittest.TestCase):
    def test_median_of_five(self):
        self.assertEqual(q.nearest_rank_percentile([1, 2, 3, 4, 5], 50), 3)

    def test_p95_rounds_up(self):
        self.assertEqual(q.nearest_rank_percentile([1, 2, 3, 4, 5], 95), 5)

    def test_p100(self):
        self.assertEqual(q.nearest_rank_percentile([1, 2, 3, 4, 5], 100), 5)

    def test_single(self):
        self.assertEqual(q.nearest_rank_percentile([2.5], 50), 2.5)

    def test_empty_and_none(self):
        self.assertIsNone(q.nearest_rank_percentile([], 50))
        self.assertIsNone(q.nearest_rank_percentile([None, None], 50))

    def test_ignores_none(self):
        self.assertEqual(q.nearest_rank_percentile([None, 1.0, 3.0], 50), 1.0)


class TestEvaluateGroup(unittest.TestCase):
    def test_all_20_pass(self):
        r = q.evaluate_group([3.0] * 20)
        self.assertTrue(r["passed"])
        self.assertEqual(r["successes_le_threshold"], 20)
        self.assertEqual(r["p50_s"], 3.0)

    def test_19_of_20_pass(self):
        r = q.evaluate_group([3.0] * 19 + [6.0])
        self.assertTrue(r["passed"])
        self.assertEqual(r["failed_indices"], [19])

    def test_18_of_20_fails(self):
        r = q.evaluate_group([3.0] * 18 + [6.0, 7.0])
        self.assertFalse(r["passed"])

    def test_none_counts_as_failure_in_denominator(self):
        r = q.evaluate_group([3.0] * 18 + [None, None])
        self.assertFalse(r["passed"])
        self.assertEqual(r["n"], 20)
        self.assertEqual(r["successes_le_threshold"], 18)

    def test_incomplete_group_never_passes(self):
        r = q.evaluate_group([3.0] * 19)
        self.assertFalse(r["passed"])
        self.assertFalse(r["complete"])

    def test_threshold_boundary_is_inclusive(self):
        r = q.evaluate_group([5.0] * 20)
        self.assertTrue(r["passed"])
        self.assertEqual(r["successes_le_threshold"], 20)

    def test_just_over_threshold_fails_that_run(self):
        r = q.evaluate_group([3.0] * 19 + [5.001])
        self.assertTrue(r["passed"])
        self.assertEqual(r["failed_indices"], [19])

    def test_grey_zone_when_delta_pushes_over(self):
        r = q.evaluate_group([4.8] * 20, deltas_s=[0.5] * 20)
        self.assertTrue(r["passed"])
        self.assertEqual(r["grey_zone"], 20)

    def test_no_grey_when_delta_small(self):
        r = q.evaluate_group([4.0] * 20, deltas_s=[0.1] * 20)
        self.assertEqual(r["grey_zone"], 0)


class TestEvaluate(unittest.TestCase):
    def _recs(self):
        recs = []
        for g in q.GROUPS:
            for i in range(20):
                recs.append({"group": g, "latency_s": 2.0 + i * 0.01, "delta_ms": 50})
        return recs

    def test_all_groups_pass(self):
        res = q.evaluate(self._recs())
        self.assertTrue(res["_all_passed"])
        self.assertFalse(res["_needs_review"])

    def test_one_group_fails(self):
        recs = self._recs()
        recs[-1]["latency_s"] = 9.0
        recs[-2]["latency_s"] = 9.0
        res = q.evaluate(recs)
        self.assertFalse(res["_all_passed"])
        self.assertEqual(res["30s"]["successes_le_threshold"], 18)

    def test_unknown_group_rejected(self):
        with self.assertRaises(ValueError):
            q.evaluate([{"group": "7s", "latency_s": 1.0}])

    def test_report_smoke(self):
        text = q.format_report(q.evaluate(self._recs()))
        self.assertIn("三组总体：PASS", text)
        self.assertIn("| 5s |", text)


class TestAnchorAndRecord(unittest.TestCase):
    LINES = [
        (1000, "I (4948) button: BtnA 按下"),
        (5000, "I (9000) button: BtnA 松开"),
        (5060, "I (9060) audio_session: 松键提前结束 seq=66"),
        (5200, "I (9200) audio_session: END recID=1 frames=66 samples=63360"),
    ]

    def test_pick_early_anchor(self):
        got = q.pick_anchor_ts(self.LINES, "5s", 2000)
        self.assertEqual(got[0], 5000)
        self.assertIn("BtnA 松开", got[1])

    def test_pick_hardstop_anchor(self):
        got = q.pick_anchor_ts(self.LINES, "30s", 2000)
        self.assertEqual(got[0], 5200)
        self.assertIn("END recID=", got[1])

    def test_anchor_respects_begin_bound(self):
        self.assertIsNone(q.pick_anchor_ts(self.LINES, "5s", 9000))

    def test_anchor_missing_returns_none(self):
        self.assertIsNone(q.pick_anchor_ts([], "5s", 0))

    def test_delta_uses_serial_minus_ble_end(self):
        self.assertEqual(q.serial_latency_delta_ms(self.LINES, 5100), 100)

    def test_delta_none_without_end_line(self):
        self.assertIsNone(q.serial_latency_delta_ms([(1, "no end here")], 0))

    def test_build_record_ok(self):
        r = q.build_record("5s", 1, t0_ms=5000, t1_ms=8000, br04_status="applied",
                           delta_ms=100, end_frames=92, end_samples=88000,
                           summary_match=True)
        self.assertTrue(r["ok"])
        self.assertEqual(r["latency_s"], 3.0)
        self.assertEqual(r["duration_s"], 5.5)
        self.assertTrue(r["duration_ok"])

    def test_build_record_failure_keeps_denominator(self):
        r = q.build_record("5s", 2, t0_ms=5000, t1_ms=None, br04_status="timeout")
        self.assertFalse(r["ok"])
        self.assertIsNone(r["latency_s"])

    def test_build_record_summary_mismatch_is_failure(self):
        r = q.build_record("5s", 3, 5000, 8000, "applied", summary_match=False)
        self.assertFalse(r["ok"])


class TestLoadRecords(unittest.TestCase):
    def _write(self, text):
        fd, path = tempfile.mkstemp(suffix=".jsonl")
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.write(text)
        self.addCleanup(os.remove, path)
        return path

    def test_jsonl_roundtrip(self):
        recs = [{"group": "5s", "latency_s": 1.0},
                {"group": "5s", "latency_s": 2.0}]
        path = self._write("\n".join(json.dumps(r) for r in recs) + "\n")
        self.assertEqual(q.load_records(path), recs)

    def test_json_array(self):
        recs = [{"group": "15s", "latency_s": 1.5}]
        path = self._write(json.dumps(recs))
        self.assertEqual(q.load_records(path), recs)

    def test_json_object_with_records(self):
        recs = [{"group": "30s", "latency_s": 1.5}]
        path = self._write(json.dumps({"records": recs}))
        self.assertEqual(q.load_records(path), recs)

    def test_trailing_blank_lines_ignored(self):
        path = self._write('{"group": "5s", "latency_s": 1.0}\n\n\n')
        recs = q.load_records(path)
        self.assertEqual(len(recs), 1)
        self.assertEqual(recs[0]["group"], "5s")

    def test_single_jsonl_record_not_misread_as_dict(self):
        path = self._write('{"group": "30s", "index": 3, "latency_s": 2.5}\n')
        self.assertEqual(q.load_records(path),
                         [{"group": "30s", "index": 3, "latency_s": 2.5}])

    def test_empty_file(self):
        self.assertEqual(q.load_records(self._write("   \n")), [])

    def test_cli_accepts_jsonl(self):
        recs = []
        for g in q.GROUPS:
            for _ in range(20):
                recs.append({"group": g, "latency_s": 2.0, "delta_ms": 50})
        path = self._write("\n".join(json.dumps(r) for r in recs) + "\n")
        self.assertEqual(q.main(["q03_latency.py", path]), 0)


class TestDurationWindow(unittest.TestCase):
    def test_in_window(self):
        self.assertTrue(q.duration_ok("5s", 5.0))
        self.assertTrue(q.duration_ok("5s", 6.99))
        self.assertTrue(q.duration_ok("10s", 11.0))
        self.assertTrue(q.duration_ok("30s", 30.0))

    def test_out_of_window(self):
        self.assertFalse(q.duration_ok("5s", 4.99))
        self.assertFalse(q.duration_ok("5s", 7.01))
        self.assertFalse(q.duration_ok("10s", 9.99))
        self.assertFalse(q.duration_ok("30s", 29.0))
        self.assertFalse(q.duration_ok("5s", None))
        self.assertFalse(q.duration_ok("99s", 5.0))

    def test_build_record_rejects_wrong_duration(self):
        r = q.build_record("5s", 2, 5000, 8000, "applied", end_samples=63360)
        self.assertFalse(r["duration_ok"])
        self.assertIsNone(r["latency_s"])
        self.assertFalse(r["ok"])


class TestDedupeLatest(unittest.TestCase):
    def test_later_record_wins(self):
        recs = [{"group": "5s", "index": 1, "ok": False},
                {"group": "5s", "index": 1, "ok": True}]
        out = q.dedupe_latest(recs)
        self.assertEqual(len(out), 1)
        self.assertTrue(out[0]["ok"])

    def test_distinct_indices_kept(self):
        recs = [{"group": "5s", "index": 1}, {"group": "5s", "index": 2}]
        self.assertEqual(len(q.dedupe_latest(recs)), 2)

    def test_records_without_index_each_count(self):
        recs = [{"group": "5s"}, {"group": "5s"}, {"group": "5s"}]
        self.assertEqual(len(q.dedupe_latest(recs)), 3)


if __name__ == "__main__":
    unittest.main(verbosity=2)
