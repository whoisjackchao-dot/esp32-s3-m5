// M4 S4.1b TUI 状态适配 — Selective TDD（T 类）
// 把 TUI 原生读取（api.state.session.*）映射为 SnapshotSource；映射为纯函数，便于单测。
import { describe, expect, test } from "bun:test";

import { buildSnapshotSource, normalizeStatus, todoProgress } from "../../src/plugin/stateRead";
import type { TuiReads } from "../../src/plugin/stateRead";

const NOW = 3_000_000;

function reads(overrides: Partial<TuiReads> = {}): TuiReads {
    return {
        sessionStatus: { type: "idle" },
        todos: [],
        permissions: [],
        questions: [],
        session: undefined,
        messages: [],
        parts: [],
        ...overrides,
    };
}

describe("S4.1b TUI 状态适配", () => {
    test("状态归一：字符串与 {type} 两种形状；未知→idle", () => {
        expect(normalizeStatus("busy")).toBe("busy");
        expect(normalizeStatus({ type: "busy" })).toBe("busy");
        expect(normalizeStatus({ type: "retry", attempt: 2 })).toBe("retry");
        expect(normalizeStatus({ type: "idle" })).toBe("idle");
        expect(normalizeStatus(undefined)).toBe("idle");
        expect(normalizeStatus("weird")).toBe("idle");
    });

    test("todo 计数：completed/total，空表为 0/0 且 known", () => {
        expect(todoProgress([])).toEqual({ known: true, completed: 0, total: 0 });
        const list = [{ status: "completed" }, { status: "in_progress" }, { status: "completed" }];
        expect(todoProgress(list)).toEqual({ known: true, completed: 2, total: 3 });
    });

    test("pending 权限/问题 → waiting 输入（不审批、不读正文）", () => {
        const s = buildSnapshotSource({
            epoch: "ep", revision: 1, sessionID: "ses_a",
            canAppend: true, blockReason: null,
            reads: reads({ permissions: [{ id: "p1" }], questions: [{ id: "q1" }] }),
            nowMs: NOW, authoritativeReadOk: true,
        });
        expect(s.pendingPermission).toBe(true);
        expect(s.pendingQuestion).toBe(true);
        expect(s.chatText).toBeNull();
    });

    test("完整映射：状态/待办/绑定，模型与用量保守为未知（不伪装）", () => {
        const s = buildSnapshotSource({
            epoch: "ep", revision: 7, sessionID: "ses_a",
            canAppend: false, blockReason: "dialog_blocking",
            reads: reads({
                sessionStatus: { type: "busy" },
                todos: [{ status: "completed" }, { status: "pending" }],
            }),
            nowMs: NOW, authoritativeReadOk: true,
        });
        expect(s.epoch).toBe("ep");
        expect(s.revision).toBe(7);
        expect(s.sessionID).toBe("ses_a");
        expect(s.canAppend).toBe(false);
        expect(s.blockReason).toBe("dialog_blocking");
        expect(s.sessionStatus).toBe("busy");
        expect(s.todo).toEqual({ known: true, completed: 1, total: 2 });
        expect(s.model).toEqual({});
        expect(s.usage).toEqual({ present: false });
        expect(s.readAtMs).toBe(NOW);
    });

    test("权威读取失败时如实标注", () => {
        const s = buildSnapshotSource({
            epoch: "ep", revision: 1, sessionID: "ses_a",
            canAppend: true, blockReason: null,
            reads: reads(),
            nowMs: NOW, authoritativeReadOk: false,
        });
        expect(s.authoritativeReadOk).toBe(false);
    });

    test("接线 Session 累计用量/模型/标题", () => {
        const s = buildSnapshotSource({
            epoch: "ep", revision: 1, sessionID: "ses_a",
            canAppend: true, blockReason: null,
            reads: reads({
                session: {
                    title: "会话标题",
                    model: { id: "claude-sonnet-4.5", providerID: "anthropic" },
                    cost: 0.12,
                    tokens: { input: 100, output: 20, reasoning: 5, cache: { read: 3, write: 1 } },
                },
            }),
            nowMs: NOW, authoritativeReadOk: true,
        });
        expect(s.sessionTitle).toBe("会话标题");
        expect(s.model).toEqual({ current: "claude-sonnet-4.5" });
        expect(s.usage).toEqual({
            present: true, input: 100, output: 20, reasoning: 5, cacheRead: 3, cacheWrite: 1, cost: 0.12,
        });
    });

    test("Session 无 tokens → 用量保守 unknown（不伪装 0）", () => {
        const s = buildSnapshotSource({
            epoch: "ep", revision: 1, sessionID: "ses_a",
            canAppend: true, blockReason: null,
            reads: reads({ session: { title: "t" } }),
            nowMs: NOW, authoritativeReadOk: true,
        });
        expect(s.usage).toEqual({ present: false });
        expect(s.model).toEqual({});
    });

    test("无 session.model 时回退最近 assistant 的 modelID 为 lastUsed", () => {
        const s = buildSnapshotSource({
            epoch: "ep", revision: 1, sessionID: "ses_a",
            canAppend: true, blockReason: null,
            reads: reads({
                session: { title: "t" },
                messages: [
                    { role: "user" },
                    { role: "assistant", modelID: "gpt-5" },
                ],
            }),
            nowMs: NOW, authoritativeReadOk: true,
        });
        expect(s.model).toEqual({ lastUsed: "gpt-5" });
    });

    test("工具接线：running/pending 计入运行名，error 计入失败数，completed 不计", () => {
        const s = buildSnapshotSource({
            epoch: "ep", revision: 1, sessionID: "ses_a",
            canAppend: true, blockReason: null,
            reads: reads({
                parts: [
                    { type: "tool", tool: "edit", state: { status: "running" } },
                    { type: "tool", tool: "bash", state: { status: "error" } },
                    { type: "tool", tool: "read", state: { status: "completed" } },
                    { type: "tool", tool: "grep", state: { status: "pending" } },
                ],
            }),
            nowMs: NOW, authoritativeReadOk: true,
        });
        expect(s.runningTools).toEqual(["edit", "grep"]);
        expect(s.failedTools).toBe(1);
    });

    test("activeError 取最近 assistant 的 error 名；无则 null", () => {
        const withErr = buildSnapshotSource({
            epoch: "ep", revision: 1, sessionID: "ses_a",
            canAppend: true, blockReason: null,
            reads: reads({ messages: [{ role: "assistant", error: { name: "ProviderAuthError" } }] }),
            nowMs: NOW, authoritativeReadOk: true,
        });
        expect(withErr.activeError).toBe("ProviderAuthError");

        const clean = buildSnapshotSource({
            epoch: "ep", revision: 1, sessionID: "ses_a",
            canAppend: true, blockReason: null,
            reads: reads(),
            nowMs: NOW, authoritativeReadOk: true,
        });
        expect(clean.activeError).toBeNull();
        expect(clean.runningTools).toEqual([]);
        expect(clean.failedTools).toBe(0);
    });

    test("activeError 回归：最近 assistant 已成功时，更早的陈旧错误必须清除", () => {
        const s = buildSnapshotSource({
            epoch: "ep", revision: 1, sessionID: "ses_a",
            canAppend: true, blockReason: null,
            reads: reads({
                messages: [
                    { role: "user" },
                    { role: "assistant", error: { name: "MessageAbortedError" } },
                    { role: "assistant" },
                    { role: "user" },
                ],
            }),
            nowMs: NOW, authoritativeReadOk: true,
        });
        expect(s.activeError).toBeNull();
    });

    test("activeError 回归：最近 assistant 失败时取其错误名（不被更早的成功掩盖）", () => {
        const s = buildSnapshotSource({
            epoch: "ep", revision: 1, sessionID: "ses_a",
            canAppend: true, blockReason: null,
            reads: reads({
                messages: [
                    { role: "assistant" },
                    { role: "user" },
                    { role: "assistant", error: { name: "ProviderAuthError" } },
                ],
            }),
            nowMs: NOW, authoritativeReadOk: true,
        });
        expect(s.activeError).toBe("ProviderAuthError");
    });
});
