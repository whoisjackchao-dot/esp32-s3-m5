import type { TuiPromptRef, TuiPluginApi, TuiRouteCurrent } from "@opencode-ai/plugin/tui";
import type { SessionID } from "../core/types";

/**
 * 上游类型缺陷（以 B01 spike 实测为准，见 plugin/spike/FINDINGS.md）：
 * tui.d.ts 将 TuiSlotPlugin 的 id 标为 never，但运行时守卫
 * （packages/tui/src/plugin/slots.tsx isHostSlotPlugin）要求 string id 与 slots 记录。
 * 槽位注册是上游类型错误的唯一出口：以实测运行时形状窄化，spike 证据在案。
 */
interface SlotRegistration {
    id: string;
    slots: Record<string, (props: unknown) => unknown>;
}

export function registerSlotPlugin(
    api: Pick<TuiPluginApi, "slots">,
    plugin: SlotRegistration,
): void {
    const registrar = api.slots.register as unknown as (plugin: SlotRegistration) => unknown;
    registrar(plugin);
}

/**
 * M1 契约 §4.1：TuiRouteCurrent 是含兜底成员的判别联合，TS 无法收窄，
 * 必须运行时校验 —— sessionID 仅在 name === "session" 分支存在。
 */
export function currentSessionID(route: TuiRouteCurrent): SessionID | undefined {
    if (route.name !== "session") return undefined;
    const params = (route as { params?: Record<string, unknown> }).params;
    const id = params?.["sessionID"];
    return typeof id === "string" ? id : undefined;
}

/** 按 session 保存已捕获的 Prompt 引用；ref(undefined) 表示该会话的 Prompt 已卸载 */
export class RefStore {
    readonly #refs = new Map<SessionID, TuiPromptRef>();

    capture(sessionID: SessionID, ref: TuiPromptRef | undefined): void {
        if (ref) {
            this.#refs.set(sessionID, ref);
        } else {
            this.#refs.delete(sessionID);
        }
    }

    get(sessionID: SessionID): TuiPromptRef | undefined {
        return this.#refs.get(sessionID);
    }

    get size(): number {
        return this.#refs.size;
    }

    clear(): void {
        this.#refs.clear();
    }
}
