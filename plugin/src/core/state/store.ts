/**
 * M4 S4.1b — StateStore：归约器与桥接 SSE 之间的状态持有者。
 *
 * 职责：ingest(权威读取) → 复用 reduceSnapshot 归约 → 持有最新快照；
 * 提供 isStale（15s 无有效更新即过期，10s 心跳不刷新数据时间）。
 * 不含网络/BLE/UI。
 */
import { isExpired, reduceSnapshot } from "./reduce";
import type { SnapshotOutcome, SnapshotSource, StatusSnapshot } from "./types";

export class StateStore {
    #current: StatusSnapshot | undefined;

    ingest(source: SnapshotSource, nowMs: number): SnapshotOutcome {
        const outcome = reduceSnapshot(this.#current, source, nowMs);
        if (
            outcome.kind === "applied" ||
            outcome.kind === "deduped" ||
            outcome.kind === "dropped_stale"
        ) {
            this.#current = outcome.snapshot;
        }
        return outcome;
    }

    current(): StatusSnapshot | undefined {
        return this.#current;
    }

    isStale(nowMs: number): boolean {
        return this.#current === undefined || isExpired(this.#current, nowMs);
    }

    reset(): void {
        this.#current = undefined;
    }
}
