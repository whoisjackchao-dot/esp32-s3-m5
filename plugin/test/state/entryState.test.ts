// M4 插件入口接线集成测试（index.ts）—— 覆盖 S4.1b 新增的状态接线。
// 用假 TUI api + 临时 HOME，验证 tui() 启动桥接、写出 bridge.json、并输出归约快照。
import { afterEach, describe, expect, test } from "bun:test";
import { mkdtempSync, readFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const SESSION_ID = "ses_wiring";

interface FakeApi {
    route: { current: unknown };
    state: {
        session: Record<string, unknown>;
        part: (id: string) => readonly unknown[];
    };
    slots: { register: (p: unknown) => string };
    ui: { Prompt: (p: unknown) => null };
    lifecycle: { onDispose: (fn: () => void) => () => void };
    event: { on: (type: string, handler: () => void) => () => void };
}

function fakeApi(disposers: Array<() => void>): FakeApi {
    const session = {
        get: () => ({
            title: "会话标题",
            model: { id: "claude-sonnet-4.5" },
            tokens: { input: 5, output: 1, reasoning: 0, cache: { read: 0, write: 0 } },
            cost: 0.01,
        }),
        status: () => ({ type: "busy" }),
        todo: () => [{ status: "completed" }, { status: "pending" }],
        messages: () => [{ role: "assistant", modelID: "claude-sonnet-4.5" }],
        permission: () => [],
        question: () => [],
    };
    return {
        route: { current: { name: "session", params: { sessionID: SESSION_ID } } },
        state: { session, part: () => [] },
        slots: { register: () => "slot-id" },
        ui: { Prompt: () => null },
        lifecycle: {
            onDispose: (fn) => {
                disposers.push(fn);
                return () => {};
            },
        },
        event: { on: () => () => {} },
    };
}

const cleanups: Array<() => void> = [];
afterEach(() => {
    while (cleanups.length > 0) cleanups.pop()?.();
});

describe("M4 插件入口状态接线（index.ts）", () => {
    test("tui() 启动桥接、写 bridge.json、/v1/state/stream 输出归约快照", async () => {
        const home = mkdtempSync(join(tmpdir(), "sticks3-home-"));
        const descriptorDir = join(home, "sticks3-opencode");
        process.env.STICKS3_DESCRIPTOR_DIR = descriptorDir;
        cleanups.push(() => {
            rmSync(home, { recursive: true, force: true });
            delete process.env.STICKS3_DESCRIPTOR_DIR;
        });

        const plugin = (await import("../../src/index")).default;
        const disposers: Array<() => void> = [];
        const tui = plugin.tui as unknown as (
            api: unknown,
            options: unknown,
            meta: unknown,
        ) => Promise<void>;
        await tui(fakeApi(disposers), undefined, {});
        cleanups.push(() => disposers.forEach((fn) => fn()));

        const descriptor = JSON.parse(
            readFileSync(join(descriptorDir, "bridge.json"), "utf8"),
        ) as { url: string; token: string };

        const res = await fetch(`${descriptor.url}/v1/state/stream`, {
            headers: { authorization: `Bearer ${descriptor.token}` },
        });
        expect(res.status).toBe(200);
        expect(res.headers.get("content-type")).toBe("text/event-stream");

        const body = res.body;
        if (!body) throw new Error("no body");
        const reader = body.getReader();
        const decoder = new TextDecoder();
        let collected = "";
        for (let i = 0; i < 20 && !collected.includes("event: snapshot"); i += 1) {
            const chunk = await reader.read();
            if (chunk.done) break;
            collected += decoder.decode(chunk.value, { stream: true });
        }
        await reader.cancel();

        expect(collected).toContain("event: snapshot");
        const line = collected.split("\n").find((l) => l.startsWith("data: "));
        if (!line) throw new Error("no snapshot data line");
        const snapshot = JSON.parse(line.slice("data: ".length)) as Record<string, unknown>;
        expect(snapshot["status"]).toBe("busy");
        expect((snapshot["identity"] as Record<string, unknown>)["sessionID"]).toBe(SESSION_ID);
        expect((snapshot["todos"] as { value: unknown }).value).toEqual({ completed: 1, total: 2 });
    });

    test("无伤插入：状态读取抛错被容纳，tui() 不失败且桥接照常写出", async () => {
        const home = mkdtempSync(join(tmpdir(), "sticks3-home-"));
        const descriptorDir = join(home, "sticks3-opencode");
        process.env.STICKS3_DESCRIPTOR_DIR = descriptorDir;
        cleanups.push(() => {
            rmSync(home, { recursive: true, force: true });
            delete process.env.STICKS3_DESCRIPTOR_DIR;
        });
        const plugin = (await import("../../src/index")).default;
        const disposers: Array<() => void> = [];
        const api = fakeApi(disposers);
        api.route = {
            get current(): unknown {
                throw new Error("route read failed");
            },
        };
        const tui = plugin.tui as unknown as (
            api: unknown,
            options: unknown,
            meta: unknown,
        ) => Promise<void>;
        await expect(tui(api, undefined, {})).resolves.toBeUndefined();
        cleanups.push(() => disposers.forEach((fn) => fn()));
    });
});
