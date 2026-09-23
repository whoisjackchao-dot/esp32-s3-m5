import type { TuiPromptProps, TuiPromptRef } from "@opencode-ai/plugin/tui";

/**
 * session_prompt 槽位宿主传入的 props（@opencode-ai/plugin tui.d.ts TuiHostSlotMap）。
 * 宿主渲染时传入这些字段；home_prompt 槽位不传 session_id/on_submit。
 */
export interface SessionPromptSlotProps {
    session_id?: string;
    visible?: boolean;
    disabled?: boolean;
    on_submit?: () => void;
    ref?: (ref: TuiPromptRef | undefined) => void;
}

/**
 * 构建 session_prompt 槽位渲染函数（M1 方案 B）。
 *
 * 根因（2026-09-23，opencode 1.18.32 二进制级证据）：
 * 旧实现只渲染 api.ui.Prompt({ ref })，未转发宿主传入的
 * session_id / on_submit / visible / disabled / ref —— 会话内输入框退化成
 * 首页"新建会话"输入框，每次回车都新建会话并跳转（空会话 / 上下文丢失 /
 * MessageAbortedError）。此函数转发全部宿主 props 以保持 opencode 原生
 * 输入框行为，并通过 ref 回调把 Prompt 引用交给调用方（桥接追加草稿用，
 * M1 契约 §4.1 按 session 捕获）。
 *
 * @param render  实际渲染函数（生产为 api.ui.Prompt；测试注入 mock）
 * @param capture 按 sessionID 捕获/释放 Prompt 引用（生产为 RefStore.capture）
 * @returns 槽位渲染函数（registerSlotPlugin 的 slots.session_prompt）
 */
export function buildSessionPromptSlot(
    render: (props: TuiPromptProps) => unknown,
    capture: (sessionID: string, ref: TuiPromptRef | undefined) => void,
): (rawProps: unknown) => unknown {
    return (rawProps: unknown): unknown => {
        const props = (rawProps ?? {}) as SessionPromptSlotProps;
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
