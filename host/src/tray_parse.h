#pragma once
// 托盘用纯解析工具（无 Win32 依赖，可独立单测）。
#include <string>

namespace tray {

// 极简 JSON 取值：定位 "key"，取其后的第一个字符串值。仅用于本机自产描述文件。
inline bool json_string(const std::string& json, const std::string& key, std::string& out) {
    const std::string needle = "\"" + key + "\"";
    const size_t k = json.find(needle);
    if (k == std::string::npos) return false;
    const size_t colon = json.find(':', k + needle.size());
    if (colon == std::string::npos) return false;
    const size_t q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) return false;
    const size_t q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return false;
    out = json.substr(q1 + 1, q2 - q1 - 1);
    return true;
}

}  // namespace tray
