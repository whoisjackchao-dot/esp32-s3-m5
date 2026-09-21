import type {
    AppendOutcome,
    AppendRequest,
    BindingTarget,
    CancelOutcome,
    DraftCoordinator,
    Epoch,
    OperationID,
    QueryOutcome,
} from "./types";
import { RECEIPT_CAPACITY, RECEIPT_TTL_MS, TEXT_MAX_LEN } from "./types";
import { ReceiptStore, fingerprintOf } from "./receipts";

/** 请求载荷非法：桥接层捕获后映射 HTTP 400（契约 §3.1 载荷非法） */
export class CoordinatorValidationError extends Error {
    constructor(message: string) {
        super(message);
        this.name = "CoordinatorValidationError";
    }
}

export function createCoordinator(options: import("./types").CoordinatorOptions): DraftCoordinator {
    const capacity = options.capacity ?? RECEIPT_CAPACITY;
    const ttlMs = options.receiptTtlMs ?? RECEIPT_TTL_MS;
    const store = new ReceiptStore(options.clock, capacity, ttlMs);
    const now = () => options.clock.now();
    const fingerprint = (req: AppendRequest) => fingerprintOf(req.tuiEpoch, req.sessionID, req.text);

    function rejectFor(reason: "target_mismatch" | "dialog_blocking" | "not_visible" | "disabled"): AppendOutcome {
        return { status: "rejected", reason };
    }

    return {
        async handleAppend(req: AppendRequest, target: BindingTarget, commit: import("./types").CommitFn): Promise<AppendOutcome> {
            if (req.protocolVersion !== 1) {
                throw new CoordinatorValidationError("protocolVersion must be 1");
            }
            if (req.tuiEpoch !== target.epoch) {
                return { status: "stale_epoch" };
            }
            if (req.text.length === 0) {
                throw new CoordinatorValidationError("text is empty");
            }
            if (req.text.length > TEXT_MAX_LEN) {
                throw new CoordinatorValidationError(`text exceeds TEXT_MAX_LEN (${TEXT_MAX_LEN})`);
            }
            store.purgeExpired();

            const fp = fingerprint(req);
            const existing = store.lookup(req.tuiEpoch, req.operationID);
            if (existing) {
                // TX-10/C04：取消墓碑优先于指纹比对 —— 墓碑无指纹，同 ID 迟到 append 一律拒绝写入
                if (existing.status === "cancelled") {
                    return rejectFor("disabled");
                }
                if (existing.fingerprint !== fp) {
                    return { status: "conflict" };
                }
                switch (existing.status) {
                    case "applied":
                        return { status: "applied" };
                    case "uncertain":
                        return { status: "uncertain" };
                    case "processing":
                        return { status: "processing" };
                    case "rejected":
                        // 不可达：协调器不落 rejected 回执（拒绝不记录，重试即全新尝试）
                        break;
                }
            }

            // 目标校验顺序即契约（TUI-04/TUI-07）：commit 不得在任何拒绝路径被调用
            if (target.currentRouteSessionID === null || target.currentRouteSessionID !== req.sessionID) {
                return rejectFor("target_mismatch");
            }
            if (target.dialogBlocking) {
                return rejectFor("dialog_blocking");
            }
            if (!target.promptVisible) {
                return rejectFor("not_visible");
            }
            if (target.promptDisabled) {
                return rejectFor("disabled");
            }

            // TUI-15/C04：仅新 operation 受容量限制；已有回执的操作仍可查询/取消
            if (!existing && store.size() >= capacity) {
                return { status: "capacity_exceeded" };
            }

            store.record({
                operationID: req.operationID,
                epoch: req.tuiEpoch,
                fingerprint: fp,
                status: "processing",
                createdAtMs: now(),
            });

            let result: "applied" | "uncertain";
            try {
                result = await commit(req.text);
            } catch {
                // TUI-14：set 可能已发生而 readback 失败 → uncertain，不自动重试
                result = "uncertain";
            }
            // C03：set 已到达 TUI 的事实不可被中途取消墓碑倒转，终态以 commit 实际结果为准
            store.setTerminal(req.tuiEpoch, req.operationID, result);
            return result === "applied" ? { status: "applied" } : { status: "uncertain" };
        },

        handleCancel(epoch: Epoch, operationID: OperationID): CancelOutcome {
            if (epoch !== options.epoch) {
                return { status: "identity_error" };
            }
            store.purgeExpired();
            const existing = store.lookup(epoch, operationID);
            if (existing) {
                // TX-11：commit 已发生 → 不撤销草稿；uncertain 按安全方向同样拒绝自动重试
                if (existing.status === "applied" || existing.status === "uncertain") {
                    return { status: "already_applied" };
                }
                if (existing.status === "cancelled") {
                    return { status: "cancelled" };
                }
            }
            store.markCancelled(epoch, operationID);
            return { status: "cancelled" };
        },

        handleQuery(epoch: Epoch, operationID: OperationID): QueryOutcome {
            if (epoch !== options.epoch) {
                return { status: "unknown" };
            }
            store.purgeExpired();
            const existing = store.lookup(epoch, operationID);
            if (!existing) {
                // TUI-13：unknown 不证明请求未来不会到达
                return { status: "unknown" };
            }
            return existing.reason === undefined
                ? { status: existing.status }
                : { status: existing.status, reason: existing.reason };
        },

        size: () => store.size(),
    };
}
