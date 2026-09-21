// B01 spike v4：session_prompt 为 replace 模式 —— 插件渲染 api.ui.Prompt 并经其 ref 回调捕获引用。
// 用定时探针证明 ref 与 TUI 输入框实时绑定（打字后 current.input 变化），绝不调用 submit。
import { appendFileSync } from "node:fs";
import type { TuiPluginModule } from "@opencode-ai/plugin/tui";

const EVIDENCE = "/tmp/stick3-m1-spike/spike-evidence.log";
const log = (msg: unknown) => {
    try {
        appendFileSync(EVIDENCE, `STICKS3_SPIKE ${JSON.stringify(msg)}\n`);
    } catch {
        // 证据文件不可写时静默；不影响 TUI
    }
};

let probeCount = 0;
let capturedRef: { current?: { input?: unknown }; set?: unknown } | null = null;

function startProbe(): void {
    const timer = setInterval(() => {
        probeCount += 1;
        const input = typeof capturedRef?.current?.input === "string" ? capturedRef.current.input : undefined;
        log({ event: "probe", n: probeCount, inputLen: input?.length ?? -1, input: input ?? null });
        if (probeCount >= 12) clearInterval(timer);
    }, 2000);
}

const spike: TuiPluginModule = {
    id: "sticks3-spike",
    tui: async (api) => {
        log({ event: "plugin-load", version: api.app?.version });
        api.slots.register({
            id: "sticks3-spike-slots",
            slots: {
                session_prompt: () => {
                    log({ event: "session-prompt-replace-render" });
                    return api.ui.Prompt({
                        ref: (ref) => {
                            if (!ref) {
                                log({ event: "ref", value: "undefined" });
                                return;
                            }
                            capturedRef = ref;
                            log({
                                event: "ref-captured",
                                currentKeys: Object.keys(ref.current ?? {}),
                                inputIsString: typeof ref.current?.input === "string",
                                partsIsArray: Array.isArray(ref.current?.parts),
                                hasSet: typeof ref.set === "function",
                                hasSubmit: typeof ref.submit === "function",
                            });
                            if (probeCount === 0) startProbe();
                        },
                    });
                },
            },
        });
        log({ event: "slot-registered" });
    },
};
export default spike;
