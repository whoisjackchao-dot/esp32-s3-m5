/**
 * M4 S4.1 — M10 精简快照归约（纯函数）
 *
 * 依据 契约 C08 与 HLD §7.4。归约只依据权威状态与 pending 权限/问题，
 * 不读聊天正文猜状态；用量取当前投影（重复快照不翻倍，删除后不维持最大值）。
 */
import { PROTOCOL_VERSION } from "../types";
import type {
    Availability,
    NotifyDecision,
    ProgrammingStatus,
    SnapshotOutcome,
    SnapshotSource,
    StatusSnapshot,
    SubtaskHint,
    ToolRun,
    UsageBreakdown,
} from "./types";

export const FRESHNESS_LIMIT_MS = 15_000;

function avail<T>(value: T | null | undefined): Availability<T> {
    if (value === undefined) {
        return { kind: "unknown" };
    }
    if (value === null) {
        return { kind: "not_reported" };
    }
    return { kind: "known", value };
}

export function deriveStatus(src: SnapshotSource): ProgrammingStatus {
    if (src.pendingPermission || src.pendingQuestion) {
        return "waiting";
    }
    if (src.sessionStatus === "retry") {
        return "retry";
    }
    if (src.sessionStatus === "busy") {
        return "busy";
    }
    if (src.rootIdleButChildBusy) {
        return "busy";
    }
    if (src.activeError !== null) {
        return "error";
    }
    if (src.sessionStatus === "idle") {
        return "idle";
    }
    return "unknown";
}

function deriveHint(src: SnapshotSource): SubtaskHint {
    if (src.sessionStatus === "busy" && src.failedTools > 0) {
        return "tool_failed";
    }
    if (src.rootIdleButChildBusy) {
        return "child_busy";
    }
    return null;
}

function aggregateTools(names: readonly string[]): readonly ToolRun[] {
    const counts = new Map<string, number>();
    for (const name of names) {
        counts.set(name, (counts.get(name) ?? 0) + 1);
    }
    return [...counts.entries()].map(([name, count]) => ({ name, count }));
}

function buildUsage(read: SnapshotSource["usage"]): UsageBreakdown {
    if (!read.present) {
        return {
            input: { kind: "unknown" },
            output: { kind: "unknown" },
            reasoning: { kind: "unknown" },
            cacheRead: { kind: "unknown" },
            cacheWrite: { kind: "unknown" },
        };
    }
    return {
        input: avail(read.input),
        output: avail(read.output),
        reasoning: avail(read.reasoning),
        cacheRead: avail(read.cacheRead),
        cacheWrite: avail(read.cacheWrite),
    };
}

function buildSnapshot(src: SnapshotSource, nowMs: number, prev?: StatusSnapshot): StatusSnapshot {
    const dataAtMs = src.authoritativeReadOk
        ? src.readAtMs
        : (prev?.freshness.dataAtMs ?? nowMs);
    return {
        identity: {
            protocolVersion: PROTOCOL_VERSION,
            tuiEpoch: src.epoch,
            sessionID: src.sessionID,
            snapshotRevision: src.revision,
        },
        bind: { canAppend: src.canAppend, blockReason: src.blockReason },
        status: deriveStatus(src),
        errorCode: src.activeError,
        subtaskHint: deriveHint(src),
        project: avail(src.projectName),
        sessionTitle: avail(src.sessionTitle),
        tools: aggregateTools(src.runningTools),
        todos: src.todo.known
            ? { kind: "known", value: { completed: src.todo.completed, total: src.todo.total } }
            : { kind: "unknown" },
        model: { current: avail(src.model.current), lastUsed: avail(src.model.lastUsed) },
        usage: buildUsage(src.usage),
        cost: src.usage.present ? avail(src.usage.cost) : { kind: "unknown" },
        freshness: { authoritativeReadOk: src.authoritativeReadOk, dataAtMs },
    };
}

function contentKey(snapshot: StatusSnapshot): string {
    return JSON.stringify({
        bind: snapshot.bind,
        status: snapshot.status,
        errorCode: snapshot.errorCode,
        subtaskHint: snapshot.subtaskHint,
        project: snapshot.project,
        sessionTitle: snapshot.sessionTitle,
        tools: snapshot.tools,
        todos: snapshot.todos,
        model: snapshot.model,
        usage: snapshot.usage,
        cost: snapshot.cost,
        dataAtMs: snapshot.freshness.dataAtMs,
    });
}

export function isExpired(snapshot: StatusSnapshot, nowMs: number): boolean {
    return nowMs - snapshot.freshness.dataAtMs >= FRESHNESS_LIMIT_MS;
}

export function decideNotify(prev: StatusSnapshot, next: StatusSnapshot): NotifyDecision {
    if (next.status === "waiting" && prev.status !== "waiting") {
        return "waiting";
    }
    if (next.status === "error" && (prev.status !== "error" || next.errorCode !== prev.errorCode)) {
        return "error";
    }
    return "none";
}

export function reduceSnapshot(
    prev: StatusSnapshot | undefined,
    src: SnapshotSource,
    nowMs: number,
): SnapshotOutcome {
    const next = buildSnapshot(src, nowMs, prev);
    if (!prev) {
        return { kind: "applied", snapshot: next, notify: "none" };
    }
    if (src.epoch !== prev.identity.tuiEpoch) {
        return { kind: "applied", snapshot: next, notify: decideNotify(prev, next) };
    }
    if (src.revision < prev.identity.snapshotRevision) {
        return { kind: "dropped_stale", snapshot: prev };
    }
    if (src.revision === prev.identity.snapshotRevision) {
        if (contentKey(prev) === contentKey(next)) {
            return { kind: "deduped", snapshot: prev, notify: "none" };
        }
        return { kind: "protocol_error", reason: "same revision with conflicting content" };
    }
    return { kind: "applied", snapshot: next, notify: decideNotify(prev, next) };
}
