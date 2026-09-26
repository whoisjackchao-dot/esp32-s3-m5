#pragma once
// M4 —— 控制端点 accept() 错误分级策略（纯 C，可单测）。
// 契约：accept() 返回 INVALID_SOCKET 时，必须区分
//   - 瞬时网络错误（连接被重置/被打断/超时/缓冲不足…）→ 记录错误并继续服务；
//   - 套接字本身不可用（已关闭/未处于监听态…）→ 才允许终止 accept 循环。
// 目的：防止控制端点因一次性瞬时错误永久失效（M4 否决项②「重试入口不可用」根因）。
// 依据：evidence/M4/2026-09-27-控制端点accept瞬时错误修复.md
#include <winsock2.h>

// 返回非 0 = 该 WSA 错误应终止 accept 循环；返回 0 = 记录后继续。
// 只有「监听套接字本身不可用」才是致命：继续循环也只会持续失败。
// 其余（连接重置/被打断/超时/缓冲不足/非阻塞无数据…）一律视为瞬时，继续服务。
static inline int http_accept_error_is_fatal(int wsa_error) {
    switch (wsa_error) {
        case WSAENOTSOCK:     /* 套接字已关闭/描述符无效 */
        case WSAEINVAL:       /* 套接字未处于监听态 */
        case WSAEOPNOTSUPP:   /* 该套接字不支持 accept */
        case WSAEAFNOSUPPORT: /* 地址族不支持 */
        case WSAENETDOWN:     /* Winsock 栈已死：重试只会永久空转，应显式终止 */
            return 1;
        default:
            return 0;
    }
}
