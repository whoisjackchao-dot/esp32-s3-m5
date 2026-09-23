#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Q03 判定核心单测（先红后绿的回归护栏）。运行：python -m unittest test_q03_latency"""
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
                           delta_ms=100, end_frames=66, end_samples=63360,
                           summary_match=True)
        self.assertTrue(r["ok"])
        self.assertEqual(r["latency_s"], 3.0)

    def test_build_record_failure_keeps_denominator(self):
        r = q.build_record("5s", 2, t0_ms=5000, t1_ms=None, br04_status="timeout")
        self.assertFalse(r["ok"])
        self.assertIsNone(r["latency_s"])

    def test_build_record_summary_mismatch_is_failure(self):
        r = q.build_record("5s", 3, 5000, 8000, "applied", summary_match=False)
        self.assertFalse(r["ok"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
