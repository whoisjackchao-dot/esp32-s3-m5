#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Q04 ASR 准确率评分器单测（先红后绿的回归护栏）。

黄金用例期望按已批准 Q04 规则人工推导并硬编码，不从实现导出。
运行：python3 tools/test_q04_grader.py（repo 根目录）
"""
import io
import json
import os
import tempfile
import unittest
from contextlib import redirect_stdout

import q04_grader as q

# 黄金固定句对（期望值见各类断言；规则：矩阵 §ASR-10、契约 M3-ASR §1）
REVIEW_REF = "请更新三个模块的文档并提交"
REVIEW_HYP = "请更新三个组件说明并提交"
REWRITE_REF = "把配置文件保存到临时目录"
REWRITE_HYP = "请立即关闭当前应用窗口"


def corpus(pass_n, fail_n=0, review_n=0):
    """构造固定规模语料：pass 样本引用=假设（必 PASS），fail 用空输出（EMPTY 仍在分母），
    review 用黄金歧义用例（NEEDS_REVIEW 不计 PASS）。"""
    samples = []
    for i in range(pass_n):
        text = "样本{:02d}：运行全部单元测试".format(i)
        samples.append({"id": "p{:02d}".format(i), "reference": text, "hypothesis": text})
    for i in range(fail_n):
        samples.append({"id": "f{:02d}".format(i),
                        "reference": REWRITE_REF, "hypothesis": ""})
    for i in range(review_n):
        samples.append({"id": "r{:02d}".format(i),
                        "reference": REVIEW_REF, "hypothesis": REVIEW_HYP})
    return samples


class TestNormalization(unittest.TestCase):
    """归一化红线：路径分隔符、小数点、负号、版本点、代码标识不得被删除。"""

    def test_nfkc_folds_fullwidth_digits_and_punct(self):
        self.assertEqual(q.normalize("１.５，"), "1.5,")

    def test_decimal_and_integer_numeric_tokens_differ(self):
        self.assertEqual(q.numeric_tokens("超时设为 1.5 秒"), ["1.5"])
        self.assertEqual(q.numeric_tokens("超时设为 15 秒"), ["15"])
        self.assertNotEqual(q.numeric_tokens("超时设为 1.5 秒"),
                            q.numeric_tokens("超时设为 15 秒"))

    def test_minus_sign_kept_in_numeric_tokens(self):
        self.assertEqual(q.numeric_tokens("回退 -5 行"), ["-5"])

    def test_path_separator_never_stripped(self):
        self.assertEqual(q.path_tokens("检查 a/b 目录"), ["a/b"])
        self.assertEqual(q.path_tokens("检查 ab 目录"), [])

    def test_code_token_keeps_version_dots(self):
        self.assertEqual(q.content_tokens(q.normalize("升级 v2.10.1")),
                         ["升", "级", "v2.10.1"])

    def test_negation_counts(self):
        self.assertEqual(q.negation_counts("不要删除文件"), {"不": 1})
        self.assertEqual(q.negation_counts("要删除文件"), {})

    def test_punct_stream_separate_from_content(self):
        self.assertEqual(q.punct_stream(q.normalize("打开文件，然后保存")), [","])
        self.assertEqual(q.content_tokens(q.normalize("打开文件，然后保存")),
                         list("打开文件然后保存"))


class TestGolden(unittest.TestCase):
    """黄金 13 例：期望由规则人工固定，驱动实现。"""

    def test_golden_01_identical_pass(self):
        v = q.grade_sample("把测试结果写入报告", "把测试结果写入报告")
        self.assertIs(v.status, q.Status.PASS)
        self.assertEqual(v.local_corrections, 0)
        self.assertEqual(v.is_rewrite, 0)
        self.assertEqual(v.key_slot_violations, 0)
        self.assertEqual(v.similarity, 1.0)
        self.assertEqual(v.reasons, ())

    def test_golden_02_punct_only_pass_documented(self):
        # 文档化行为：标点/空格单独计数——半角/全角逗号差异 + 去掉句号
        # 不占局部修正额度（local_corrections=0），差异单列记 PUNCT_SPACE_DIFF，仍 PASS。
        v = q.grade_sample("打开文件,然后保存。", "打开文件，然后保存")
        self.assertIs(v.status, q.Status.PASS)
        self.assertEqual(v.local_corrections, 0)
        self.assertGreaterEqual(v.punct_space_diff, 1)
        self.assertIn("PUNCT_SPACE_DIFF", v.reasons)

    def test_golden_03_one_local_correction_pass(self):
        v = q.grade_sample("提交代码变更", "提交代码修改")
        self.assertIs(v.status, q.Status.PASS)
        self.assertEqual(v.local_corrections, 1)

    def test_golden_04_two_local_corrections_pass(self):
        v = q.grade_sample("先构建项目再运行测试", "先编译项目再执行测试")
        self.assertIs(v.status, q.Status.PASS)
        self.assertEqual(v.local_corrections, 2)

    def test_golden_05_three_local_corrections_fail(self):
        # 3 处互不相邻的局部修正：构建→编译、运→执、报告→文档。
        # （相邻替换按规则"连续片段一次修正计 1 处"会合并，故夹具必须分段隔开。）
        v = q.grade_sample("先构建项目再运行测试并把结果写入报告",
                           "先编译项目再执行测试并把结果写入文档")
        self.assertIs(v.status, q.Status.FAIL)
        self.assertEqual(v.local_corrections, 3)
        self.assertIn("TOO_MANY_CORRECTIONS", v.reasons)
        self.assertEqual(v.is_rewrite, 0)  # 分散小修正不得误判为整句重写

    def test_golden_06_negation_reversal_fail(self):
        # 否定反转直接不合格，即使只差 1 处（未声明槽位时自动检测兜底）。
        v = q.grade_sample("不要删除文件", "要删除文件")
        self.assertIs(v.status, q.Status.FAIL)
        self.assertEqual(v.key_slot_violations, 1)
        self.assertIn("KEY_SLOT_NEGATION", v.reasons)

    def test_golden_06b_negation_slot_declared_fail(self):
        v = q.grade_sample("不要删除文件", "要删除文件", slots={"negation": "不要"})
        self.assertIs(v.status, q.Status.FAIL)
        self.assertIn("KEY_SLOT_NEGATION", v.reasons)
        self.assertEqual(v.key_slot_violations, 1)  # 声明槽位与自动检测去重后计 1

    def test_golden_07_wrong_file_path_fail(self):
        v = q.grade_sample("编辑 src/a.py", "编辑 src/b.py")
        self.assertIs(v.status, q.Status.FAIL)
        self.assertIn("KEY_SLOT_PATH", v.reasons)
        v2 = q.grade_sample("编辑 src/a.py", "编辑 src/b.py",
                            slots={"path": "src/a.py"})
        self.assertIs(v2.status, q.Status.FAIL)
        self.assertEqual(v2.key_slot_violations, 1)

    def test_golden_08_wrong_key_number_fail(self):
        v = q.grade_sample("重试 3 次", "重试 5 次")
        self.assertIs(v.status, q.Status.FAIL)
        self.assertIn("KEY_SLOT_NUMBER", v.reasons)
        self.assertEqual(v.key_slot_violations, 1)

    def test_golden_09_full_rewrite_fail(self):
        # 整句替换不得按 1 处轻微修正放行：is_rewrite 置位并 FAIL。
        v = q.grade_sample(REWRITE_REF, REWRITE_HYP)
        self.assertIs(v.status, q.Status.FAIL)
        self.assertEqual(v.is_rewrite, 1)
        self.assertIn("REWRITE", v.reasons)
        self.assertLess(v.similarity, 0.5)

    def test_golden_10_empty_response_fail(self):
        # 空输出记不合格，且保留在分母（由 corpus 测试覆盖分母）。
        for hyp in ("", "   ", None):
            v = q.grade_sample("参考文本", hyp)
            self.assertIs(v.status, q.Status.FAIL, hyp)
            self.assertIn("EMPTY", v.reasons)

    def test_golden_11_decimal_vs_integer_not_equal(self):
        # "1.5" 与 "15" 不得因"标点另计"视为相同。
        self.assertNotEqual(q.numeric_tokens("1.5"), q.numeric_tokens("15"))
        v = q.grade_sample("超时设为 1.5 秒", "超时设为 15 秒")
        self.assertIs(v.status, q.Status.FAIL)
        self.assertIn("KEY_SLOT_NUMBER", v.reasons)

    def test_golden_12_path_separator_preserved(self):
        # 路径分隔符不得被归一化删除："a/b" 与 "ab" 不相等。
        self.assertNotEqual(q.path_tokens("检查 a/b 目录"),
                            q.path_tokens("检查 ab 目录"))
        v = q.grade_sample("检查 a/b 目录", "检查 ab 目录")
        self.assertIs(v.status, q.Status.FAIL)
        self.assertIn("KEY_SLOT_PATH", v.reasons)

    def test_golden_13_ambiguous_needs_review(self):
        # 大段连续片段（5/13 字）被替换：不算 1 处轻微修正放行，标 NEEDS_REVIEW。
        v = q.grade_sample(REVIEW_REF, REVIEW_HYP)
        self.assertIs(v.status, q.Status.NEEDS_REVIEW)
        self.assertNotEqual(v.status, q.Status.PASS)
        self.assertIn("LARGE_DIFF", v.reasons)


class TestExtraRules(unittest.TestCase):
    def test_object_slot_violation_fails(self):
        # 操作对象错误：由冻结槽位声明判定。
        v = q.grade_sample("删除临时文件", "删除配置文件", slots={"object": "临时"})
        self.assertIs(v.status, q.Status.FAIL)
        self.assertIn("KEY_SLOT_OBJECT", v.reasons)

    def test_reorder_needs_review(self):
        # 语序重排（同一组词换序）：是否改变含义不可自动判定，标 NEEDS_REVIEW。
        v = q.grade_sample("先保存再编译", "先编译再保存")
        self.assertIs(v.status, q.Status.NEEDS_REVIEW)
        self.assertIn("REORDER", v.reasons)

    def test_empty_reference_is_rewrite_fail(self):
        v = q.grade_sample("", "删除文件")
        self.assertIs(v.status, q.Status.FAIL)
        self.assertEqual(v.is_rewrite, 1)

    def test_multiple_punct_differences_still_pass(self):
        v = q.grade_sample("a, b。c", "a、b c")
        self.assertIs(v.status, q.Status.PASS)
        self.assertEqual(v.local_corrections, 0)
        self.assertGreaterEqual(v.punct_space_diff, 1)

    def test_non_string_slot_value_rejected(self):
        with self.assertRaises(ValueError):
            q.grade_sample("重试 3 次", "重试 3 次", slots={"number": 3})

    def test_non_dict_slots_rejected(self):
        with self.assertRaises(ValueError):
            q.grade_sample("a", "a", slots=["object"])


class TestGradeCorpus(unittest.TestCase):
    def test_14_of_15_passes(self):
        report = q.grade_corpus(corpus(14, fail_n=1))
        self.assertEqual(report.total, 15)
        self.assertEqual(report.pass_count, 14)
        self.assertTrue(report.overall_pass)

    def test_14_boundary_passes(self):
        report = q.grade_corpus(corpus(14, fail_n=1))
        self.assertEqual(report.total, 15)
        self.assertEqual(report.pass_count, 14)
        self.assertTrue(report.overall_pass)

    def test_13_fails(self):
        report = q.grade_corpus(corpus(13, fail_n=2))
        self.assertEqual(report.pass_count, 13)
        self.assertFalse(report.overall_pass)

    def test_review_not_counted_as_pass(self):
        report = q.grade_corpus(corpus(14, fail_n=0, review_n=1))
        self.assertEqual(report.pass_count, 14)
        self.assertEqual(report.review_count, 1)
        self.assertTrue(report.overall_pass)
        report2 = q.grade_corpus(corpus(13, fail_n=1, review_n=1))
        self.assertEqual(report2.pass_count, 13)
        self.assertFalse(report2.overall_pass)

    def test_per_sample_verdicts_exposed(self):
        report = q.grade_corpus(corpus(14, fail_n=1))
        self.assertEqual(len(report.results), 15)
        self.assertIsInstance(report.results[0], q.Verdict)
        self.assertIs(report.results[0].status, q.Status.PASS)
        self.assertIs(report.results[14].status, q.Status.FAIL)

    def test_empty_corpus_fails(self):
        report = q.grade_corpus([])
        self.assertEqual(report.total, 0)
        self.assertEqual(report.pass_count, 0)
        self.assertFalse(report.overall_pass)

    def test_invalid_samples_rejected(self):
        with self.assertRaises(ValueError):
            q.grade_corpus([{"reference": "x"}])
        with self.assertRaises(ValueError):
            q.grade_corpus(["not-a-dict"])
        with self.assertRaises(ValueError):
            q.grade_corpus({"reference": "x", "hypothesis": "x"})


class TestCli(unittest.TestCase):
    def _write(self, text):
        fd, path = tempfile.mkstemp(suffix=".json")
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.write(text)
        self.addCleanup(os.remove, path)
        return path

    def test_all_pass_exit_0(self):
        path = self._write(json.dumps(corpus(15)))
        out = io.StringIO()
        with redirect_stdout(out):
            self.assertEqual(q.main(["q04_grader.py", path]), 0)
        self.assertIn("15/15", out.getvalue())
        self.assertIn("验收 PASS", out.getvalue())

    def test_below_threshold_exit_1(self):
        # 门槛 = >=14/15 才 PASS：13 条合格必须 exit 1（14+1 失败属达标，不可作反例）。
        path = self._write(json.dumps(corpus(13, fail_n=2)))
        out = io.StringIO()
        with redirect_stdout(out):
            self.assertEqual(q.main(["q04_grader.py", path]), 1)
        self.assertIn("13/15", out.getvalue())
        self.assertIn("验收 FAIL", out.getvalue())

    def test_invalid_json_exit_2(self):
        path = self._write("not json{")
        self.assertEqual(q.main(["q04_grader.py", path]), 2)

    def test_missing_file_exit_2(self):
        self.assertEqual(q.main(["q04_grader.py", "no_such_samples.json"]), 2)

    def test_missing_arg_exit_2(self):
        self.assertEqual(q.main(["q04_grader.py"]), 2)

    def test_utf8_output_through_cp1252_stdout(self):
        # 回归护栏：Windows cp1252 控制台不得让中文报告崩溃（q03_run.py 前车之鉴）。
        path = self._write(json.dumps(corpus(15)))
        buf = io.BytesIO()
        fake = io.TextIOWrapper(buf, encoding="cp1252")
        with redirect_stdout(fake):
            self.assertEqual(q.main(["q04_grader.py", path]), 0)
        fake.flush()
        self.assertIn("PASS".encode("utf-8"), buf.getvalue())

    def test_report_smoke(self):
        text = q.format_report(q.grade_corpus(corpus(14, fail_n=1)))
        self.assertIn("14/15", text)
        self.assertIn("验收 PASS", text)
        review_text = q.format_report(q.grade_corpus(corpus(14, fail_n=0, review_n=1)))
        self.assertIn("NEEDS_REVIEW", review_text)


if __name__ == "__main__":
    unittest.main(verbosity=2)
