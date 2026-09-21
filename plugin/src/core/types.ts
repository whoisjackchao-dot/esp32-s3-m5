/**
 * StickS3 × OpenCode — M1 核心类型契约（冻结）
 *
 * 依据 contracts/M1-桥接契约冻结.md（2026-09-20 已批准）。
 * 本文件由编排者维护，是实现模块间的**共享接口契约**：
 * 各模块实现时不得修改本文件；需要的附加类型在各自模块内定义并导出。
 *
 * 模块归属：
 *   src/core/receipts.ts + src/core/coordinator.ts  → createCoordinator（Agent A）
 *   src/bridge/server.ts                            → startBridge（Agent B）
 *   src/index.ts                                    → TUI 插件入口（Agent C，集成）
 */

export type Epoch = string;
export type OperationID = string;
export type SessionID = string;

/** 已批准工程取值（M1 契约 §5/§7，2026-09-20 用户批准） */
export const RECEIPT_CAPACITY = 64;
export const TEXT_MAX_LEN = 4096;
export const BODY_MAX_BYTES = 8192;
export const RECEIPT_TTL_MS = 300_000;
export const PROTOCOL_VERSION = 1;

/**
 * 追加分隔规则（主设计文档已批准）：
 * 原稿非空且末尾无空白时加一个换行，其他情况直接追加。
 */
export function appendWithSeparator(existing: string, addition: string): string {
    if (existing.length > 0 && !/\s$/.test(existing)) {
        return `${existing}\n${addition}`;
    }
    return `${existing}${addition}`;
}

export type OperationStatus =
    | "processing"
    | "applied"
    | "rejected"
    | "cancelled"
    | "uncertain";

export type RejectReason =
    | "target_mismatch"
    | "dialog_blocking"
    | "not_visible"
    | "disabled"
    | "stale_epoch"
    | "conflict"
    | "capacity_exceeded"
    | "identity_error";

export interface AppendRequest {
    protocolVersion: number;
    tuiEpoch: Epoch;
    sessionID: SessionID;
    operationID: OperationID;
    transactionGeneration: number;
    text: string;
}

export type AppendOutcome =
    | { status: "applied" }
    | { status: "processing" }
    | { status: "uncertain" }
    | { status: "rejected"; reason: "target_mismatch" | "dialog_blocking" | "not_visible" | "disabled" }
    | { status: "conflict" }
    | { status: "stale_epoch" }
    | { status: "capacity_exceeded" };

export type CancelOutcome =
    | { status: "cancelled" }
    | { status: "already_applied" }
    | { status: "identity_error" };

export type QueryOutcome =
    | { status: OperationStatus; reason?: RejectReason }
    | { status: "unknown" };

/** 插件在写入前从 TUI 原生状态采集的目标上下文（同一同步上下文内采集） */
export interface BindingTarget {
    epoch: Epoch;
    boundSessionID: SessionID;
    /** route.name === "session" 时的 params.sessionID；其余（home 等）为 null（M1 契约 §4.1：须运行时判别） */
    currentRouteSessionID: SessionID | null;
    promptVisible: boolean;
    promptDisabled: boolean;
    dialogBlocking: boolean;
}

/**
 * 注入的真实写入：read→append→set→readback（C06）。
 * 返回 "applied" 或 "uncertain"（set 后 readback 失败/异常，TUI-14）。
 * 实现**绝不调用 ref.submit**。
 */
export type CommitFn = (text: string) => Promise<"applied" | "uncertain">;

/** 注入时钟（毫秒）。测试用可控时钟；业务代码禁止用真实 setTimeout 做时限裁决 */
export interface Clock {
    now(): number;
}

/** 回执：不含转写正文，仅指纹（C02：摘要也不写日志） */
export interface Receipt {
    operationID: OperationID;
    epoch: Epoch;
    fingerprint: string;
    status: OperationStatus;
    reason?: RejectReason;
    createdAtMs: number;
}

export interface CoordinatorOptions {
    /** 当前绑定的 TUI epoch（插件入口生成）；handleCancel/handleQuery 以此判定 stale */
    epoch: Epoch;
    /** 默认 RECEIPT_CAPACITY；测试可注入小容量 */
    capacity?: number;
    /** 默认 RECEIPT_TTL_MS */
    receiptTtlMs?: number;
    clock: Clock;
}

/**
 * M1 核心协调器（Agent A 实现，导出 createCoordinator）。
 * 同一 operation 的 commit/cancel/结果状态变更必须在单一串行执行上下文内裁决（C03）。
 */
export interface DraftCoordinator {
    handleAppend(req: AppendRequest, target: BindingTarget, commit: CommitFn): Promise<AppendOutcome>;
    handleCancel(epoch: Epoch, operationID: OperationID): CancelOutcome;
    handleQuery(epoch: Epoch, operationID: OperationID): QueryOutcome;
    /** 当前回执+墓碑条数（容量观测） */
    size(): number;
}

/** M1 桥接处理器（由插件入口把 Coordinator 适配进来） */
export interface BridgeHandlers {
    append(req: AppendRequest): Promise<AppendOutcome>;
    cancel(epoch: Epoch, operationID: OperationID): CancelOutcome;
    query(epoch: Epoch, operationID: OperationID): QueryOutcome;
    binding(): BindingTarget;
}

export interface BridgeServerOptions {
    /** 缺省 0 = 随机端口；仅监听 127.0.0.1 */
    port?: number;
    token: string;
    tuiEpoch: Epoch;
    pluginVersion: string;
    handlers: BridgeHandlers;
}

/** M1 桥接服务器（Agent B 实现，导出 startBridge） */
export interface BridgeServer {
    readonly port: number;
    readonly hostname: "127.0.0.1";
    stop(): void;
}
