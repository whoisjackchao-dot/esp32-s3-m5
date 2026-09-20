// StickS3 x OpenCode — 主机工具链自检（非产品代码）
//
// 目的：证明 MSVC + CMake + Ninja 能编译、链接并调用 Win32 API。
// 不实现任何产品行为，不属于 M1-M6 任何交付物。

#include <windows.h>

#include <cstdio>
#include <string>

int wmain() {
    wchar_t computerName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    const BOOL ok = GetComputerNameW(computerName, &size);

    std::printf("sticks3 host toolchain check\n");
    std::printf("  MSVC _MSC_VER     : %d\n", _MSC_VER);
    std::printf("  __cplusplus       : %ld\n", static_cast<long>(__cplusplus));
    std::printf("  GetComputerNameW  : %s\n", ok ? "OK (win32 link ok)" : "FAILED");

    if (!ok) {
        return 1;
    }
    return 0;
}
