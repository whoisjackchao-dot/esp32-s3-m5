// cost_scale_test.c —— cost(浮点) → 毫单位 换算回归测试。
// 契约：cost_milli = 费用 × 1000（四舍五入），负值归 0，超上限夹到哨兵下方。
#include "cost_scale.h"
#include <stdio.h>

static int failures = 0;

static void expect_u32(const char* what, uint32_t got, uint32_t want) {
    if (got == want) {
        printf("ok   %-30s %lu\n", what, (unsigned long)got);
    } else {
        printf("FAIL %-30s got=%lu want=%lu\n", what, (unsigned long)got, (unsigned long)want);
        ++failures;
    }
}

int main(void) {
    expect_u32("cost 1.8468997800000002", cost_to_milli(1.8468997800000002), 1847u);
    expect_u32("cost 0.0 -> 0", cost_to_milli(0.0), 0u);
    expect_u32("cost 1.0 -> 1000", cost_to_milli(1.0), 1000u);
    expect_u32("cost 0.0004 -> 0", cost_to_milli(0.0004), 0u);
    expect_u32("cost 0.0006 -> 1", cost_to_milli(0.0006), 1u);
    expect_u32("cost negative -> 0", cost_to_milli(-2.5), 0u);
    expect_u32("cost huge -> clamp", cost_to_milli(4294968.0), S3P_STATE_NOT_REPORTED - 1u);
    printf("%s: %d failure(s)\n", failures ? "RED" : "GREEN", failures);
    return failures ? 1 : 0;
}
