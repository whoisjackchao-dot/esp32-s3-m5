// 工具链冒烟测试（非产品逻辑）
//
// 目的：证明 `bun test` 可运行、插件类型可解析，并固定 route 判别语义。
// 不含任何 M1 业务逻辑；业务用例将在对应故事中以 RED→GREEN 方式新增。

import { describe, expect, test } from "bun:test";

import type { TuiPromptRef, TuiRouteCurrent } from "@opencode-ai/plugin/tui";

/**
 * 从当前路由取出会话 ID。
 *
 * 依据 @opencode-ai/plugin@1.18.31 的 tui.d.ts:13-24，`TuiRouteCurrent` 是判别联合：
 *   { name: "home" }
 * | { name: "session"; params: { sessionID: string; prompt?: unknown } }
 * | { name: string; params?: Record<string, unknown> }   ← 兜底成员
 *
 * ⚠️ 关键：由于存在 `{name: string}` 兜底成员，`route.name === "session"` **无法**让
 * TypeScript 收窄到具体分支，`route.params` 仍是联合类型。因此 M1 的路由校验必须做
 * **运行时类型检查**，不能依赖编译期收窄。
 */
function currentSessionID(route: TuiRouteCurrent): string | undefined {
    if (route.name !== "session") {
        return undefined;
    }
    const params = (route as { params?: Record<string, unknown> }).params;
    const id = params?.["sessionID"];
    return typeof id === "string" ? id : undefined;
}

describe("toolchain smoke", () => {
    test("bun test runner 可运行", () => {
        expect(1 + 1).toBe(2);
    });

    test("TuiRouteCurrent 运行时判别：home 无 sessionID，session 有 sessionID", () => {
        const home: TuiRouteCurrent = { name: "home" };
        const session: TuiRouteCurrent = { name: "session", params: { sessionID: "ses_example" } };

        expect(currentSessionID(home)).toBeUndefined();
        expect(currentSessionID(session)).toBe("ses_example");
    });

    test("兜底成员：name 为 session 但 params 非字符串 sessionID 时返回 undefined", () => {
        const malformed: TuiRouteCurrent = { name: "session", params: { sessionID: 42 } };

        expect(currentSessionID(malformed)).toBeUndefined();
    });

    test("TuiPromptRef 含 current/set；submit 存在但本项目禁用", () => {
        type RefKeys = keyof TuiPromptRef;
        const required: RefKeys[] = ["current", "set", "reset", "focus", "blur", "submit", "focused"];

        expect(required).toHaveLength(7);
    });
});
