/* s3p_reasm 单元测试（M2 Selective TDD：RED 先行）
 * 用例 ID 对应测试矩阵 §4：BLE-05/06/07/08/09/12/13/14/15 + C04 容量
 * 构建运行（Windows/MSVC）：
 *   vcvars64 && cl /nologo /W4 /Fe:test_reasm.exe s3p_reasm_test.c s3p_reasm.c && test_reasm.exe
 */
#include "s3p_reasm.h"

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

/* 固定容量静态缓冲（真实主机侧亦为静态分配，BLE-15 显式失败） */
static uint8_t g_buf[128 * 1024];

static void fill_payload(uint8_t *p, uint8_t seed, size_t len) {
    for (size_t i = 0; i < len; i += 1) p[i] = (uint8_t)(seed + (int)i);
}

int main(void) {
    /* BLE-05：500 帧顺序到达 → 完整，总字节正确，无重复 */
    TEST("BLE-05: 500 frames in order");
    {
        s3p_reasm_t *r = s3p_reasm_create(g_buf, sizeof(g_buf));
        CHECK(s3p_reasm_begin(r, 1u, 500u, 16000u) == S3P_REASM_OK);
        uint8_t payload[8];
        for (uint32_t seq = 0; seq < 500u; seq += 1) {
            fill_payload(payload, (uint8_t)seq, sizeof(payload));
            CHECK(s3p_reasm_feed(r, 1u, seq, 0u, 1u, payload, sizeof(payload)) == S3P_REASM_OK);
        }
        CHECK(s3p_reasm_received_frames(r) == 500u);
        CHECK(s3p_reasm_total_bytes(r) == 500u * 8u);
        CHECK(s3p_reasm_is_complete(r));
        s3p_reasm_destroy(r);
    }

    /* BLE-05/序号：frameSeq 超出宣告范围 → 拒绝（协议 violation） */
    TEST("BLE-13: seq overflow rejected");
    {
        s3p_reasm_t *r = s3p_reasm_create(g_buf, sizeof(g_buf));
        CHECK(s3p_reasm_begin(r, 1u, 10u, 16000u) == S3P_REASM_OK);
        uint8_t payload[4] = {1, 2, 3, 4};
        CHECK(s3p_reasm_feed(r, 1u, 10u, 0u, 1u, payload, sizeof(payload)) == S3P_REASM_SEQ_OVERFLOW);
        CHECK(s3p_reasm_feed(r, 1u, 11u, 0u, 1u, payload, sizeof(payload)) == S3P_REASM_SEQ_OVERFLOW);
        s3p_reasm_destroy(r);
    }

    /* BLE-07：同片重复 → 忽略，字节数与帧数不变 */
    TEST("BLE-07: duplicate fragment ignored");
    {
        s3p_reasm_t *r = s3p_reasm_create(g_buf, sizeof(g_buf));
        CHECK(s3p_reasm_begin(r, 1u, 4u, 16000u) == S3P_REASM_OK);
        uint8_t payload[4] = {5, 5, 5, 5};
        CHECK(s3p_reasm_feed(r, 1u, 0u, 0u, 1u, payload, sizeof(payload)) == S3P_REASM_OK);
        CHECK(s3p_reasm_feed(r, 1u, 0u, 0u, 1u, payload, sizeof(payload)) == S3P_REASM_DUPLICATE);
        CHECK(s3p_reasm_received_frames(r) == 1u);
        CHECK(s3p_reasm_total_bytes(r) == 4u);
        s3p_reasm_destroy(r);
    }

    /* BLE-08：同 (seq,frag) 内容冲突 → 整段失败 */
    TEST("BLE-08: conflicting fragment fails recording");
    {
        s3p_reasm_t *r = s3p_reasm_create(g_buf, sizeof(g_buf));
        CHECK(s3p_reasm_begin(r, 1u, 4u, 16000u) == S3P_REASM_OK);
        uint8_t p1[4] = {1, 1, 1, 1};
        uint8_t p2[4] = {2, 2, 2, 2};
        CHECK(s3p_reasm_feed(r, 1u, 0u, 0u, 1u, p1, sizeof(p1)) == S3P_REASM_OK);
        CHECK(s3p_reasm_feed(r, 1u, 0u, 0u, 1u, p2, sizeof(p2)) == S3P_REASM_CONFLICT);
        CHECK(s3p_reasm_state(r) == S3P_REASM_STATE_FAILED);
        s3p_reasm_destroy(r);
    }

    /* BLE-09：乱序到达 → 仍完整 */
    TEST("BLE-09: out-of-order frames reassemble");
    {
        s3p_reasm_t *r = s3p_reasm_create(g_buf, sizeof(g_buf));
        CHECK(s3p_reasm_begin(r, 1u, 3u, 16000u) == S3P_REASM_OK);
        uint8_t p[4] = {0, 0, 0, 0};
        for (int pass = 0; pass < 3; pass += 1) {
            const uint32_t order[3] = {2u, 0u, 1u};
            p[0] = (uint8_t)order[pass];
            CHECK(s3p_reasm_feed(r, 1u, order[pass], 0u, 1u, p, sizeof(p)) == S3P_REASM_OK);
        }
        CHECK(s3p_reasm_is_complete(r));
        /* 重组缓冲按 frameSeq 有序：第一字节应为 seq 0 的载荷 */
        CHECK(s3p_reasm_read_frame(r, 0, p, sizeof(p)) == 4 && p[0] == 0);
        s3p_reasm_destroy(r);
    }

    /* BLE-06/11：缺片 + END 后 FRAME_DRAIN_TIMEOUT 到期 → 整段失败 */
    TEST("BLE-06/11: missing fragment fails after drain timeout");
    {
        s3p_reasm_t *r = s3p_reasm_create(g_buf, sizeof(g_buf));
        uint8_t payload[4] = {9, 9, 9, 9};
        CHECK(s3p_reasm_begin(r, 1u, 3u, 16000u) == S3P_REASM_OK);
        CHECK(s3p_reasm_feed(r, 1u, 0u, 0u, 1u, payload, sizeof(payload)) == S3P_REASM_OK);
        CHECK(!s3p_reasm_is_complete(r));
        CHECK(s3p_reasm_end(r, 1u, 3u, 3u * 960u, 0u /*summary 由上层校验*/) == S3P_REASM_WAITING);
        CHECK(!s3p_reasm_is_complete(r));
        /* 时钟推进超过 FRAME_DRAIN_TIMEOUT（2000ms） */
        CHECK(s3p_reasm_on_timeout(r, S3P_FRAME_DRAIN_TIMEOUT_MS + 1) == S3P_REASM_FAILED);
        s3p_reasm_destroy(r);
    }

    /* BLE-12：END 声明的帧数/样本数与实际不符 → 失败（不静默截短） */
    TEST("BLE-12: END count mismatch fails");
    {
        s3p_reasm_t *r = s3p_reasm_create(g_buf, sizeof(g_buf));
        uint8_t payload[4] = {7, 7, 7, 7};
        CHECK(s3p_reasm_begin(r, 1u, 3u, 16000u) == S3P_REASM_OK);
        for (uint32_t seq = 0; seq < 3u; seq += 1) {
            CHECK(s3p_reasm_feed(r, 1u, seq, 0u, 1u, payload, sizeof(payload)) == S3P_REASM_OK);
        }
        /* 声称 5 帧实际 3 帧 → 完成判定失败并转 FAILED */
        CHECK(s3p_reasm_end(r, 1u, 5u, 5u * 960u, 0u) == S3P_REASM_FAILED);
        s3p_reasm_destroy(r);
    }

    /* M2 契约语义（冻结 §3.2）：AUDIO 载荷为 Opus 字节，totalSamples 为 16kHz
     * 采样数——两者不同单位，end() 不得做字节/样本对账（TX-18 由 ASR 层校验） */
    TEST("M2: opus payload bytes not reconciled against totalSamples");
    {
        s3p_reasm_t *r = s3p_reasm_create(g_buf, sizeof(g_buf));
        uint8_t payload[137] = {0x5A}; /* 模拟 Opus 帧载荷 */
        CHECK(s3p_reasm_begin(r, 1u, 3u, 16000u) == S3P_REASM_OK);
        for (uint32_t seq = 0; seq < 3u; seq += 1) {
            CHECK(s3p_reasm_feed(r, 1u, seq, 0u, 1u, payload, sizeof(payload)) == S3P_REASM_OK);
        }
        /* totalSamples=2880（采样）≠ append_pos(411B)/2 —— 契约下必须 COMPLETE */
        CHECK(s3p_reasm_end(r, 1u, 3u, 3u * 960u, 0u) == S3P_REASM_OK);
        CHECK(s3p_reasm_is_complete(r));
        s3p_reasm_destroy(r);
    }

    /* BLE-14：不属于当前 recording 的包 → 丢弃 */
    TEST("BLE-14: foreign recording id dropped");
    {
        s3p_reasm_t *r = s3p_reasm_create(g_buf, sizeof(g_buf));
        CHECK(s3p_reasm_begin(r, 1u, 4u, 16000u) == S3P_REASM_OK);
        uint8_t payload[4] = {3, 3, 3, 3};
        CHECK(s3p_reasm_feed(r, 999u, 0u, 0u, 1u, payload, sizeof(payload)) == S3P_REASM_FOREIGN);
        CHECK(s3p_reasm_received_frames(r) == 0u);
        s3p_reasm_destroy(r);
    }

    /* BLE-15：容量耗尽 → 显式失败，不静默丢弃 */
    TEST("BLE-15: capacity exhausted fails explicitly");
    {
        static uint8_t small[4096];
        s3p_reasm_t *r = s3p_reasm_create(small, sizeof(small));
        CHECK(s3p_reasm_begin(r, 1u, 64u, 16000u) == S3P_REASM_OK);
        uint8_t payload[256];
        memset(payload, 0xAA, sizeof(payload));
        int saw_capacity = 0;
        for (uint32_t seq = 0; seq < 64u; seq += 1) {
            const int rc = s3p_reasm_feed(r, 1u, seq, 0u, 1u, payload, sizeof(payload));
            if (rc == S3P_REASM_CAPACITY) {
                saw_capacity = 1;
                break;
            }
            CHECK(rc == S3P_REASM_OK);
        }
        CHECK(saw_capacity);
        CHECK(s3p_reasm_state(r) == S3P_REASM_STATE_FAILED);
        s3p_reasm_destroy(r);
    }

    /* C04/会话：未 begin 就 feed → 无会话错误；新 recording 需重新 begin */
    TEST("session: feed without begin rejected");
    {
        s3p_reasm_t *r = s3p_reasm_create(g_buf, sizeof(g_buf));
        uint8_t payload[4] = {1, 2, 3, 4};
        CHECK(s3p_reasm_feed(r, 1u, 0u, 0u, 1u, payload, sizeof(payload)) == S3P_REASM_NO_SESSION);
        CHECK(s3p_reasm_begin(r, 1u, 4u, 16000u) == S3P_REASM_OK);
        CHECK(s3p_reasm_feed(r, 1u, 0u, 0u, 1u, payload, sizeof(payload)) == S3P_REASM_OK);
        /* 重新 begin（新 recording）→ 旧数据清空 */
        CHECK(s3p_reasm_begin(r, 2u, 4u, 16000u) == S3P_REASM_OK);
        CHECK(s3p_reasm_received_frames(r) == 0u);
        s3p_reasm_destroy(r);
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
