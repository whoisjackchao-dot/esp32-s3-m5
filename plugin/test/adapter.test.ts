// capture/safeAppend 单元测试：mock ref，验证写入语义与安全边界（TUI-01/02/03/14、C06）
import { describe, expect, test } from "bun:test";

import { RefStore, currentSessionID } from "../src/plugin/capture";
import { buildCommitFn } from "../src/plugin/safeAppend";
import type { TuiPromptInfo, TuiPromptRef, TuiRouteCurrent } from "@opencode-ai/plugin/tui";

interface MockPrompt {
    input: string;
    parts: TuiPromptInfo["parts"];
}

function makeRef(state: MockPrompt): TuiPromptRef & { state: MockPrompt; submitCalls: number } {
    const ref = {
        state,
        submitCalls: 0,
        focused: true,
        get current() {
            return { input: state.input, parts: state.parts };
        },
        set(prompt: TuiPromptInfo) {
            state.input = prompt.input;
            state.parts = prompt.parts;
        },
        reset() {},
        blur() {},
        focus() {},
        submit() {
            ref.submitCalls += 1;
        },
    };
    return ref;
}

describe("currentSessionID（M1 契约 §4.1 运行时判别）", () => {
    test("home 路由无 sessionID", () => {
        expect(currentSessionID({ name: "home" })).toBeUndefined();
    });
    test("session 路由取 params.sessionID", () => {
        const route: TuiRouteCurrent = { name: "session", params: { sessionID: "ses_x" } };
        expect(currentSessionID(route)).toBe("ses_x");
    });
    test("兜底成员 sessionID 非字符串 → undefined", () => {
        const route = { name: "session", params: { sessionID: 42 } } as unknown as TuiRouteCurrent;
        expect(currentSessionID(route)).toBeUndefined();
    });
});

describe("RefStore", () => {
    test("capture/get/delete 按 session 隔离", () => {
        const store = new RefStore();
        const a = makeRef({ input: "", parts: [] });
        store.capture("ses_a", a);
        expect(store.get("ses_a")).toBe(a);
        expect(store.size).toBe(1);
        store.capture("ses_a", undefined);
        expect(store.get("ses_a")).toBeUndefined();
    });
});

describe("buildCommitFn（安全追加写入）", () => {
    test("空草稿：精确追加，parts 保留，返回 applied", async () => {
        const state: MockPrompt = { input: "", parts: [{ type: "text", text: "已有附件" }] };
        const ref = makeRef(state);
        const commit = buildCommitFn(() => ref);
        const outcome = await commit("修复 API");
        expect(outcome).toBe("applied");
        expect(state.input).toBe("修复 API");
        expect(state.parts).toEqual([{ type: "text", text: "已有附件" }]);
    });

    test("TUI-02：非空原稿按分隔规则追加", async () => {
        const state: MockPrompt = { input: "已有说明", parts: [] };
        const ref = makeRef(state);
        await buildCommitFn(() => ref)("修复 API");
        expect(state.input).toBe("已有说明\n修复 API");
    });

    test("TUI-03：原稿末尾已有空白不重复插分隔符", async () => {
        const state: MockPrompt = { input: "已有说明 ", parts: [] };
        const ref = makeRef(state);
        await buildCommitFn(() => ref)("修复 API");
        expect(state.input).toBe("已有说明 修复 API");
    });

    test("TUI-14：ref 缺失 → uncertain", async () => {
        const commit = buildCommitFn(() => undefined);
        expect(await commit("任意")).toBe("uncertain");
    });

    test("TUI-14：set 抛错 → uncertain", async () => {
        const ref = makeRef({ input: "", parts: [] });
        const broken = {
            current: ref.current,
            set() {
                throw new Error("set failed");
            },
        } as unknown as TuiPromptRef;
        expect(await buildCommitFn(() => broken)("x")).toBe("uncertain");
    });

    test("安全边界：全程不调用 submit", async () => {
        const ref = makeRef({ input: "", parts: [] });
        await buildCommitFn(() => ref)("文字");
        expect(ref.submitCalls).toBe(0);
    });
});
