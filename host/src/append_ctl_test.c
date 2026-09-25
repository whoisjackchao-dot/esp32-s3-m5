// append_ctl 单元测试（M4/S4.7d）：draft.append 请求构造与结果分类（纯逻辑）。
#include <stdio.h>
#include <string.h>

#include "append_ctl.h"

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
    char buf[512];
    const size_t n = append_build_request("ep", "ses_a", "op_1", "hello", buf, sizeof(buf));
    ck(n > 0, "build ok");
    ck(strstr(buf, "\"protocolVersion\":1") != NULL, "has protocolVersion");
    ck(strstr(buf, "\"tuiEpoch\":\"ep\"") != NULL, "has epoch");
    ck(strstr(buf, "\"sessionID\":\"ses_a\"") != NULL, "has session");
    ck(strstr(buf, "\"operationID\":\"op_1\"") != NULL, "has op");
    ck(strstr(buf, "\"transactionGeneration\":1") != NULL, "has gen");
    ck(strstr(buf, "\"text\":\"hello\"") != NULL, "has text");

    ck(append_build_request("e", "s", "o", "a\"b\\c\nd", buf, sizeof(buf)) > 0 &&
           strstr(buf, "a\\\"b\\\\c\\nd") != NULL,
       "escapes quote/backslash/newline");
    ck(append_build_request("e", "s", "o", "x", buf, 4) == 0, "cap too small -> 0");

    ck(append_classify(200, "{\"status\":\"applied\"}", strlen("{\"status\":\"applied\"}")) ==
           APPEND_APPLIED,
       "applied");
    ck(append_classify(202, "{\"status\":\"processing\"}", strlen("{\"status\":\"processing\"}")) ==
           APPEND_PROCESSING,
       "processing");
    ck(append_classify(200, "{\"status\":\"uncertain\"}", strlen("{\"status\":\"uncertain\"}")) ==
           APPEND_UNCERTAIN,
       "uncertain");
    ck(append_classify(409, "{\"status\":\"rejected\",\"reason\":\"conflict\"}",
                       strlen("{\"status\":\"rejected\",\"reason\":\"conflict\"}")) == APPEND_REJECTED,
       "rejected");
    ck(append_classify(500, "", 0) == APPEND_REJECTED, "empty -> rejected");

    printf("%d checks, %d failures\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
