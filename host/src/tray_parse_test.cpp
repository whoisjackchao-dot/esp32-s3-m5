// tray_parse 单元测试（M4/S4.7）：bridge.json 解析的边界用例。
#include <cstdio>
#include <string>

#include "tray_parse.h"

namespace {
int g_failures = 0;
void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) ++g_failures;
}
}  // namespace

int main() {
    std::string out;
    check(tray::json_string("{\"url\":\"http://127.0.0.1:18999\",\"token\":\"abc\"}", "url", out) &&
              out == "http://127.0.0.1:18999",
          "extract url");
    check(tray::json_string("{\"url\":\"x\",\"token\":\"abc\"}", "token", out) && out == "abc",
          "extract token");
    check(!tray::json_string("{\"foo\":\"bar\"}", "url", out), "missing key -> false");
    check(!tray::json_string("{\"url\":", "url", out), "no value -> false");
    check(!tray::json_string("{\"url\":\"unterminated", "url", out), "unterminated -> false");
    check(tray::json_string("{\"url\":\"\",\"token\":\"t\"}", "url", out) && out.empty(),
          "empty string ok");
    check(!tray::json_string("", "url", out), "empty json -> false");
    std::printf("%s\n", g_failures == 0 ? "TRAY_PARSE_PASS" : "TRAY_PARSE_FAIL");
    return g_failures == 0 ? 0 : 1;
}
