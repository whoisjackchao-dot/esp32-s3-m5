#pragma once
// M4 S4.2 生产 host —— SSE(event-stream) 帧解析（纯逻辑，无平台依赖，可独立单测）。
// 依据 W3C SSE：事件块以空行分隔；行内 `field: value`；忽略注释行（':' 开头）与未知字段。
#include <string>

namespace sse {

struct Event {
    std::string name;  // `event:` 值；缺省为空
    std::string data;  // 多行 `data:` 以 '\n' 连接
};

namespace detail {

inline std::string strip_eol(std::string s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}

// SSE 规范：字段值若以单个空格开头则移除该空格。
inline std::string field_value(const std::string& line, size_t colon) {
    std::string v = line.substr(colon + 1);
    if (!v.empty() && v[0] == ' ') v.erase(0, 1);
    return v;
}

}  // namespace detail

// 从 buf 头部解析下一个“含字段”的事件，推进 consumed 并写入 out。
// - 仅含注释/空白的块被跳过；头部无完整事件块（无空行终止）时返回 false 且不推进。
// - 失败路径不修改 out（局部累积，成功才提交），故可安全对同一 buf 反复调用直至追加数据。
inline bool next_event(const std::string& buf, size_t& consumed, Event& out) {
    size_t pos = consumed;
    Event acc;
    while (pos < buf.size()) {
        const size_t eol = buf.find('\n', pos);
        if (eol == std::string::npos) return false;  // 行未结束 → 等待更多数据
        const std::string line = detail::strip_eol(buf.substr(pos, eol - pos));
        pos = eol + 1;
        if (line.empty()) {
            if (!acc.name.empty() || !acc.data.empty()) {
                out = acc;
                consumed = pos;
                return true;
            }
            continue;  // 注释/空块：跳过
        }
        if (line[0] == ':') continue;  // 注释行
        const size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string field = line.substr(0, colon);
        if (field == "event") {
            acc.name = detail::field_value(line, colon);
        } else if (field == "data") {
            if (!acc.data.empty()) acc.data += '\n';
            acc.data += detail::field_value(line, colon);
        }
    }
    return false;
}

}  // namespace sse
