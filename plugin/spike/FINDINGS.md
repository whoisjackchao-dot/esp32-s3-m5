# B01 Spike 结论：TUI 插件可捕获活的 Prompt 引用（已实证）

> 实验：2026-09-21，OpenCode 1.18.31，隔离实例（独立 HOME/XDG），证据文件
> `/tmp/stick3-m1-spike/spike-evidence.log`，探针源码 `spike-plugin.ts`。
> B01（可行性核查 §8："语音写入首要阻断门槛"）据此解除。

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
6. **槽位 props 不透传 ref/session_id**：`home_prompt`/`session_prompt` 的插件
   内容 props 实测仅 `{"theme"}`。ref 只能经 `api.ui.Prompt` 的 ref 回调获得。

## 2. 上游类型与运行时的两处不一致（已实测，建议上报上游）

| 处 | 类型（tui.d.ts） | 运行时（实测/源码） | 本项目处理 |
| --- | --- | --- | --- |
| 槽位注册 id | `TuiSlotPlugin` 将 id 标为 `never` | `isHostSlotPlugin`（slots.tsx:21）要求 string id + slots 记录 | `capture.ts registerSlotPlugin` 单点窄化（注释在案） |
| 路由收窄 | `TuiRouteCurrent` 含 `{name:string}` 兜底成员，TS 无法收窄 | 同左 | 运行时类型检查（coordinator/capture） |

## 3. M1 集成配方（已被 index.ts 采用）

```text
.opencode/tui.json → {"plugin": ["<plugin/src/index.ts 绝对路径>"]}
槽位 session_prompt（replace）→ api.ui.Prompt({ ref: (r) => refs.capture(当前会话, r) })
桥接：插件内 startBridge(127.0.0.1:0) → 地址+token 写 ~/.local/share/sticks3-opencode/bridge.json (0600)
```

## 4. 遗留与边界

- 未验证：replace 模式下原生快捷键/附件行为的完整回归（TUI-16，属 M1 出口用例，
  需在真实使用中复核）。
- `home_prompt` 同样可注册（实测仅收到 theme），M1 不使用。
- 会话切换时 ref 的生命周期（旧 ref 是否回收）由 RefStore 按 session 覆盖处理，
  已有单测覆盖存取语义；跨会话误写由 route 校验阻断（TUI-04）。
