import { mkdirSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";
import type { TuiPluginApi, TuiPluginModule } from "@opencode-ai/plugin/tui";

import { createCoordinator } from "./core/coordinator";
import { StateStore } from "./core/state/store";
import type {
    AppendOutcome,
    AppendRequest,
    BindingTarget,
    CancelOutcome,
    Epoch,
    OperationID,
    QueryOutcome,
} from "./core/types";
import { RefStore, currentSessionID, registerSlotPlugin } from "./plugin/capture";
import { buildCommitFn } from "./plugin/safeAppend";
import { buildSessionPromptSlot } from "./plugin/slotPrompt";
import { buildSnapshotSource, readTuiState } from "./plugin/stateRead";
import { startBridge } from "./bridge/server";

const PLUGIN_VERSION = "0.1.0";
const HEARTBEAT_MS = 10_000;
const REFRESH_MIN_MS = 500;

const STATE_EVENT_TYPES = [
    "session.status",
    "session.idle",
    "session.updated",
    "session.error",
    "message.updated",
    "message.part.updated",
    "message.part.removed",
    "message.removed",
    "permission.asked",
    "permission.replied",
    "question.asked",
    "question.replied",
    "question.rejected",
    "todo.updated",
] as const;

function registerStateEvents(api: TuiPluginApi, onEvent: () => void): Array<() => void> {
    const register = api.event.on as unknown as (type: string, handler: () => void) => () => void;
    return STATE_EVENT_TYPES.map((type) => register(type, onEvent));
}
const DESCRIPTOR_DIR =
    process.env.STICKS3_DESCRIPTOR_DIR ?? join(homedir(), ".local/share/sticks3-opencode");
const DESCRIPTOR_FILE = join(DESCRIPTOR_DIR, "bridge.json");

function randomToken(): string {
    const bytes = new Uint8Array(32);
    crypto.getRandomValues(bytes);
    return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
}

/**
 * M1 安全草稿插件入口。
 *
 * 桥接仅监听 127.0.0.1（契约 §2）；桥接地址与 token 写入 0600 描述文件
 * 供本机 Windows 入口读取（IPC-02：M1 允许 0600 权限文件，禁入命令行/日志/git）。
 * 本插件绝不提交：不调用 ref.submit，不暴露任何提交类处理器（SEC-02/TUI-17）。
 */
const plugin: TuiPluginModule = {
    id: "sticks3-voice-draft",
    tui: async (api) => {
        const tuiEpoch: Epoch = crypto.randomUUID();
        const token = randomToken();
        const refs = new RefStore();
        const coordinator = createCoordinator({
            epoch: tuiEpoch,
            clock: { now: () => Date.now() },
        });

        registerSlotPlugin(api, {
            id: "sticks3-voice-draft-slots",
            slots: {
                // session_prompt 为 replace 模式（B01 实证）：必须转发宿主 props
                // （session_id/on_submit/visible/disabled/ref），否则会话内输入框会
                // 退化成首页"新建会话"输入框——每次回车新建会话（2026-09-23 根因，
                // opencode 1.18.32 二进制级证据；方案 B 修复）。
                session_prompt: buildSessionPromptSlot(
                    (props) => api.ui.Prompt(props),
                    (sessionID, ref) => refs.capture(sessionID, ref),
                ),
            },
        });

        function binding(): BindingTarget {
            const route = api.route.current;
            const current = currentSessionID(route) ?? null;
            return {
                epoch: tuiEpoch,
                boundSessionID: current ?? "",
                currentRouteSessionID: current,
                // M1 最小实现：可见性等价于"处于会话路由"；阻塞/禁用细化归 M4
                promptVisible: current !== null,
                promptDisabled: false,
                dialogBlocking: false,
            };
        }

        const handlers = {
            append(req: AppendRequest): Promise<AppendOutcome> {
                const commit = buildCommitFn(() => refs.get(req.sessionID));
                return coordinator.handleAppend(req, binding(), commit);
            },
            cancel(epoch: Epoch, operationID: OperationID): CancelOutcome {
                return coordinator.handleCancel(epoch, operationID);
            },
            query(epoch: Epoch, operationID: OperationID): QueryOutcome {
                return coordinator.handleQuery(epoch, operationID);
            },
            binding,
        };

        const store = new StateStore();
        let revision = 0;

        function refreshState(): void {
            try {
                const session = currentSessionID(api.route.current);
                if (!session) return;
                const now = Date.now();
                const target = binding();
                const canAppend = target.promptVisible && !target.promptDisabled && !target.dialogBlocking;
                const blockReason = target.dialogBlocking
                    ? "dialog_blocking"
                    : target.promptVisible
                      ? target.promptDisabled
                          ? "disabled"
                          : null
                      : "not_visible";
                const { reads, ok } = readTuiState(api, session);
                revision += 1;
                store.ingest(
                    buildSnapshotSource({
                        epoch: tuiEpoch,
                        revision,
                        sessionID: session,
                        canAppend,
                        blockReason,
                        reads,
                        nowMs: now,
                        authoritativeReadOk: ok,
                    }),
                    now,
                );
            } catch {
                /* 无伤插入：状态读取/归约的任何异常都不得冒泡到 OpenCode 的事件总线或定时器。 */
            }
        }

        let lastRefreshMs = 0;
        let pendingRefresh: ReturnType<typeof setTimeout> | undefined;
        function scheduleRefresh(): void {
            const now = Date.now();
            const elapsed = now - lastRefreshMs;
            if (elapsed >= REFRESH_MIN_MS) {
                lastRefreshMs = now;
                refreshState();
                return;
            }
            if (pendingRefresh === undefined) {
                pendingRefresh = setTimeout(() => {
                    pendingRefresh = undefined;
                    lastRefreshMs = Date.now();
                    refreshState();
                }, REFRESH_MIN_MS - elapsed);
            }
        }

        const bridge = startBridge({
            token,
            tuiEpoch,
            pluginVersion: PLUGIN_VERSION,
            handlers,
            state: { snapshot: () => store.current() ?? null, heartbeatMs: HEARTBEAT_MS },
        });
        refreshState();
        lastRefreshMs = Date.now();
        const stateTimer = setInterval(refreshState, HEARTBEAT_MS);
        const eventUnsubscribers = registerStateEvents(api, scheduleRefresh);

        try {
            mkdirSync(DESCRIPTOR_DIR, { recursive: true, mode: 0o700 });
            writeFileSync(
                DESCRIPTOR_FILE,
                JSON.stringify({
                    url: `http://127.0.0.1:${bridge.port}`,
                    token,
                    tuiEpoch,
                    pluginVersion: PLUGIN_VERSION,
                }),
                { mode: 0o600 },
            );
        } catch (error) {
            void error;
            bridge.stop();
            throw new Error(`无法写入桥接描述文件 ${DESCRIPTOR_FILE}`);
        }

        api.lifecycle.onDispose(() => {
            clearInterval(stateTimer);
            if (pendingRefresh !== undefined) clearTimeout(pendingRefresh);
            for (const unsubscribe of eventUnsubscribers) unsubscribe();
            bridge.stop();
            refs.clear();
        });
    },
};
export default plugin;
