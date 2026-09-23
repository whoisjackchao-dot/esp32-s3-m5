// session_prompt 槽位转发测试（方案 B 二次修复回归；根因 2026-09-23 二进制级证据）
// 根因：opencode solid Slot 组件以 renderer(context, data) 调用插件，data 才是宿主 props。
// 首版误把第 1 参 context（仅 {theme}）当 props → session_id/on_submit 恒 undefined →
// 会话内输入框退化成首页新建会话框，每次回车新建会话（TUI-16 回归）。
import { describe, expect, test } from "bun:test";

import { buildSessionPromptSlot } from "../src/plugin/slotPrompt";
import type { TuiPromptInfo, TuiPromptProps, TuiPromptRef } from "@opencode-ai/plugin/tui";

function makeRef(): TuiPromptRef {
    return {
        focused: true,
        current: { input: "", parts: [] } as TuiPromptInfo,
        set() {},
        reset() {},
        blur() {},
        focus() {},
        submit() {},
    };
}

function harness() {
    const rendered: TuiPromptProps[] = [];
    const captured: Array<[string, TuiPromptRef | undefined]> = [];
    let hostRef: TuiPromptRef | undefined = "unset" as unknown as TuiPromptRef;
    const hostRefCb = (r: TuiPromptRef | undefined) => {
        hostRef = r;
    };
    const slot = buildSessionPromptSlot(
        (props) => {
            rendered.push(props);
            return "rendered";
        },
        (sessionID, ref) => {
            captured.push([sessionID, ref]);
        },
    );
    return { slot, rendered, captured, getHostRef: () => hostRef, hostRefCb };
}

describe("buildSessionPromptSlot（M1 方案 B 二次修复：context / data 双参）", () => {
    test("回归：第 1 参 context（{theme}）不得被当作宿主 props", () => {
        const h = harness();
        const fn = () => {};
        h.slot({ theme: "dark" }, { session_id: "ses_x", on_submit: fn });
        expect(h.rendered[0]?.sessionID).toBe("ses_x");
        expect(h.rendered[0]?.onSubmit).toBe(fn);
    });

    test("转发 session_id → Prompt.sessionID", () => {
        const h = harness();
        h.slot(undefined, { session_id: "ses_x" });
        expect(h.rendered[0]?.sessionID).toBe("ses_x");
    });

    test("转发 on_submit → Prompt.onSubmit", () => {
        const h = harness();
        const fn = () => {};
        h.slot(undefined, { session_id: "ses_x", on_submit: fn });
        expect(h.rendered[0]?.onSubmit).toBe(fn);
    });

    test("转发 visible / disabled", () => {
        const h = harness();
        h.slot(undefined, { session_id: "ses_x", visible: true, disabled: false });
        expect(h.rendered[0]?.visible).toBe(true);
        expect(h.rendered[0]?.disabled).toBe(false);
    });

    test("宿主 ref 被转发：ref 回调收到 Prompt 引用", () => {
        const h = harness();
        h.slot(undefined, { session_id: "ses_x", ref: h.hostRefCb });
        const promptRef = makeRef();
        h.rendered[0]?.ref?.(promptRef);
        expect(h.getHostRef()).toBe(promptRef);
    });

    test("ref 回调同时捕获到 RefStore（按 sessionID）", () => {
        const h = harness();
        h.slot(undefined, { session_id: "ses_x" });
        const promptRef = makeRef();
        h.rendered[0]?.ref?.(promptRef);
        expect(h.captured).toEqual([["ses_x", promptRef]]);
    });

    test("卸载（ref=undefined）时宿主 ref 收到 undefined 且 capture 传递 undefined", () => {
        const h = harness();
        h.slot(undefined, { session_id: "ses_x", ref: h.hostRefCb });
        h.rendered[0]?.ref?.(undefined);
        expect(h.getHostRef()).toBeUndefined();
        expect(h.captured).toEqual([["ses_x", undefined]]);
    });

    test("data 缺失时不抛错，字段显式 undefined", () => {
        const h = harness();
        h.slot(undefined, undefined);
        expect(h.rendered[0]?.sessionID).toBeUndefined();
        expect(h.rendered[0]?.onSubmit).toBeUndefined();
        expect(h.rendered[0]?.visible).toBeUndefined();
    });

    test("session_id 缺失时不调用 capture（避免误绑）", () => {
        const h = harness();
        h.slot(undefined, { visible: true });
        const promptRef = makeRef();
        h.rendered[0]?.ref?.(promptRef);
        expect(h.captured).toEqual([]);
    });
});
