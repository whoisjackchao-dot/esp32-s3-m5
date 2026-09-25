// M4 S4.1b StateStore — Selective TDD（T 类）
// Store 是归约器与桥接 SSE 之间的状态持有者：ingest→reduce→持有最新快照，并判过期。
import { describe, expect, test } from "bun:test";

import { StateStore } from "../../src/core/state/store";
import type { SnapshotSource } from "../../src/core/state/types";

const BASE_MS = 2_000_000;

function src(overrides: Partial<SnapshotSource> = {}): SnapshotSource {
    return {
        epoch: "ep1",
        revision: 1,
        sessionID: "ses_a",
        canAppend: true,
        blockReason: null,
        sessionStatus: "idle",
        pendingPermission: false,
        pendingQuestion: false,
        activeError: null,
        runningTools: [],
        failedTools: 0,
        rootIdleButChildBusy: false,
        chatText: null,
        todo: { known: false, completed: 0, total: 0 },
        projectName: null,
        sessionTitle: null,
        model: {},
        usage: { present: true },
        authoritativeReadOk: true,
        readAtMs: BASE_MS,
        ...overrides,
    };
}

describe("S4.1b StateStore", () => {
    test("首次 ingest 为 applied，current 就位", () => {
        const store = new StateStore();
        expect(store.current()).toBeUndefined();
        const out = store.ingest(src({ revision: 1 }), BASE_MS);
        expect(out.kind).toBe("applied");
        expect(store.current()?.identity.snapshotRevision).toBe(1);
    });

    test("递增 revision 更新 current", () => {
        const store = new StateStore();
        store.ingest(src({ revision: 1, sessionStatus: "idle" }), BASE_MS);
        store.ingest(src({ revision: 2, sessionStatus: "busy", readAtMs: BASE_MS + 500 }), BASE_MS + 500);
        expect(store.current()?.status).toBe("busy");
        expect(store.current()?.identity.snapshotRevision).toBe(2);
    });

    test("deduped 不改动 current", () => {
        const store = new StateStore();
        store.ingest(src({ revision: 3 }), BASE_MS);
        const before = store.current();
        const out = store.ingest(src({ revision: 3 }), BASE_MS);
        expect(out.kind).toBe("deduped");
        expect(store.current()).toBe(before);
    });

    test("dropped_stale 不改动 current", () => {
        const store = new StateStore();
        store.ingest(src({ revision: 5 }), BASE_MS);
        const out = store.ingest(src({ revision: 4 }), BASE_MS);
        expect(out.kind).toBe("dropped_stale");
        expect(store.current()?.identity.snapshotRevision).toBe(5);
    });

    test("protocol_error 不改动 current 并如实返回", () => {
        const store = new StateStore();
        store.ingest(src({ revision: 7, sessionStatus: "idle" }), BASE_MS);
        const out = store.ingest(src({ revision: 7, sessionStatus: "busy" }), BASE_MS);
        expect(out.kind).toBe("protocol_error");
        expect(store.current()?.status).toBe("idle");
    });

    test("epoch 变化替换 current", () => {
        const store = new StateStore();
        store.ingest(src({ epoch: "ep1", revision: 9 }), BASE_MS);
        store.ingest(src({ epoch: "ep2", revision: 1, sessionStatus: "busy" }), BASE_MS + 10);
        expect(store.current()?.identity.tuiEpoch).toBe("ep2");
    });

    test("isStale：10s 未过期、15s 过期；无快照视为 stale", () => {
        const store = new StateStore();
        expect(store.isStale(BASE_MS)).toBe(true);
        store.ingest(src({ readAtMs: BASE_MS }), BASE_MS);
        expect(store.isStale(BASE_MS + 10_000)).toBe(false);
        expect(store.isStale(BASE_MS + 15_000)).toBe(true);
    });

    test("reset 清空", () => {
        const store = new StateStore();
        store.ingest(src({}), BASE_MS);
        store.reset();
        expect(store.current()).toBeUndefined();
    });
});
