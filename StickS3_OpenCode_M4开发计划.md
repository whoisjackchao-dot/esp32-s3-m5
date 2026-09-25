# M4 开发计划 — 状态屏与托盘

> 日期：2026-09-25。依据：[交付节点 M4](./StickS3_OpenCode_交付节点与完成定义.md)、
> [HLD §7.4 BR-03 快照字典 / §11](./StickS3_OpenCode_功能模块HLD.md)、
> [契约 C08/C10](./StickS3_OpenCode_契约与演进设计补充.md)、
> [设计文档 §11 屏幕/按键/电源](./StickS3_OpenCode_语音状态屏_设计文档.md)、
> [测试矩阵 ST/UI/REL](./StickS3_OpenCode_测试用例与追踪矩阵.md)。

## 0. 基线

- **M3 出口**：Q03 ✅ PASS（缩减样本）；**Q04 采样任务暂存**（2026-09-25，当前不具备收音条件，非放弃）。
- **代码现状**：插件**无状态归约**（仅 bridge/draft/coordinator）；固件 `display.c` 仅 M2 单帧桩（STATE/BUSY/IDLE）；host **无托盘**（仅 `asr_cred_tool` 过渡 CLI）。
- M4 入口条件（交付节点）：M3 核心闭环 ✅；Q02/Q05/Q06 相关参数**部分待冻结**（见 §3）。

## 1. M4 目标（引自交付节点）

- **用户价值**：不切窗口即可查看真实状态，知道何时等待/出错，电池时能息屏保连。
- **交付**：三页信息、用量口径标签、短提示音、托盘管理、取消/重试入口、USB/电池策略、字体及第三方来源。
- **出口**：全部信息可从真实事件/快照追溯；未知不造值；中文和按键由用户实际体验确认。
- **不算完成**：只有静态假数据屏幕、状态靠字符串猜测、费用显示 0 但口径不明。

## 2. 故事清单

| 故事 | 用户价值 | R | 用例 | 分类 | DoR 状态 |
| --- | --- | --- | --- | --- | --- |
| **S4.1** M10 精简快照归约（插件） | 状态真实可追溯 | R17/R18 | ST-01—ST-12 | **T** | ✅ **可开工**（依赖 BR-03 字典，已冻结） |
| **S4.2** 主机合并状态 + BLE 下发 | 设备可见真实状态 | R17 | BLE-API-06 | T+C | ⏳ 依赖 S4.1；快照 BLE 编码需随 S4.2 冻结 |
| **S4.3** 固件三页显示（M03） | 不切窗口看状态 | R17 | UI-02 | T+C+H | ✅ **完成**（英文三页；中文暂缓-用户裁定；真机合格） |
| **S4.4** 短提示音（等待/错误） | 被动提醒 | R19 | ST-11 / UI-05 | T+C+H | ✅ **完成**（1kHz/150ms 默认；等待/错误各一次、录音延后、过期不响） |
| **S4.5** USB/电池策略（M03） | 息屏保连、续航 | R20 | UI-03 / UI-04 | C+H | ✅ **息屏完成**（60s 背光关+按键/事件/录音唤醒，心跳不唤醒；真机合格）。✅ **电量读数完成**（通路＝M5PM1 `reg0x22` mV + `0x12` bit0 充电位；**三页常驻** `BAT n%`；真机串口 `battery 4200mV 100% chg=0`）。**USB 插入/拔出策略（UI-04）待做**。 |
| **S4.6** 侧键手势（切页/长按取消） | 免看屏操作 | R17 / Q06 | UI-06 | T+C+H | ✅ **完成**（BtnB 短按切页/长按取消；阈值 **800ms**、去抖 **30ms**；真机合格） |
| **S4.7** Windows 托盘（M07） | 配置/重试/退出 | R16 | UI-01 / UI-06 | C+H | ✅ **host 已解锁（Q6 已解除）**；启动器 LLD 待冻结 |

> 顺序：**S4.1 → S4.2 → S4.4 →（字体冻结后）S4.3 → S4.5 → S4.6 → S4.7**。
> S4.7 托盘为 Windows 原生 C++（Win32 `Shell_NotifyIconW` + Credential Manager），量级最大，放最后。

## 3. 待冻结（C10：M4 相关故事前必须冻结）

| 项 | 现状 | 影响故事 | 需要的动作 |
| --- | --- | --- | --- |
| **中文字体** | **用户裁定：界面保持英文，中文暂缓**（2026-09-26） | S4.3 | 如需中文再启（Noto Sans SC/OFL 方案与生成器已实现过，可自 git 历史恢复） |
| **长按阈值 / 去抖** | **已冻结：长按 ≥800ms、去抖 30ms**（用户 2026-09-26 批准） | S4.6 | 已实现并真机合格 |
| **息屏参数** | 60s 已批 | S4.5 | 亮度/唤醒细节；HIL 校准 |
| **提示音时长/频率** | 取默认 **1kHz 方波 / 150ms**（2026-09-26） | S4.4 | 实机校准后可修订 |
| **LVGL 或自绘** | 未定 | S4.3 | 三页布局 + 中文渲染的内存/许可权衡；现为自绘 `display.c` |

## 4. 不变量与安全边界（跨故事）

- 状态**不读聊天正文猜**（C08/§7.4）：只依据权威状态与 pending 权限/问题。
- 用量**未知 ≠ 0**：保留 `unknown` / `not_reported` / `known(value)`；账户/云 ASR 费用不进会话累计。
- 心跳（10s）**不刷新**权威数据新鲜度；15s 无有效更新标过期。
- 设备按键**不重试/不提交/不审批/不终止 OpenCode**；托盘才提供重试识别/填入。
- 过期快照、旧 revision 不回滚、不重播历史提示音。

## 5. S4.1 细化（本轮首个故事）

- **目标**：把 TUI 状态/事件归约为 BR-03 精简快照（纯逻辑，TDD）。
- **输入**：合成事件/状态序列；**输出**：快照对象（身份与排序、绑定能力、编程状态、项目/会话、工具/待办、模型、用量、新鲜度）。
- **验收**：`bun test` 覆盖 ST-01—ST-12 的归约语义；未知/未报告/已知三态正确；epoch/revision 去重与旧值丢弃正确。
- **明确未交付**：本故事只做**归约逻辑与类型**，不含真实 TUI SDK 接线、不含 BLE 下发、不含 UI。

## 6. 进度登记

- **2026-09-25**：**S4.1 完成**（T 类，选择性 TDD）。新增 `plugin/src/core/state/types.ts`（BR-03 类型 + 三态可用性）、`plugin/src/core/state/reduce.ts`（纯归约函数）、`plugin/test/state/reduce.test.ts`（**15 用例**，覆盖 ST-01—ST-12 归约语义与 C08 epoch/revision/协议错误）。证据：`bun test` **85/85**（原 70 + 15）、`tsc --noEmit` **exit 0**。
  - **明确未交付**：真实 TUI SDK（TUI-API-04/05）接线、BLE 快照下发、UI —— 归后续故事/增量。

- **2026-09-25（续）S4.1b 完成**（T 类，选择性 TDD）：新增 `plugin/src/core/state/store.ts`（`StateStore`：ingest→归约→持有最新快照 + 15s 过期）、`plugin/src/plugin/stateRead.ts`（TUI 状态→`SnapshotSource` 的**防御式**读取与**纯映射**）、桥接 `bridge/server.ts` 的 `/v1/state/stream` 接入可选快照提供者 + 心跳（**附加类型留在 bridge 模块，未改冻结的 `core/types.ts`**）、`index.ts` 接线。**已接线**：①用量/模型（`Session` 累计 `tokens`/`cost`/`model.id`；无则回退最近 assistant 的 `modelID` 作 lastUsed；缺失保守 unknown）；②行工具/失败数/错误码（`state.part` → tool parts 的 running/pending/error）；③**事件驱动刷新**（订阅 `session.status`/`session.idle`/`message.updated`/`permission.*`/`question.*`/`todo.updated` 等 14 类，节流 500ms；并保留 10s 心跳保活新鲜度）。证据：`bun test` **107/107**（含桥接状态流扩展 3 用例 + **入口接线集成 1 用例**：假 TUI api → `tui()` → 写 bridge.json → `/v1/state/stream` 输出归约快照）、`tsc --noEmit` **exit 0**。另：`index.ts` 的描述文件目录支持环境变量 `STICKS3_DESCRIPTOR_DIR` 覆盖（可配置/可测）。
  - **明确未交付**：①真实 TUI 运行验证（需注册插件并重启 OpenCode，属用户侧动作，与 M1 桥接注册同类）；②快照 BLE 下发（S4.2，待契约批准）；③**子任务 busy 检测（ST-12 的 `rootIdleButChildBusy`）**：TUI 状态 API 仅暴露 `get/status/todo/messages/permission/question/diff/part`，**无 children 访问器**（OC-07 无对应 TUI-API），适配层只能置 `false` —— 归约逻辑已支持并有测试，**缺口在数据源**，需另寻来源或降级承诺。

- **2026-09-26 S4.3/S4.4/S4.5/S4.6 完成登记**（用户逐项真机确认合格）：
  - **S4.3 三页显示（英文）**：`display.c` 三页 + `draw_str_px` 像素字号；命中并修复**帧缓冲字节序**缺陷（ST7789 高字节在前 → 黄↔紫、紫↔绿）；中文按用户裁定**暂缓**（界面英文）。**取消 5s 自动轮巡** → 固定首页、仅侧键切页。后续**布局校准**：全屏文本整体下移 `Y_SHIFT=8`（用户判定顶部"缺字"实为**打印外壳遮挡**）+ 页脚锚点防底部裁切 + 模拟器同步。证据：[evidence/M4/2026-09-26-S4.3-UI布局校准.md](./evidence/M4/2026-09-26-S4.3-UI布局校准.md)。
  - **S4.4 短提示音**：I2S 全双工 TX + ES8311 播放；等待/错误各响一次、录音中延后、过期不响；默认 **1kHz 方波/150ms**；修复功放长鸣（仅提示音期间使能 + 静音尾）。真机"等待"响一声 ✅。
  - **S4.5 息屏**：`power_task` 60s 无活动关背光（GPIO38）、BLE 保连；按键/事件/录音唤醒、**心跳不唤醒**。真机合格 ✅。
  - **S4.6 侧键**：BtnB（GPIO12）短按(<800ms) 切页 / 长按(≥800ms) 取消；阈值 **800ms**、去抖 **30ms**。真机合格 ✅。
  - **S4.5 电量读数 ✅（补齐"待查"）**：通路查明为 **M5PM1 I2C `0x6E` 寄存器 `0x22`（VBAT_L，2 字节小端＝mV）** + 充电位 `0x12` bit0（低有效，PM1 GPIO0 配输入）；实现 `pm1_battery()` + **三页常驻** `BAT n%`（`power_task` 10s 刷新）。真机证据：串口 `battery 4200mV 100% chg=0`（USB 满电）。**未验**：放电下降曲线、`CHG` 真机显示。**未做**：USB 插入/拔出策略（UI-04）。证据：[evidence/M4/2026-09-26-S4.5-电量读数.md](./evidence/M4/2026-09-26-S4.5-电量读数.md)。

## 7. S4.2 契约（BLE STATE 快照编码）—— ✅ **已批准 v1.0（2026-09-25）**

> **已冻结**：[contracts/M4-STATE快照冻结.md](./contracts/M4-STATE快照冻结.md)（用户"批准"）。
> 下文为提案原文，保留作设计记录；实现以冻结契约逐字节为准。
>
> **实现进度（2026-09-25）**：
> - ✅ 编解码器 `firmware/sticks3/main/s3p_state.{h,c}`（固件/主机共享，纯 C）；主机测试 `host/src/s3p_state_test.c` **27 checks / 0**（黄金向量、往返、码点安全截断、拒绝路径、哨兵）。
> - ✅ **host 合并映射** `host/src/state_merge.{h,c}`（BR-03 → §7 字段/标志）；主机测试 `state_merge_test.c` **19 checks / 0**（含 merge→encode→decode 端到端）。此件**与 host 进程形态无关**，可直接复用。
> - ✅ 固件解码接入 `ble_gatt.c`（`case 0x12` → `s3p_state_decode` → `s_state_status/flags/valid`）；新增 `ble_gatt_host_status()`，**显示按真实 status 呈现 IDLE/BUSY/RETRY/WAIT/ERROR**（修正"ERROR 被伪装成 IDLE"的 ST-04 缺陷）；`main/CMakeLists.txt` 登记源。
> - ✅ 固件重建 **734,368 B**（分区余量 30%）；`flash-artifacts` 已更新（sha `d17a63a1…`）。
> - ✅ **host 发送端（经 `ble_client` 验证）+ 设备端到端 PASS**：`ble_client.cpp` 已升级为经 `state_merge` + `s3p_state_encode` 下发 **v1.0 STATE**；固件已**刷入**（734,368 B）。实测：`ble_client` 写 `status=0 flags=0x02 rev=1 len=23` → 设备串口回显 `host STATE status=0 flags=0x02 rev=1 tools=0`；会话 500 帧 + FNV 一致（`ble_client PASS`）。
>   - ✅ **验证增量 PASS（2026-09-25 续，用户裁定）**：`ble_client` 状态源已为 **`GET /v1/state/stream` 真实快照**（`read_bridge_snapshot`，binding 仅作回退）。端到端复跑证据：`src=snapshot status=2 flags=0x39 rev=1 len=71` → 设备串口回显 `host STATE status=2 flags=0x39 rev=1`；会话 500 帧 + FNV 一致（`ble_client PASS`）。**更正**：本行旧述"状态源为桥接 binding"**已过时**（代码早已实现 snapshot 源，本次实测证实）。
>   - ✅ **生产 host 交付（2026-09-25 续）**：新增常驻 `host/src/state_host.exe`——读 `bridge.json` → 连 `StickS3` BLE（CTRL）→ 订阅 `/v1/state/stream` → **按 `snapshotRevision` 去重** → `state_merge`+`s3p_state_encode` → 持续写 `STATE(0x12)`；桥接/BLE 断开**自动重连**；**不写 READY、不开录音会话**。配套：SSE 帧解析纯件 `sse_parse.h`（7 用例 TDD）、共享映射 `bridge_snapshot.h`（`ble_client` 已收敛复用）、`state_merge` 文本 **UTF-8 码点安全截断**（修中文截断缺陷）。HIL：60s 内连续 **7 次**下发（rev 804→817），设备串口逐条回显一致（`status=2 flags=0x39`）。证据：[evidence/M4/2026-09-25-S4.2-生产host.md](./evidence/M4/2026-09-25-S4.2-生产host.md)。
>   - **仍未交付**：Windows 服务（SCM）/托盘内嵌包装（host 需手动或启动器拉起）；S4.7d（重试入口，待 IPC-02）；（可选）插件侧事件即时推送以降低 >10s 延迟。
> - ✅ **命中并修复真实缺陷（S4.2 e2e 暴露）**：富 TLV 快照（len=66）被设备以 **ATT error 13** 拒绝——根因 `chr_ctrl_access` 仅把**前 8 字节**拷入 `buf[8]`，却把 `om_len` 传给解码器 → **越界读栈垃圾** → 解码失败。**修复**：按整包拷贝（≤`S3P_STATE_MAX_LEN`）+ 超长拒绝。重刷后设备回显 `host STATE status=0 flags=0x3e rev=1 tools=2`（与发送一致），`ble_client PASS`（sha `0d8e8de4…`）。
>   - **⚠ 教训**：先前 23 字节的"PASS"实为**栈上巧合（UB）**，非真验证；只有≥8 字节边界以上的用例才能暴露它。
>
> **待澄清（契约一致性，登记待裁）**：[HLD §7.4](./StickS3_OpenCode_功能模块HLD.md) 称"Windows 本地另合并 `voicePhase`、设备电量/充电/连接状态后**通过 BLE 下发**"，但本 §7 `STATE` 载荷**不含** voicePhase/battery 字段（电量与录音为**设备本地**已知）。两种解读：①HLD 表述泛指 Windows 侧 BR-03 快照（**非** BLE 下发），本 §7 无缺；②STATE 确实应扩入 `voicePhase`（host 侧已知的转写/追加阶段，设备无法自知）。**需用户裁定**；若为②，按契约 §6 走 ADR + 版本升级（v1.1）。

> 依据：M2 契约 §消息集 `0x12=STATE{快照版本 u8 + 精简状态位图 u8}`（状态位图字段声明"在 M4 冻结"）。
> 本提案**不改变** M2 冻结的线格式与黄金向量；仅为 M4 的 STATE 载荷提出字段布局，**待用户批准后方可两端实现**（契约先于两端实现）。

**设计原则**：设备只需渲染三页所需字段；长文本（项目名/会话标题/模型名/工具名）按**有界 UTF-8 截断**（保留字素边界），超限以占位符表示；未知不伪装 0。

| 段 | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 0 | msgType | u8 = 0x12 | M2 已冻结 |
| 1 | snapshotVersion | u8 = 1 | 载荷版本 |
| 2 | status | u8 | 0=unknown,1=idle,2=busy,3=retry,4=waiting,5=error |
| 3 | flags | u8 | bit0 canAppend, bit1 expired, bit2 hasProject, bit3 hasTitle, bit4 hasModel, bit5 hasUsage, bit6 toolFailed, bit7 childBusy |
| 4..7 | snapshotRevision | u32 LE | 旧 revision 不回滚 |
| 8 | toolCount | u8 | 运行工具数（>255 截断并置位） |
| 9..10 | todoCompleted / todoTotal | u8×2 | 无 todo 时 total=0 |
| 11..14 | tokens.input | u32 LE | 缺失=0xFFFFFFFF（unknown 哨兵） |
| 15..18 | tokens.output | u32 LE | 同上 |
| 19..22 | tokens.cost_milli | u32 LE | 费用×1000；缺失=0xFFFFFFFF |
| 23.. | 变长文本区 | TLV | project/title/model 各 [tag u8][len u8][utf8…] |

**待批准项**：①是否采用本布局与哨兵约定；②变长文本区的截断上限（建议每段 ≤32 字节）；③工具名是否需要下发（或仅数量）；④MTU 分片策略（单通知 ≤ MTU-3）。

**可行性分析（MTU 适配，供批准参考）**：
- 固定段 = 23 字节；变长文本区最坏 = 3×(tag 1 + len 1 + utf8 32) = 102 字节；**最坏总长约 125 字节**。
- 契约 MTU 偏好 247 → 单通知净载荷 ≤ **244** 字节：**125 < 244，单通知放得下，无需分片**。
- 契约硬底线 MTU 65 → 单通知净载荷 ≤ **62** 字节：**125 > 62，低 MTU 下需分片**（或截断文本区）。→ 建议：MTU ≥ 128 时单通知；否则对文本区截断并置位（此点需批准时确认）。

**未交付**：本提案无实现（codec/固件解析/host 序列化均待批准后进行）；不构成 S4.2 达标。

## 8. S4.7 托盘（M07）故事卡 —— Q6 解除后可开工

- **用户价值 / 需求**：R16 —— 不切窗口管理设备/连接/Key；提供识别重试 / 填入重试 / 取消 / 退出。
- **入口**：M3 核心闭环；**Q06 托盘分工已批准**；**Q6 已解除**（host WinRT 在本机可构建，见核查报告 §12）。
- **交付（切片顺序，每片一轮实现+演示）**：
  1. **S4.7a 最小托盘壳**：Win32 `Shell_NotifyIconW` + 消息循环 + 菜单（退出）。← 本次
  2. S4.7b 桥接健康显示：读 `127.0.0.1` 的 `/v1/health`（Bearer token 来自 `bridge.json`），显示连接态。
  3. S4.7c Key 配置：Windows 凭据管理器（复用 `asr_credential.c`）。
  4. S4.7d 重试入口：识别重试 / 填入重试（区分；不确定先检查，不盲目重发）。
- **分类**：C+H（Win32/WinRT 集成；UI 需实机目视）。
- **适用用例**：UI-01（首启/配对/Key/退出）、UI-06（托盘重试分工）。
- **出口**：普通用户可安装运行、无日常管理员；托盘状态可追溯；未知不造值；中文体验由用户确认。
- **明确未交付**：长按手势（S4.6）、电源策略（S4.5）、三页（S4.3）、启动器/IPC。
- **依赖/风险**：①WinRT 用 workaround（`C++17 + /await`，非项目声明 C++20）；②启动器↔托盘 IPC（IPC-02）**尚未实现**，S4.7b 前需冻结 LLD；③`.cmd` 必须**纯 ASCII**（本机 cmd.exe 按 GBK 解析）。

**进度**：
- **S4.7a 完成（2026-09-25）**：`host/src/tray.cpp`（Win32 `Shell_NotifyIconW` + 消息循环 + 退出菜单 + `TaskbarCreated` 重建）+ `build_tray.cmd`（纯 ASCII）。BUILD_OK；启动冒烟 RUNNING_OK。**无 WinRT 依赖**。
- **S4.7b 完成（2026-09-25）**：托盘读 `bridge.json`（候选路径或环境变量 `STICKS3_BRIDGE_JSON`）→ WinHTTP `GET /v1/health`（Bearer）→ 菜单显示连接态；附 ASCII `status_code`。**全路径已实测**：`NO_BRIDGE`（无描述文件）、`DESCRIPTOR_BAD`（缺字段）、`HEALTHY`（对本地 mock `/v1/health` 返回 `{"alive":true}`）、`UNREACHABLE`（关闭端口）。`tray.exe` BUILD_OK（158,208 B），各路径 RUNNING_OK。**唯一未测**：真插件桥接下的 `HEALTHY`（需注册插件并重启 OpenCode，属用户侧动作）。
- **S4.7c 完成（2026-09-25）**：托盘菜单「配置 ASR Key…」/「删除 ASR Key」→ 复用已测试的 `asr_cred_tool.exe`（`store`/`delete`；**Key 由控制台隐藏输入，不进命令行**，符合凭据规则）。`tray.exe` BUILD_OK（161,792 B）、冒烟 RUNNING_OK。**未测**：真实点击菜单的 spawn（需 UI 交互）与真机凭据写入。
- **构建路径补齐（2026-09-25）**：`tray` 目标已加入 `host/CMakeLists.txt`（`WIN32_EXECUTABLE`），与 `build_tray.cmd` 双路径一致；CMake(Ninja+VS2019) 构建 `toolchain_check.exe`/`sticks3_host_entry.exe`/`tray.exe` 全部成功。
- **可测性补强（2026-09-25）**：把 `bridge.json` 解析提为纯模块 `host/src/tray_parse.h`（无 Win32 依赖），新增 `tray_parse_test.cpp`（**7 用例**：提取 url/token、缺失键、无值、未闭合串、空串、空 json）。实测 `TRAY_PARSE_PASS`；重构后 `tray.exe` 重建 BUILD_OK，且 **HEALTHY 路径复验通过**（防重构回归）。
  - **明确未交付**：S4.7d（重试入口）；启动器 IPC-02 LLD 仍待冻结。

## 9. S4.7d 故事卡（重试入口）—— 设计已批准（2026-09-25）

- **用户价值 / 需求**：R19/Q06 —— 托盘明确区分「重试识别」与「重试填入」；**填入重试限已知未写入且目标有效**，不确定时先人工检查，不盲目重发；设备按键**不**重试。
- **入口**：S4.7a—c；M1 协调器（操作回执/查询）；**host 常驻服务设计（阻塞点）**。
- **交付（拟）**：
  1. S4.7d-1 「重试识别」：对主机内存中 ≤5 分钟的失败音频重跑 ASR（**不**重采音）。
  2. S4.7d-2 「重试填入」：仅当上一操作状态为**已知未写入**（`rejected`/未 `applied`）且目标有效时，以**新 operationID** 重发 `draft.append`。
- **分类**：C+T（协调器状态机）+ H（实机）。
- **用例**：TX-10/C03 适用部分、UI-06。
- **阻塞（关键，需设计冻结）**：
  - **转写正文归属**：C02 规定回执只存指纹、不存正文；M1 协调器**无法**自行重试填入（无正文）。
  - **主机形态**：现 host 工具均为**一次性 exe**（`voice_append.exe` 跑完即退），无进程持有正文/音频内存 → 「重试」需要 **host 常驻服务**或 IPC-02 启动器。
  - → **在 host 常驻服务与 IPC-02 冻结前，S4.7d 不实现**（避免在未定形态上编码）。
- **明确未交付**：常驻 host 吸收 voice 路径、控制端点、托盘接线、HIL（见下方进度）。
- **进度（2026-09-25）**：设计冻结 → [S4.7d 与 IPC-02 LLD **已批准** D1—D5](./StickS3_OpenCode_S4.7d与IPC-02_LLD草案.md)。**纯逻辑 Slice 1 已实现**：`host/src/retry_store.{h,c}`（重试资格/TTL；`retry_store_test` **13/0**）+ `build_retry_store_test.cmd`。
- **关键前置（LLD §7 补充发现）**：现音频/追加路径为**一次性 `voice_append.exe`**（退出即失），故"内存持有音频/正文"要求**常驻 host 吸收该 voice 路径** —— S4.7d 真正前置，归下一切片。
- **吸收进展①（2026-09-25 续）**：常驻 `state_host` 已具备 **BLE 音频/事件接收 + 会话重组 + s3pcap 落盘**（订阅 `CTRL/AUDIO/EVENT`；`s3p_reasm`；`STICKS3_CAPTURE_DIR`；`STICKS3_HOST_TRIGGER` 仅供测试触发）。HIL：触发会话 → `BEGIN/END rec=1 frames=500 samples=480000 rc=0` → `rec_1.s3pcap`（75,332 B）；**STATE 推送无回归**（连续 rev 递增）。
- **吸收进展②-a（2026-09-25 续）数据面**：会话音频**内存持有**（`g_held`，s3pcap 字节；**不落盘**，符合 D1/C02/SEC-04）+ `retry_store` 资格接线。HIL：END 后 `retry: audio held (75,479 B) can_recognize=1`。host 产出的 s3pcap 经既有 `decode_capture.py` **解码兼容**（500 帧 → 30.00s）。
- **仍未吸收**：**ASR / `draft.append`**（含 Opus 解码编排；仓库无 C/C++ Opus 解码器，暂用 PyAV helper 子进程——登记偏差）。
- **吸收进展②-b（2026-09-25 续）**：抽出 `append_ctl.{h,c}`（`draft.append` 请求构造 + 结果分类，纯逻辑；`append_ctl_test` **14/0**），`voice_append.exe` 已**收敛复用**（去重复实现；重建 BUILD_OK）。供 host 吸收 append 时直接调用。
- **吸收进展②-c（2026-09-25 续）**：常驻 host 编排 **解码（PyAV helper 子进程）+ ASR（`asr_adapter`/`asr_transport`）+ `draft.append`（`append_ctl`）→ `retry_store` 状态迁移**（`STICKS3_HOST_VOICE=1` 启用；worker 线程避免阻塞 BLE 回调；session/epoch 取自 SSE 快照与 bridge.json）。HIL：`voice: decoded -> wav` + `ASR skipped (no key)`（门控生效）、临时文件已清、STATE 无回归。**待控场 HIL**：带 ASR key 的真实 ASR+append（会写入当前会话草稿 + 云端调用，需另行确认时机）。
- **#2 控制端点（IPC-02）完成（2026-09-25 续）**：常驻 host 起 **loopback HTTP**（`127.0.0.1:${STICKS3_HOST_PORT:-45910}`，Bearer `${STICKS3_HOST_TOKEN:-sticks3-host}`），暴露 `GET /v1/retry/status`、`POST /v1/retry/{recognize,append}`。curl 验证：status JSON（`canRecognize/canAppend/hasAudio/hasText/status`）、无鉴权 **401**、未就绪 **409 not_eligible**。
- **#3 托盘接线 + C11 凭据（2026-09-25 续）**：托盘菜单新增**「重试识别」「重试填入」**→ 经 `%LOCALAPPDATA%\sticks3\host.json`（host 写出）发现端点并 POST（结果 MessageBox）。host 的 ASR Key 改为 **env → 凭据管理器（C11 `asr_cred_load`）回退**（Key 不落命令行/env 亦可）。`tray`/`state_host` 均 BUILD_OK；描述文件实测写出。**托盘菜单点击**需人工 UI 验证（登记）。
- **#4 HIL（2026-09-25 续，Key 已由用户经 `asr_cred_tool store` 存入）**：常驻 host 对设备会话自动完成 `解码→ASR→draft.append`。实测：**ASR 真实调用成功**（HTTP 200；凭据指纹 …c877c3a7 有效，非 auth/transport 错）；出现 `voice: append outcome=0 http=200`（**填入成功**）与 `outcome=3 http=409`（**拒绝 → 状态 REJECTED → `canAppend:true`，即"已知未写入"，正是重试填入适用态**）；`POST /v1/retry/recognize` → **202**（资格成立）。→ **重试机制与真实云 ASR 均打通**。
- **遗留① append 409 —— 确定性复测：链路正常（2026-09-26）**：用 **Windows SAPI TTS** 合成中文语音（`Microsoft Huihui Desktop / zh-CN`）→ `voice_append.exe`（新增 **C11 凭据回退**，免 env）→ `ASR HTTP 200 class=ASR_OK`（`text: 这是一条语音测试消息，用于验证识别与草稿填入。`）→ `桥接 HTTP 200 {"status":"applied","reason":null}`（**[PASS] 已填入草稿**）。→ **含语音时 ASR+append 完全正常**；HIL 中的 409 属场景性（疑 **epoch/session 漂移** 或**连发短会话**），host 已注入 `append resp=` 以抓 reason。
- **遗留② 多短会话 —— 干净运行未复现（2026-09-26）**：稳定 BLE 下实测 **触发 1 次 / 订阅 1 次 / 会话 1 个**（`rec=1`）。早先的 16/42/105 帧多会话**非稳定复现**，最可能为 **BLE 重连时测试触发重复 fire** 或**按键**。**硬化**：`STICKS3_HOST_TRIGGER` 改为**每进程仅触发一次**；生产不设该变量，无此路径。另修 `asr_cred_tool` 中文乱码（`SetConsoleOutputCP`）。
- **用户目视确认（2026-09-26）**：用户在会话输入框**看到** TTS 语音测试识别结果（`这是一条语音测试消息，用于验证识别与草稿填入。`）并**手动发送**。→ **M4「语音→识别→填入草稿→人工发送」E2E 由使用者本人确认成立**（不使用自动提交；对应 E2E-01p 的人工确认子步）。
