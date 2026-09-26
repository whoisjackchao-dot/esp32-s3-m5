// http_accept_policy_test.c —— accept() 错误分级策略回归测试。
// 断言：瞬时网络错误不得终止控制端点；只有「套接字不可用」类错误才终止。
// 这样即使真实瞬时错误无法在受控环境重现，规则本身仍被锁定（防回归）。
#include "http_accept_policy.h"
#include <stdio.h>

static int failures = 0;

static void expect(const char* what, int got, int want) {
    if (got == want) {
        printf("ok   %-26s fatal=%d\n", what, got);
    } else {
        printf("FAIL %-26s fatal=%d want=%d\n", what, got, want);
        ++failures;
    }
}

int main(void) {
    /* 瞬时：连接被重置 / 被信号打断 / 超时 / 缓冲不足 / 非阻塞无数据 → 必须继续 */
    expect("WSAECONNRESET transient", http_accept_error_is_fatal(WSAECONNRESET), 0);
    expect("WSAEINTR transient", http_accept_error_is_fatal(WSAEINTR), 0);
    expect("WSAETIMEDOUT transient", http_accept_error_is_fatal(WSAETIMEDOUT), 0);
    expect("WSAENOBUFS transient", http_accept_error_is_fatal(WSAENOBUFS), 0);
    expect("WSAEWOULDBLOCK transient", http_accept_error_is_fatal(WSAEWOULDBLOCK), 0);
    /* 致命：监听套接字已失效 / 未处于监听态 / Winsock 栈已死 → 终止 */
    expect("WSAENOTSOCK fatal", http_accept_error_is_fatal(WSAENOTSOCK), 1);
    expect("WSAEINVAL fatal", http_accept_error_is_fatal(WSAEINVAL), 1);
    expect("WSAENETDOWN fatal", http_accept_error_is_fatal(WSAENETDOWN), 1);
    printf("%s: %d failure(s)\n", failures ? "RED" : "GREEN", failures);
    return failures ? 1 : 0;
}
