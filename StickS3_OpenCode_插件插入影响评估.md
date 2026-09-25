# S4.2 前置：插件插入 OpenCode 的影响评估与无伤插入程序

> 日期：2026-09-25。触发：用户要求「S4.2 开始前先检查对 opencode 的影响，确保无伤插入」。
> 背景：2026-09-23 曾因插入本插件致 OpenCode **会话反复重置 / 上下文丢失**（见 [故障复盘](../evidence/M3/2026-09-23-插件会话重置根因与修复.md)）。

## 1. 上次故障（必须不再复发）

- **现象**：每次回车**新建会话**、上下文丢失、`MessageAbortedError`。
- **根因**：插件以 **replace 模式**接管 `session_prompt` 槽位，却**未转发宿主 props**（`session_id`/`on_submit`/`visible`/`disabled`/`ref`）→ 会话内输入框退化为"首页新建会话"框。
- **二次根因**：renderer 真实签名是 **`renderer(context, data)` 双参**；首版误把第 1 参 `{theme}` 当宿主 props。
- **现状**：`slotPrompt.ts` 已改双参、转发全部宿主 props；2026-09-23 旧机真实 TUI（连续 2 条消息同会话）验证通过。**但那是旧机**；本机从未插入，须重新验证。

## 2. 接触点清单与风险

| # | 接触点 | 位置 | 性质 | 风险 | 现状 |
| --- | --- | --- | --- | --- | --- |
| 1 | `api.slots.register` **replace** `session_prompt` | index.ts:76 | 改写会话输入框 | **高** | 已转发全部宿主 props（本机未验） |
| 2 | `api.ui.Prompt(props)` | index.ts:84 | 渲染输入框 | 高 | 随 #1 |
| 3 | `api.route.current` 读 | binding/refresh | 只读 | 低 | — |
| 4 | `api.state.session.*` + `api.state.part` 读 | stateRead.ts | 只读 | 低-中（长会话 `messages` 读取） | safe 包装 |
| 5 | `api.event.on` × 14 类 | index.ts:44,180 | 回调 | 中（含高频 `message.part.updated`） | 节流 500ms |
| 6 | `setInterval(refreshState, 10s)` | index.ts:179 | 周期读 | 低 | dispose 清理 |
| 7 | `api.lifecycle.onDispose` | index.ts:200 | 清理 | 低（正面） | 清理 timer/pending/订阅/bridge |
| 8 | `Bun.serve`（127.0.0.1 随机端口） | bridge/server.ts | 本地服务 | 低 | 仅 loopback + token |
| 9 | 写 `bridge.json`（0600） | index.ts:182 | 落盘 | 低 | 用户态目录 |

## 3. S4.1b 新增项的专门风险与加固

- **风险 A（已修 + 有测试）**：`refreshState`（被事件回调/定时器调用）**未包 try/catch** → 若 `api.route.current`/状态读取抛错，会**冒泡到 OpenCode 事件总线/定时器**。**加固**：整函数 try/catch；新增回归测试（`entryState.test.ts`：`route.current` 抛错时 `tui()` 仍 resolve）。**证据：`bun test` 108/108、`tsc` exit 0。**
- **风险 B（待观察）**：订阅了高频事件 `message.part.updated` → 流式生成期频繁触发（节流 500ms）→ 读 `messages()`+`part()`。**缓解选项**：去掉最高频类型或提高阈值；当前保留 500ms（评估为低）。
- **风险 C（低）**：`tui()` 中 `Bun.serve`/描述文件写失败会 reject → 插件加载失败（显示错误，不改写会话）；描述写失败已 `bridge.stop()` 回滚。

## 4. 无伤插入程序（顺序执行）

1. **备份**：`cp ~/.config/opencode/tui.json ~/.config/opencode/tui.json.bak-<ts>`。
2. **隔离预演（非破坏）**：独立 HOME/XDG + `--pure` 启动 → 确认插件**加载无 ERROR**、`bridge.json` 写出、`/v1/health` alive、0 ERROR/WARN。
3. **最小插入**：在 `tui.json` 的 `plugin` 数组**追加**本项目插件路径（**保留** `oh-my-openagent@4.19.4`）。
4. **重启 OpenCode**。
5. **即时真实 TUI 验证（决定性）**：会话内**连续发送 ≥2 条消息** → 必须落在**同一会话**、上下文完整、无新建/跳空会话、无 `MessageAbortedError`（同 2026-09-23 §9.5 判据）。
6. **回滚**：若出现重置/异常 → 恢复 `tui.json` 备份并重启（即时、无损）。
7. **观察**：OpenCode 日志无插件报错。

## 5. 诚实边界

- 隔离预演**不能**证明槽位渲染（隔离实例无 provider 认证，触发不了 `session_prompt`）；**真验证只能靠第 5 步真实会话**。
- 本机从未插入过本插件 → #1 高风险项在本机**未验证**；不得把旧机结论当作本机已验证。
- S4.2「真实快照」路径**依赖插件在本机运行**；插入成功前，S4.2 只到 mock / `ble_client` 层。

## 6. 本次已做 / 未做

- **已做**：加固风险 A（`refreshState` try/catch）+ 回归测试；`bun test` **108/108**、`tsc` exit 0。
- **已做**：**隔离预演（非破坏）**，见 §7。
- **未做**：未改动 `tui.json`（保持**未插入**态）；未重启 OpenCode；未执行真实会话验证（第 5 步）。

## 7. 隔离预演结果（2026-09-25，已执行）

独立 HOME/XDG + 本插件注册（opencode 1.18.32）：

- 插件**加载并完整执行** `tui()`：写出隔离 `bridge.json`（端口 45419）。
- `/v1/health` → `{"alive":true,"pluginVersion":"0.1.0"}`；`/v1/binding` → 首页态（`promptVisible:false`）；`/v1/state/stream` → `: connected` + `event: snapshot` `data: null`（无会话）——S4.1b 状态流正常。
- 日志 **无 ERROR/WARN/plugin/fail**。
- **结论**：加载期风险（#3—#9）**干净**；**唯一仍未验的是 #1（replace 槽位真实渲染）**——隔离实例无 provider 认证，触发不了 `session_prompt`，**必须靠真实会话验证（第 5 步）**。
- 隔离实例已清理（tmux 杀、目录删）。
