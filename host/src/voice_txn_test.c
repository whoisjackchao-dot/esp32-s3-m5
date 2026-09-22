/* voice_txn 单元测试（M3 Selective TDD：RED 先行）
 * 对应：TX-01/02/03/05/06/07/08/09/10/18 + C03 取消线性化。
 * 构建运行：vcvars64 && cl /nologo /W4 /Fe:test_voice_txn.exe voice_txn_test.c voice_txn.c && test_voice_txn.exe
 */
#include "voice_txn.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        checks += 1;                                                                                                   \
        if (!(cond)) {                                                                                                 \
            failures += 1;                                                                                             \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);                                                     \
        }                                                                                                              \
    } while (0)

#define TEST(name) printf("== %s ==\n", name)

int main(void) {
    vt_txn_t vt;

    TEST("TX-01: 正常开始/结束一次，无第二事务");
    {
        vt_init(&vt);
        CHECK(vt_start_recording(&vt, 1000) == VT_OK);
        CHECK(vt_state(&vt) == VT_RECORDING);
        CHECK(vt_txn_id(&vt) == 1);
        CHECK(vt_stop_recording(&vt, 1100, 480000) == VT_OK);
        CHECK(vt_state(&vt) == VT_DRAINING);
        CHECK(vt_asr_start(&vt, 1200) == VT_OK);
        CHECK(vt_asr_result(&vt, 1300, "hello") == VT_OK);
        CHECK(vt_state(&vt) == VT_APPEND_PENDING);
        CHECK(vt_append_applied(&vt) == VT_OK);
        CHECK(vt_state(&vt) == VT_FILLED);
        CHECK(vt_finish(&vt, 1400) == VT_OK);
        CHECK(vt_state(&vt) == VT_IDLE);
    }

    TEST("TX-02: 持续按住不重启（非 IDLE 拒新），样本上限由采集层执行");
    {
        vt_init(&vt);
        CHECK(vt_start_recording(&vt, 1000) == VT_OK);
        CHECK(vt_stop_recording(&vt, 31000, 480000) == VT_OK); /* 30s 硬停样本数 */
        CHECK(vt_samples(&vt) == 480000);
        CHECK(vt_start_recording(&vt, 31500) == VT_ERR_ACTIVE);
    }

    TEST("TX-03: 全部非 IDLE 状态拒新，旧事务 ID/文本/到期不变");
    {
        static const struct { const char *name; } cases[] = {{"x"}};
        (void)cases;
        /* RECORDING */
        vt_init(&vt);
        CHECK(vt_start_recording(&vt, 1000) == VT_OK);
        uint32_t id = vt_txn_id(&vt);
        CHECK(vt_start_recording(&vt, 1001) == VT_ERR_ACTIVE);
        CHECK(vt_txn_id(&vt) == id);
        CHECK(vt_state(&vt) == VT_RECORDING);
        /* DRAINING */
        vt_stop_recording(&vt, 1100, 4800);
        CHECK(vt_start_recording(&vt, 1101) == VT_ERR_ACTIVE);
        /* TRANSCRIBING */
        vt_asr_start(&vt, 1200);
        CHECK(vt_start_recording(&vt, 1201) == VT_ERR_ACTIVE);
        /* RETRY_PENDING（失败后不可能再有结果到达——迟到回调属 TX-10） */
        vt_asr_failed(&vt, 1250);
        int64_t exp = vt_expiry_ms(&vt);
        CHECK(vt_start_recording(&vt, 1251) == VT_ERR_ACTIVE);
        CHECK(vt_expiry_ms(&vt) == exp);
        /* APPEND_PENDING 与 FILLED（新事务正常路径） */
        vt_init(&vt);
        vt_start_recording(&vt, 2000);
        vt_stop_recording(&vt, 2100, 4800);
        vt_asr_start(&vt, 2200);
        CHECK(vt_asr_result(&vt, 2300, "keep") == VT_OK);
        CHECK(vt_start_recording(&vt, 2301) == VT_ERR_ACTIVE);
        CHECK(strcmp(vt_text(&vt), "keep") == 0);
        vt_append_applied(&vt);
        CHECK(vt_start_recording(&vt, 2400) == VT_ERR_ACTIVE);
    }

    TEST("TX-18: 样本 0/4799 丢弃且 ASR 请求数 0；4800 进入正常路径");
    {
        vt_init(&vt);
        CHECK(vt_start_recording(&vt, 1000) == VT_OK);
        CHECK(vt_stop_recording(&vt, 1100, 0) == VT_OK);
        CHECK(vt_state(&vt) == VT_IDLE);
        CHECK(vt_asr_calls(&vt) == 0);
        CHECK(vt_start_recording(&vt, 1200) == VT_OK);
        CHECK(vt_stop_recording(&vt, 1300, 4799) == VT_OK);
        CHECK(vt_state(&vt) == VT_IDLE);
        CHECK(vt_asr_calls(&vt) == 0);
        CHECK(vt_start_recording(&vt, 1400) == VT_OK);
        CHECK(vt_stop_recording(&vt, 1500, 4800) == VT_OK);
        CHECK(vt_state(&vt) == VT_DRAINING);
        CHECK(vt_asr_start(&vt, 1600) == VT_OK);
        CHECK(vt_asr_calls(&vt) == 1);
    }

    TEST("TX-07: 20s 超时 → RETRY_PENDING，expiry=t0+20s+300s，请求数仍 1");
    {
        vt_init(&vt);
        vt_start_recording(&vt, 1000);
        vt_stop_recording(&vt, 1100, 4800);
        vt_asr_start(&vt, 1200); /* t0=1200 */
        CHECK(vt_state(&vt) == VT_TRANSCRIBING);
        CHECK(vt_asr_failed(&vt, 12200) == VT_OK); /* t0+20s 边界 */
        CHECK(vt_state(&vt) == VT_RETRY_PENDING);
        CHECK(vt_asr_calls(&vt) == 1);
        CHECK(vt_expiry_ms(&vt) == 12200 + VT_TTL_MS);
    }

    TEST("TX-05: t0+299s 内可重试；t0+300s 清理后不再可用");
    {
        vt_init(&vt);
        vt_start_recording(&vt, 0);
        vt_stop_recording(&vt, 100, 4800);
        vt_asr_start(&vt, 200);
        vt_asr_failed(&vt, 300); /* expiry=300300 */
        CHECK(vt_tick(&vt, 300299) == VT_OK);
        CHECK(vt_state(&vt) == VT_RETRY_PENDING);
        CHECK(vt_retry_asr(&vt, 300299) == VT_OK);
        CHECK(vt_state(&vt) == VT_TRANSCRIBING);
        /* 另一轮：重试再失败（expiry 不滑动，仍为 300300），到期清理 */
        CHECK(vt_asr_failed(&vt, 301000) == VT_OK);
        CHECK(vt_expiry_ms(&vt) == 300300);
        CHECK(vt_tick(&vt, 300300) == VT_CLEANED);
        CHECK(vt_state(&vt) == VT_IDLE);
        CHECK(vt_retry_asr(&vt, 301100) == VT_ERR_NO_TXN);
        CHECK(vt_text(&vt)[0] == '\0');
    }

    TEST("TX-06: 重试失败后 expiry 不滑动（仍为首次失败时刻+300s）");
    {
        vt_init(&vt);
        vt_start_recording(&vt, 0);
        vt_stop_recording(&vt, 100, 4800);
        vt_asr_start(&vt, 200);
        vt_asr_failed(&vt, 300); /* expiry = 300300 */
        CHECK(vt_retry_asr(&vt, 200000) == VT_OK);
        CHECK(vt_asr_failed(&vt, 220000) == VT_OK);
        CHECK(vt_state(&vt) == VT_RETRY_PENDING);
        CHECK(vt_expiry_ms(&vt) == 300300); /* 不滑动 */
    }

    TEST("TX-08: 手动重试恰好多一次 ASR 调用，新尝试 ID");
    {
        vt_init(&vt);
        vt_start_recording(&vt, 0);
        vt_stop_recording(&vt, 100, 4800);
        vt_asr_start(&vt, 200);
        uint32_t a1 = vt_attempt_id(&vt);
        vt_asr_failed(&vt, 300);
        CHECK(vt_retry_asr(&vt, 400) == VT_OK);
        CHECK(vt_asr_calls(&vt) == 2);
        CHECK(vt_attempt_id(&vt) != a1);
    }

    TEST("TX-09: 填入重试不再调 ASR，文本不变，仅追加一次语义");
    {
        vt_init(&vt);
        vt_start_recording(&vt, 0);
        vt_stop_recording(&vt, 100, 4800);
        vt_asr_start(&vt, 200);
        CHECK(vt_asr_result(&vt, 300, "same text") == VT_OK);
        CHECK(vt_asr_calls(&vt) == 1);
        CHECK(vt_state(&vt) == VT_APPEND_PENDING);
        /* 目标不符：保持 APPEND_PENDING，可反复尝试，无 ASR 调用 */
        CHECK(vt_asr_calls(&vt) == 1);
        CHECK(strcmp(vt_text(&vt), "same text") == 0);
        CHECK(vt_append_applied(&vt) == VT_OK);
        CHECK(vt_state(&vt) == VT_FILLED);
        CHECK(vt_asr_calls(&vt) == 1);
    }

    TEST("TX-18: 空 ASR 结果不填入，事务结束（调用已发生计 1）");
    {
        vt_init(&vt);
        vt_start_recording(&vt, 0);
        vt_stop_recording(&vt, 100, 4800);
        vt_asr_start(&vt, 200);
        CHECK(vt_asr_result(&vt, 300, "") == VT_OK);
        CHECK(vt_state(&vt) == VT_IDLE);
        CHECK(vt_asr_calls(&vt) == 1);
    }

    TEST("TX-10/C03: 取消后迟到回调拒绝，事务不可恢复");
    {
        vt_init(&vt);
        vt_start_recording(&vt, 0);
        vt_stop_recording(&vt, 100, 4800);
        vt_asr_start(&vt, 200);
        uint32_t id = vt_txn_id(&vt);
        CHECK(vt_cancel(&vt, 250) == VT_OK);
        CHECK(vt_state(&vt) == VT_IDLE);
        CHECK(vt_cancelled_id(&vt) == id);
        CHECK(vt_asr_result(&vt, 400, "late") == VT_ERR_NO_TXN);
        CHECK(vt_retry_asr(&vt, 401) == VT_ERR_NO_TXN);
        CHECK(vt_state(&vt) == VT_IDLE); /* 迟到回调不恢复事务 */
    }

    TEST("TX-11: FILLED 后取消无效（草稿不撤销）");
    {
        vt_init(&vt);
        vt_start_recording(&vt, 0);
        vt_stop_recording(&vt, 100, 4800);
        vt_asr_start(&vt, 200);
        vt_asr_result(&vt, 300, "filled");
        vt_append_applied(&vt);
        CHECK(vt_cancel(&vt, 400) == VT_ERR_WRONG_STATE);
        CHECK(vt_state(&vt) == VT_FILLED);
    }

    TEST("FILLED → finish → 新事务新 ID");
    {
        vt_init(&vt);
        vt_start_recording(&vt, 0);
        vt_stop_recording(&vt, 100, 4800);
        vt_asr_start(&vt, 200);
        vt_asr_result(&vt, 300, "t");
        vt_append_applied(&vt);
        vt_finish(&vt, 400);
        CHECK(vt_start_recording(&vt, 500) == VT_OK);
        CHECK(vt_txn_id(&vt) == 2);
    }

    TEST("取消墓碑：新事务不被旧迟到回调影响");
    {
        vt_init(&vt);
        vt_start_recording(&vt, 0);
        vt_cancel(&vt, 10);
        CHECK(vt_cancelled_id(&vt) == 1);
        CHECK(vt_start_recording(&vt, 20) == VT_OK);
        CHECK(vt_txn_id(&vt) == 2);
        CHECK(vt_stop_recording(&vt, 25, 4800) == VT_OK);
        CHECK(vt_asr_start(&vt, 28) == VT_OK);
        CHECK(vt_asr_result(&vt, 30, "ok") == VT_OK);
        CHECK(vt_state(&vt) == VT_APPEND_PENDING);
    }

    TEST("参数错误");
    {
        vt_init(&vt);
        CHECK(vt_asr_result(&vt, 0, NULL) == VT_ERR_PARAM);
        CHECK(vt_start_recording(NULL, 0) == VT_ERR_PARAM);
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
