# B01 Spike 结论：TUI 插件可捕获活的 Prompt 引用（已实证）

> 实验：2026-09-21，OpenCode 1.18.31，隔离实例（独立 HOME/XDG），证据文件
> `/tmp/stick3-m1-spike/spike-evidence.log`，探针源码 `spike-plugin.ts`。
> B01（可行性核查 §8："语音写入首要阻断门槛"）据此解除。
>
> **2026-09-23 修订 A（首版修复，后证不足）**：§1.6 曾把"槽位 props 仅 {theme}"判为错误，
> 据此设计的 §3 首版配方（只传 ref、不转发宿主 props）导致 opencode 会话反复重置的严重
> 故障。首版更正了"要不要转发 props"，但**转发的参数位置仍然错**（见修订 B）。opencode 1.18.32。
>
> **2026-09-23 修订 B（二次修复，本次生效）**：方案 B 首版误把槽位 renderer 的**第 1 参**
> `context`（仅 `{theme}`）当作宿主 props；实际 props 在**第 2 参** `data`（opencode solid
> Slot 组件 `splitProps` 的 rest，二进制级证据）。首版上线后重启实测仍复现——"一个对话只能
> 进行一轮"。已将 `buildSessionPromptSlot` 改为双参 `(context, data)` 并取第 2 参。§1.6/§3/§5 同步更正。

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
6. **槽位 renderer 为双参 `(context, data)`（2026-09-23 二次更正，二进制级证据）**：
   宿主渲染 `session_prompt` 时传入 `session_id`/`on_submit`/`visible`/`disabled`/`ref`，
   opencode solid Slot 组件（`D9`：`let [picked,rest]=splitProps(props,[...])`）以
   **`renderer(context, data)`** 调用插件 renderer：第 1 参 `context` 仅含 `{theme}`，
   **第 2 参 `data`（splitProps 的 rest）才是上述宿主 props**。
   2026-09-21 探针"仅 `{"theme"}`"实为**第 1 参 context**（结论本身对）；2026-09-23 上午的
   "§1.6 更正"误判为"读取位置错误"，并把"须转发 props"错误地落在第 1 参上，直接导致方案 B
   首版失效（见 §5 二次根因）。插件必须以第 2 参为 props 源。

## 2. 上游类型与运行时的两处不一致（已实测，建议上报上游）

| 处 | 类型（tui.d.ts） | 运行时（实测/源码） | 本项目处理 |
| --- | --- | --- | --- |
| 槽位注册 id | `TuiSlotPlugin` 将 id 标为 `never` | `isHostSlotPlugin`（slots.tsx:21）要求 string id + slots 记录 | `capture.ts registerSlotPlugin` 单点窄化（注释在案） |
| 路由收窄 | `TuiRouteCurrent` 含 `{name:string}` 兜底成员，TS 无法收窄 | 同左 | 运行时类型检查（coordinator/capture） |

## 3. M1 集成配方（2026-09-23 修订：必须转发宿主 props）

```text
.opencode/tui.json → {"plugin": ["<plugin/src/index.ts 绝对路径>"]}
槽位 session_prompt（replace）→ 以**双参 (context, data)** 渲染，从 data 取宿主 props：
    (context, data) => api.ui.Prompt({
      sessionID: data.session_id, visible: data.visible,
      disabled: data.disabled, onSubmit: data.on_submit,
      ref: (r) => { data.ref?.(r); if (data.session_id) refs.capture(data.session_id, r); },
    })
  实现：src/plugin/slotPrompt.ts buildSessionPromptSlot；单测 test/slotPrompt.test.ts（9 用例）
  ⚠️ 切勿写成单参 `(props) => …`：第 1 参是 context（{theme}），props 在第 2 参。
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

### 5.1 二次修复（2026-09-23 14:xx，本次）

**首版方案 B 上线后仍复现**：用户重启 opencode（插件在 14:14 重新加载，bridge.json 已更新、
slot 注册未抛错）后，14:14–14:16 的每一次输入仍各落入一个独立新会话（`/dsh`×2、"授权，可以
继续"、"重启了。还是相同问题"）——"一个对话只能进行一轮"未变。

**二次根因（opencode 1.18.32 二进制级证据）**：插件槽位 renderer 的真实调用签名是
**`renderer(context, data)`**——opencode solid Slot 组件：

```js
function D9(_){
  let [J,$] = splitProps(_, ["registry","name","mode","children","pluginFailurePlaceholder"])
  //          J=picked，$=rest（即 session_id/visible/disabled/on_submit/ref）
  ...
  F=(D,M)=>{ try{ I = D.renderer(Z().context, $) } ... }   // 插件函数收到 (context, rest)
}
```

首版把第 1 参 `context`（仅 `{theme}`）当 props → `data.session_id`/`data.on_submit` 恒
undefined → 渲染出的 `api.ui.Prompt` 无 sessionID/onSubmit → 退化为首页"新建会话"框。
**§1.6 修订 A 之所以错，是因为只看了"宿主传给 `<Slot>` 的属性"，未看"Slot 组件转发给插件的
参数形状"。**

**二次修复**：`src/plugin/slotPrompt.ts` 的 `buildSessionPromptSlot` 改双参
`(context, data)`，从第 2 参读取并转发 `session_id`/`on_submit`/`visible`/`disabled`/`ref`
（ref 捕获语义不变）；`capture.ts` 的 `SlotRegistration.slots` 类型放宽为
`(...args: unknown[]) => unknown` 以匹配真实双参调用。

**验证**：`bun test` **70/70**（含"第 1 参 context 不得当 props"回归用例）、`tsc --noEmit`
通过。**未覆盖（诚实边界）**：真实 TUI "回车不再新建会话、可多轮"仍需用户重启 opencode 实测。
