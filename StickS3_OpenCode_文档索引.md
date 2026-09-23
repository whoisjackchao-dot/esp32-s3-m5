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
- **固件产物（本轮构建，未刷机）**：`sticks3.bin` size=769104，SHA-256=`b89b12527d15925cae9bc98479ec8549d8c511ce865c049e25fcd437867fb7e5`。刷机须**另行授权**；交付前重算 `flash-artifacts/SHA256SUMS`。
- **明确未验证**：①固件松键路径**未刷机、未 HIL**（须授权刷写后按住/松开实测）；②`READY(0x10)` 仍为等价启动触发（M3 真机 UX 以按键为主，移交语义待确认）；③去抖值/长按阈值未批准。

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
- **M3 故事 4**：✅ **闭环已成立（2026-09-23 16:25，E2E-01p PASS）**；物理按键路径由本轮"提前结束增量"补齐，**待刷机 HIL 验证**
- M3 验收：30 条语料冻结 + 时延 60 次（Q03）+ 准确率 30 条（Q04）
  - Q03：计时方案已裁决（方案 B 串口外部观察，2026-09-23）；**测量工具待建**；松键固件已落地但**未 HIL**
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
