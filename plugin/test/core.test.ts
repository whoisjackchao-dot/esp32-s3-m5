// M1 核心协调器 Selective TDD —— 用例 ID 对应 StickS3_OpenCode_测试用例与追踪矩阵.md
import { describe, expect, test } from "bun:test";

import { CoordinatorValidationError, createCoordinator } from "../src/core/coordinator";
import { appendWithSeparator, RECEIPT_TTL_MS, TEXT_MAX_LEN } from "../src/core/types";
import type { AppendRequest, BindingTarget, Clock, CommitFn } from "../src/core/types";

class ManualClock implements Clock {
    #now: number;
    constructor(start = 1_000_000) {
        this.#now = start;
    }
    now(): number {
        return this.#now;
    }
    advance(ms: number): void {
        this.#now += ms;
    }
}

function deferred<T>(): { promise: Promise<T>; resolve: (value: T) => void } {
    let resolve!: (value: T) => void;
    const promise = new Promise<T>((res) => {
        resolve = res;
    });
    return { promise, resolve };
}

const EPOCH = "epoch-test";
const BASE_MS = 1_000_000;

function makeReq(overrides: Partial<AppendRequest> = {}): AppendRequest {
    return {
        protocolVersion: 1,
        tuiEpoch: EPOCH,
        sessionID: "ses_a",
        operationID: "op_1",
        transactionGeneration: 1,
        text: "修复 API",
        ...overrides,
    };
}

function makeTarget(overrides: Partial<BindingTarget> = {}): BindingTarget {
    return {
        epoch: EPOCH,
        boundSessionID: "ses_a",
        currentRouteSessionID: "ses_a",
        promptVisible: true,
        promptDisabled: false,
        dialogBlocking: false,
        ...overrides,
    };
}

function countingCommit(calls: string[]): CommitFn {
    return async (text) => {
        calls.push(text);
        return "applied";
    };
}

describe("appendWithSeparator（TUI-02/03 分隔规则）", () => {
    test("空原稿直接追加", () => {
        expect(appendWithSeparator("", "修复 API")).toBe("修复 API");
    });
    test("末尾有空白直接追加，不额外插分隔", () => {
        expect(appendWithSeparator("已有说明 ", "修复 API")).toBe("已有说明 修复 API");
        expect(appendWithSeparator("已有说明\n", "修复 API")).toBe("已有说明\n修复 API");
    });
    test("非空且末尾无空白 → 插入单个换行", () => {
        expect(appendWithSeparator("已有说明", "修复 API")).toBe("已有说明\n修复 API");
    });
});

describe("M1 核心协调器", () => {
    test("TUI-01：正常追加 → applied，文本精确，回执可查", async () => {
        const calls: string[] = [];
        const coord = createCoordinator({ epoch: EPOCH, clock: new ManualClock() });
        const outcome = await coord.handleAppend(makeReq({ text: "修复 API" }), makeTarget(), countingCommit(calls));
        expect(outcome).toEqual({ status: "applied" });
        expect(calls).toEqual(["修复 API"]);
        expect(coord.handleQuery(EPOCH, "op_1")).toEqual({ status: "applied" });
    });

    test("TUI-04：绑定 A 但当前 B → target_mismatch 且不调用 commit", async () => {
        const calls: string[] = [];
        const coord = createCoordinator({ epoch: EPOCH, clock: new ManualClock() });
        const outcome = await coord.handleAppend(
            makeReq(),
            makeTarget({ currentRouteSessionID: "ses_b" }),
            countingCommit(calls),
        );
        expect(outcome).toEqual({ status: "rejected", reason: "target_mismatch" });
        expect(calls).toHaveLength(0);
    });

    test("TUI-07适用：home(null 路由)/模态阻塞/不可见/禁用 各自拒绝且不写入", async () => {
        const cases = [
            { target: makeTarget({ currentRouteSessionID: null }), reason: "target_mismatch" },
            { target: makeTarget({ dialogBlocking: true }), reason: "dialog_blocking" },
            { target: makeTarget({ promptVisible: false }), reason: "not_visible" },
            { target: makeTarget({ promptDisabled: true }), reason: "disabled" },
        ] as const;
        for (const c of cases) {
            const calls: string[] = [];
            const coord = createCoordinator({ epoch: EPOCH, clock: new ManualClock() });
            const outcome = await coord.handleAppend(makeReq(), c.target, countingCommit(calls));
            expect(outcome).toEqual({ status: "rejected", reason: c.reason });
            expect(calls).toHaveLength(0);
        }
    });

    test("TUI-09：完成后同 ID 同 payload 重发 → 去重回放 applied，不再 commit", async () => {
        const calls: string[] = [];
        const coord = createCoordinator({ epoch: EPOCH, clock: new ManualClock() });
        await coord.handleAppend(makeReq(), makeTarget(), countingCommit(calls));
        const outcome = await coord.handleAppend(makeReq(), makeTarget(), countingCommit(calls));
        expect(outcome).toEqual({ status: "applied" });
        expect(calls).toHaveLength(1);
    });

    test("TUI-09：同 ID 在途期间重复 append → processing，不重复提交", async () => {
        const gate = deferred<"applied" | "uncertain">();
        let calls = 0;
        const commit: CommitFn = async () => {
            calls += 1;
            return gate.promise;
        };
        const coord = createCoordinator({ epoch: EPOCH, clock: new ManualClock() });
        const first = coord.handleAppend(makeReq(), makeTarget(), commit);
        const second = await coord.handleAppend(makeReq(), makeTarget(), commit);
        expect(second).toEqual({ status: "processing" });
        gate.resolve("applied");
        expect(await first).toEqual({ status: "applied" });
        expect(calls).toBe(1);
    });

    test("TUI-10：同 ID 换正文或换 session → conflict，首份回执不被覆盖", async () => {
        const coord = createCoordinator({ epoch: EPOCH, clock: new ManualClock() });
        await coord.handleAppend(makeReq({ text: "第一版" }), makeTarget(), async () => "applied");
        const withOtherText = await coord.handleAppend(
            makeReq({ text: "第二版" }),
            makeTarget(),
            async () => "applied",
        );
        const withOtherSession = await coord.handleAppend(
            makeReq({ sessionID: "ses_b" }),
            makeTarget({ currentRouteSessionID: "ses_b", boundSessionID: "ses_b" }),
            async () => "applied",
        );
        expect(withOtherText).toEqual({ status: "conflict" });
        expect(withOtherSession).toEqual({ status: "conflict" });
        expect(coord.handleQuery(EPOCH, "op_1")).toEqual({ status: "applied" });
    });

    test("TUI-10：过期 epoch → stale_epoch，不产生回执", async () => {
        const coord = createCoordinator({ epoch: EPOCH, clock: new ManualClock() });
        const outcome = await coord.handleAppend(
            makeReq({ tuiEpoch: "epoch-old" }),
            makeTarget(),
            async () => "applied",
        );
        expect(outcome).toEqual({ status: "stale_epoch" });
        expect(coord.handleQuery("epoch-old", "op_1")).toEqual({ status: "unknown" });
    });

    test("TUI-14：commit 返回 uncertain → uncertain，不自动重试", async () => {
        let calls = 0;
        const coord = createCoordinator({ epoch: EPOCH, clock: new ManualClock() });
        const outcome = await coord.handleAppend(makeReq(), makeTarget(), async () => {
            calls += 1;
            return "uncertain";
        });
        expect(outcome).toEqual({ status: "uncertain" });
        expect(calls).toBe(1);
        expect(coord.handleQuery(EPOCH, "op_1")).toEqual({ status: "uncertain" });
    });

    test("TUI-14：commit 抛异常 → uncertain（无法证明未写入）", async () => {
        const coord = createCoordinator({ epoch: EPOCH, clock: new ManualClock() });
        const outcome = await coord.handleAppend(makeReq(), makeTarget(), async () => {
            throw new Error("readback failed");
        });
        expect(outcome).toEqual({ status: "uncertain" });
        expect(coord.handleQuery(EPOCH, "op_1")).toEqual({ status: "uncertain" });
    });

    test("TUI-15：容量满 → 新 operation 在写入前被拒；既有回执仍可查询/取消", async () => {
        const calls: string[] = [];
        const coord = createCoordinator({ epoch: EPOCH, clock: new ManualClock(), capacity: 2 });
        await coord.handleAppend(makeReq({ operationID: "op_1", text: "一" }), makeTarget(), countingCommit(calls));
        await coord.handleAppend(makeReq({ operationID: "op_2", text: "二" }), makeTarget(), countingCommit(calls));
        expect(coord.size()).toBe(2);
        const third = await coord.handleAppend(
            makeReq({ operationID: "op_3", text: "三" }),
            makeTarget(),
            countingCommit(calls),
        );
        expect(third).toEqual({ status: "capacity_exceeded" });
        expect(calls).toHaveLength(2);
        expect(coord.size()).toBe(2);
        expect(coord.handleQuery(EPOCH, "op_1")).toEqual({ status: "applied" });
        // 容量满不影响既有操作的取消（TX-11：已 applied → already_applied）
        expect(coord.handleCancel(EPOCH, "op_1")).toEqual({ status: "already_applied" });
    });

    test("TX-10：取消先于 append 到达 → 迟到 append 被拒且不写入", async () => {
        const calls: string[] = [];
        const coord = createCoordinator({ epoch: EPOCH, clock: new ManualClock() });
        expect(coord.handleCancel(EPOCH, "op_1")).toEqual({ status: "cancelled" });
        const outcome = await coord.handleAppend(makeReq(), makeTarget(), countingCommit(calls));
        expect(outcome).toEqual({ status: "rejected", reason: "disabled" });
        expect(calls).toHaveLength(0);
        expect(coord.handleQuery(EPOCH, "op_1")).toEqual({ status: "cancelled" });
    });

    test("TX-10屏障：commit 在途时取消 → cancelled；commit 完成 → 以实际结果为准（C03 不可倒转）", async () => {
        const gate = deferred<"applied" | "uncertain">();
        const coord = createCoordinator({ epoch: EPOCH, clock: new ManualClock() });
        const pending = coord.handleAppend(makeReq(), makeTarget(), () => gate.promise);
        expect(coord.handleCancel(EPOCH, "op_1")).toEqual({ status: "cancelled" });
        expect(coord.handleQuery(EPOCH, "op_1")).toEqual({ status: "cancelled" });
        gate.resolve("applied");
        expect(await pending).toEqual({ status: "applied" });
        expect(coord.handleQuery(EPOCH, "op_1")).toEqual({ status: "applied" });
        const late = await coord.handleAppend(makeReq(), makeTarget(), async () => "applied");
        expect(late).toEqual({ status: "applied" });
    });

    test("TX-11屏障：commit 终态先记录 → 取消返回 already_applied", async () => {
        const gate = deferred<"applied" | "uncertain">();
        const coord = createCoordinator({ epoch: EPOCH, clock: new ManualClock() });
        const pending = coord.handleAppend(makeReq(), makeTarget(), () => gate.promise);
        gate.resolve("applied");
        expect(await pending).toEqual({ status: "applied" });
        expect(coord.handleCancel(EPOCH, "op_1")).toEqual({ status: "already_applied" });
        expect(coord.handleQuery(EPOCH, "op_1")).toEqual({ status: "applied" });
    });

    test("TX-12适用：墓碑经 TTL 过期 → 查询 unknown", async () => {
        const clock = new ManualClock(BASE_MS);
        const coord = createCoordinator({ epoch: EPOCH, clock });
        expect(coord.handleCancel(EPOCH, "op_1")).toEqual({ status: "cancelled" });
        clock.advance(RECEIPT_TTL_MS);
        expect(coord.handleQuery(EPOCH, "op_1")).toEqual({ status: "unknown" });
    });

    test("TX-13适用：重复取消/去重不滑动延长 TTL", async () => {
        const clock = new ManualClock(BASE_MS);
        const coord = createCoordinator({ epoch: EPOCH, clock });
        expect(coord.handleCancel(EPOCH, "op_1")).toEqual({ status: "cancelled" });
        clock.advance(RECEIPT_TTL_MS - 1000);
        expect(coord.handleCancel(EPOCH, "op_1")).toEqual({ status: "cancelled" });
        clock.advance(1000);
        expect(coord.handleQuery(EPOCH, "op_1")).toEqual({ status: "unknown" });
    });

    test("cancel 语义矩阵：applied/uncertain → already_applied；cancelled → cancelled 幂等", async () => {
        const coord = createCoordinator({ epoch: EPOCH, clock: new ManualClock() });
        await coord.handleAppend(makeReq({ operationID: "op_a" }), makeTarget(), async () => "applied");
        await coord.handleAppend(makeReq({ operationID: "op_b" }), makeTarget(), async () => "uncertain");
        expect(coord.handleCancel(EPOCH, "op_a")).toEqual({ status: "already_applied" });
        expect(coord.handleCancel(EPOCH, "op_b")).toEqual({ status: "already_applied" });
        expect(coord.handleCancel(EPOCH, "op_c")).toEqual({ status: "cancelled" });
        expect(coord.handleCancel(EPOCH, "op_c")).toEqual({ status: "cancelled" });
    });

    test("epoch 不匹配：cancel → identity_error；query → unknown", () => {
        const coord = createCoordinator({ epoch: EPOCH, clock: new ManualClock() });
        expect(coord.handleCancel("epoch-other", "op_1")).toEqual({ status: "identity_error" });
        expect(coord.handleQuery("epoch-other", "op_1")).toEqual({ status: "unknown" });
    });

    test("空文本/超长文本 → CoordinatorValidationError（桥接映射 400）", async () => {
        const coord = createCoordinator({ epoch: EPOCH, clock: new ManualClock() });
        await expect(coord.handleAppend(makeReq({ text: "" }), makeTarget(), async () => "applied")).rejects.toBeInstanceOf(
            CoordinatorValidationError,
        );
        await expect(
            coord.handleAppend(makeReq({ text: "x".repeat(TEXT_MAX_LEN + 1) }), makeTarget(), async () => "applied"),
        ).rejects.toBeInstanceOf(CoordinatorValidationError);
    });
});
