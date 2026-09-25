/**
 * M4 S4.1b — TUI 状态适配：把 api.state.session.* 的原生读取映射为 SnapshotSource。
 *
 * 只依据权威状态与 pending 权限/问题；不读聊天正文猜状态（C08）。
 * 模型/用量在未接线前保守为 unknown（不伪装 0）。映射为纯函数，便于单测。
 */
import type { SnapshotSource, UsageRead } from "../core/state/types";

export interface TuiReads {
    readonly sessionStatus: unknown;
    readonly todos: readonly unknown[];
    readonly permissions: readonly unknown[];
    readonly questions: readonly unknown[];
    readonly session: unknown;
    readonly messages: readonly unknown[];
    readonly parts: readonly unknown[];
}

export interface ReadInput {
    readonly epoch: string;
    readonly revision: number;
    readonly sessionID: string;
    readonly canAppend: boolean;
    readonly blockReason: string | null;
    readonly reads: TuiReads;
    readonly nowMs: number;
    readonly authoritativeReadOk: boolean;
}

type SessionRunStatus = "idle" | "busy" | "retry";

export function normalizeStatus(raw: unknown): SessionRunStatus {
    const value = typeof raw === "string" ? raw : asRecord(raw)?.["type"];
    if (value === "busy" || value === "retry") {
        return value;
    }
    return "idle";
}

export function todoProgress(list: readonly unknown[]): {
    readonly known: true;
    readonly completed: number;
    readonly total: number;
} {
    let completed = 0;
    for (const item of list) {
        if (asRecord(item)?.["status"] === "completed") {
            completed += 1;
        }
    }
    return { known: true, completed, total: list.length };
}

function asRecord(value: unknown): Record<string, unknown> | undefined {
    return typeof value === "object" && value !== null ? (value as Record<string, unknown>) : undefined;
}

function readLastAssistantModel(messages: readonly unknown[]): string | undefined {
    for (let i = messages.length - 1; i >= 0; i -= 1) {
        const message = asRecord(messages[i]);
        if (message?.["role"] !== "assistant") continue;
        const modelID = message["modelID"];
        if (typeof modelID === "string" && modelID.length > 0) {
            return modelID;
        }
    }
    return undefined;
}

function readModel(
    session: unknown,
    messages: readonly unknown[],
): { current?: string; lastUsed?: string } {
    const model = asRecord(session)?.["model"];
    const modelID = typeof model === "string" ? model : asRecord(model)?.["id"];
    if (typeof modelID === "string" && modelID.length > 0) {
        return { current: modelID };
    }
    const lastUsed = readLastAssistantModel(messages);
    return lastUsed === undefined ? {} : { lastUsed };
}

function readUsage(session: unknown): UsageRead {
    const record = asRecord(session);
    const tokens = asRecord(record?.["tokens"]);
    if (!tokens) {
        return { present: false };
    }
    const cache = asRecord(tokens["cache"]);
    const cost = record?.["cost"];
    return {
        present: true,
        input: numberOrUndefined(tokens["input"]),
        output: numberOrUndefined(tokens["output"]),
        reasoning: numberOrUndefined(tokens["reasoning"]),
        cacheRead: numberOrUndefined(cache?.["read"]),
        cacheWrite: numberOrUndefined(cache?.["write"]),
        cost: typeof cost === "number" ? cost : cost === null ? null : undefined,
    };
}

function numberOrUndefined(value: unknown): number | undefined {
    return typeof value === "number" ? value : undefined;
}

function deriveTools(parts: readonly unknown[]): { running: string[]; failed: number } {
    const running: string[] = [];
    let failed = 0;
    for (const part of parts) {
        const record = asRecord(part);
        if (record?.["type"] !== "tool") continue;
        const name = record["tool"];
        const status = asRecord(record["state"])?.["status"];
        if (typeof name !== "string") continue;
        if (status === "running" || status === "pending") {
            running.push(name);
        } else if (status === "error") {
            failed += 1;
        }
    }
    return { running, failed };
}

/**
 * 当前错误只看**最近一条 assistant 消息**：其后若有成功回复，陈旧错误必须清除。
 * 历史中任何一条曾出错都不能让会话永久停在 error 态（否则空闲会话会被误判 error）。
 */
function deriveActiveError(messages: readonly unknown[]): string | null {
    for (let i = messages.length - 1; i >= 0; i -= 1) {
        const message = asRecord(messages[i]);
        if (message?.["role"] !== "assistant") continue;
        const error = message["error"];
        if (error === undefined || error === null) return null;
        const name = asRecord(error)?.["name"];
        return typeof name === "string" && name.length > 0 ? name : "error";
    }
    return null;
}

function readTitle(session: unknown): string | null {
    const title = asRecord(session)?.["title"];
    return typeof title === "string" && title.length > 0 ? title : null;
}

export function buildSnapshotSource(input: ReadInput): SnapshotSource {
    const { reads } = input;
    const tools = deriveTools(reads.parts);
    return {
        epoch: input.epoch,
        revision: input.revision,
        sessionID: input.sessionID,
        canAppend: input.canAppend,
        blockReason: input.blockReason,
        sessionStatus: normalizeStatus(reads.sessionStatus),
        pendingPermission: reads.permissions.length > 0,
        pendingQuestion: reads.questions.length > 0,
        activeError: deriveActiveError(reads.messages),
        runningTools: tools.running,
        failedTools: tools.failed,
        rootIdleButChildBusy: false,
        chatText: null,
        todo: todoProgress(reads.todos),
        projectName: null,
        sessionTitle: readTitle(reads.session),
        model: readModel(reads.session, reads.messages),
        usage: readUsage(reads.session),
        authoritativeReadOk: input.authoritativeReadOk,
        readAtMs: input.nowMs,
    };
}

export interface TuiStateApi {
    readonly state: {
        readonly session: {
            get(id: string): unknown;
            status(id: string): unknown;
            todo(id: string): readonly unknown[];
            messages(id: string): readonly unknown[];
            permission(id: string): readonly unknown[];
            question(id: string): readonly unknown[];
        };
        readonly part: (messageID: string) => readonly unknown[];
    };
}

function lastAssistantId(messages: readonly unknown[]): string | undefined {
    for (let i = messages.length - 1; i >= 0; i -= 1) {
        const message = asRecord(messages[i]);
        if (message?.["role"] === "assistant" && typeof message["id"] === "string") {
            return message["id"];
        }
    }
    return undefined;
}

/** 防御性读取：任一子读取失败不影响其余；失败时 authoritativeReadOk=false。 */
export function readTuiState(api: TuiStateApi, sessionID: string): { reads: TuiReads; ok: boolean } {
    const s = api.state.session;
    let ok = true;
    const safe = <T>(fn: () => T, fallback: T): T => {
        try {
            return fn();
        } catch {
            ok = false;
            return fallback;
        }
    };
    const messages = safe(() => s.messages(sessionID), [] as readonly unknown[]);
    const parts = safe(() => {
        const last = lastAssistantId(messages);
        return last === undefined ? [] : api.state.part(last);
    }, [] as readonly unknown[]);
    const reads: TuiReads = {
        sessionStatus: safe(() => s.status(sessionID), undefined),
        todos: safe(() => s.todo(sessionID), []),
        permissions: safe(() => s.permission(sessionID), []),
        questions: safe(() => s.question(sessionID), []),
        session: safe(() => s.get(sessionID), undefined),
        messages,
        parts,
    };
    return { reads, ok };
}
