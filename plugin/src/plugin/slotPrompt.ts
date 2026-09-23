import type { TuiPromptProps, TuiPromptRef } from "@opencode-ai/plugin/tui";

/**
 * session_prompt 槽位宿主第 2 参（opencode solid Slot 组件 rest props）。
 *
 * 宿主渲染 `<Slot name="session_prompt" session_id=… on_submit=… visible=… disabled=… ref=…>`
 * 时，opencode 的 solid Slot 组件（二进制级证据，1.18.32）执行：
 *   `let [picked, rest] = splitProps(props, ["registry","name","mode","children","pluginFailurePlaceholder"])`
 * 然后以 `renderer(context, rest)` 调用插件 renderer。
 * 即插件 renderer 的入参是 **两个**：第 1 个为 context（仅 `{theme}`），
 * 第 2 个才是承载 `session_id` / `on_submit` / `visible` / `disabled` / `ref` 的宿主 props。
 */
export interface SessionPromptSlotProps {
    session_id?: string;
    visible?: boolean;
    disabled?: boolean;
    on_submit?: () => void;
    ref?: (ref: TuiPromptRef | undefined) => void;
}

/**
 * 构建 session_prompt 槽位渲染函数（M1 方案 B **二次修复**，2026-09-23）。
 *
 * 根因（opencode 1.18.32 二进制级证据）：插件 renderer 的真实调用签名是
 * `renderer(context, data)`，`data` 为 splitProps 后的 rest（宿主 props）。
 * 首版方案 B 误将第 1 个参数 context（仅 `{theme}`）当作宿主 props，导致
 * `data.session_id` / `data.on_submit` 恒为 undefined，渲染出的 `api.ui.Prompt`
 * 退化为首页"新建会话"输入框——每次回车新建会话、一个对话只能进行一轮、
 * 上下文丢失 / `MessageAbortedError`。
 *
 * 修复：签名改为 `(context, data)`，从**第 2 个参数**读取宿主 props 并转发；
 * 仍经 ref 回调把 Prompt 引用交给调用方（桥接追加草稿用，M1 契约 §4.1 按 session 捕获）。
 *
 * @param render  实际渲染函数（生产为 api.ui.Prompt；测试注入 mock）
 * @param capture 按 sessionID 捕获/释放 Prompt 引用（生产为 RefStore.capture）
 * @returns 槽位渲染函数（registerSlotPlugin 的 slots.session_prompt）
 */
export function buildSessionPromptSlot(
    render: (props: TuiPromptProps) => unknown,
    capture: (sessionID: string, ref: TuiPromptRef | undefined) => void,
): (context: unknown, data: unknown) => unknown {
    return (_context: unknown, data: unknown): unknown => {
        const props = (data ?? {}) as SessionPromptSlotProps;
        const sessionID = props.session_id;
        return render({
            sessionID,
            visible: props.visible,
            disabled: props.disabled,
            onSubmit: props.on_submit,
            ref: (ref) => {
                props.ref?.(ref);
                if (sessionID) capture(sessionID, ref);
            },
        });
    };
}
