// retry_store 单元测试（M4/S4.7d）：重试资格与 TTL（纯逻辑）。
#include <stdio.h>

#include "retry_store.h"

static int g_fail = 0;
static int g_checks = 0;
static void ck(bool ok, const char *label) {
    ++g_checks;
    if (!ok) {
        ++g_fail;
        printf("FAIL %s\n", label);
    }
}

int main(void) {
    retry_store s;
    retry_store_init(&s);
    ck(!retry_can_recognize(&s, 1000), "empty: no recognize");
    ck(!retry_can_append(&s, 1000), "empty: no append");

    const uint32_t t0 = 1000000u;
    retry_store_note(&s, RETRY_FAILED, true, false, "ses_a", true, t0);
    ck(retry_can_recognize(&s, t0), "failed+audio: recognize");
    ck(!retry_can_append(&s, t0), "failed: no append");

    retry_store_note(&s, RETRY_REJECTED, false, true, "ses_a", true, t0);
    ck(retry_can_append(&s, t0), "rejected+text+target: append");
    ck(!retry_can_recognize(&s, t0), "rejected w/o audio: no recognize");

    retry_store_note(&s, RETRY_APPLIED, false, true, "ses_a", true, t0);
    ck(!retry_can_append(&s, t0), "applied: no append");

    retry_store_note(&s, RETRY_UNCERTAIN, false, true, "ses_a", true, t0);
    ck(!retry_can_append(&s, t0), "uncertain: no auto append");

    retry_store_note(&s, RETRY_REJECTED, false, true, "ses_a", false, t0);
    ck(!retry_can_append(&s, t0), "invalid target: no append");

    retry_store_note(&s, RETRY_FAILED, true, false, "ses_a", true, t0);
    ck(retry_can_recognize(&s, t0 + RETRY_TTL_MS), "ttl boundary inclusive");
    ck(!retry_can_recognize(&s, t0 + RETRY_TTL_MS + 1u), "ttl expired");

    retry_store_note(&s, RETRY_REJECTED, false, true, "ses_x", true, t0);
    ck(retry_can_append(&s, t0), "target copied: append");
    retry_store_expire(&s, t0 + RETRY_TTL_MS + 1u);
    ck(!retry_can_append(&s, t0 + RETRY_TTL_MS + 1u), "expire clears");

    printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
