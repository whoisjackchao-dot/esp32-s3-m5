#pragma once
// M4 S4.2 —— 桥接 BR-03 快照 JSON → br03_snapshot 映射（WinRT JSON；host 侧共享，
// 供 ble_client 与 state_host 复用，避免分叉实现）。
// 文本按 UTF-8 处理：解析 hstring（UTF-8→UTF-16），取回时 UTF-16→UTF-8，
// 保证中文标题/模型名不被降级为 '?'。
#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

#include <cstdint>
#include <cstring>
#include <string>

extern "C" {
#include "s3p_state.h"
#include "state_merge.h"
}
#include "cost_scale.h"

namespace bridge {

inline uint8_t status_from_string(const std::wstring& s) {
    if (s == L"idle") return S3P_STATUS_IDLE;
    if (s == L"busy") return S3P_STATUS_BUSY;
    if (s == L"retry") return S3P_STATUS_RETRY;
    if (s == L"waiting") return S3P_STATUS_WAITING;
    if (s == L"error") return S3P_STATUS_ERROR;
    return S3P_STATUS_UNKNOWN;
}

// `{kind,value}` 可用性包装 → 数值；known 取值，not_reported 取哨兵，其余 missing。
inline uint32_t avail_number(const winrt::Windows::Data::Json::JsonObject& parent,
                             const wchar_t* key, uint32_t missing) {
    using namespace winrt::Windows::Data::Json;
    if (!parent.HasKey(key)) return missing;
    const JsonObject a = parent.GetNamedObject(key);
    const std::wstring kind = a.HasKey(L"kind") ? std::wstring(a.GetNamedString(L"kind")) : L"";
    if (kind == L"known" && a.HasKey(L"value")) return (uint32_t)a.GetNamedNumber(L"value");
    if (kind == L"not_reported") return S3P_STATE_NOT_REPORTED;
    return missing;
}

// `{kind,value}` 可用性包装 → cost 毫单位：known 时 ×1000 取整；not_reported 取哨兵；其余 missing。
inline uint32_t avail_cost_milli(const winrt::Windows::Data::Json::JsonObject& parent,
                                 const wchar_t* key, uint32_t missing) {
    using namespace winrt::Windows::Data::Json;
    if (!parent.HasKey(key)) return missing;
    const JsonObject a = parent.GetNamedObject(key);
    const std::wstring kind = a.HasKey(L"kind") ? std::wstring(a.GetNamedString(L"kind")) : L"";
    if (kind == L"known" && a.HasKey(L"value")) return cost_to_milli(a.GetNamedNumber(L"value"));
    if (kind == L"not_reported") return S3P_STATE_NOT_REPORTED;
    return missing;
}

// `{kind,value}` 可用性包装 → UTF-8 文本；仅 known 时置 *has。
inline std::string avail_string(const winrt::Windows::Data::Json::JsonObject& parent,
                                const wchar_t* key, bool* has) {
    using namespace winrt::Windows::Data::Json;
    *has = false;
    if (!parent.HasKey(key)) return {};
    const JsonObject a = parent.GetNamedObject(key);
    const std::wstring kind = a.HasKey(L"kind") ? std::wstring(a.GetNamedString(L"kind")) : L"";
    if (kind == L"known" && a.HasKey(L"value")) {
        *has = true;
        return winrt::to_string(a.GetNamedString(L"value"));
    }
    return {};
}

inline bool parse_snapshot(const std::string& json, br03_snapshot& out) {
    using namespace winrt::Windows::Data::Json;
    try {
        const JsonObject o = JsonObject::Parse(winrt::to_hstring(json));
        br03_snapshot b;
        memset(&b, 0, sizeof(b));
        b.tokens_input = S3P_STATE_UNKNOWN;
        b.tokens_output = S3P_STATE_UNKNOWN;
        b.cost_milli = S3P_STATE_UNKNOWN;
        if (o.HasKey(L"status")) {
            b.status = status_from_string(std::wstring(o.GetNamedString(L"status")));
        }
        if (o.HasKey(L"identity")) {
            const JsonObject id = o.GetNamedObject(L"identity");
            if (id.HasKey(L"snapshotRevision")) {
                b.revision = (uint32_t)id.GetNamedNumber(L"snapshotRevision");
            }
        }
        if (o.HasKey(L"bind")) {
            const JsonObject bd = o.GetNamedObject(L"bind");
            if (bd.HasKey(L"canAppend")) b.can_append = bd.GetNamedBoolean(L"canAppend") ? 1 : 0;
        }
        if (o.HasKey(L"freshness")) {
            const JsonObject fr = o.GetNamedObject(L"freshness");
            if (fr.HasKey(L"authoritativeReadOk")) {
                b.expired = fr.GetNamedBoolean(L"authoritativeReadOk") ? 0 : 1;
            }
        }
        if (o.HasKey(L"subtaskHint") && o.GetNamedValue(L"subtaskHint").ValueType() == JsonValueType::String) {
            const std::wstring h(o.GetNamedString(L"subtaskHint"));
            if (h == L"tool_failed") {
                b.tool_failed = 1;
            } else if (h == L"child_busy") {
                b.child_busy = 1;
            }
        }
        if (o.HasKey(L"tools")) {
            const auto tools = o.GetNamedArray(L"tools");
            uint32_t count = 0;
            for (uint32_t i = 0; i < tools.Size(); ++i) {
                const JsonObject t = tools.GetObjectAt(i);
                if (t.HasKey(L"count")) count += (uint32_t)t.GetNamedNumber(L"count");
            }
            b.tool_count = count > 255 ? 255 : (uint8_t)count;
        }
        if (o.HasKey(L"todos")) {
            const JsonObject td = o.GetNamedObject(L"todos");
            const std::wstring kind = td.HasKey(L"kind") ? std::wstring(td.GetNamedString(L"kind")) : L"";
            if (kind == L"known" && td.HasKey(L"value")) {
                const JsonObject v = td.GetNamedObject(L"value");
                b.todo_known = 1;
                b.todo_completed = (uint8_t)v.GetNamedNumber(L"completed");
                b.todo_total = (uint8_t)v.GetNamedNumber(L"total");
            }
        }
        if (o.HasKey(L"usage")) {
            const JsonObject us = o.GetNamedObject(L"usage");
            b.tokens_input = avail_number(us, L"input", S3P_STATE_UNKNOWN);
            b.tokens_output = avail_number(us, L"output", S3P_STATE_UNKNOWN);
            b.tokens_reasoning = avail_number(us, L"reasoning", S3P_STATE_UNKNOWN);
            b.tokens_cache_read = avail_number(us, L"cacheRead", S3P_STATE_UNKNOWN);
        }
        b.cost_milli = avail_cost_milli(o, L"cost", S3P_STATE_UNKNOWN);
        static std::string s_project, s_title, s_model;
        bool hp = false, ht = false, hm = false;
        s_project = avail_string(o, L"project", &hp);
        s_title = avail_string(o, L"sessionTitle", &ht);
        if (o.HasKey(L"model")) {
            const JsonObject md = o.GetNamedObject(L"model");
            s_model = avail_string(md, L"current", &hm);
        }
        b.project = hp ? s_project.c_str() : nullptr;
        b.title = ht ? s_title.c_str() : nullptr;
        b.model = hm ? s_model.c_str() : nullptr;
        out = b;
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace bridge
