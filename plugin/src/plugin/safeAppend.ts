import type { TuiPromptRef } from "@opencode-ai/plugin/tui";
import type { CommitFn } from "../core/types";
import { appendWithSeparator } from "../core/types";

/**
 * 构造注入协调器的真实写入函数（C06）：校验、读取、追加、set、回读
 * 必须发生在同一同步块内；唯一可能的异步点是协调器侧的 await。
 *
 * 安全边界：绝不调用 ref.submit（TUI-17 需实际传输记录证明）。
 * 返回 "uncertain" 的情形（TUI-14）：无可用 ref / set 抛错 / 回读不一致 —— 均不自动重试。
 */
export function buildCommitFn(getRef: () => TuiPromptRef | undefined): CommitFn {
    return async (text) => {
        const ref = getRef();
        if (!ref) return "uncertain";
        try {
            const current = ref.current;
            const nextInput = appendWithSeparator(current.input, text);
            ref.set({ ...current, input: nextInput });
            const readback = ref.current;
            const applied = readback.input === nextInput && readback.parts.length === current.parts.length;
            return applied ? "applied" : "uncertain";
        } catch (error) {
            void error;
            return "uncertain";
        }
    };
}
