// M4 S4.2 —— BR-03 → STATE 合并映射测试。
#include <stdio.h>
#include <string.h>

#include "state_merge.h"

static int g_fail = 0;
static int g_checks = 0;
static void ck(bool ok, const char *label) {
    ++g_checks;
    if (!ok) {
        ++g_fail;
        printf("FAIL %s\n", label);
    }
}

static br03_snapshot base(void) {
    br03_snapshot in;
    memset(&in, 0, sizeof(in));
    in.status = S3P_STATUS_IDLE;
    in.can_append = 1;
    in.revision = 9;
    in.tokens_input = S3P_STATE_UNKNOWN;
    in.tokens_output = S3P_STATE_UNKNOWN;
    in.cost_milli = S3P_STATE_UNKNOWN;
    in.tokens_reasoning = S3P_STATE_UNKNOWN;
    in.tokens_cache_read = S3P_STATE_UNKNOWN;
    return in;
}

static void test_minimal(void) {
    br03_snapshot in = base();
    s3p_state_snapshot out;
    state_merge(&out, &in);
    ck(out.status == S3P_STATUS_IDLE, "minimal: status");
    ck((out.flags & S3P_FLAG_CAN_APPEND) != 0, "minimal: canAppend set");
    ck((out.flags & S3P_FLAG_HAS_USAGE) == 0, "minimal: no usage flag");
    ck(out.revision == 9, "minimal: revision");
    ck(out.project[0] == '\0' && out.title[0] == '\0', "minimal: no texts");
}

static void test_full(void) {
    br03_snapshot in = base();
    in.status = S3P_STATUS_ERROR;
    in.expired = 1;
    in.tool_failed = 1;
    in.child_busy = 1;
    in.tool_count = 2;
    in.todo_known = 1;
    in.todo_completed = 2;
    in.todo_total = 3;
    in.tokens_input = 100;
    in.tokens_output = S3P_STATE_NOT_REPORTED;
    in.cost_milli = 0;  // 有依据的 0
    in.tokens_reasoning = 771532;
    in.tokens_cache_read = 270690560u;
    in.project = "stick-s3";
    in.title = "会话标题";
    in.model = "claude-sonnet-4.5";
    s3p_state_snapshot out;
    state_merge(&out, &in);
    ck((out.flags & S3P_FLAG_EXPIRED) != 0, "full: expired");
    ck((out.flags & S3P_FLAG_TOOL_FAILED) != 0, "full: toolFailed");
    ck((out.flags & S3P_FLAG_CHILD_BUSY) != 0, "full: childBusy");
    ck((out.flags & S3P_FLAG_HAS_USAGE) != 0, "full: usage flag (cost 0 is known)");
    ck((out.flags & S3P_FLAG_HAS_PROJECT) != 0, "full: hasProject");
    ck((out.flags & S3P_FLAG_HAS_TITLE) != 0, "full: hasTitle");
    ck((out.flags & S3P_FLAG_HAS_MODEL) != 0, "full: hasModel");
    ck(strcmp(out.project, "stick-s3") == 0, "full: project text");
    ck(strcmp(out.model, "claude-sonnet-4.5") == 0, "full: model text");
    ck(out.tool_count == 2 && out.todo_completed == 2 && out.todo_total == 3, "full: tools/todo");
    ck(out.tokens_input == 100 && out.tokens_output == S3P_STATE_NOT_REPORTED, "full: usage sentinels");
    ck(out.tokens_reasoning == 771532u && out.tokens_cache_read == 270690560u,
       "full: reasoning/cacheRead mapped");
    // 端到端：合并 → 编码 → 解码
    uint8_t buf[S3P_STATE_MAX_LEN];
    const size_t n = s3p_state_encode(&out, buf, sizeof(buf));
    s3p_state_snapshot d;
    memset(&d, 0, sizeof(d));
    ck(n > 0 && s3p_state_decode(buf, n, &d), "full: merge->encode->decode");
    ck(d.status == S3P_STATUS_ERROR && strcmp(d.project, "stick-s3") == 0, "full: roundtrip fields");
    ck(d.tokens_reasoning == 771532u && d.tokens_cache_read == 270690560u,
       "full: reasoning/cacheRead roundtrip");
}

static void test_todo_unknown(void) {
    br03_snapshot in = base();
    in.todo_known = 0;
    in.todo_completed = 5;  // 应被忽略
    in.todo_total = 9;
    s3p_state_snapshot out;
    state_merge(&out, &in);
    ck(out.todo_completed == 0 && out.todo_total == 0, "todo unknown -> 0/0");
}

static void test_title_utf8_clip(void) {
    br03_snapshot in = base();
    in.title = "\xe4\xb8\x80\xe4\xba\x8c\xe4\xb8\x89\xe5\x9b\x9b\xe4\xba\x94\xe5\x85\xad\xe4\xb8\x83\xe5\x85\xab\xe4\xb9\x9d\xe5\x8d\x81\xe5\x8d\x81\xe4\xb8\x80";  // 11 CJK = 33 bytes (> 32)
    s3p_state_snapshot out;
    state_merge(&out, &in);
    ck(strlen(out.title) == 30, "utf8: clipped to 10 CJK chars (30 bytes), not 32");
    const unsigned char last = (unsigned char)out.title[29];
    ck((last & 0xC0u) == 0x80u, "utf8: no split multibyte char at boundary");
}

int main(void) {
    test_minimal();
    test_full();
    test_todo_unknown();
    test_title_utf8_clip();
    printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
