// M4 桥接状态流扩展 — 验证 /v1/state/stream 的快照提供者与心跳（BR-03 扩展）
import { afterEach, describe, expect, test } from "bun:test";

import { startBridge } from "../../src/bridge/server";
import type {
    AppendOutcome,
    AppendRequest,
    BindingTarget,
    BridgeServer,
    CancelOutcome,
    QueryOutcome,
} from "../../src/core/types";
import type { StatusSnapshot } from "../../src/core/state/types";
import { reduceSnapshot } from "../../src/core/state/reduce";
import { buildSnapshotSource } from "../../src/plugin/stateRead";

const TOKEN = "bridge-state-test-token";
const EPOCH = "epoch-state-test";

const BINDING: BindingTarget = {
    epoch: EPOCH,
    boundSessionID: "ses_x",
    currentRouteSessionID: "ses_x",
    promptVisible: true,
    promptDisabled: false,
    dialogBlocking: false,
};

function snapshot(): StatusSnapshot {
    const source = buildSnapshotSource({
        epoch: EPOCH,
        revision: 1,
        sessionID: "ses_x",
        canAppend: true,
        blockReason: null,
        reads: {
            sessionStatus: "busy",
            todos: [],
            permissions: [],
            questions: [],
            session: undefined,
            messages: [],
            parts: [],
        },
        nowMs: 1_000_000,
        authoritativeReadOk: true,
    });
    const outcome = reduceSnapshot(undefined, source, source.readAtMs);
    if (outcome.kind !== "applied") throw new Error(`expected applied, got ${outcome.kind}`);
    return outcome.snapshot;
}

interface StateOption {
    snapshot: () => StatusSnapshot | null;
    heartbeatMs?: number;
}

const liveServers: BridgeServer[] = [];

function start(state?: StateOption): BridgeServer {
    const server = startBridge({
        token: TOKEN,
        tuiEpoch: EPOCH,
        pluginVersion: "0.1.0-test",
        handlers: {
            append: async (_req: AppendRequest) => ({ status: "applied" }) as AppendOutcome,
            cancel: () => ({ status: "cancelled" }) as CancelOutcome,
            query: () => ({ status: "applied" }) as QueryOutcome,
            binding: () => BINDING,
        },
        ...(state ? { state } : {}),
    });
    liveServers.push(server);
    return server;
}

afterEach(() => {
    while (liveServers.length > 0) {
        liveServers.pop()?.stop();
    }
});

async function readStream(port: number, stop: (collected: string) => boolean): Promise<string> {
    const res = await fetch(`http://127.0.0.1:${port}/v1/state/stream`, {
        headers: { authorization: `Bearer ${TOKEN}` },
    });
    expect(res.status).toBe(200);
    expect(res.headers.get("content-type")).toBe("text/event-stream");
    const body = res.body;
    if (!body) throw new Error("no body");
    const reader = body.getReader();
    const decoder = new TextDecoder();
    let collected = "";
    for (let i = 0; i < 60 && !stop(collected); i += 1) {
        const chunk = await reader.read();
        if (chunk.done) break;
        collected += decoder.decode(chunk.value, { stream: true });
    }
    await reader.cancel();
    return collected;
}

describe("M4 桥接状态流扩展", () => {
    test("提供 state 时，快照来自提供者而非 binding，且 : connected 先于 snapshot", async () => {
        const snap = snapshot();
        const server = start({ snapshot: () => snap, heartbeatMs: 10_000 });
        const text = await readStream(server.port, (s) => s.includes("event: snapshot"));
        expect(text.indexOf(": connected")).toBeLessThan(text.indexOf("event: snapshot"));
        expect(text).toContain(JSON.stringify(snap));
    });

    test("心跳周期后再次推送快照（≥2 次 event: snapshot）", async () => {
        const snap = snapshot();
        const server = start({ snapshot: () => snap, heartbeatMs: 30 });
        const text = await readStream(server.port, (s) => (s.match(/event: snapshot/g) ?? []).length >= 2);
        expect((text.match(/event: snapshot/g) ?? []).length).toBeGreaterThanOrEqual(2);
    });

    test("提供者返回 null 时仍推送 event: snapshot（内容 null），不抛错", async () => {
        const server = start({ snapshot: () => null, heartbeatMs: 10_000 });
        const text = await readStream(server.port, (s) => s.includes("event: snapshot"));
        expect(text).toContain("event: snapshot");
        expect(text).toContain("null");
    });
});
