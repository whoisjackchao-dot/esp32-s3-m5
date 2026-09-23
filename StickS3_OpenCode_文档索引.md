# StickS3 OpenCode项目文档索引

> 2026-09-15；当前为设计审阅与测试规格阶段；没有项目实现或已执行产品测试。  
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
3. **桥接插件已注册**到 WSL OpenCode 的 **`~/.config/opencode/tui.json`** 的 `plugin` 数组（已备份 `tui.json.bak-20260923`）——**需重启 OpenCode 生效**。
   - 更正记录：该插件是 **TUI 插件**（`TuiPluginModule = { id?, tui, server?:never }`，`@opencode-ai/plugin/tui`），注册处是 `tui.json` 而非 `opencode.json` 的 server `plugin` 数组；一度加错位置，已撤回（`opencode.json[.jsonc]` 已还原备份）。
   - 加载方式：OpenCode 用 `Bun.resolve(entry, ~/.config/opencode)` 解析后 `import()`；实测绝对路径与 `file://` 两种写法均可解析。
4. 小智 KB 适用边界（`references/APPLICABILITY.md` + SKILL.md 收紧）；硬件文档附录 B 更新为"已解决"。

### 下一步（重启 OpenCode 后执行）
```powershell
. "C:\Temp\ESP32-S3M5\tools\bridge_env.ps1"     # 读 bridge.json + 查 /v1/binding，装载 4 个环境变量
powershell -ExecutionPolicy Bypass -File "C:\Temp\ESP32-S3M5\tools\voice_slice.ps1"
```
对设备说 30 秒 → 预期 `[PASS] 已填入草稿（未提交，请手动发送）` → 在 OpenCode 输入框**手动发送** → **M3 用户价值闭环达成**。

若装载器报"找不到 bridge.json"：插件未加载成功，检查 OpenCode 启动输出是否有插件报错。
> 注意：桥接在 WSL 内监听 127.0.0.1，Windows 侧经 WSL localhost 转发访问；若不通，改用 WSL IP（需同步改 `bridge_env.ps1` 的 URL 策略）。

### 交付前必做（诊断脚手架登记）
- `firmware/sticks3/main/main.c`：启动 `zcr` 证据、`audio_capture_log_clocks()`、`audio_capture_probe_rate()`、心跳周期时钟打印
- `firmware/sticks3/main/audio_session.c`：END 日志 `zcr`
- `ble_gatt.c`：测试钩子 `0x7F`（用户已裁定保留，登记于 `contracts/M2` 附节）
- 以上**修复验证完成后应移除或显式登记**，否则"交付固件=已验证固件"的对应关系需附条件说明

### 未竟项
- M3 验收：30 条语料冻结 + 时延 60 次（Q03）+ 准确率 30 条（Q04）
- M4：状态屏三页 UI、托盘、凭据管理器（C11）落地
