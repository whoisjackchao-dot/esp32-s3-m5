import { mkdirSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { join } from "node:path";
import type { TuiPluginModule } from "@opencode-ai/plugin/tui";

import { createCoordinator } from "./core/coordinator";
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
import { startBridge } from "./bridge/server";

const PLUGIN_VERSION = "0.1.0";
const DESCRIPTOR_DIR = join(homedir(), ".local/share/sticks3-opencode");
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

        const bridge = startBridge({ token, tuiEpoch, pluginVersion: PLUGIN_VERSION, handlers });

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
            bridge.stop();
            refs.clear();
        });
    },
};
export default plugin;
