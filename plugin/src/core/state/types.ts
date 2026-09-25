/**
 * M4 S4.1 — M10 精简快照类型（BR-03 数据字典）
 *
 * 依据 StickS3_OpenCode_功能模块HLD.md §7.4 与 契约 C08。
 * 本模块只做**归约逻辑与类型**：不接 TUI SDK、不发网络、不涉 BLE/UI。
 */

/** 字段可用性三态：缺失不统一为 0（C08）。 */
export type Availability<T> =
    | { readonly kind: "unknown" }
    | { readonly kind: "not_reported" }
    | { readonly kind: "known"; readonly value: T };

export type ProgrammingStatus =
    | "idle"
    | "busy"
    | "retry"
    | "waiting"
    | "error"
    | "unknown";

export type SubtaskHint = "tool_failed" | "child_busy" | null;

export type NotifyDecision = "none" | "waiting" | "error";

export interface ToolRun {
    readonly name: string;
    readonly count: number;
}

export interface TodoProgress {
    readonly completed: number;
    readonly total: number;
}

export interface UsageBreakdown {
    readonly input: Availability<number>;
    readonly output: Availability<number>;
    readonly reasoning: Availability<number>;
    readonly cacheRead: Availability<number>;
    readonly cacheWrite: Availability<number>;
}

export interface ModelView {
    readonly current: Availability<string>;
    readonly lastUsed: Availability<string>;
}

export interface SnapshotIdentity {
    readonly protocolVersion: number;
    readonly tuiEpoch: string;
    readonly sessionID: string;
    readonly snapshotRevision: number;
}

export interface BindCapability {
    readonly canAppend: boolean;
    readonly blockReason: string | null;
}

export interface Freshness {
    readonly authoritativeReadOk: boolean;
    readonly dataAtMs: number;
}

export interface StatusSnapshot {
    readonly identity: SnapshotIdentity;
    readonly bind: BindCapability;
    readonly status: ProgrammingStatus;
    readonly errorCode: string | null;
    readonly subtaskHint: SubtaskHint;
    readonly project: Availability<string>;
    readonly sessionTitle: Availability<string>;
    readonly tools: readonly ToolRun[];
    readonly todos: Availability<TodoProgress>;
    readonly model: ModelView;
    readonly usage: UsageBreakdown;
    readonly cost: Availability<number>;
    readonly freshness: Freshness;
}

/**
 * 归约输入：一次权威状态读取的合成结果。
 * `usage` 字段约定 — 键缺失(undefined)=unknown；显式 null=not_reported；数字(含 0)=known。
 */
export interface UsageRead {
    readonly present: boolean;
    readonly input?: number | null;
    readonly output?: number | null;
    readonly reasoning?: number | null;
    readonly cacheRead?: number | null;
    readonly cacheWrite?: number | null;
    readonly cost?: number | null;
}

export interface SnapshotSource {
    readonly epoch: string;
    readonly revision: number;
    readonly sessionID: string;
    readonly canAppend: boolean;
    readonly blockReason: string | null;
    readonly sessionStatus: "idle" | "busy" | "retry";
    readonly pendingPermission: boolean;
    readonly pendingQuestion: boolean;
    readonly activeError: string | null;
    readonly runningTools: readonly string[];
    readonly failedTools: number;
    readonly rootIdleButChildBusy: boolean;
    /** 聊天正文：状态归约必须忽略，不得据此猜测（ST-03）。 */
    readonly chatText: string | null;
    readonly todo: { readonly known: boolean; readonly completed: number; readonly total: number };
    readonly projectName: string | null;
    readonly sessionTitle: string | null;
    readonly model: { readonly current?: string; readonly lastUsed?: string };
    readonly usage: UsageRead;
    readonly authoritativeReadOk: boolean;
    readonly readAtMs: number;
}

export type SnapshotOutcome =
    | { readonly kind: "applied"; readonly snapshot: StatusSnapshot; readonly notify: NotifyDecision }
    | { readonly kind: "deduped"; readonly snapshot: StatusSnapshot; readonly notify: NotifyDecision }
    | { readonly kind: "dropped_stale"; readonly snapshot: StatusSnapshot }
    | { readonly kind: "protocol_error"; readonly reason: string };
