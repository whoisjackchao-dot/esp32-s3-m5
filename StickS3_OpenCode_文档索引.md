# StickS3 OpenCode项目文档索引

> **当前状态以文末 [当前状态与下一步](#当前状态与下一步2026-09-23-收工交接) 为准**；下列 2026-09-15 标注为**历史快照**（当时无实现、无测试）。
> 2026-09-15（历史）；当时为设计审阅与测试规格阶段；没有项目实现或已执行产品测试。  
> 方法论以用户最新要求为准：Agile＋Evolutionary Design＋Selective TDD。

> 最新集中审阅（2026-09-15）：用户已批准其余已写明方案，仅将电池息屏改为1分钟（60秒）、心跳改为10秒。状态过期仍15秒，其他数值不变。测试未执行，未填写的工程参数仍需补齐。

## 建议阅读顺序

**先看直观总览**：[功能模块HLD](./StickS3_OpenCode_功能模块HLD.md)，包含模块职责、技术栈、总图/时序图和分层API目录，明确标注付费ASR及OpenCode既有模型费用。

**申请与配置云服务**：[智谱ASR API申请与配置操作手册](./智谱ASR_API申请与配置操作手册.md)，独立说明注册/Key申请、计费核对、首次调用、错误排查与未来托盘配置。真实调用可能付费；当前托盘尚未实现。配套[配置快速指南](./智谱ASR_API配置快速指南.md)（2026-09-16，2026-09-20复核）：申请步骤、参数速查表、SDK/cURL调用示例与错误速查，内容与手册保持一致，费用/合规细节以手册为准。**2026-09-22状态**：**Key 已配置并首次真实调用成功**（HTTP 200，text 非空，evidence/M3/2026-09-22-ASR_Key验证与首次真实调用.md）；企业网络需 `--ssl-revoke-best-effort`（手册 §7.2）。M3 入口 Key 条件满足；Key 迁入凭据管理器与 30 条语料冻结仍待 M3 进行中/验收前完成。

1. [方法论审查与决策](./StickS3_OpenCode_方法论审查与决策.md)：原稿缺口、T/C/H/D分类、Q01—Q09决策状态。
2. [主设计文档](./StickS3_OpenCode_语音状态屏_设计文档.md)：R01—R24已确认需求与候选架构。
3. [契约与演进设计补充](./StickS3_OpenCode_契约与演进设计补充.md)：C01—C10，身份、取消、幂等、协议和数据语义。
4. [测试用例与追踪矩阵](./StickS3_OpenCode_测试用例与追踪矩阵.md)：Given/When/Then、测试分层、R需求映射，尚未执行。
5. [交付节点与完成定义](./StickS3_OpenCode_交付节点与完成定义.md)：M0—M6、DoR、故事DoD、完整产品DoD。
6. [可行性核查](./StickS3_OpenCode_可行性核查.md)：前序实际运行/源码/官方文档证据及未实测边界。
7. [硬件知识文档](./M5StickS3_硬件知识文档.md)：K150规格、引脚、供电注意和资料来源。

## 开发骨架与契约冻结（2026-09-20）

进入开发准备阶段后新增的工程产物；**这些是骨架与冻结，不是已实现功能**。

- [开发准备就绪核查](./开发准备就绪核查.md)：环境/骨架/契约/测试/许可的准备状态与已批准工程取值（2026-09-20）。
- [README.md](./README.md)：仓库结构、开发环境版本、各模块构建/测试命令、安全边界。
- [contracts/](./contracts/README.md)：各里程碑开工前的契约冻结与冻结时间表。
  - [M1 桥接契约冻结](./contracts/M1-桥接契约冻结.md)：**已冻结** BR-01—BR-06 路径/认证/状态码/容量/期限，解除 C02/C04 开工阻塞；工程取值待用户确认。
  - [M2 BLE 线协议冻结清单](./contracts/M2-BLE线协议冻结清单.md)：清单就绪，取值待设计（M2 前）。
  - [M3 ASR 验收冻结清单](./contracts/M3-ASR验收冻结清单.md)：清单就绪，语料待冻结（M3 验收前）。
- `plugin/`（Bun/TypeScript，M09/M10）、`host/`（C++20/MSVC，M04—M08）、`firmware/`（ESP-IDF 5.5，M01—M03）、`tests/`（[测试基础设施](./tests/README.md)）。
- [LICENSE](./LICENSE)（MIT）、[NOTICE.md](./NOTICE.md)（第三方许可登记，逐项审查未完成）。

## 文档关系与决策纪律

- 用户明确确认的需求优先。架构、阈值、UI细节和许可证建议只有在相应Q项批准后才成为实现基线。
- 本轮主设计升为0.2；新增契约处理原稿歧义，不改变“不自动提交、单会话、30秒、5分钟保留”等已确认目标。
- 发现跨文档冲突时登记并修订所有受影响文档，不能悄悄选对实现最容易的版本。
- 原始证据保留其日期与验证范围；不因文档更新把历史实验当成新代码已测试。
- 新故事最少形成链路：R需求→C契约/决策→测试ID→M节点→真实证据；发布时覆盖全部需求。
- 文档审阅完成不等于M0批准，M0批准不等于M1—M6产品完成，生成测试代码也不等于测试通过。

## 当前需用户确认

用户已集中通过当前已写明方案，Q02电池息屏60秒、心跳10秒，状态过期15秒。已列256KiB Opus载荷限额等其余建议获批；未填写的回执容量、尾帧/追加回执超时、具体手势阈值、协议字节布局、实际字体与语料仍须依据事实补齐，不假造用户批准值。GitHub发布仍需另行授权，产品测试尚未执行。下一步是工程参数补齐与故事DoR准备，不再重复询问已批准产品选择。ASR云服务准备（2026-09-20）：充值已完成；剩余待用户确认项为专用Key创建与保管、≤30秒非敏感测试音频准备、首次真实调用授权（可能计费）。

无须重复确认：Windows＋WSL OpenCode TUI、BLE单设备、智谱云ASR、30秒按住录音、追加草稿不发送、完整状态信息、失败内存保留5分钟、USB升级和不开OTA。

## 当前状态与下一步（2026-09-23 收工交接）

### 本轮完成
1. **采音时基修复（重大）**：根因 `I2S_SLOT_MODE_MONO` → WS 实际 32kHz → 2 倍拉伸；修复为 **STEREO 槽位 + 取左声道**。实测 `rate_probe` 32029/s → **16013/s**；真实语音经智谱 ASR 得到**正确中文**（两次验证）。详见 `evidence/M3/2026-09-23-采音时基诊断.md`。
2. **M3 故事 4 纵切**：设备采音 → BLE → Opus → 解码 → ASR 全链验证通过；**仅剩"追加草稿"一跳**。
3. **桥接插件已注册**到 WSL OpenCode 的 **`~/.config/opencode/tui.json`** 的 `plugin` 数组（已备份 `tui.json.bak-20260923`）——**需重启 OpenCode 生效**（**已于 2026-09-23 11:37 重启生效并核实，见下方"开工就绪核查"**）。
   - 更正记录：该插件是 **TUI 插件**（`TuiPluginModule = { id?, tui, server?:never }`，`@opencode-ai/plugin/tui`），注册处是 `tui.json` 而非 `opencode.json` 的 server `plugin` 数组；一度加错位置，已撤回（`opencode.json[.jsonc]` 已还原备份）。
   - 加载方式：OpenCode 用 `Bun.resolve(entry, ~/.config/opencode)` 解析后 `import()`；实测绝对路径与 `file://` 两种写法均可解析。
4. 小智 KB 适用边界（`references/APPLICABILITY.md` + SKILL.md 收紧）；硬件文档附录 B 更新为"已解决"。

### 下一步（2026-09-23 11:39 已核实可执行，重启已生效）
```powershell
. "C:\Temp\ESP32-S3M5\tools\bridge_env.ps1"     # 读 bridge.json + 查 /v1/binding，装载 4 个环境变量
powershell -ExecutionPolicy Bypass -File "C:\Temp\ESP32-S3M5\tools\voice_slice.ps1"
```
对设备说 30 秒 → 预期 `[PASS] 已填入草稿（未提交，请手动发送）` → 在 OpenCode 输入框**手动发送** → **M3 用户价值闭环达成**。

> **2026-09-23 已执行并 PASS**：见 `evidence/M3/2026-09-23-M3故事4纵切闭环.md`（桥接 `status:"applied"`，端口 45093＝重启后新实例）。剩余为"用户手动发送 + 目视确认"与 §7 登记的差异。

**开工就绪核查（2026-09-23 11:39，本轮实测）**：
- 插件已加载运行：`bridge.json`（11:37 写入）+ WSL `127.0.0.1:36997` 监听（opencode pid 17996）→ 重启生效，上一条"需重启"已过期。
- `/v1/binding` HTTP 200：`currentRouteSessionID=ses_f33a84ae7ffewmdx4l3qM6PvpA`、`promptVisible=true`、`promptDisabled=false`、`dialogBlocking=false` → `canAppend=true`。
- Windows 侧可达：`\\wsl.localhost\Ubuntu\...\bridge.json` 可读；`http://127.0.0.1:36997/v1/binding` 返回 **401**（服务存活、鉴权正常）。
- 主机工具齐备：`host/src/ble_client.exe`、`host/src/voice_append.exe`；Python 3.12.10；串口 **COM5** 存在。
- `plugin`：`bun test` **61/61 通过**（本轮实测）。
- 主机回归（本轮实测）：`test_reasm` 566/0、`test_voice_txn` 93/0、`test_asr_adapter` 35/0、`test_asr_transport` 32/0（**需先起 mock-asr `bun serve.ts 18765 0.0.0.0`，否则 31 项因无服务而假失败**）、`test_asr_credential` 21/0 → **747 checks + 61 bun tests 全绿**。
- 变量链一致：`bridge_env.ps1` 导出 4 变量 = `voice_append.cpp` 读取的 4 变量 = 插件 `/v1/binding`\|`/v1/draft/append` 契约。
- **未核实**：设备当前固件是否为 STEREO 修复版（缺设备侧启动/日志证据）；A3 会话的 `captures/` 隐私目录不入库。

**开工前待处理（本轮新发现，未处理）**：
- `flash-artifacts/SHA256SUMS` 已陈旧：`bootloader.bin`、`sticks3.bin` 与实际哈希不一致（仅 `partition-table.bin` 一致）。当前 `sticks3.bin` SHA-256 = `8dbeced7dd16f8988ca11b2f9c1df0153738040b016e93d8feff143fc1477067`。交付前须重算登记。
- `voice_slice.ps1` 仍以 `Read-Host` 手工输入 ASR Key，未走 C11 凭据管理器（故事 4 计划写的是"Key 由凭据管理器供给"）。不阻塞 M3 纵切，但属计划/实现的登记差异。

**2026-09-23 14:10 会话登记（Sisyphus）——插件根因与修复（阻塞 M3 闭环）**：
- 背景：13:39:12 起 ESP32 桥接插件曾被禁用（备份 `tui.json.bak-before-esp32-disable-20260923-133912`）；13:46 本会话曾恢复 `tui.json` 注册（禁用态备份 `tui.json.bak-esp32-disabled-20260923-134645`），当时未知下述根因。
- **根因（二进制级）**：插件首版实现**未转发宿主 `session_prompt` props**（`session_id`/`on_submit`/`visible`/`disabled`/`ref`），使会话内输入框退化为"首页新建会话"框——每次回车新建会话、变空会话、上下文丢失（11:18 起重启风暴）。证据：`evidence/M3/2026-09-23-插件会话重置根因与修复.md`。
- **修复（方案 B）**：新增 `plugin/src/plugin/slotPrompt.ts`（`buildSessionPromptSlot` 转发全部宿主 props + ref 捕获）、`plugin/src/index.ts` 接入、`plugin/test/slotPrompt.test.ts` 8 用例。验证：`bun test` **69/69**、`tsc --noEmit` 通过、隔离 HOME 实例 `/v1/health alive:true` 且 0 ERROR/WARN（本轮实测）。
- **跨文档修订**：`plugin/spike/FINDINGS.md` §1.6/§3 更正（首版"仅传 ref"配方被证伪）+ §5 新增。
  - **⚠️ 重启前置（未完成）**：重启 opencode 后**先验证**"连续发两条消息停留同一会话、不跳空会话"，验证通过后再执行 `tools/M3闭环执行指引-20260923.md` 的 M3 闭环步骤。

**2026-09-23 14:21 会话登记（Sisyphus）——推送与 SHA256SUMS（用户授权"授权，可以继续"）**：
- **推送已执行**：`b27d9d6..7e7142d main -> main`，`origin/main` 已与本地同步。含 `7e7142d` 插件修复、`E2E-01p` 脚本、M3 纵切证据与文档。推送前敏感扫描：无 MAC/密钥；远端仓库 PRIVATE。
- **`flash-artifacts/SHA256SUMS` 已重算**：`bootloader.bin`、`sticks3.bin` 哈希对齐实际产物（该目录在 `.gitignore` 中，不入库）。
- **⚠️ 插件修复的真实 TUI 验证未通过**：用户 14:16 重启后仍报告"会话反复重置"——方案 B 的隔离实例证据不足以覆盖真实 TUI 渲染；M3 闭环仍被此缺陷阻塞（另一会话正在排查，见下方"待用户决定"）。

**2026-09-23 15:3x 会话登记（Sisyphus）——二次修复验证与登记（接上条"另一会话正在排查"）**：
- **排查已出结论**：首版方案 B（`7e7142d`）**参数位置错误**——槽位 renderer 被上游以双参 `renderer(context, data)` 调用（第 1 参仅 `{theme}`），首版单参实现把 `{theme}` 当宿主 props，`session_id` 恒 undefined → 14:16 重启后必复发。
- **二次修复已在工作区**（另一会话 14:23 写入）：`buildSessionPromptSlot` 改双参 `(context, data)` 从第 2 参取宿主 props；`capture.ts` 槽位类型改 `(...args: unknown[])`；测试同步。详见 `evidence/M3/2026-09-23-插件会话重置根因与修复.md` §9。
- **二进制证据独立复核（本会话）**：从 opencode 1.18.32 二进制提取 `F=(D,M)=>{I=D.renderer(Z().context,$)}`、`replace` 模式 `D.map(S=>F(S))`、splitProps 剥离清单——与二次修复实现一致（证据同文档 §9.2）。
- **验证（本会话实测）**：`bun test` **70/70**、`tsc --noEmit` PASS、隔离 HOME 实例 `bridge.json` 写入 + `/v1/health` `alive:true` + 日志 0 ERROR/WARN；隔离实例已清理。
- **用户 15:33 重启的当前实例已加载二次修复代码**（`bridge.json` 15:33、tuiEpoch 更新）——**真实 TUI 验证可直接在当前会话进行**（连续发 ≥2 条消息，不跳会话即生效）。
- 二次修复**尚未提交**（本地 commit 见下方提交边界）；push 仍需另行授权。

**2026-09-23 16:5x 会话登记（Sisyphus）——M3 闭环收尾 + 提前结束(松键)增量**

- **M3 用户价值闭环证据已本地 commit**（`6158bbd`）：`evidence/M3/2026-09-23-162541-M3人工闭环确认.md` + 本文档更新。**仅本地，未 push**。
- **Q03 计时链路调查结论（双阻塞，已核实）**：
  - **P-A**：固件原**无松键停止**（`firmware/.../audio_session.c` 注释"按键触发为下一增量"）→ 5/15s 组无法录制。
  - **P-B**：BLE `END(0x03)` 无时间戳 → "**批准的**跨端计时方案"未冻结。
- **Q03 计时方案已裁决（用户 2026-09-23）：方案 B——串口日志外部观察**。起点锚点=设备 END 串口日志行（`device_log.py` 主机打戳）；终点=桥接 readback；须先做串口延迟标定并报告误差。**Q03 测量工具待建**（见"未竟项"）。
- **跨端契约冲突已裁决（用户 2026-09-23）→ M2 契约升 v1.1**：`BEGIN.frameCount` = **预告上限**；`END.totalFrames` = **权威实际帧数**（≤ 预告）；`> 预告`／已收 `frameSeq ≥ 声明`／缺片 三种例外显式失败。**线格式与黄金向量不变**。详见 `contracts/M2-BLE线协议冻结.md` 变更记录。
- **提前结束增量已落地（本轮实测）**：
  - **固件**：新增 `button.c/.h`（BtnA=GPIO11 低电平按下；按住/松开，去抖≈20ms 为**未批准默认值**）；`audio_session` 增 `request_start/request_stop`，松开时以**实际帧数**发 END（无有效帧则发 DEVICE_CANCEL）；`main.c` 接线；`CMakeLists.txt` 登记。`source tools/idf-env.sh && idf.py build` **通过**。
  - **主机**：`s3p_reasm.c` 的 `!=` 改 `>` + 越界/缺片检查；`s3p_reasm_test.c` 新增 3 条提前结束用例；新增 `build_reasm.cmd`。**MSVC 实测 816 checks / 0 failures**（原 566）。
  - **契约/用例**：`contracts/M2-…`（v1.1 + 变更记录）、测试矩阵 **BLE-12** 同步。
- **固件产物（本轮构建，已刷机）**：`sticks3.bin` SHA-256=`3835fb92753c9168601b9378b5657234c4d2cecdfddcea995ed5682bb59106f8`；上一版镜像存 `flash-artifacts/prev/` 可回滚。
- **✅ 松键 HIL 已通过（2026-09-23 17:0x，真实设备）**：见 `evidence/M3/2026-09-23-松键提前结束HIL.md`。
  - 提前结束：`BEGIN.frameCount=500` → `END.totalFrames=116`（≈6.96s），116/116 帧收齐，独立 FNV-1a 摘要 MATCH。
  - 30s 硬停：`END.totalFrames=500`、`samples=480000`（30.00s），摘要 MATCH；未回归。
  - 串口锚点（Q03 方案 B）：`BtnA 按下/松开` 均带主机时间戳 → 锚点机制可用。
  - **HIL 命中真实缺陷并修复**：`pdMS_TO_TICKS(5)=0`（HZ=100）→ `vTaskDelay(0)` 忙等 → 任务看门狗循环；改 `BUTTON_POLL_MS=10` + `_Static_assert` 护栏，重刷后无 WDT。**编译期不可见，须 HIL**（教训已登记）。
- **明确未验证**：①**Q03 时延未测**（"松键→TUI 填入 ≤5s"门槛，测量工具未建）；②`READY(0x10)` 仍为等价启动触发（移交语义待确认）；③去抖值(≈30ms)/长按阈值未批准；④侧键 BtnB(GPIO12) 未实现（归 M4）；⑤出厂备份缺失（设备早已非出厂态，未额外损失）。

**2026-09-23 17:2x 会话登记（Sisyphus）——Q03 工具链落地**

- **契约**：`contracts/M3-Q03时延测量契约.md` **已批准 v1.0**（方案B；起点=设备串口会话结束行／主机时钟，终点=BR-04 `applied`／主机时钟；**无跨端钟换算**；Δ=BLE vs 串口 END 到达差；灰区不自动放行）。§6 已批准：**60 次真实云调用授权（预算上限 60 次）**、锚点行选择、并口采样、Δ 标定、30s 起点、落点。
- **判定核心**：`tools/q03_latency.py` + `tools/test_q03_latency.py`（**28 用例**，`python3 -m unittest` 全绿）：nearest-rank P50/P95、每组 ≥19/20、失败保留分母、锚点/Δ/记录构造。
- **实时编排**：`tools/q03_run.py`（单进程：串口线程 + BLE 采集{不发 READY} + 解码 + ASR/BR-04 计时；逐次写 run record JSONL）+ `tools/q03_run.ps1`（点源 `bridge_env.ps1`）。
- **连接冒烟（本轮实测，无按键、无云调用）**：bridge 装载 OK（`canAppend=True`）→ 同进程串口+BLE 连接订阅 OK → BEGIN 超时**优雅失败** → 写 `ok=false` 记录（**保留分母**）、退出 1。冒烟命中并修复一真实缺陷：Windows Python stdout cp1252 致中文 `UnicodeEncodeError` 崩溃 → 加 `reconfigure(encoding="utf-8", errors="replace")`。
- **战役编排**：`tools/q03_campaign.ps1`（Key 只输一次 → 自动跑 5/15/30s 各 20 次 → 末尾自动出 Q03 报告）。`.ps1` 补 **UTF-8 BOM**（项目惯例；无 BOM 时 PS 5.1 按 ANSI 读，中文会吞掉 `}` 致解析失败——本轮实测命中）。
- **依赖与解码步核查（本轮实测）**：编排所用 Windows Python 依赖齐备（`av 18.1.0`、`pyserial 3.5`、`bleak` 可导入）；用既有捕获 `captures/slice/rec_1.s3pcap` 跑编排的解码命令 → `500 frames → 1440000 samples (48kHz) = 30.00s`，与既有证据一致。
- **未竟**：Q03 **真实试点**（需用户按键 + 1 次云调用）未跑；通过后再跑 60 次正式测量（`q03_campaign.ps1`）。

**2026-09-23 17:3x 会话登记（Sisyphus）——首次 Q03 战役结果核查 + 重跑准备**

- **战役确实跑了**：`captures/q03/records.jsonl` 共 46 条（5s×21、15s×20、30s×5）→ **全部不可用于验收**：`latency_s` 全空、`ok` 全 false。
- **采集链路零缺陷**：45 段音频（**44 段判为真实语音**，RMS −31~−37 dBFS）、46/46 FNV 摘要 MATCH、br04 applied 42/46。→ "采样成功"属实，但"时延测得"不成立。
- **致命缺陷（已修，`19c16c3`）**：`voice_append.exe` 的 `[3] 桥接 HTTP …` 行含中文，子进程 stdout 被 Python 以 **cp1252** 解码成乱码 → `startswith("[3] 桥接 HTTP")` 永不命中 → `t1=None`。修复：子进程 `encoding="utf-8"` + t1 判定改用 ASCII 前缀 `[3]` + `classify_append_line()` 回归测试（`tools/test_q03_run.py`，41→47 用例）。
- **另一发现**：实际录音时长与组名不符（"15s"组实际 **9.78~13.38s**；"30s"组 5 次中 3 次为 0.24/0.42/5.22s、**无一达 30s**）→ 用户裁定 **"严格：录音须真达到对应时长"**；分组改 **5/10/30**。
- **中断原因**：campaign 停在 30s#3（≈17:28），与 `bridge.json` 17:28 更新、**tuiEpoch 旋转**同时发生 → OpenCode 重启/退出致旧 URL 失效（已加"每次运行前重载桥接 + 装载失败即停并提示续跑"）。
- **重跑前置就绪**：`tools/q03_campaign.ps1`（Key 只输一次、逐次重载桥接、超窗/失败自动重试 ≤3 次、末尾自动出报告）；时长闸门（窗值**已确认**：5s[5.0,7.0]、10s[10.0,12.0]、30s=30.00）+ 串口证据落盘已实现。组次：**5s / 10s / 30s 各 5 次**（用户两次修订，均自 20 缩减）。
- **旧数据已归档**：`captures/q03/run1-20260923/`（45 段音频 + 46 条记录，保留以备 Q04 语料复用）；`captures/q03/` 已清空供重跑。
- **已确认（2026-09-23 用户）**：各组门槛 `ceil(0.95×5)=5/5`（由已批准 19/20 推导）。**Q03 测量无任何待批准取值，可开跑。**
- **⚠ 边界登记（重要）**：用户缩减理由"之前跑过、可信度高"**不构成证据**——2026-09-23 那 46 次**未记录任何时延**（t1 缺陷），只能证明"端到端跑通"，不能证明"≤5s"。故本次为**缩减样本版**（n=5 零容忍），**不得按原 Q03（约 5% 失败率）口径宣称达标**；如需该口径须补足样本或另行批准。详见 `contracts/M3-Q03时延测量契约.md` §8 边界登记。
- **未竟**：Q03 正式测量仍未开始（本次 46 次记录作废，须重跑）。

**2026-09-23 17:5x 会话登记（Sisyphus）——✅ Q03 时延验收 PASS（缩减样本版）**

- **正式测量已跑并 PASS**：`tools/q03_campaign.ps1`（17:39–17:49），三组各 **5/5**，**最大时延 3.719s（全部 <5s）**；P50：5s 2.66 / 10s 2.64 / 30s 3.64。
- **证据**：`evidence/M3/2026-09-23-1750-Q03时延验收.md`（判定表 + 15 次逐项复核 + Δ + 口述内容 + 诚实边界）。
- **亮点**：时长闸门生效——5 次超窗（4.98/9.6/13.2/9.84/9.42/26.46s）**全部被拦下并自动重做**；30s 组 5 次均 `frames=500/samples=480000`（硬停）；15 次摘要全 MATCH、串口锚点齐全、wav 时长与记录一致。
- **锁定项**：各组 5 次、门槛 5/5、时长窗 5s[5,7]/10s[10,12]/30s=30.00。
- **⚠ 效力边界（不得省略）**：**缩减样本版**——n=5 零容忍**不能**体现原 Q03"约 5% 失败率"的统计效力，**不得按原口径宣称达标**；冷启动/重连/主动取消未测；**只验时延不验准确率**（Q04 另测，首轮 45 段语音可作候选语料）。
- **M3 出口**：时延项（Q03）**已由真实数据关闭**；M3 剩余为 Q04 准确率 + 30 条语料冻结。

### 2026-09-23 全天收工交接（18:0x）

**今日提交 36 次**（08:18→17:51）；本轮推送 `origin/main`（19 次）。

**今日主线**
1. **采音时基修复（重大）**：`I2S_SLOT_MODE_MONO`→`STEREO`；实测 **16013/s**（原 32029/s＝2 倍拉伸）。证据 `evidence/M3/2026-09-23-采音时基诊断.md`。
2. **插件会话重置根因 + 二次修复**：TUI 插件未转发宿主 `session_prompt` props → 会话反复重置；二次修复为槽位 renderer 双参签名。**真实 TUI 验证通过**。
3. **M3 用户价值闭环**：E2E-01p（草稿落点 + **人工手动发送** + 未自动提交）**PASS**；一键脚本 `tools/m3_e2e_01p.ps1`。证据 `evidence/M3/2026-09-23-162541-M3人工闭环确认.md`。
4. **松键提前结束增量**：契约冲突裁决 → **M2 v1.1**（`BEGIN.frameCount`=预告上限、`END.totalFrames`=权威实际帧数）；固件按键模块（BtnA=GPIO11）；**真实设备 HIL 三态 PASS**（提前结束 116 帧 / 30s 硬停 500 帧 / 串口锚点）。证据 `evidence/M3/2026-09-23-松键提前结束HIL.md`。
5. **Q03 时延测量工具链 + 正式测量 PASS**：契约 `contracts/M3-Q03时延测量契约.md`（方案B 串口外部观察）；判定核心 + 实时编排 + 战役脚本；三组各 **5/5**、最大 **3.719s**。证据 `evidence/M3/2026-09-23-1750-Q03时延验收.md`。

**今日命中的真实缺陷（全部修复 + 回归测试）**

| 缺陷 | 后果 | 修复 |
| --- | --- | --- |
| `pdMS_TO_TICKS(5)=0`（HZ=100） | 按键任务忙等 → 任务看门狗循环 | 10ms + `_Static_assert` 护栏 |
| Windows Python cp1252 stdout | Q03 `t1` 永不命中 → **46 次测量全废** | UTF-8 解码 + ASCII 前缀 `[3]` + 回归测试 |
| `write` 产出的 `.ps1` 无 BOM | PS 5.1 解析失败（中文吞 `}`） | 补 UTF-8 BOM |
| 判定核心读 JSONL / 单条记录 | 战役报告崩溃 / 试点误判 | `load_records()` 兼容 4 种格式 |
| 录音时长与组名不符（无闸门） | 组标签失真、分母污染 | 时长合格窗 + 超窗自动重做 |

**今日关闭**：M1 会话重置缺陷（二次修复+真实 TUI 验证）；M3 故事4 用户价值闭环 + **物理按键路径**；**M3 时延项 Q03（缩减样本版 PASS）**。

**未竟 / 下一步**
- **Q04 准确率**（M3 最后一项）：需冻结 30 条语料 + 人工参考文本；首轮 **45 段真实语音可作候选语料**（`captures/q03/run1-20260923/`）。
- M4：状态屏三页 / 托盘 / USB-电池策略；Q06 长按阈值与去抖仍待冻结。
- 交付前：移除或登记诊断脚手架；重算 `flash-artifacts/SHA256SUMS`（固件新 SHA `3835fb92…`）。
- 待决：`READY(0x10)` 仍为等价启动入口（真机 UX 以按键为主，移交语义待确认）。

**敏感信息审查（推送前）**
- 32 个待推送文件扫描：**无密钥/token/真实 MAC**；音频（wav/s3pcap）与串口日志均未入库（`captures/` 已 gitignore）。
- **发现 1 处历史 MAC**：`host/src/ble_client.cpp:129` 注释含真实设备 MAC（由 `7cbf884` 引入，**已在 origin/main 历史中**）。本轮**掩码为 `<MAC>`** 并登记；**未做历史改写 / force-push**（破坏性，需另行明确授权）。远端仓库为 **PRIVATE**。

若装载器报"找不到 bridge.json"：插件未加载成功，检查 OpenCode 启动输出是否有插件报错。
> 注意：桥接在 WSL 内监听 127.0.0.1，Windows 侧经 WSL localhost 转发访问；若不通，改用 WSL IP（需同步改 `bridge_env.ps1` 的 URL 策略）。

### 交付前必做（诊断脚手架登记）
- `firmware/sticks3/main/main.c`：启动 `zcr` 证据、`audio_capture_log_clocks()`、`audio_capture_probe_rate()`、心跳周期时钟打印
- `firmware/sticks3/main/audio_session.c`：END 日志 `zcr`
- `ble_gatt.c`：测试钩子 `0x7F`（用户已裁定保留，登记于 `contracts/M2` 附节）
- 以上**修复验证完成后应移除或显式登记**，否则"交付固件=已验证固件"的对应关系需附条件说明

### 纵切末跳登记（2026-09-23，本次）
- **M3 故事 4 技术链路端到端 PASS**：`voice_slice.ps1` 全流程 → 桥接 `status:"applied"`（草稿填入、未提交）。证据：`evidence/M3/2026-09-23-M3故事4纵切闭环.md`。
  - 独立校验：设备 END.summary `0xa1ffa3c3` == 主机 FNV-1a；容器分解 12+500×6+77509=80521；WAV 44+1440000×2=2880044；时长 500×60ms=30.00s。
  - 桥接实例为**重启后新实例**（端口 36997 → 45093）。
- **闭环仍待用户确认**：是否已在输入框目视确认并手动发送（交付节点 M3 的闭环判据）。
- **闭环确认路径已登记（未执行）**：子用例 **E2E-01p**（E2E-01 的人工确认子步，H 类）已登记入测试矩阵，配套脚本 `tools/m3_manual_send_verify.ps1`；**当前 NOT_RUN**，需用户目视确认并手动发送后由该脚本归档 `evidence/M3/<时间戳>-M3人工闭环确认.md`。**登记不等于通过**。
- 未按计划/未覆盖：`voice_slice.ps1` 仍 `Read-Host` 手输 Key（未走 C11）；本次无托盘、非物理松键、设备状态屏 UNKNOWN（详见该证据 §7）。

### 未竟项
- **M3 故事 4**：✅ **闭环已成立（2026-09-23 16:25，E2E-01p PASS）**；✅ **物理按键路径已 HIL 通过（2026-09-23 17:0x，提前结束+30s 硬停，见 `evidence/M3/2026-09-23-松键提前结束HIL.md`）**
- M3 验收：30 条语料冻结 + 时延 60 次（Q03）+ 准确率 30 条（Q04）
  - Q03：计时方案已裁决并冻结（方案B 串口外部观察，`contracts/M3-Q03时延测量契约.md` 已批准）；**工具链已建**（判定核心 28 单测 + 实时编排，连接冒烟通过）；**真实测量（试点/60次）仍未跑**，阻塞于用户按键与云调用
  - Q04：语料与评分规则待冻结（用户提供/批准）
- **登记差异**：`voice_slice.ps1` 的 ASR Key 接入 C11 凭据管理器（故事 4 计划写的是"凭据管理器供给"）
- M4：状态屏三页 UI、托盘、USB/电池策略（**C11 凭据管理器核心已落地**：`asr_cred_tool`，TDD 21/21，见 `evidence/M3/2026-09-22-M3故事2-3完成记录.md`；托盘设置流归 M4）
- 交付前：移除或显式登记诊断脚手架（见上节"交付前必做"）；重算 `flash-artifacts/SHA256SUMS`（固件已更新，新 SHA 见 16:5x 登记）

### 待用户决定
- **M3 用户价值闭环确认**：**✅ 已成立（2026-09-23 16:25，E2E-01p PASS）**——见上方 16:2x 会话登记。原"待运行"登记作废，由真实证据取代。
- **二次修复 push 授权**：修复已本地 commit（`9f50570`、`2231cbb`、`d98d6be`）并全级验证通过；是否 push 由用户授权。
- **`flash-artifacts/SHA256SUMS` 重算**：**已于 2026-09-23 14:21 重算完成**（见上方登记）。原登记"仍待授权"，现履行。
- M2 出口审查（工程判据已全绿，仅剩"用户看过演示与已知风险"一项，见 `evidence/M2/2026-09-22-M2演示执行记录.md` §9）

> **本轮提交边界（2026-09-23）**：仅本地 commit（`tools/bridge_env.ps1` 字段对齐、`E2E-01p` 用例与脚本、M3 纵切证据与本文档更新）；**不含 push / GitHub 发布**（推送不在本轮授权内）。设备 MAC 已按项目"不入库 MAC"惯例掩码后入库。
> **2026-09-23 14:21 补**：用户授权"授权，可以继续"后**已推送** `b27d9d6..7e7142d main -> main`（私有仓库）。GitHub 发布（Release / 公开化）仍不在授权内。

### 2026-09-24 会话登记（Sisyphus）——Q04 评分器 + 语料冻结工具（M3 最后一项前置件）

**进入阶段**：M3 收尾（唯一剩余项 = Q04 准确率 + 语料冻结）。本轮**只做工程前置件与提案**，**不判定 Q04 达标**。
> ⚠ **本块为改写前快照**：其中"30 条"为工具初建时的值；用户随后裁定**改为 15 条/≥14**（见下方"2026-09-24 Q04 裁定"）。工具与文档已按 15/14 修订。

**本轮完成（全部本会话实测）**：
1. **Q04 评分核心** `tools/q04_grader.py`（303 行）：确定性实现已批准判据——≤2 处局部修正、整句重写 FAIL、
   关键语义槽位（否定/对象/数字/路径）直接不合格、NFKC 归一化保护 `1.5`≠`15`/`a/b`≠`ab`、标点空格单列、固定分母 30、`≥27` 达标。
2. **黄金测试** `tools/test_q04_grader.py`（316 行，**41 用例**）：13 黄金例 + 归一化红线 + 整卷门槛（27 边界/26 失败/复核不计 PASS）+ CLI + **cp1252 回归护栏**。
3. **语料冻结/防篡改工具** `tools/q04_corpus.py`（286 行，**49 用例**）：运行前冻结 30 条（拒绝≠30、拒绝缺字段、拒绝覆盖），
   `sha256(canonical_json)` 内容哈希 → 改参考文本/改 wav/增删重排**必被检出**（"不得看模型输出后改参考"的机械执行）。
4. **Q04 契约草案** `contracts/M3-Q04准确率验收契约.md`（**未批准**）：已批准判据 / 提案实现 / 待冻结 **三态分列**；
   `contracts/M3-ASR验收冻结清单.md` §2 与 `contracts/README.md` 冻结时间表已同步。

**本轮证据（命令与结果）**：
- `python3 -m unittest discover -s tools -p 'test_*.py'` → **143 tests OK**（基线 53 + 评分器 41 + 语料 49，**无回归**）
- `plugin` `bun test` → **70/70**（基线，未改）
- 篡改演示（CLI）：freeze 30 条=0 → verify 未改=0 → **改参考文本后 verify=1（哈希不一致）** → 29 条 freeze=1（拒绝）→ 覆盖已有=1（拒绝）
- 评分器 CLI：30 条（27 一致 + 否定反转/错误路径/整句重写）→ **27/30 边界 PASS**，三类关键错误均判 FAIL

**⚠ 效力边界（不得省略）**：工具已建、单测全绿 **≠ Q04 达标**。语料未冻结、细则未批准、云调用未授权前，
Q04 判据 **BLOCKED**（详见契约 §8）。本轮**未**产生任何真实 ASR 准确率数据。

**待用户批准（不可逆，不得代批，详见契约 §7）**：
1. **30 条语料内容 + 人工参考文本**的来源与定稿（工具要求：id/wav+sha256/说话人授权/语言/技术词/时长/噪声/距离/参考文本/关键槽位/等价形式/音频环境）
2. **关键语义槽位判定规则**最终口径（契约 §3.3 提案的严格点：是否"任何数字差异即失败"）
3. **标点/空格归一化解读**（契约 §3.4：是否接受"标点/空格差异不影响 status"）
4. **真实云调用授权与费用上限**（Q03 的 60 次授权**不含** Q04）
5. 是否接受契约 §3 的**提案阈值**为验收基线

> **逐条审阅版已备**（2026-09-24）：`contracts/M3-Q04审批清单-20260924.md`（A 语料 / B 判定规则 / C 云授权 / D 缺口，每条含选项+推荐+**不覆盖边界**+回填栏）；
> `tools/Q04语料采集与冻结清单-模板.md`（30 条台账模板，内容留空**不代写**）。另本会话发现两处缺口待决：`acceptable_equivalents` 评分器未消费、冻结语料 `key_slots` 与评分器 `slots` 未接线（见审批清单 Q04D-1/D-2）。

### 2026-09-24 Q04 裁定（用户逐条审阅）——阈值修订 + 规则批准

**上条"待用户批准 5 项"已裁定；并修订已批准门槛**：

- **语料 30 → 15 条**、**门槛 ≥27/30 → ≥14/15**（均 90%）、**云调用上限 30 → 15 次**（用户 2026-09-24 裁定）。
- 其余 **22 项照 ★ 批准**：判定规则从严（数字/否定/路径任一差异即 FAIL）、标点空格差异不影响 status、自动兜底从严生效、NEEDS_REVIEW 由用户逐条裁定、等价形式**不自动放行**、`key_slots↔slots` **建适配器**。
- **效力边界（不得省略）**：n=15 统计效力弱于 30，**不得按原 30 条口径宣称达标**（沿用 Q03 缩减样本先例）；云上限 15 = 语料 15 → **无重试余量**，失败句不得重试、记不合格。
- **已修订文档**：[Q04 契约 v0.2](./contracts/M3-Q04准确率验收契约.md)（主基线）、[审批清单](./contracts/M3-Q04审批清单-20260924.md)（转已裁定）、[ASR 冻结清单 §1/§2](./contracts/M3-ASR验收冻结清单.md)、[测试矩阵 ASR-10/§8](./StickS3_OpenCode_测试用例与追踪矩阵.md)、[交付节点](./StickS3_OpenCode_交付节点与完成定义.md)、[方法论 Q04](./StickS3_OpenCode_方法论审查与决策.md)、[契约补充 §126](./StickS3_OpenCode_契约与演进设计补充.md)、[设计文档 §245](./StickS3_OpenCode_语音状态屏_设计文档.md)（历史条目**加取代说明、不改日期**）、[采集模板](./tools/Q04语料采集与冻结清单-模板.md)（15 行）、`contracts/README.md`。
- **工具已同步**：`q04_grader.py`/`q04_corpus.py` 的 `REQUIRED_SAMPLES`/`MIN_PASS` 改为 15/14，测试同步，全量回归见下方"验证"。
- **未竟**：①**15 条语料内容**：**已由执行者按用户指示（2026-09-24）生成**，落 `tools/Q04语料草稿-20260924.json`（含参考文本+4 槽位；网络取材）；**修订 Q04A-7=G1**（原"用户逐条定稿"→"执行者生成、用户采用"）。待**用户采用 → 对设备录音 → 冻结 → 跑 ASR**；~~②Q04D-2 适配器未实现~~ → **②✅ Q04D-2 适配器已建**（`tools/q04_adapter.py`，24 用例，端到端实测 14/15 PASS + 错误对象判 FAIL + 篡改拒绝；2026-09-24），§3.3 声明槽位（含操作对象）**现生效**。
- **采音就绪（2026-09-24）**：`tools/q04_capture.py`（**23 用例**，全量 tools 190 OK）——驱动 15 句设备采音：**只采音、绝不跑 ASR**；BLE 一律 `subprocess` 复用已验证的 `button_hil.py`（不重写）；每句成功后增量写 `tools/Q04语料草稿-采集完成.json`。运行：`python tools/q04_capture.py`（先 `--list` 看句子）。
  - **ASR 取文本路径已定**：`host/src/asr_smoke.exe <wav>`（打印 `text: …`，**无 draft 副作用**；需 `STICKS3_ASR_KEY`）——冻结后用它取 15 条假设文本。
- **Q03 陈旧值已对齐（本轮发现并修订）**：多处前向文档仍写 Q03 旧值"5/15/30 各 20 次/≥19"，与 [Q03 契约 v1.1](./contracts/M3-Q03时延测量契约.md)（**5/10/30 各 5 次/门槛 5/5**）不一致；已修订：ASR 冻结清单 §1/**§2**、交付节点、功能模块 HLD、契约补充、延伸思考、方法论 Q03 行、**Q03 契约 §1 自身**（§1 原与 §9 自相矛盾）。**历史/证据文件与已标注条目不改**（`evidence/`、Q03 契约 §6/§7/§8 变更记录、设计文档历史表述、可行性核查历史表述）。

**其它登记**：
- **环境变更**：本会话 Python 实测 **3.14.4**（索引旧记 3.12.10）——Q03/ASR 工具链复现时须注意版本漂移（未影响本轮工具，均纯标准库）。
- **工作区新出现的非本任务文件**：`tools/project_token_stats.py`、`tools/token_prices.json`、`tools/project_token_config.json`、
  `evidence/token-stats/2026-09-24-*` —— 经查为 **OMO/opencode 项目 token 统计机制**自动生成（读 OpenCode SQLite，含 09-24 手写 override），
  **非本会话 Q04 交付物**；是否入库/加 `.gitignore` 待用户决定，本轮**未纳入交付**。
- **本轮提交边界**：所有新增文件（`tools/q04_*.py`、`tools/test_q04_*.py`、`contracts/M3-Q04*.md`、本索引更新）**仅在工作区，未 commit、未 push**。
- 过程登记：评分器首次派发（12 分钟）**只完成设计未落盘任何文件**，经续接会话强制落盘后补齐（tools/q04_grader.py 时间戳 09:24）；语料工具首次即落盘。**"代理报告完成"不等于"文件存在"——本轮以磁盘核验纠正。**

### 2026-09-25 会话登记（Sisyphus）——新机 WSL 开发链路重建（环境就绪，非功能交付）

**背景**：本项目开发工作迁移到**另一台电脑**（TIMI TM1707 / 8GB / WSL Ubuntu 26.04 / 用户 `home` / 路径 `C:\Users\home\esp32-s3-m5`）。旧机（HP EliteBook / 32GB / 用户 `shiwenc` / `C:\Temp\ESP32-S3M5`）的"已验证环境"不随仓库迁移，本机 WSL 链路原为空。用户指示：**修复 WSL 链路与依赖包，不使用 Windows 原生 ESP-IDF**。

**完成（全部本机实测）**：
1. **ESP-IDF v5.5.4 源码**：主仓库取自 Gitee 镜像（GitHub 不可达）；子模块源文件按 §D1 补齐；工具链经 `IDF_GITHUB_ASSETS=dl.espressif.com/github_assets` 下载。
2. **工具链**：xtensa-esp-elf 14.2.0_20260121；Python venv `idf5.5_py3.14_env`（3.14.4）；ninja 1.13.2（pip）；openocd v0.12.0-esp32-20251215；libusb 1.0.29 用户态解包。
3. **固件构建通过**：`toolchain-check` → 197,152 B（sha `cb2fe121…`）；**真实工程 `firmware/sticks3` → `sticks3.bin` 734,000 B（sha `8c1377d9…`，分区 30% 空闲）**，`ninja -j4` 限流。
4. **插件**：bun 1.4.2；`npm install` + `bun test` **70/70** + `tsc --noEmit` exit 0。
5. **WSL Python 工具链**：`tools/` 单测 **190 OK**。
6. **行尾归一**：新增 `.gitattributes`，160 项 CRLF 假改动清零。
7. **`tools/idf-env.sh` 适配**：自动探测 venv、注入 `IDF_SKIP_CHECK_SUBMODULES=1`。

**关键网络约束**：GitHub 不可达（000）；使用 Gitee + `IDF_GITHUB_ASSETS` 镜像 + 本机官方副本补齐子模块。

**详细报告**：[开发准备就绪核查-2026-09-25-新机WSL重建](./开发准备就绪核查-2026-09-25-新机WSL重建.md)

**工作区改动（未提交）**：`.gitattributes`（新增）、`tools/idf-env.sh`、`firmware/{sticks3,toolchain-check}/sdkconfig`（版本再生）。

**待决（不得代批）**：Q1 ESP-IDF 版本（文档基线 5.5.5 vs 本机 5.5.4）；Q2 是否提交上述改动；Q4 Windows 侧 Host/Q03-Q04 脚本路径迁移（硬编码 `C:\Temp\ESP32-S3M5`）；Q5 `ble-probe.py` 断言陈旧。

**Q3 设备链路 —— ✅ 已完成（2026-09-25）**：
- 真实在线设备**仅 1 台**：串口 **COM5**，esptool 识别为 ESP32-S3（8MB Flash + 8MB PSRAM ＝ M5StickS3），USB-Serial/JTAG，MAC `<MAC>`；COM7/COM10/COM11 为幽灵记录。
- 串口：项目固件运行中，心跳 `sticks3: hb: uptime=25s heap=8489715 audio_ready=1 ble_conn=0`。
- BLE：广播名 `StickS3`，地址 `<MAC>`；服务/三特征与 M2 契约一致；配对加密、MTU 247。
- 会话：READY → BEGIN(500/16000/60) → 500 帧 → END(500 帧/480000 样本)，**FNV-1a 独立复算 == 设备 summary `0x91d5d552`** → **PASS 15/15**。设备功能正常。
- **Q5（已修复）**：`tools/ble-probe.py` 断言陈旧（50 帧/3s/等待 25s），与现行 M2 v1.1（500 帧/30s）不符。**已按冻结契约更新并真实设备复跑 PASS（21/0，500 帧、FNV 一致）**。
- **新操作约束**：多次连接/会话后设备侧 BLE 会拒绝订阅（`start_notify` 前断连）；**会话前先复位设备**（`python -m esptool --chip esp32s3 -p COM5 read_mac` 即硬复位）可稳定恢复。

### 2026-09-25 会话登记（续）——Q04 采样暂存 + M4 启动

- **Q04 采样任务暂存**（用户 2026-09-25 指示）：当前不具备收音条件，暂停 15 条语料采音；**M3 出口维持未闭**（Q03 ✅，Q04 ⏳ 暂存），非放弃。
- **M4 启动**：进入 **M4 状态屏与托盘** 实现。现状为 greenfield（插件无状态归约、固件仅 M2 单帧桩、host 无托盘）。
- **新增计划**：[M4 开发计划](./StickS3_OpenCode_M4开发计划.md)：7 个故事（S4.1—S4.7）+ DoR 表 + C10 待冻结项（中文字体 / 长按阈值 / 息屏细节 / LVGL-vs-自绘）。
- **首个故事 S4.1（M10 精简快照归约，插件，T 类）可开工**：依赖 BR-03 数据字典（已冻结），纯逻辑 TDD，不涉字体/硬件。
- **阻塞项**：S4.3/S4.6/S4.5 分别待冻结中文字体、长按阈值、息屏细节。
- **S4.1 完成（2026-09-25）**：`plugin/src/core/state/{types,reduce}.ts` + `plugin/test/state/reduce.test.ts`（15 用例，覆盖 ST-01—ST-12 归约语义）；`bun test` **85/85**、`tsc` exit 0。证据见 [M4 开发计划 §6](./StickS3_OpenCode_M4开发计划.md)。
- **Q4 host 工具链（2026-09-25 部分完成）**：复用 VS2019 BuildTools；修 7 个 host 构建脚本（vcvars 回退 + `%~dp0` + `/utf-8`）、7 个 tools 脚本 + 2 份文档（路径推导 + 本机 Python + WSL 用户名 `home`）。重建 **8 个 host .exe**，协议测试 **997 checks / 0**（`asr_transport` 需先起 mock-asr）。**`ble_client.exe` 阻塞**：需 VS2022 新 cppwinrt/SDK，本机仅 VS2019/SDK 10.0.19041 且非管理员 → 见 Q6。
- **S4.1b 完成（2026-09-25 续）**：`plugin/src/core/state/store.ts` + `plugin/src/plugin/stateRead.ts`；桥接 `/v1/state/stream` 接入快照提供者与心跳；`index.ts` 接线。已接线用量/模型、行工具/失败数/错误码、事件驱动刷新（14 类事件 + 500ms 节流 + 10s 心跳保活）。`bun test` **107/107**、`tsc` exit 0。未交付：真实 TUI 运行验证、BLE 下发、**ST-12 子任务 busy（TUI-API 无 children 数据源）**。
- **S4.2 契约提案（待批准）**：BLE `STATE(0x12)` 快照载荷字段布局见 [M4 开发计划 §7](./StickS3_OpenCode_M4开发计划.md)；**未冻结、无实现**。
- **Q4 补验（2026-09-25）**：host 自检**重新实测** 997 checks / 0（reasm 816 + adapter 35 + voice_txn 93 + credential 21 + transport 32）；**CMake host 路径**（README §3）经 Ninja + VS2019 验证，`toolchain_check.exe` 输出 `win32 link ok`（exit 0）；本机无 clang-cl → `ble_client` 确无 VS2022 替代。
- **Q6 已解决（2026-09-25，免管理员）**：`ble_client.exe` 经「NuGet 现代 cppwinrt 头 + `/std:c++17 /await`」构建成功并**端到端 PASS**（独立 FNV 一致）。附带修复：含中文的 `.cmd` 在本机 GBK 下崩坏，`build_ble_client.cmd`/`flash-sticks3.cmd` 改纯 ASCII。**M4 S4.7 托盘不再被 VS2022 阻塞**（偏离：C++17+/await，非 C++20）。
- **S4.7a 完成（2026-09-25）**：`host/src/tray.cpp`（Win32 托盘壳 + 消息循环 + 退出菜单）+ `build_tray.cmd`（纯 ASCII）；`tray.exe` BUILD_OK（117,760 B）、启动冒烟 RUNNING_OK。无 WinRT 依赖。
- **S4.7b 完成（2026-09-25）**：托盘读 `bridge.json` → WinHTTP `GET /v1/health` → 菜单连接态 + ASCII `status_code`；`tray.exe` BUILD_OK（158,208 B）。**全路径实测**：`NO_BRIDGE`/`DESCRIPTOR_BAD`/`HEALTHY`（本地 mock）/`UNREACHABLE`（关闭端口）。唯一未测：真插件桥接下的 HEALTHY（需注册插件+重启 OpenCode）。
- **S4.7c 完成（2026-09-25）**：托盘菜单「配置/删除 ASR Key」→ 复用 `asr_cred_tool.exe`（Key 控制台隐藏输入，不进命令行）；`tray.exe` BUILD_OK（161,792 B）、冒烟 RUNNING_OK。未测：真实菜单点击 spawn 与真机凭据写入。
- **分析性推进（2026-09-25）**：[M4 计划 §7](./StickS3_OpenCode_M4开发计划.md) 增 **MTU 可行性分析**（最坏 ~125 B < MTU247 单通知；MTU65 需分片/截断）；§9 增 **S4.7d（重试入口）故事卡**——**阻塞于 host 常驻服务与 IPC-02 冻结**（现 host 均一次性 exe，且回执不存正文）。
- **托盘可测性（2026-09-25）**：新增 `host/src/tray_parse.h`（纯解析）+ `tray_parse_test.cpp`（**7 用例**，`TRAY_PARSE_PASS`）；重构后 `tray.exe` BUILD_OK 且 HEALTHY 复验通过。
- **刷写产物就绪（2026-09-25）**：`flash-artifacts/` 用本机 ESP-IDF v5.5.4 产物填充（bootloader 20,832 B / partition-table 3,072 B / **sticks3 734,000 B sha `8c1377d9…`**）+ `SHA256SUMS`（`sha256sum -c` **全 OK**）；受 `.gitignore` 本地保留。USB 刷写流程（`flash-sticks3.cmd`/`USB刷写清单.md`）在本机就绪。
- **tools 脚本校验（2026-09-25）**：7 个被编辑的 `.ps1` 经 PowerShell 解析器全部 **OK**（含 `zhipu_asr_verify.ps1`）；`$PSScriptRoot` 用于 param 默认值的模式经最小运行时验证 **PSROOT_OK**。
- **S4.2 契约冻结 + 固件侧实现（2026-09-25）**：用户**批准** → [contracts/M4-STATE快照冻结.md](./contracts/M4-STATE快照冻结.md) v1.0。实现：`s3p_state.{h,c}`（编解码，主机测试 **27/0**）；`state_merge.{h,c}`（BR-03→STATE 合并，**19/0**）；固件 `ble_gatt.c` case 0x12 接入解码 + `ble_gatt_host_status()`；display 按真实 status 呈现（**修正 ERROR→IDLE 的 ST-04 缺陷**）；固件重建 **734,368 B**、`flash-artifacts` 更新（sha `d17a63a1…`）。**⏳ host GATT 写端待 host 形态；设备端到端待刷机授权**。
- **S4.2 设备端到端 PASS（2026-09-25，用户授权刷机后）**：新固件已刷入；`ble_client`（升级为 v1.0 STATE：`state_merge`+`s3p_state_encode`）→ 设备回显一致。**并命中/修复真实缺陷**：`chr_ctrl_access` 仅拷前 8 字节 → 越界读栈垃圾 → 富 TLV 快照（66B）被 ATT error 13 拒绝；修复为整包拷贝（≤`S3P_STATE_MAX_LEN`）+ 超长拒绝；重刷后设备回显 `host STATE status=0 flags=0x3e rev=1 tools=2`，`ble_client PASS`（sha `0d8e8de4…`）。先前 23B 的"PASS"实为 UB 巧合。**仍未交付**：生产 host（托盘）。
- **插件插入影响评估（2026-09-25，用户要求"无伤插入"）**：新增 [插件插入影响评估](./StickS3_OpenCode_插件插入影响评估.md)——9 项接触点风险清单 + 无伤插入程序（备份→隔离预演→最小插入→重启→2 消息验证→回滚）。**加固**：`refreshState` 包 try/catch，异常不再冒泡 OpenCode 事件总线/定时器（+回归测试；`bun test` **108/108**）。**未改动 `tui.json`**（保持未插入）。**隔离预演已执行**：独立 HOME/XDG 下插件加载无错、`bridge.json` 写出、`/v1/health alive:true`、`/v1/state/stream` 正常、日志 0 ERROR/WARN；**唯 #1（replace 槽位真实渲染）仍需真实会话验证**。

### 2026-09-25 收工登记（Sisyphus）——插件真实插入 + S4.1b 真机验证 + activeError 缺陷修复 + 旧机路径迁移

**背景**：新机 WSL 链路重建后，桥接插件首次在本机 OpenCode **真实注册并重启生效**；随后完成 S4.1b 真机验证、命中并修复一处仅真机才暴露的缺陷，并收尾旧机（shiwenc）路径迁移。

- **插件真实插入 + 决定性验证 PASS（2026-09-25）**：`~/.config/opencode/tui.json` 追加本机插件路径（保留 `oh-my-openagent`），重启后 `bridge.json` 写出、`/v1/health alive:true`、`tuiEpoch` 一致。**连续多条消息落在同一会话、无新建/跳空** → `session_prompt` replace 槽位修复在真实 TUI 下**通过**（判据同 2026-09-23）。
- **S4.1b 真实 TUI 验证 PASS（2026-09-25）**：`GET /v1/state/stream` 在真实插件下产出完整精简快照（identity/bind/status/tools/todos/model/usage/cost/freshness 全字段，`authoritativeReadOk:true`）。
- **命中并修复真实缺陷（T 类，选择性 TDD）**：`stateRead.deriveActiveError` 原**倒扫任意历史消息**取 error 名 → 历史一旦出错，会话空闲后 `deriveStatus` 会误判为 `error`（设备误显 ERROR）且 `errorCode` 长期陈旧。**修复**：仅取**最近一条 assistant 消息**的 error（其后成功即清除）。新增回归用例 2 条（先红后绿）；`bun test` **110/110**、`tsc --noEmit` exit 0。
- **旧机路径迁移收尾（代码/脚本/配置）**：`C:\Temp\ESP32-S3M5` / `/mnt/c/Temp/ESP32-S3M5` / `shiwenc` → 本机 `C:\Users\home\esp32-s3-m5` / WSL `home`。含 `tools/{make_lessons_ppt.py,q03_run.py,USB刷写清单.md}`、`plugin/spike/project/*.json`；`dayu` skill 的 `provenance/extraction-notes.md` 来源路径同步。历史/证据文件按纪律**保持原样**（不改写历史）。
- **推送前敏感审查**：全仓无 token/密钥；设备真实 MAC 在新增文档中按项目惯例**掩码为 `<MAC>`**（此前不在历史中）。
- **测试**：`plugin` `bun test` **110/110**、`tsc --noEmit` exit 0；`tools/` 单测 **190 OK**。

**未竟 / 下一步**：S4.2 生产 host（托盘订阅 `state/stream` + 真实快照合并 → BLE 下发，**待范围确认**）；S4.7d（待 host 常驻服务 + IPC-02 冻结）；M3 Q04 语料采音暂存；C10 待冻结项（中文字体 / 长按阈值 / 息屏细节 / LVGL）。

### 2026-09-25 工作登记（Sisyphus）——S4.2 生产 host「验证增量」PASS（状态源=真实快照）

**范围裁定（用户）**：今天做 **S4.2 验证增量**——`ble_client` 状态源由 binding 改为 `/v1/state/stream` **真实快照**，不锁定常驻/托盘进程形态；**STATE 载荷不加 `voicePhase`**（维持 v1.0）。

- **端到端 PASS（2026-09-25 实测）**：重建 `ble_client.exe`（`BUILD_OK`）→ 复位设备（COM5）→ `bridge_env.ps1` 装载 → 运行。关键证据：`[PASS] write STATE | src=snapshot status=2 flags=0x39 rev=1 len=71`（**src=snapshot** ＝真实快照路径，非 binding 回退）；设备串口回显 `ble_gatt: host STATE status=2 flags=0x39 rev=1 tools=0`（与下发一致）；会话 500 帧、`reasm COMPLETE`、独立 FNV-1a 一致、`ble_client PASS`（exit 0）。
- **文档更正**：M4 §7 原述"现 `ble_client` 状态源为桥接 binding"**已过时**——代码（`read_bridge_snapshot`）早已实现 snapshot 源，本次**实测证实**并回写该行。
- **仍未交付**：**常驻/托盘形态的生产 host**（持续订阅 + 重连 + 长时会话稳定性）；S4.7d（待 host 常驻服务 + IPC-02）。
- **证据**：[evidence/M4/2026-09-25-S4.2-snapshot源端到端.md](./evidence/M4/2026-09-25-S4.2-snapshot源端到端.md)。

### 2026-09-25 工作登记（Sisyphus）——S4.2 生产 host 交付（常驻，SSE→BLE 持续 STATE）

**范围**：交付 **S4.2 生产 host**（独立常驻 `state_host.exe`，即 M4 §8/§9 所指 host 常驻服务）；`voicePhase` 维持 v1.0 不加。

- **新增**：`host/src/state_host.cpp`（读 `bridge.json` → 连 BLE CTRL 特征 → 订阅 `/v1/state/stream` → **按 `snapshotRevision` 去重** → `state_merge`+`s3p_state_encode` → 持续写 `STATE(0x12)`；桥接/BLE 断开**自动重连**；**不写 READY、不开录音会话**）+ `build_state_host.cmd`；SSE 纯解析件 `sse_parse.h`（**7 用例 TDD，先 RED 后 GREEN**）；共享映射 `bridge_snapshot.h`（`ble_client` 已收敛复用，去重复实现）。
- **命中并修复真实缺陷（T 类）**：①`state_merge.copy_text` 按字节截断可能切断中文字符 → 改 **UTF-8 码点安全**（+回归；`state_merge_test` **21/0**）；②`ble_client` 原 `narrow()` 把非 ASCII 降级为 `'?'`（中文标题变问号）→ 共享映射改 UTF-8。
- **HIL 端到端 PASS（2026-09-25）**：`state_host` 60s 内**连续 7 次**下发（`rev 804→817`，`status=2 flags=0x39 len=71`），设备串口**逐条回显一致**；`ble_client` 重构后复跑**无回归**（`src=snapshot … len=71`）。
- **效力边界**：观察到 **SSE 约每 ~20s 自动重订阅**（host 能恢复，根因未定位）；桥接仅在 **10s 心跳**重发快照 → 设备更新延迟 **≤~10s**（更低需插件改事件即时推送）；未做 Windows 服务/托盘包装与长时稳定性。
- **证据**：[evidence/M4/2026-09-25-S4.2-生产host.md](./evidence/M4/2026-09-25-S4.2-生产host.md)。

### 2026-09-25 工作登记（Sisyphus）——桥接 SSE 长连修复 + 事件即时推送（#3/#4）

**背景**：生产 host 运行中观察到 SSE 约每 ~20s 被重订阅。按调试方法论实测定位。

- **根因（已定位）**：`Bun.serve` **默认 `idleTimeout=10s`** 会关闭空闲连接，**安静的 SSE 也算空闲**；插件桥接 `Bun.serve({...})` 未设该值，而心跳恰为 **10s** → 必竞态。实测 WSL 侧直连 curl `time=16.0/20.0s exit=18`（abrupt close），Windows 侧同 —— **两侧都被服务端切断**（非 Windows↔WSL 转发）。依据：Bun 官方文档 `idleTimeout`（默认 10s，SSE 需 `server.timeout(req, 0)`）。
- **修复 #4**：`plugin/src/bridge/server.ts` SSE 路由加 `server.timeout(req, 0)`（不改冻结的 `core/types.ts`，按 M4 先例在 bridge 模块扩展 `BridgeServerHandle`）。
- **修复 #3（降延迟）**：新增 `bridge.broadcast()`；`index.ts` 的 `refreshState` 在 `store.ingest` 结果为 `applied` 时**立即推送**，不再仅靠 10s 心跳（原延迟 ≤10s）。
- **验证**：真实代码探针（加载真实 `startBridge`）——长连 **35s 不再中断**（`exit=28` 跑满）、broadcast 于 **3s/6s 即时到达**；回归 `bun test` **111 pass / 1 skip / 0 fail**（含 opt-in 22s idle 长测 5 pass）、`tsc` exit 0。
- **⚠ 生效条件**：修复随**下次 OpenCode 重启**加载（当前实例仍为旧代码）。
- **证据**：并入 [evidence/M4/2026-09-25-S4.2-生产host.md](./evidence/M4/2026-09-25-S4.2-生产host.md) §5。

### 2026-09-25 工作登记（Sisyphus）——S4.7d + IPC-02 LLD 草案（待批）

- **产出**：[S4.7d 与 IPC-02 LLD 草案](./StickS3_OpenCode_S4.7d与IPC-02_LLD草案.md)（**未批准**；设计冻结按纪律不得代批）。
- **结论（重大）**：host 常驻服务（`state_host`）已就位后，S4.7d **不再被"无 host 常驻服务"阻塞**；剩余阻塞为 **IPC-02 设计冻结** + **正文/音频归属裁定**。
- **推荐方案**：数据面**仅内存**持有（≤5min、不落盘，符合 C02/SEC-04）；控制面（IPC-02）用 **loopback HTTP**；host 形态 = **扩展 `state_host` 为常驻服务**。
- **待批决策 D1—D5**（音频/正文仅内存 · 控制面 loopback HTTP · host 形态 · 识别重试次数 · 端点鉴权），详见草案 §4；批准后按 §5 切片实现。

### 2026-09-25 工作登记（Sisyphus）——S4.7d 设计批准 + Slice 1（重试资格纯逻辑）

- **设计冻结**：用户批准 **D1—D5** → [S4.7d 与 IPC-02 LLD](./StickS3_OpenCode_S4.7d与IPC-02_LLD草案.md) 转**已批准**。
- **补充发现（LLD §7）**：现音频/追加路径由**一次性 `voice_append.exe`** 承担（退出即失）→ "内存持有音频/正文"要求**常驻 host 吸收该 voice 路径**（最大件，S4.7d 真正前置）。
- **Slice 1 完成**：`host/src/retry_store.{h,c}`（重试资格/TTL 纯逻辑，内存态不落盘，符合 C02/SEC-04）+ 测试 `retry_store_test.c`（**13 checks / 0**）+ `build_retry_store_test.cmd`。
- **下一步**：① 常驻 host 吸收 voice 路径 → ② 控制端点（loopback HTTP）→ ③ 托盘接线 → ④ HIL → ⑤ 证据/矩阵。
- **（S4.7d ①—⑤ 已完成，详见 [M4 计划 §9](./StickS3_OpenCode_M4开发计划.md)；含 host 吸收 voice 路径、控制端点 IPC-02、托盘重试菜单、真实 ASR HIL。）**

### 2026-09-26 工作登记（Sisyphus）——S4.4 短提示音交付 + S4.3 字体方向回退

- **S4.3 字体方向回退（用户裁定）**：实现中文点阵字库（Noto Sans SC/OFL）后，用户发现字形显示问题并决定**界面保持英文**（原英文正常）→ **回滚**字体工作（移除 `font_cjk.h`/生成器/分区改动；设备已重刷英文固件）。S4.3「三页显示」后续以**英文**推进（R17 中文呈现暂缓，待用户另行裁定）。
- **S4.4 短提示音 ✅ 交付**：`audio_capture` 增 **I2S 全双工 TX**（DOUT=GPIO14）+ **ES8311 播放**；`main.c` 增 **alert 任务**——进入「等待/错误」**各响一次**、**录音中延后**（结束后状态仍在才响）、**过期快照不响**（设计 §310/M4 不变量）；默认音 **1kHz 方波 / 150ms**。
  - **长鸣修复**：功放（AW8737）改**仅提示音期间使能** + 20ms **静音尾** + 播放后 codec 静音（原：功放常开 + I2S underrun 停在方波沿 → 长鸣）。
  - **HIL**：① 开机自检短「嘀」✅；② 发起 OpenCode 提问 → 会话 waiting → 设备**响一声**（用户确认）✅。
  - **未验**：错误状态触发（同码路）、录音中延后实机、过期不响；时长/频率待实机校准。回归：麦克风正常（`rate_probe 16039/s`）。

### 2026-09-26 工作登记（Sisyphus）——S4.3 三页显示完成（英文界面）

- **设计迭代**：新增**屏幕模拟器** `tools/gen_screen_mock.py`（按 135×240 复现固件渲染、放大输出 PNG），与用户**逐页评审**定稿：
  - **第 1 页 STATUS**：淡紫标题（铺满）；`BLE:`/`HOST:`（白 16）各与值 `CONN`/`OK`（黄 16）**分行**；状态词 **23px 蓝 `#3399FF` 居中**；
  - **第 2 页 TASK**：`TITLE:`/`PROJ:`/`TOOL:`/`TODO:` + 值（16，左对齐）；
  - **第 3 页 USAGE**：`IN:`/`OUT:`/`COST:`（白 16）+ 值（**黄 16**，左对齐）。
- **固件**：`display.c` 三页渲染 + 5s 自动翻页；固件保留**完整 STATE 快照**（新增 `ble_gatt_host_snapshot()`）；新增**按像素字号**绘制 `draw_str_px`（支持 23px）。
- **命中并修复真实缺陷**：**帧缓冲字节序**——ST7789(SPI) 像素高字节在前，而 ESP32 帧缓冲 `uint16` 为小端 → 全屏颜色 **G/B 互换**（黄↔紫、紫↔绿；此前"暗绿背景被看成红"同因）。修复：写入前 16 位字节交换（`pxswap`）。
- **判定**：用户真机目视**合格**；**中文按用户裁定暂缓**（界面英文）。
- **教训**：设计评审用模拟器时，**模拟器与固件布局必须一起改**（上一轮只改模拟、漏同步固件，导致真机旧布局）。

### 2026-09-26 工作登记（Sisyphus）——S4.6 侧键 + S4.5 息屏 + 取消轮巡

- **S4.6 侧键手势 ✅**：`button.c` 新增 **BtnB（GPIO12）**——**短按(<800ms) 切页 / 长按(≥800ms) 取消语音**（长按释放不再触发短按）；去抖 **30ms**。真机合格。
- **S4.5 息屏 ✅（电量项除外）**：`power_task`——**60s 无活动关背光(GPIO38)、BLE/OpenCode 保连**；**按键 / 新「等待/错误」事件 / 录音**可唤醒；**心跳不唤醒**（UI-03）。真机合格。
  - **待查**：**电池电量读数**——硬件文档仅记 M5PM1 GPIO0=充电状态，**未见电池 ADC 通路**（§356 电车续航/健康"未验证"；需查 M5PM1 寄存器或 M5Unified 实现）；**USB 插入/拔出策略**（UI-04）随之。
- **显示（用户裁定）**：**取消 5s 自动轮巡** → 屏幕**固定首页**，**仅侧键切页**。

### 2026-09-26 工作登记（Sisyphus）——S4.5 电量读数交付 + UI 布局校准（外壳遮挡）

- **S4.5 电量读数 ✅（补齐上一节"待查"项）**：通路查明——**M5PM1 I2C `0x6E` 寄存器 `0x22`（VBAT_L，2 字节小端，值即 mV）**；充电位 **`0x12` bit0**（低有效，需 PM1 GPIO0 配输入）；百分比 `clamp((mv-3300)*100/800, 0, 100)`。实现 `pm1_battery()` + **三页常驻** `BAT n%`（`BAT` 16px 蓝、百分比/充电 8px 白），`power_task` 每 **10s** 刷新。**真机证据**：`I (16238) sticks3: battery 4200mV 100% chg=0`（USB 满电）。**未验**：放电百分比下降曲线、`CHG` 真机显示。**未做**：USB 插入/拔出策略（UI-04）。
- **UI 布局校准（用户逐条裁定，多轮）**：①电量**三页常驻**（移出逐页分支到公共区）；②`BAT` 16px、百分比/充电 8px **白**；③**全屏文本整体下移 8px（`Y_SHIFT`）**——用户判定顶部"缺字"实为**打印外壳遮挡**，**非显示缺陷**；④**页脚 `1/3` 锚点补偿 `Y_SHIFT`**（否则 236..243 越出 240 面板底部被裁）；⑤模拟器 `tools/gen_screen_mock.py` **同步**（沿用"S4.3 模拟器与固件布局必须一起改"教训）。
- **测试/构建**：固件 `idf.py build` OK（`sticks3.bin 0xb45a0`，app 余 30%）；插件 `bun test` **111 pass / 1 skip**、`tsc` exit 0；`tools/` 190 OK；host 单测（state_merge/retry_store/append_ctl/sse_parse）全绿。
- **刷写**：`flash-artifacts/` 更新 + `SHA256SUMS` 重算；本轮末固件 sha **`b62ac78e96dd0b62`**（`sticks3.bin`）。run 期间 `state_host.exe` 反复重启以配合刷机。
- **真机确认 ✅（2026-09-26）**：用户确认下移 8px 后**已完全避开外壳** → `Y_SHIFT=8` **冻结**。

### 2026-09-26 工作登记（Sisyphus）——S4.5 UI-04 USB 策略：✅ 完成（HIL 全项通过）

**用户裁定**：插电＝常亮；电池＝60s 息屏；**不做**亮度分级；**不上** BLE 电量。

- **信号选型（含实测反证）**：查证 M5PM1 数据手册 + 官方 `m5stack/M5PM1` 驱动 + M5Unified → **`PWR_SRC (0x04)` bit0 = 5VIN(USB) 有效（位图）**，`VIN (0x24/0x25)` 为 USB 5V 电压；**PMIC 无"USB 在位"专用寄存器**。**实测反证 `CHG_STAT` 不可用**：80s 全寄存器转储显示——插电充电时 `0x12=0x14`（充电中），**停充瞬间翻成 `0x15`，而 `0x04` 仍 `0x05`、`VIN` 仍 5132mV（USB 仍在）**；即"插着但没在充电"会被充电位误判为电池。另：`M5PM1_Class::isCharging()` 是 M5Unified 的**永久桩函数**；IDF `usb_serial_jtag_is_connected()` 依 SOF，**接充电宝会误判未连接**，均不可用。
- **实现（`552979b`）**：`pm1_power()`（`0x04` bit0 为主 + `VIN>4000mV` 为辅）；`power_task` 电量与电源来源每 10s 同批刷新、仅变化时打 `power usb=%d`；判据 `usb_present || 空闲<60s || 录音中`；**只改背光状态、不触碰录音会话、无重启路径**。
- **HIL ① 通过（我自验）**：插电静置 **75s → 息屏 0 次**（改前 60s 必灭）；串口 `power usb=1`；sha `b4f0fbf0f540c557`。
- **物理限制**：串口与供电同一根 USB → **纯电池态寄存器值无法串口观测**，`0x04` bit0 清零只能靠行为验证。
- **HIL ②③④ ✅ 通过（2026-09-26 真机）**：②拔电静置 60s **熄灭** ③按键**唤醒** ④**录音中拔插：无重启、`REC` 读秒无中断**；**host 侧 BLE 连续性独立复核**（37 次 push、无 disconnect/reconnect/error）。→ **S4.5 收口**。
- **附带（用户提出"按录音键无反馈"）**：根因＝①`REC` 只在第 0 页；②**`audio_session.c:58` 要求 BLE 主机已连且已订阅音频**，未连主机时会话不启动（当时 `ble_conn=0`，**实际未录音**）。新增**全屏录音画面**（`REC`+计时／无主机显 `NO HOST`）；并修复 **`draw_char_px` 漏 `pxswap`** 致 REC 显蓝、`BUSY` 长期错色（`856dc00`）。

### 2026-09-26 工作登记（Sisyphus）——S4.7 启动器 LLD 草案（**未批准**，待裁定 D1—D9）

- **产出**：[S4.7 启动器 LLD 草案](./StickS3_OpenCode_S4.7-启动器LLD草案.md)（**未批准**；设计冻结按纪律**不得代批**）。
- **范围发现（最大未决）**：仓库内"启动器"有两种含义，工作量差一个数量级——**A** Windows 侧拉起器（拉 `state_host`/`tray`、单实例、崩溃重启、退出收尾；依据 `M4 §8 L138`、`evidence/M4/2026-09-25-S4.2-生产host.md L73`、`contracts/M1-桥接契约冻结.md L20`）／**B** HLD **M08「专用启动与绑定」**（在指定 WSL 发行版与项目目录启 OpenCode、**显式传目标 session**、生成随机 `tuiEpoch`、交接桥接凭据；依据 `设计文档 L111—114`、`HLD OC-03 L339`，其握手细节 `HLD §11.3 L431` 仍标**待定**）／**C** 两者合一。→ **D1 由用户裁定**。
- **已发现两处需裁定的一致性问题**：①**端口归属**——`M1 契约 L20`（已冻结）要求"端口由**启动器**分配"，而现实现由**插件**取 `port ?? 0`（OS 随机，实测 34901）→ **D5**；②**`host.json` 安全**——写文件未显式设 0600 ACL，且默认 token 为常量 `"sticks3-host"`，而已批 **D5 推荐"复用 bridge token 语义"**（模式一致、取值不同）→ **D8**。
- **现状缺口（均有据）**：`tray.exe` 退出**不停止** `state_host.exe`；host **无 shutdown 端点**；**无单实例锁**；控制端口 `bind` 失败时**进程继续运行但控制面不可用**；日志全走 **stdout**（分离启动后去向未定）。
- **自启**：全仓**无任何**登录自启/服务/任务计划要求（"开机"命中均指设备硬件上电）→ 属产品选择（**D3**），不得默认引入。
- **待裁决策 D1—D9**（范围 / 进程模型 / 自启 / 停止重启语义 / 端口契约 / 配置发现与发行版 / 日志去向 / 安全 / 验收映射）见草案 §4；批准后按 §5 切片实现，**并先走 ADR + 契约更新（C10）**。
- **⚠ 起草时发现的独立缺口（非 LLD 可解，需登记）**：`契约补充 C05`（2026-09-15 已批，cleanup-first）要求常驻进程在**锁屏(L70)/挂起休眠(L71)**时取消并清理在途事务（原文「收到挂起通知即取消并清理…即使未满5分钟也不继续旧事务」）；而 `tray.cpp`/`state_host.cpp` **均未订阅 `WM_WTSSessionNotification`，该义务当前未实现**。凡持有音频/正文内存的常驻进程（现为 `state_host`）均受其约束 → 需单独立项处置（已同步写入草案 §1 缺口 6）。

### 2026-09-26 工作登记（Sisyphus）——S4.7 启动器**设计冻结**（D1—D9 全部裁定 + ADR + M1 契约 v1.1）

- **裁定结果（用户 2026-09-26 逐条点头，全部取推荐项）**：**D1=A**（仅 Windows 侧拉起器）；D2=常驻 `launcher.exe` 监督；**D3=不自启**（手动运行）；D4=launcher 持句柄并终止；D5=**修订 M1 契约**（端口由**桥接侧**分配并经 `bridge.json` 发布）；D6=`STICKS3_BRIDGE_JSON` 优先 + 发行版名可配置；D7=launcher 收编子进程输出统一写 `%LOCALAPPDATA%\sticks3\logs\`（轮转）；D8=`host.json` 补显式 ACL + token 复用 bridge token；D9=挂靠现有用例 + 新增 3 条 launcher 专属（不新立 S4.7e）。
- **ADR**：[contracts/ADR-2026-09-26-启动器设计冻结与端口归属.md](./contracts/ADR-2026-09-26-启动器设计冻结与端口归属.md)（按 C10 L144 最小字段：问题/证据/候选/选定与未选定/批准状态/关联R·TC·M/回退条件/验证状态）。
- **契约修订（按 contracts/README L20 流程）**：`M1-桥接契约冻结.md` §2「端口」行 → **v1.1**：原述"启动器分配"在 D1=A 下**不可实现**（插件在 WSL 的 OpenCode 内先起，launcher 管不到），改为"由桥接侧分配并经 `bridge.json` 发布，launcher/托盘只读发现"。**其余条款不变**（不保留未发布版本的兼容包袱）。
- **明文排除**：SCM 服务化（与 `REL-01`「不修改全局安全策略」、`UI-01`「不需常驻管理员」冲突，且已被列为另议）、一次性引导、`tray` 内嵌持有、一切自启机制、设备侧改动、M6 安装布局。
- **契约登记表同步**：`contracts/README.md` 更新——「中文字体/按键/息屏参数」行由"⏳ 未开始"更正为 **✅ 已定**（英文界面 / 长按 800ms·去抖 30ms / USB 常亮·电池 60s）；新增「启动器与桥接端口归属」行；并确立 **ADR 命名位置约定** `contracts/ADR-YYYY-MM-DD-<主题>.md`。
- **落地状态（2026-09-26 更新）**：**D1—D9 已全部裁定并实现完毕**——启动器 Slice 1—2 + **D8(a)(b)** 均已实现并验证（详见本节下文各条与证据）。
- **Slice 1 已实现并本机冒烟通过（2026-09-26）**：新增 `host/src/launcher.cpp` + `host/src/build_launcher.cmd`（**未改既有文件**）；构建 `BUILD_OK`、`launcher.exe` 182,272 B。核对：**单实例**（第二实例 `already running - exiting`）、**依序拉起**（删 `host.json` 后 `descriptor ready after 250ms`，host 日志 235725 → tray 235726）、**日志落盘**（`%LOCALAPPDATA%\sticks3\logs\{state_host,tray}-*.log`）、**退出经 Job 收尾**（`cleanup ok (no leftover)`）、**描述符发现**（`\\wsl.localhost\<distro>\…`，发行版可配）。修复 2 处宽窄串格式化缺陷。**（当时未实现的崩溃重启/轮转触发/真机 E2E 已在本节后续条目补齐验证。）** 证据：[evidence/M4/2026-09-26-S4.7-启动器Slice1-2.md](./evidence/M4/2026-09-26-S4.7-启动器Slice1-2.md)。
- **Slice 2 完成并验证（2026-09-26 同日）**：`launcher.cpp` 增**崩溃重启 + 指数退避**（`min(1s<<(n-1), 30s)`；退出前存活 ≥60s 则重置；**重启复用同一日志**防爆炸）。实测：杀 `state_host`(336) → 自动重启 **3204**，日志 `restart #1 in 1000ms`；**轮转路径真跑**（预置 1,258,291 B → `rotate: … -> state_host.1.log`，`rotated=True`）。**未验**：退避封顶/重置路径、`tray` 单独重启、真实设备 E2E。
- **真实设备端到端 ✅（2026-09-26 同日）**：经 `launcher.exe` 单命令拉起，`state_host` 日志实证全链路——`control endpoint on 127.0.0.1:45910` → **`ble connected + subscribed (CTRL/AUDIO/EVENT)`** → `subscribe /v1/state/stream` → **`push #1/#2/#3 rev=1935…1937`**（STATE 持续下发）；退出 `cleanup ok`。**未验**：退避封顶/重置路径、`tray` 单独重启。
- **边界场景补验 ✅（2026-09-26 同日）**：①**控制端口 45910 被占** → `control endpoint bind failed` 后 host **继续运行**且 **BLE/STATE 链路不受影响**（`push rev=2003…2005`）；②**`tray` 单独被杀 → 自动重启**（17380→3044，复用同一日志）。**仍未验**：退避封顶（30s）/ 60s 稳定重置路径。复跑脚本：`tools/launcher_edge.ps1`。
- **退避封顶与稳定重置 ✅（2026-09-26 同日，最后一条未验路径闭合）**：实测递增 `1000→2000→4000→8000→16000`；**封顶 `#6/#7 = 30000ms`**；**60 s 稳定后重置**（`alive=86750ms → restart #1 in 1000ms`）。复跑脚本：`tools/launcher_backoff.ps1`。→ **启动器监督行为全项验证完毕**；仅剩 **D8 未实现**。
- **D8(a) 已实现并验证（2026-09-26 同日）**：`state_host.cpp` 写出 `host.json` 后加 `restrict_descriptor_acl()`（当前用户 SID → `SetEntriesInAclW` → `SetNamedSecurityInfoW` + `PROTECTED_DACL` 剥离继承）。`BUILD_OK`（560,640 B）；`icacls` 实证 **仅 `DESKTOP-F7NTBB2\home:(F)` 一条 ACE**，无 `Users`/`Everyone`/继承项。**D8(b)（token 复用 bridge token）仍未实现**（时序坑注记见 LLD §5 表 3b）。
- **D8(b) 已实现并验证（2026-09-26 同日）**：`bearer_ok()` 加锁（变量可被主循环更新）；主循环取得 bridge token 后（未显式设 `STICKS3_HOST_TOKEN` 时）切换控制 token 并重写 `host.json`（复用 ACL 路径）。`BUILD_OK`（561,152 B）；实测 **`tokens_equal=True`、新 token `200`、旧默认 `401`**。→ **D8(a+b) 完成，启动器侧无未实现项**。
- **M4 收口就绪核查（2026-09-26）**：新增 [M4收口就绪核查.md](./M4收口就绪核查.md)——把 M4 出口判据逐条对到证据。**达标**：三页/提示音/取消·重试/USB·电池策略/状态可追溯/按键体验/蓝牙息屏 HIL。**否决项 3 条**：①`NOTICE.md` **许可证逐项审查未完成**；②**UI-01 未执行** + 托盘真实点击路径未验；③**用量口径标签待确认**。**进展（同日）**：①已按**实际交付状态**重写 `NOTICE.md` 并**逐项实读校验**（固件 4 项：ESP-IDF v5.5.4 / `78/esp-opus` / `espressif/es8311` / `font8x8_basic.h`；插件 4 直接依赖 + 锁文件 37 条扫描；LVGL 与中文字体标"**未采用**"），由"未完成"降为"**部分完成**"——残余：ESP-IDF 组件例外细目、Opus `LICENSE_PLEASE_READ.txt` 全文、交付包许可归集（M6）。**未关闭里程碑**（关闭由用户裁定）。
- **⚠ 未排入切片的已批项**：**D8（`host.json` 显式当前用户 ACL + token 复用 bridge token）** 已批准但**尚未实现**，且我起草的 LLD §5 切片表里**漏列了它**（属我草案的缺口）——实现需改 `state_host.cpp`。
- **证据**：[evidence/M4/2026-09-26-S4.5-USB策略.md](./evidence/M4/2026-09-26-S4.5-USB策略.md)。
- **证据**：[evidence/M4/2026-09-26-S4.5-电量读数.md](./evidence/M4/2026-09-26-S4.5-电量读数.md)、[evidence/M4/2026-09-26-S4.3-UI布局校准.md](./evidence/M4/2026-09-26-S4.3-UI布局校准.md)。

### 2026-09-27 工作登记（Sisyphus）—— M4 收口执行清单 #1：控制端点 accept 瞬时错误修复（✅ 完成）

- **范围**：M4 收口执行清单 §6 #1（用户 2026-09-26 批准"修端点缺陷"，直指否决项②重试失败真因）。**本会话只推进这一个可演示行为增量**；#2/#3/#4/#5 未动。
- **根因（已核）**：`http_thread` 的 accept 循环在无停止请求时**唯一**退出路径是 `accept()==INVALID_SOCKET`（`g_http_stop` 全仓从未被写入）→ 旧实现把任何 accept 错误都当终止 → 端点**永久失效**。
- **变更**：新增 `host/src/http_accept_policy.h`（`http_accept_error_is_fatal`：仅 `WSAENOTSOCK/WSAEINVAL/WSAEOPNOTSUPP/WSAEAFNOSUPPORT` 致命）+ `http_accept_policy_test.c` + `build_http_accept_policy_test.cmd`；改 `state_host.cpp` accept 分支为 **记录 errno → 致命才 break，否则 `Sleep(50)`+continue**。**无冻结契约影响**。
- **证据（本轮实测）**：单测 **RED（exit 1，5 failure）→ GREEN（exit 0）**；回归 `state_merge` 21/0、`retry_store` 13/0、`sse_parse` PASS、`append_ctl` 14/0；`build_state_host.cmd` **BUILD_OK**；HIL `tools/host_accept_resilience.ps1`——RST 风暴(800) 前后 **24 次 GET 全 200（PASS）**。证据：[evidence/M4/2026-09-27-控制端点accept瞬时错误修复.md](./evidence/M4/2026-09-27-控制端点accept瞬时错误修复.md)。
- **诚实边界**：HIL **未复现**原始 accept 瞬时错误（Windows 对 RST backlog 连接仍返回可用套接字）；`continue` 分支由**单测**锁定，非 HIL 触发。
- **独立复核（Oracle，只读）已采纳 1 处修正**：`WSAENETDOWN` 由"瞬时"改为**致命**（否则 Winsock 栈已死时会 ~20 Hz 永久重试+刷日志且端点静默不可用）；补断言 RED→GREEN（**8/8**）。并核对测试常量均为 `WSAE*`（非 errno.h）——复核所提"可能误用 errno 常量"隐患不成立。
- **新发现（独立缺口）——✅ 已修复（2026-09-27，用户确认）**：端点**单线程 + 阻塞 `recv`（无超时）**——一条"连上不发"的连接即令 accept 循环**停摆**（端点仍在 `LISTENING` 却不再响应）。**与原始缺陷机制不同**（原始：监听套接字**消失**）。修法（Oracle 推荐最小）：`http_handle_conn` 设 `SO_RCVTIMEO`/`SO_SNDTIMEO`=`DWORD` **5000ms**。**RED→GREEN**：`recovered_after_ms -1(FAIL) → ≈4998ms(PASS)`（停顿连接全程未关）。详见证据 §5.2/§7/§8 与 [M4收口就绪核查.md](./M4收口就绪核查.md) §7。
- **用户已裁定（2026-09-27）**：① 修 §5.2 ✅（已完成）；② 继续执行清单 **#2+#4**（cost 毫单位换算 + 固件日志，需刷机）与 **#3** 扩 `reasoning/cache` 字段（报表应走 ADR + 契约 v1.1）；③ **授权提交**本轮改动。
- **已提交（用户授权，2026-09-27）**：`07332c3 fix(m4): 控制端点加固…`、`2667257 docs(m4): …证据/脚本/登记`。
- **§6 #2 换算已完成（2026-09-27）**：新增 `host/src/cost_scale.h`（`cost_to_milli`：四舍五入 ×1000、负值→0、超限夹到哨兵下方）+ 接入 `bridge_snapshot.h`（`avail_cost_milli`）；单测 **RED→GREEN 7/7**（并暴露"负值回绕撞 NOT_REPORTED"已修）；`state_host`/`ble_client` 重建 **BUILD_OK**。**登记文档冲突**：`M4收口就绪核查.md` #2 原"→1846"与公式 `+0.5` 矛盾，应为 **→1847**（对两位小数显示无影响）。证据：[evidence/M4/2026-09-27-cost毫单位换算.md](./evidence/M4/2026-09-27-cost毫单位换算.md)。
- **§6 #3 + #4 已实现（2026-09-27）**：契约 **v1.1**（ADR-2026-09-27；固定段 31B / version 2；`tokens.reasoning`/`cacheRead`；MTU 阈值 136）；`s3p_state`/`state_merge`/`bridge_snapshot`/`ble_gatt`（+`cost=%lu reason=%lu cache=%lu`）/`display`（`cost_text` `%.2f`+`¥` 字形；`REASON`/`CACHE` 行）全部更新。单测 `s3p_state` **34/0**、`state_merge` **23/0**；固件**已刷机**（`sticks3.bin` sha `143590c7…`）；**受控 E2E 串口实证** `host STATE … cost=1847 reason=771532 cache=270690560`。证据：[evidence/M4/2026-09-27-STATEv1.1用量字段.md](./evidence/M4/2026-09-27-STATEv1.1用量字段.md)。
- **真机目视 ✅（2026-09-27，用户确认"正常"）**：设备 USAGE 页（侧键切 `3/3`）显示 `COST: 1.85¥` / `REASON 771532` / `CACHE 270690560`。→ **#3/#5 由 🟡 转 ✅**。
- **否决项② 快测复现 ✅（2026-09-27）**：真实 `launcher`→`state_host`→BLE + 真实桥接（OpenCode pid 11523）已起；等价复现托盘 `retry_action`：`GET /v1/retry/status` **×10 全 200**、`POST 重试识别/填入` → **409（可达）**（原症状为"无法连接 state_host"）；**并已由用户实点托盘「重试识别/填入」确认弹窗为期望内容（2026-09-27）**。→ **端点存活 + GUI 点击路径闭环**；**重试全流程成功**（真实录音→保留→识别 **202** → ASR → 追加 **200 APPLIED**）。**②残余**：`UI-01` 系统记录、托盘 spawn 凭据工具、"实点→202"若要严格可再录。证据：[evidence/M4/2026-09-27-否决项②-托盘重试端点存活复测.md](./evidence/M4/2026-09-27-否决项②-托盘重试端点存活复测.md)。
- **不要重复询问**：#1、§5.2、#2 换算、#3/#4 已批准/实现/目视，无需再确认。

