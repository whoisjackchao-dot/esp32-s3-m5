// M4 S4.2 —— STATE(0x12) 编解码黄金/边界测试（依据 contracts/M4-STATE快照冻结.md v1.1）。
#include <stdio.h>
#include <string.h>

#include "s3p_state.h"

static int g_fail = 0;
static int g_checks = 0;

static void ck(bool ok, const char *label) {
    ++g_checks;
    if (!ok) {
        ++g_fail;
        printf("FAIL %s\n", label);
    }
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void test_idle_golden(void) {
    s3p_state_snapshot s;
    memset(&s, 0, sizeof(s));
    s.status = S3P_STATUS_IDLE;
    s.flags = S3P_FLAG_CAN_APPEND;
    s.revision = 42;
    s.tokens_input = S3P_STATE_UNKNOWN;
    s.tokens_output = S3P_STATE_UNKNOWN;
    s.cost_milli = S3P_STATE_UNKNOWN;
    s.tokens_reasoning = S3P_STATE_UNKNOWN;
    s.tokens_cache_read = S3P_STATE_UNKNOWN;

    uint8_t buf[S3P_STATE_MAX_LEN];
    const size_t n = s3p_state_encode(&s, buf, sizeof(buf));
    ck(n == S3P_STATE_FIXED_LEN, "idle: length = fixed(31)");
    ck(buf[0] == 0x12 && buf[1] == 2, "idle: msgType/version=2");
    ck(buf[2] == S3P_STATUS_IDLE, "idle: status");
    ck(buf[3] == S3P_FLAG_CAN_APPEND, "idle: flags");
    ck(rd32(buf + 4) == 42, "idle: revision LE");
    ck(rd32(buf + 11) == 0xFFFFFFFFu, "idle: in unknown");
    ck(rd32(buf + 23) == 0xFFFFFFFFu, "idle: reasoning unknown");
    ck(rd32(buf + 27) == 0xFFFFFFFFu, "idle: cacheRead unknown");
}

static void test_roundtrip_full(void) {
    s3p_state_snapshot s;
    memset(&s, 0, sizeof(s));
    s.status = S3P_STATUS_BUSY;
    s.flags = S3P_FLAG_CAN_APPEND | S3P_FLAG_HAS_PROJECT | S3P_FLAG_HAS_TITLE | S3P_FLAG_HAS_MODEL |
              S3P_FLAG_HAS_USAGE | S3P_FLAG_TOOL_FAILED;
    s.revision = 123456;
    s.tool_count = 2;
    s.todo_completed = 2;
    s.todo_total = 3;
    s.tokens_input = 100;
    s.tokens_output = 20;
    s.cost_milli = 120;
    s.tokens_reasoning = 771532u;
    s.tokens_cache_read = 270690560u;
    strcpy(s.project, "stick-s3");
    strcpy(s.title, "会话标题");
    strcpy(s.model, "claude-sonnet-4.5");

    uint8_t buf[S3P_STATE_MAX_LEN];
    const size_t n = s3p_state_encode(&s, buf, sizeof(buf));
    ck(n > S3P_STATE_FIXED_LEN && n <= S3P_STATE_MAX_LEN, "full: length bounds");

    s3p_state_snapshot d;
    ck(s3p_state_decode(buf, n, &d), "full: decode ok");
    ck(d.status == s.status && d.flags == s.flags, "full: status/flags");
    ck(d.revision == s.revision, "full: revision");
    ck(d.tool_count == 2 && d.todo_completed == 2 && d.todo_total == 3, "full: tools/todo");
    ck(d.tokens_input == 100 && d.tokens_output == 20 && d.cost_milli == 120, "full: usage in/out/cost");
    ck(d.tokens_reasoning == 771532u && d.tokens_cache_read == 270690560u,
       "full: usage reasoning/cacheRead");
    ck(rd32(buf + 23) == 771532u && rd32(buf + 27) == 270690560u,
       "full: reasoning/cacheRead LE at 23/27");
    ck(strcmp(d.project, "stick-s3") == 0, "full: project");
    ck(strcmp(d.title, "会话标题") == 0, "full: title (utf8)");
    ck(strcmp(d.model, "claude-sonnet-4.5") == 0, "full: model");
}

static void test_truncation_codepoint_safe(void) {
    s3p_state_snapshot s;
    memset(&s, 0, sizeof(s));
    s.status = S3P_STATUS_IDLE;
    s.flags = S3P_FLAG_HAS_TITLE;
    s.revision = 1;
    for (int i = 0; i < 32; ++i) {
        strncpy(s.title + i * 3, "项", 3);  // 每字 3 字节，共 96 字节
    }
    s.title[96] = '\0';

    uint8_t buf[S3P_STATE_MAX_LEN];
    const size_t n = s3p_state_encode(&s, buf, sizeof(buf));
    ck(n == S3P_STATE_FIXED_LEN + 2u + 30u, "truncate: 10 chars = 30 bytes (<=32)");
    ck(buf[S3P_STATE_FIXED_LEN + 1u] == 30, "truncate: TLV len = 30");

    s3p_state_snapshot d;
    ck(s3p_state_decode(buf, n, &d), "truncate: decode ok");
    ck(strlen(d.title) == 30, "truncate: no partial codepoint");
}

static void test_reject(void) {
    uint8_t buf[S3P_STATE_MAX_LEN];
    s3p_state_snapshot s;
    memset(&s, 0, sizeof(s));
    s.status = S3P_STATUS_IDLE;
    s.revision = 1;
    const size_t n = s3p_state_encode(&s, buf, sizeof(buf));

    uint8_t bad_ver[S3P_STATE_MAX_LEN];
    memcpy(bad_ver, buf, n);
    s3p_state_snapshot d;
    bad_ver[1] = 3;
    ck(!s3p_state_decode(bad_ver, n, &d), "reject: bad version");
    bad_ver[1] = 1;
    ck(!s3p_state_decode(bad_ver, n, &d), "reject: v1(old) rejected under v2");

    ck(!s3p_state_decode(buf, S3P_STATE_FIXED_LEN - 1, &d), "reject: short buffer");

    // TLV 声明长度越界
    uint8_t bad_tlv[64];
    memset(bad_tlv, 0, sizeof(bad_tlv));
    bad_tlv[0] = 0x12; bad_tlv[1] = 1; bad_tlv[3] = S3P_FLAG_HAS_TITLE;
    bad_tlv[S3P_STATE_FIXED_LEN] = 0x02; bad_tlv[S3P_STATE_FIXED_LEN + 1] = 40;  // 声明 40 但无数据
    ck(!s3p_state_decode(bad_tlv, S3P_STATE_FIXED_LEN + 2, &d), "reject: TLV overrun");

    ck(s3p_state_encode(&s, buf, S3P_STATE_FIXED_LEN - 1) == 0, "reject: cap too small");
}

static void test_sentinels(void) {
    s3p_state_snapshot s;
    memset(&s, 0, sizeof(s));
    s.status = S3P_STATUS_UNKNOWN;
    s.revision = 7;
    s.tokens_input = S3P_STATE_UNKNOWN;
    s.tokens_output = S3P_STATE_NOT_REPORTED;
    s.cost_milli = 0;  // 有依据的 0
    s.tokens_reasoning = S3P_STATE_NOT_REPORTED;
    s.tokens_cache_read = 0;

    uint8_t buf[S3P_STATE_MAX_LEN];
    const size_t n = s3p_state_encode(&s, buf, sizeof(buf));
    s3p_state_snapshot d;
    ck(s3p_state_decode(buf, n, &d), "sentinel: decode ok");
    ck(d.tokens_input == S3P_STATE_UNKNOWN, "sentinel: unknown");
    ck(d.tokens_output == S3P_STATE_NOT_REPORTED, "sentinel: not_reported");
    ck(d.cost_milli == 0, "sentinel: known zero stays zero");
    ck(d.tokens_reasoning == S3P_STATE_NOT_REPORTED, "sentinel: reasoning not_reported");
    ck(d.tokens_cache_read == 0, "sentinel: cacheRead known zero stays zero");
}

int main(void) {
    test_idle_golden();
    test_roundtrip_full();
    test_truncation_codepoint_safe();
    test_reject();
    test_sentinels();
    printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
