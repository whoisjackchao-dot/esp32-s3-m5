// sse_parse 单元测试（M4/S4.2 生产 host）：SSE 事件帧解析的边界用例。
#include <cstdio>
#include <string>

#include "sse_parse.h"

namespace {
int g_failures = 0;
void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) ++g_failures;
}
}  // namespace

int main() {
    using sse::Event;

    {
        const std::string buf = "event: snapshot\ndata: {\"a\":1}\n\n";
        size_t c = 0;
        Event e;
        check(sse::next_event(buf, c, e) && e.name == "snapshot" && e.data == "{\"a\":1}" &&
                  c == buf.size(),
              "basic event");
    }
    {
        const std::string buf = ": connected\n\nevent: snapshot\ndata: X\n\n";
        size_t c = 0;
        Event e;
        check(sse::next_event(buf, c, e) && e.name == "snapshot" && e.data == "X" &&
                  c == buf.size(),
              "skip comment block");
    }
    {
        const std::string buf = "event: snapshot\ndata: {";
        size_t c = 0;
        Event e;
        check(!sse::next_event(buf, c, e) && c == 0, "incomplete -> false, no consume");
    }
    {
        const std::string buf = "data: a\ndata: b\n\n";
        size_t c = 0;
        Event e;
        check(sse::next_event(buf, c, e) && e.data == "a\nb" && e.name.empty(),
              "multi data lines joined");
    }
    {
        const std::string buf = "event: snapshot\r\ndata: Y\r\n\r\n";
        size_t c = 0;
        Event e;
        check(sse::next_event(buf, c, e) && e.name == "snapshot" && e.data == "Y" &&
                  c == buf.size(),
              "crlf event");
    }
    {
        const std::string buf = "data:  x\n\n";
        size_t c = 0;
        Event e;
        check(sse::next_event(buf, c, e) && e.data == " x", "strip single leading space only");
    }
    {
        const std::string buf = "data: one\n\ndata: two\n\n";
        size_t c = 0;
        Event e1, e2;
        const bool a = sse::next_event(buf, c, e1);
        const bool b = sse::next_event(buf, c, e2);
        check(a && b && e1.data == "one" && e2.data == "two" && c == buf.size(), "two events");
    }

    std::printf("%s\n", g_failures == 0 ? "SSE_PARSE_PASS" : "SSE_PARSE_FAIL");
    return g_failures == 0 ? 0 : 1;
}
