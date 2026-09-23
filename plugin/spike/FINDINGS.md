# B01 Spike 结论：TUI 插件可捕获活的 Prompt 引用（已实证）

> 实验：2026-09-21，OpenCode 1.18.31，隔离实例（独立 HOME/XDG），证据文件
> `/tmp/stick3-m1-spike/spike-evidence.log`，探针源码 `spike-plugin.ts`。
> B01（可行性核查 §8："语音写入首要阻断门槛"）据此解除。
>
> **2026-09-23 修订（重要）**：§1.6 的"槽位 props 仅 {theme}"结论**有误**（探针读取位置
> 错误），据此设计的 §3 首版配方（只传 ref、不转发宿主 props）导致 opencode 会话反复
> 重置的严重故障。已更正 §1.6/§3 并新增 §5 根因记录与修复。opencode 1.18.32。

## 1. 实证结论（按证据强度排序）

1. **TUI 插件可加载**：配置文件为 **`.opencode/tui.json`**（`{"plugin": ["<绝对路径.ts>"]}`）。
   证据：`STICKS3_SPIKE {"event":"plugin-load","version":"1.18.31"}`。
   注意：主配置 `opencode.json` 的 `plugin` 键是**服务器**插件；放 TUI 模块会报
   "must default export an object with server()"。
2. **session_prompt 槽位为 replace 模式**：插件在槽内渲染的组件会**替换**内建输入框。
   证据：进入会话视图后探针收到 `session-prompt-replace-render`。
3. **正解 = 槽内渲染 `api.ui.Prompt` 并经其 ref 回调捕获引用**（对应 HLD TUI-API-02
   "复用内置 Prompt 并获取/转发 ref"）。证据：`ref-captured`，
   `currentKeys=["input","parts"]`、`hasSet=true`。
4. **ref 与真实输入框实时绑定**：TUI 中键入 "AB" 后，定时探针读到
   `{"inputLen":2,"input":"AB"}`。read/set 路径真实可用。
5. **路由运行时形状**：主屏（新会话）即 `{"name":"home"}`；选择已有会话后仍未见
   `name:"session"` 独立路由——route 判别须按 M1 契约 §4.1 做运行时检查。
6. ~~**槽位 props 不透传 ref/session_id**~~ **2026-09-23 更正**：宿主渲染 `session_prompt`
   时**确实传入** `session_id`/`on_submit`/`visible`/`disabled`/`ref`（opencode 1.18.32
   二进制级证据 + `tui.d.ts` `TuiHostSlotMap.session_prompt`）。2026-09-21 探针实测
   "仅 `{"theme"}`" 是**错误结论**（读取位置错误，非槽位函数入参）。插件必须转发这些
   props，否则会话内输入框退化为首页"新建会话"框（见 §5 故障）。

## 2. 上游类型与运行时的两处不一致（已实测，建议上报上游）

| 处 | 类型（tui.d.ts） | 运行时（实测/源码） | 本项目处理 |
| --- | --- | --- | --- |
| 槽位注册 id | `TuiSlotPlugin` 将 id 标为 `never` | `isHostSlotPlugin`（slots.tsx:21）要求 string id + slots 记录 | `capture.ts registerSlotPlugin` 单点窄化（注释在案） |
| 路由收窄 | `TuiRouteCurrent` 含 `{name:string}` 兜底成员，TS 无法收窄 | 同左 | 运行时类型检查（coordinator/capture） |

## 3. M1 集成配方（2026-09-23 修订：必须转发宿主 props）

```text
.opencode/tui.json → {"plugin": ["<plugin/src/index.ts 绝对路径>"]}
槽位 session_prompt（replace）→ 转发宿主 props 后渲染 api.ui.Prompt：
    api.ui.Prompt({
      sessionID: props.session_id, visible: props.visible,
      disabled: props.disabled, onSubmit: props.on_submit,
      ref: (r) => { props.ref?.(r); if (props.session_id) refs.capture(props.session_id, r); },
    })
  实现：src/plugin/slotPrompt.ts buildSessionPromptSlot；单测 test/slotPrompt.test.ts（8 用例）
桥接：插件内 startBridge(127.0.0.1:0) → 地址+token 写 ~/.local/share/sticks3-opencode/bridge.json (0600)
```

> ⚠️ 首版配方（2026-09-21）为 `api.ui.Prompt({ ref: (r) => refs.capture(当前会话, r) })`
> —— **只传 ref、不转发宿主 props**，是 §5 故障的直接原因。切勿照抄首版。

## 4. 遗留与边界

- **2026-09-23 已暴露**：replace 模式下若不转发宿主 props，会破坏会话输入框语义（§5）。
  转发 props 后，原生快捷键/附件行为的完整回归仍需真实使用复核（TUI-16，M1 出口用例）。
- `home_prompt` 同样可注册，M1 不使用。
- 会话切换时 ref 的生命周期（旧 ref 是否回收）由 RefStore 按 session 覆盖处理，
  已有单测覆盖存取语义；跨会话误写由 route 校验阻断（TUI-04）。

## 5. 2026-09-23 故障与修复（重要）

**故障现象**：插件加载后 opencode 会话反复重置——每次回车新建会话、界面变空会话、
正常对话中上下文丢失、`MessageAbortedError`。

**根因**（opencode 1.18.32 二进制级证据 + `tui.d.ts`）：宿主渲染 `session_prompt` 时传入
`session_id`/`on_submit`/`visible`/`disabled`/`ref`；`home_prompt` 则**不传**
`sessionID`/`onSubmit`。两者复用同一 `api.ui.Prompt` 组件。插件替换槽位后**一个 props
都没转发** → 会话内输入框退化成"首页新建会话"输入框 → 每次提交都在新建会话并跳转。

**修复（方案 B）**：`buildSessionPromptSlot` 转发全部宿主 props（保持原生输入框契约），
同时经 ref 回调捕获 Prompt 引用（桥接追加草稿功能不丢）。实现 `src/plugin/slotPrompt.ts`。

**验证**：
- 单测 `test/slotPrompt.test.ts` 8/8（session_id→sessionID、on_submit→onSubmit、
  visible/disabled、宿主 ref 转发、RefStore 捕获、卸载、空 props、无 session_id 不误绑）。
- 全量 `bun test` 69/69 + `tsc --noEmit` 通过。
- 隔离 HOME 实例加载：bridge.json 写入、`/v1/health` `alive:true`、日志 0 ERROR/WARN。
- **未覆盖（诚实边界）**：真实 TUI 下"回车不再新建会话"需重启 opencode 实测（会话视图
  渲染无法在无 provider 的隔离实例中触发）。
