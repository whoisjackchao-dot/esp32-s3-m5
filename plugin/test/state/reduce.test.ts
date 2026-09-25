// M4 S4.1 M10 精简快照归约 — Selective TDD（T 类）
// 用例对应 StickS3_OpenCode_测试用例与追踪矩阵.md 的 ST-01—ST-12；
// 字段语义对应 HLD §7.4 BR-03 快照字典与契约 C08。
import { describe, expect, test } from "bun:test";

import {
    FRESHNESS_LIMIT_MS,
    decideNotify,
    deriveStatus,
    isExpired,
    reduceSnapshot,
} from "../../src/core/state/reduce";
import type { SnapshotSource, StatusSnapshot } from "../../src/core/state/types";

const BASE_MS = 1_000_000;

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

function apply(s: SnapshotSource, prev?: StatusSnapshot): StatusSnapshot {
    const out = reduceSnapshot(prev, s, s.readAtMs);
    if (out.kind !== "applied" && out.kind !== "deduped" && out.kind !== "dropped_stale") {
        throw new Error(`unexpected outcome: ${out.kind}`);
    }
    return out.snapshot;
}

describe("S4.1 编程状态归约（ST-01/02/03/04）", () => {
    test("ST-01 idle/busy/retry 映射正确，idle 不表示任务完成", () => {
        expect(deriveStatus(src({ sessionStatus: "idle" }))).toBe("idle");
        expect(deriveStatus(src({ sessionStatus: "busy" }))).toBe("busy");
        expect(deriveStatus(src({ sessionStatus: "retry" }))).toBe("retry");
        expect(apply(src({ sessionStatus: "idle" })).status).toBe("idle");
    });

    test("ST-02 pending permission/question => waiting，且不产生审批动作", () => {
        expect(deriveStatus(src({ pendingPermission: true }))).toBe("waiting");
        expect(deriveStatus(src({ pendingQuestion: true }))).toBe("waiting");
    });

    test("ST-03 纯文本『请确认』不猜成结构化 waiting", () => {
        expect(deriveStatus(src({ chatText: "请确认是否继续" }))).toBe("idle");
    });

    test("ST-04 并行工具其一失败：busy 与工具失败提示共存，不互相伪造", () => {
        const s = src({ sessionStatus: "busy", runningTools: ["edit", "bash"], failedTools: 1 });
        expect(deriveStatus(s)).toBe("busy");
        const snap = apply(s);
        expect(snap.tools.map((t) => t.name)).toEqual(["edit", "bash"]);
        expect(snap.subtaskHint).toBe("tool_failed");
        expect(snap.status).toBe("busy");
    });
});

describe("S4.1 待办与用量（ST-05/06/07/08）", () => {
    test("ST-05 空 todo 与 2/3 分别显示，无百分比推断", () => {
        const empty = apply(src({ todo: { known: true, completed: 0, total: 0 } }));
        expect(empty.todos.kind).toBe("known");
        expect(empty.todos.kind === "known" && empty.todos.value.total).toBe(0);

        const two = apply(src({ todo: { known: true, completed: 2, total: 3 } }));
        expect(two.todos.kind === "known" && two.todos.value).toEqual({ completed: 2, total: 3 });
    });

    test("ST-06 累计快照重复送达不翻倍", () => {
        const u = { present: true, input: 100, output: 20, cost: 0.03 };
        const first = apply(src({ usage: u }));
        const prev = reduceSnapshot(undefined, src({ usage: u }), BASE_MS);
        const second = prev.kind === "applied"
            ? reduceSnapshot(prev.snapshot, src({ usage: u, revision: 2, readAtMs: BASE_MS + 1000 }), BASE_MS + 1000)
            : undefined;
        const snap = second && second.kind === "applied" ? second.snapshot : first;
        expect(snap.usage.input).toEqual({ kind: "known", value: 100 });
        expect(snap.cost).toEqual({ kind: "known", value: 0.03 });
    });

    test("ST-07 累计因删除记录减少：取当前投影，不维持历史最大值", () => {
        const high = src({ usage: { present: true, input: 500 } });
        const p = apply(high);
        const low = src({ usage: { present: true, input: 300 }, revision: 2, readAtMs: BASE_MS + 1000 });
        const out = reduceSnapshot(p, low, BASE_MS + 1000);
        expect(out.kind).toBe("applied");
        if (out.kind === "applied") {
            expect(out.snapshot.usage.input).toEqual({ kind: "known", value: 300 });
        }
    });

    test("ST-08 区分 unknown / not_reported / known(0)", () => {
        const snap = apply(src({
            model: {},
            usage: { present: true, input: 0, cost: null, output: undefined },
        }));
        expect(snap.usage.input).toEqual({ kind: "known", value: 0 });
        expect(snap.cost).toEqual({ kind: "not_reported" });
        expect(snap.usage.output).toEqual({ kind: "unknown" });
        expect(snap.model.current).toEqual({ kind: "unknown" });
    });

    test("ST-08b 整体 usage 缺失 => 全 unknown（不伪装 0 费）", () => {
        const snap = apply(src({ usage: { present: false } }));
        expect(snap.usage.input).toEqual({ kind: "unknown" });
        expect(snap.cost).toEqual({ kind: "unknown" });
    });
});

describe("S4.1 模型与新鲜度（ST-09/10）", () => {
    test("ST-09 当前模型与最近使用标签，技术名原样保留", () => {
        const snap = apply(src({ model: { current: "claude-sonnet-4.5", lastUsed: "gpt-5" } }));
        expect(snap.model.current).toEqual({ kind: "known", value: "claude-sonnet-4.5" });
        expect(snap.model.lastUsed).toEqual({ kind: "known", value: "gpt-5" });
    });

    test("ST-10 10s 心跳不刷新数据时间，15s 到期；旧 revision 不回滚", () => {
        const s1 = apply(src({ readAtMs: BASE_MS }));
        expect(isExpired(s1, BASE_MS + 10_000)).toBe(false);
        expect(isExpired(s1, BASE_MS + FRESHNESS_LIMIT_MS)).toBe(true);

        const stale = reduceSnapshot(s1, src({ revision: 0, readAtMs: BASE_MS + 1 }), BASE_MS + 1);
        expect(stale.kind).toBe("dropped_stale");
        if (stale.kind === "dropped_stale") {
            expect(stale.snapshot.identity.snapshotRevision).toBe(1);
        }
    });
});

describe("S4.1 去重/协议错误/子任务（ST-11/12 + C08）", () => {
    test("ST-11 重复错误/等待不重复提醒，新转换各提醒一次", () => {
        const none = apply(src({}));
        const waiting = apply(src({ pendingPermission: true, revision: 2, readAtMs: BASE_MS + 100 }), none);
        expect(decideNotify(none, waiting)).toBe("waiting");
        expect(decideNotify(waiting, waiting)).toBe("none");

        const err = apply(src({ activeError: "E_TIMEOUT", revision: 3, readAtMs: BASE_MS + 200 }), waiting);
        expect(decideNotify(waiting, err)).toBe("error");
        expect(decideNotify(err, err)).toBe("none");
    });

    test("ST-12 根 idle 但子任务 busy：不显示总完成，状态为 busy，子费用不入根累计", () => {
        const s = src({ sessionStatus: "idle", rootIdleButChildBusy: true, usage: { present: true, input: 10 } });
        expect(deriveStatus(s)).toBe("busy");
        const snap = apply(s);
        expect(snap.subtaskHint).toBe("child_busy");
        expect(snap.usage.input).toEqual({ kind: "known", value: 10 });
    });

    test("C08 同 revision 不同内容 => protocol_error；同 epoch 递增 => applied", () => {
        const p = apply(src({ revision: 5, sessionStatus: "idle" }));
        const conflict = reduceSnapshot(p, src({ revision: 5, sessionStatus: "busy" }), BASE_MS);
        expect(conflict.kind).toBe("protocol_error");

        const same = reduceSnapshot(p, src({ revision: 5 }), BASE_MS);
        expect(same.kind).toBe("deduped");

        const next = reduceSnapshot(p, src({ revision: 6, sessionStatus: "busy" }), BASE_MS + 500);
        expect(next.kind).toBe("applied");
    });

    test("C08 epoch 变化 => 重新绑定，revision 基线重置", () => {
        const p = apply(src({ epoch: "ep1", revision: 9 }));
        const out = reduceSnapshot(p, src({ epoch: "ep2", revision: 1, sessionStatus: "busy" }), BASE_MS + 10);
        expect(out.kind).toBe("applied");
        if (out.kind === "applied") {
            expect(out.snapshot.identity.tuiEpoch).toBe("ep2");
            expect(out.snapshot.identity.snapshotRevision).toBe(1);
        }
    });
});
