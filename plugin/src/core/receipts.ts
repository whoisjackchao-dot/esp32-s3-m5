import type { Clock, Epoch, OperationID, Receipt } from "./types";
import { RECEIPT_CAPACITY, RECEIPT_TTL_MS } from "./types";

function fnv1a32(input: string, seed: number): number {
    let hash = seed >>> 0;
    for (let i = 0; i < input.length; i += 1) {
        hash ^= input.charCodeAt(i);
        hash = Math.imul(hash, 0x01000193) >>> 0;
    }
    return hash >>> 0;
}

/** 双通道 FNV-1a：仅用于同 ID 载荷一致性比对（冲突检测），非安全摘要，不落日志（C02） */
export function fingerprintOf(epoch: Epoch, sessionID: string, text: string): string {
    const key = `${epoch}\u0000${sessionID}\u0000${text}`;
    const hi = fnv1a32(key, 0x811c9dc5);
    const lo = fnv1a32(key, 0x27d4eb2f);
    return `${hi.toString(16)}${lo.toString(16)}`;
}

export class ReceiptStore {
    private readonly entries = new Map<string, Receipt>();
    private readonly clock: Clock;
    private readonly capacity: number;
    private readonly ttlMs: number;

    constructor(clock: Clock, capacity: number = RECEIPT_CAPACITY, ttlMs: number = RECEIPT_TTL_MS) {
        this.clock = clock;
        this.capacity = capacity;
        this.ttlMs = ttlMs;
    }

    get capacityLimit(): number {
        return this.capacity;
    }

    private static key(epoch: Epoch, operationID: OperationID): string {
        return `${epoch}\u0000${operationID}`;
    }

    lookup(epoch: Epoch, operationID: OperationID): Receipt | undefined {
        return this.entries.get(ReceiptStore.key(epoch, operationID));
    }

    /** 记录/覆写回执；已存在时保留最初 createdAtMs（重试不得滑动 TTL） */
    record(receipt: Receipt): void {
        const key = ReceiptStore.key(receipt.epoch, receipt.operationID);
        const existing = this.entries.get(key);
        this.entries.set(key, existing ? { ...receipt, createdAtMs: existing.createdAtMs } : receipt);
    }

    /**
     * 取消语义：已有回执只翻转状态（保留原指纹与 createdAtMs，迟到去重仍可判别）；
     * 对未知操作创建无指纹墓碑（TX-10：迟到 append 由状态检查直接拒绝）。
     */
    markCancelled(epoch: Epoch, operationID: OperationID): void {
        const key = ReceiptStore.key(epoch, operationID);
        const existing = this.entries.get(key);
        if (existing) {
            this.entries.set(key, { ...existing, status: "cancelled" });
            return;
        }
        this.entries.set(key, {
            operationID,
            epoch,
            fingerprint: "",
            status: "cancelled",
            createdAtMs: this.clock.now(),
        });
    }

    /** C03：commit 到达 TUI 的事实不可倒转 —— 终态以实际写入结果为准 */
    setTerminal(epoch: Epoch, operationID: OperationID, status: "applied" | "uncertain"): void {
        const key = ReceiptStore.key(epoch, operationID);
        const existing = this.entries.get(key);
        if (!existing) {
            this.entries.set(key, {
                operationID,
                epoch,
                fingerprint: "",
                status,
                createdAtMs: this.clock.now(),
            });
            return;
        }
        this.entries.set(key, { ...existing, status });
    }

    purgeExpired(): number {
        const now = this.clock.now();
        let purged = 0;
        for (const [key, receipt] of this.entries) {
            if (receipt.createdAtMs + this.ttlMs <= now) {
                this.entries.delete(key);
                purged += 1;
            }
        }
        return purged;
    }

    size(): number {
        return this.entries.size;
    }
}
