// M1 桥接服务器 Selective TDD —— 契约 §2/§3/§3.1 与 SEC-01/SEC-02
import { afterEach, describe, expect, test } from "bun:test";

import { startBridge } from "../src/bridge/server";
import { BODY_MAX_BYTES, PROTOCOL_VERSION } from "../src/core/types";
import type {
    AppendOutcome,
    AppendRequest,
    BindingTarget,
    BridgeServer,
    CancelOutcome,
    QueryOutcome,
} from "../src/core/types";

const TOKEN = "bridge-token-test-3f9a2b";
const EPOCH = "epoch-2026-test";
const PLUGIN_VERSION = "0.1.0-test";

const BINDING: BindingTarget = {
    epoch: EPOCH,
    boundSessionID: "ses_abc123",
    currentRouteSessionID: "ses_abc123",
    promptVisible: true,
    promptDisabled: false,
    dialogBlocking: false,
};

interface StubConfig {
    appendOutcome?: AppendOutcome;
    queryOutcome?: QueryOutcome;
    cancelOutcome?: CancelOutcome;
    binding?: BindingTarget;
    token?: string;
}

const liveServers: BridgeServer[] = [];

function startTestServer(config: StubConfig = {}): BridgeServer {
    const server = startBridge({
        token: config.token ?? TOKEN,
        tuiEpoch: EPOCH,
        pluginVersion: PLUGIN_VERSION,
        handlers: {
            append: async (_req: AppendRequest) => config.appendOutcome ?? { status: "applied" },
            cancel: () => config.cancelOutcome ?? { status: "cancelled" },
            query: () => config.queryOutcome ?? { status: "applied" },
            binding: () => config.binding ?? BINDING,
        },
    });
    liveServers.push(server);
    return server;
}

afterEach(() => {
    while (liveServers.length > 0) {
        const server = liveServers.pop();
        if (server) server.stop();
    }
});

function urlFor(server: BridgeServer, path: string): string {
    return `http://127.0.0.1:${server.port}${path}`;
}

function authHeaders(token: string = TOKEN): Record<string, string> {
    return { authorization: `Bearer ${token}` };
}

function appendBody(overrides: Record<string, unknown> = {}): Record<string, unknown> {
    return {
        protocolVersion: PROTOCOL_VERSION,
        tuiEpoch: EPOCH,
        sessionID: "ses_abc123",
        operationID: "op_001",
        transactionGeneration: 1,
        text: "修复 API",
        ...overrides,
    };
}

async function postJson(
    server: BridgeServer,
    path: string,
    body: unknown,
    headers: Record<string, string> = {},
): Promise<Response> {
    return fetch(urlFor(server, path), {
        method: "POST",
        headers: { "content-type": "application/json", ...authHeaders(), ...headers },
        body: typeof body === "string" ? body : JSON.stringify(body),
    });
}

function withTimeout<T>(promise: Promise<T>, ms: number): Promise<T> {
    return new Promise<T>((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error(`timeout after ${ms}ms`)), ms);
        promise.then(
            (value) => {
                clearTimeout(timer);
                resolve(value);
            },
            (error) => {
                clearTimeout(timer);
                reject(error);
            },
        );
    });
}

describe("M1 桥接服务器", () => {
    test("SEC-01：缺 token → 401，body 恰为 unauthorized（无任何草稿/状态泄露）", async () => {
        const server = startTestServer();
        const res = await fetch(urlFor(server, "/v1/health"));
        expect(res.status).toBe(401);
        expect(await res.json()).toEqual({ error: { code: "unauthorized" } });
    });

    test("SEC-01：错 token → 401", async () => {
        const server = startTestServer({ token: "right-token" });
        const res = await fetch(urlFor(server, "/v1/health"), { headers: authHeaders("wrong-token") });
        expect(res.status).toBe(401);
        expect(await res.json()).toEqual({ error: { code: "unauthorized" } });
    });

    test("SEC-01：带 Origin → 403 origin_forbidden", async () => {
        const server = startTestServer();
        const res = await fetch(urlFor(server, "/v1/health"), {
            headers: { ...authHeaders(), origin: "http://evil.example" },
        });
        expect(res.status).toBe(403);
        expect(await res.json()).toEqual({ error: { code: "origin_forbidden" } });
    });

    test("SEC-01：伪造 Host → 403 host_forbidden", async () => {
        const server = startTestServer();
        const res = await fetch(urlFor(server, "/v1/health"), {
            headers: { ...authHeaders(), host: "evil.example" },
        });
        expect(res.status).toBe(403);
        expect(await res.json()).toEqual({ error: { code: "host_forbidden" } });
    });

    test("控制组：正确 token + loopback → health 200 完整字段", async () => {
        const server = startTestServer();
        const res = await fetch(urlFor(server, "/v1/health"), { headers: authHeaders() });
        expect(res.status).toBe(200);
        expect(await res.json()).toEqual({
            protocolVersion: PROTOCOL_VERSION,
            pluginVersion: PLUGIN_VERSION,
            tuiEpoch: EPOCH,
            alive: true,
        });
    });

    test("BR-02：binding 返回目标快照", async () => {
        const server = startTestServer();
        const res = await fetch(urlFor(server, "/v1/binding"), { headers: authHeaders() });
        expect(res.status).toBe(200);
        expect(await res.json()).toEqual(BINDING);
    });

    test.each([
        ["applied", { status: "applied" } as AppendOutcome, 200],
        ["processing", { status: "processing" } as AppendOutcome, 202],
        ["uncertain", { status: "uncertain" } as AppendOutcome, 200],
        ["rejected", { status: "rejected", reason: "target_mismatch" } as AppendOutcome, 409],
        ["conflict", { status: "conflict" } as AppendOutcome, 409],
        ["stale_epoch", { status: "stale_epoch" } as AppendOutcome, 409],
        ["capacity_exceeded", { status: "capacity_exceeded" } as AppendOutcome, 429],
    ])("BR-04 状态映射：%s → %i", async (bodyStatus, outcome, http) => {
        const server = startTestServer({ appendOutcome: outcome });
        const res = await postJson(server, "/v1/draft/append", appendBody());
        expect(res.status).toBe(http);
        expect(await res.json()).toEqual({
            protocolVersion: PROTOCOL_VERSION,
            tuiEpoch: EPOCH,
            operationID: "op_001",
            status: bodyStatus,
            reason: bodyStatus === "rejected" ? "target_mismatch" : null,
        });
    });

    test("BR-05：查询已知 → 200；unknown → 404 unknown_operation", async () => {
        const known = startTestServer({ queryOutcome: { status: "cancelled" } });
        const res = await fetch(urlFor(known, "/v1/operations/op_9"), { headers: authHeaders() });
        expect(res.status).toBe(200);
        expect(await res.json()).toEqual({ status: "cancelled" });

        const missing = startTestServer({ queryOutcome: { status: "unknown" } });
        const res404 = await fetch(urlFor(missing, "/v1/operations/op_missing"), { headers: authHeaders() });
        expect(res404.status).toBe(404);
        expect(await res404.json()).toEqual({ error: { code: "unknown_operation" } });
    });

    test.each([
        ["cancelled", { status: "cancelled" } as CancelOutcome, 200],
        ["already_applied", { status: "already_applied" } as CancelOutcome, 200],
        ["identity_error", { status: "identity_error" } as CancelOutcome, 409],
    ])("BR-06 取消映射：%s → %i", async (bodyStatus, outcome, http) => {
        const server = startTestServer({ cancelOutcome: outcome });
        const res = await fetch(urlFor(server, "/v1/operations/op_1/cancel"), {
            method: "POST",
            headers: authHeaders(),
        });
        expect(res.status).toBe(http);
        expect(await res.json()).toEqual({ status: bodyStatus });
    });

    test("415：append 缺 Content-Type", async () => {
        const server = startTestServer();
        const res = await fetch(urlFor(server, "/v1/draft/append"), {
            method: "POST",
            headers: authHeaders(),
            body: JSON.stringify(appendBody()),
        });
        expect(res.status).toBe(415);
        expect(await res.json()).toEqual({ error: { code: "unsupported_media_type" } });
    });

    test("400：畸形 JSON", async () => {
        const server = startTestServer();
        const res = await postJson(server, "/v1/draft/append", "{not json");
        expect(res.status).toBe(400);
        expect(await res.json()).toEqual({ error: { code: "invalid_request" } });
    });

    test("400：schema 非法（版本错/缺字段/text 非字符串）", async () => {
        const server = startTestServer();
        for (const body of [appendBody({ protocolVersion: 2 }), appendBody({ sessionID: undefined }), appendBody({ text: 42 })]) {
            const res = await postJson(server, "/v1/draft/append", body as Record<string, unknown>);
            expect(res.status).toBe(400);
            expect(await res.json()).toEqual({ error: { code: "invalid_request" } });
        }
    });

    test("400：body 超过 BODY_MAX_BYTES（精确 8193 字节）", async () => {
        const server = startTestServer();
        const base = JSON.stringify(appendBody({ text: "" }));
        const body = JSON.stringify(appendBody({ text: "a".repeat(BODY_MAX_BYTES + 1 - base.length) }));
        expect(body.length).toBe(BODY_MAX_BYTES + 1);
        const res = await postJson(server, "/v1/draft/append", body);
        expect(res.status).toBe(400);
        expect(await res.json()).toEqual({ error: { code: "payload_too_large" } });
    });

    test("SEC-02：提交/审批/执行类路径不存在 → 404", async () => {
        const server = startTestServer();
        for (const path of ["/v1/submit", "/tui/submit-prompt", "/session/ses_1/message", "/permission/approve", "/shell/exec"]) {
            const res = await postJson(server, path, {});
            expect(res.status).toBe(404);
            expect(await res.json()).toEqual({ error: { code: "not_found" } });
        }
    });

    test("405：已知路径错误方法", async () => {
        const server = startTestServer();
        for (const [path, method] of [
            ["/v1/draft/append", "GET"],
            ["/v1/health", "POST"],
            ["/v1/binding", "POST"],
            ["/v1/operations/op_1", "POST"],
            ["/v1/operations/op_1/cancel", "GET"],
            ["/v1/state/stream", "POST"],
        ] as Array<[string, string]>) {
            const res = await fetch(urlFor(server, path), { method, headers: authHeaders() });
            expect(res.status).toBe(405);
            expect(await res.json()).toEqual({ error: { code: "method_not_allowed" } });
        }
    });

    test("404：未知路径（带合法认证）", async () => {
        const server = startTestServer();
        const res = await fetch(urlFor(server, "/v1/whatever"), { headers: authHeaders() });
        expect(res.status).toBe(404);
        expect(await res.json()).toEqual({ error: { code: "not_found" } });
    });

    test("BR-03：SSE 最小实现 → 200 text/event-stream，: connected 先于 snapshot", async () => {
        const server = startTestServer();
        const res = await withTimeout(fetch(urlFor(server, "/v1/state/stream"), { headers: authHeaders() }), 2000);
        expect(res.status).toBe(200);
        expect(res.headers.get("content-type")).toBe("text/event-stream");
        const body = res.body;
        if (!body) throw new Error("no body");
        const reader = body.getReader();
        const decoder = new TextDecoder();
        let collected = "";
        for (let i = 0; i < 20 && !collected.includes("event: snapshot"); i += 1) {
            const chunk = await withTimeout(reader.read(), 500);
            if (chunk.done) break;
            collected += decoder.decode(chunk.value, { stream: true });
        }
        expect(collected).toContain(": connected");
        expect(collected).toContain("event: snapshot");
        expect(collected).toContain(JSON.stringify(BINDING));
        expect(collected.indexOf(": connected")).toBeLessThan(collected.indexOf("event: snapshot"));
        await reader.cancel();
    });

    test("stop() 幂等且关闭监听", async () => {
        const server = startTestServer();
        const port = server.port;
        server.stop();
        server.stop();
        let failed = false;
        try {
            await withTimeout(fetch(`http://127.0.0.1:${port}/v1/health`, { headers: authHeaders() }), 2000);
        } catch {
            failed = true;
        }
        expect(failed).toBe(true);
    });
});
