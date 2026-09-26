# S4.7 启动器 LLD 草案（host 侧拉起与生命周期）

> 日期：2026-09-26。状态：**已批准（2026-09-26，用户逐条裁定 D1—D9，全部取推荐项）**。ADR：[contracts/ADR-2026-09-26-启动器设计冻结与端口归属.md](./contracts/ADR-2026-09-26-启动器设计冻结与端口归属.md)。**批准设计 ≠ 已实现**：实现按 §5 切片推进。
> 依据：[M4 开发计划 §8/§9](./StickS3_OpenCode_M4开发计划.md)、[S4.7d 与 IPC-02 LLD](./StickS3_OpenCode_S4.7d与IPC-02_LLD草案.md)（已批准 D1—D5）、[功能模块 HLD §7.1/§11](./StickS3_OpenCode_功能模块HLD.md)（M07/M08、IPC-01—03）、[设计文档 R03 与交接链](./StickS3_OpenCode_语音状态屏_设计文档.md)（L111—114）、[契约冻结 M1 §2](./contracts/M1-桥接契约冻结.md)、[契约补充 C02/C05/C10/C11](./StickS3_OpenCode_契约与演进设计补充.md)、测试矩阵 UI-01/UI-06/SEC-06。
> 前置事实：`state_host.exe`（常驻 host）与 `tray.exe`（托盘）均已交付并实测；**唯一缺的是"由谁、在何时、以什么顺序把它们拉起来，以及如何停"**。

## 1. 目标与缺口

**现状（已交付，事实）**
| 件 | 形态 | 启动方式（现状） |
| --- | --- | --- |
| `host/src/state_host.exe` | 常驻控制台进程（无 argv） | **手动**运行；`STICKS3_HOST_SECONDS=N` 仅供测试限时自退，生产不设 → 无限运行 |
| `host/src/tray.exe` | Win32 GUI 托盘（`wWinMain`，无 argv） | **手动**运行 |
| `host/src/voice_append.exe` / `host_entry.exe` | 一次性 | 手动 + `tools/bridge_env.ps1` 装载 `STICKS3_*` 环境变量 |
| 桥接（插件） | 随 OpenCode 在 WSL 内启动 | `bridge.json`（`STICKS3_DESCRIPTOR_DIR` 可覆盖，0600） |

**缺口（逐条有据）**
1. `M4 §8 L138` 明确把「启动器/IPC」列为 S4.7 **未交付**；`M4 §2 L32` 记「**启动器 LLD 待冻结**」。
2. `evidence/M4/2026-09-25-S4.2-生产host.md L73`：「未做 Windows 服务（SCM）或托盘内嵌包装；`state_host.exe` **需手动/由启动器拉起**」。
3. `tray.exe` 退出（WM_DESTROY → PostQuitMessage）**不会停止 `state_host.exe`**——两者今日互相独立，仅通过 `%LOCALAPPDATA%\sticks3\host.json` 单向发现端点。
4. 无单实例保护：`state_host` 控制端口的 `bind` 失败时**只打印 `control endpoint bind failed` 并让该线程退出，进程继续运行**（端口 45910 冲突时会出现"跑着但控制面不可用"）。
5. 无优雅停止路径：控制端点只有 `GET /v1/retry/status`、`POST /v1/retry/{recognize,append}`，**没有 shutdown**；也没有信号处理。
6. **已批义务未实现（起草时新发现）**：`契约补充 C05`（2026-09-15 批准，cleanup-first）要求常驻进程在**锁屏**（L70）与**挂起/休眠**（L71）时取消并清理在途事务——原文「收到挂起通知即取消并清理…**即使未满5分钟也不继续旧事务**」。而 `tray.cpp` / `state_host.cpp` **均未订阅** `WM_WTSSessionNotification`，即**目前不实现该义务**。凡持有音频/正文内存的常驻进程（现为 `state_host`）都受其约束 → 启动器/生命周期设计须处置。
7. 启动器**本身是承诺交付件**：`交付节点与完成定义.md L140`「固件源码、Windows主机源码、TUI插件/**启动器源码**及全部实际测试与fixture」、`L143`「README安装/配对/Key配置/**专用启动**/日常使用」→ 不是可选项。
8. `可行性核查 L208`：「**未来启动器的安全IPC尚未实现验证**」——若 D1 选 B/C，安全 IPC 属未验证区。

**约束（已批/已冻结，不得违反）**
- `evidence …L4` 用户裁定：host 形态＝**独立常驻可执行**；「Windows 服务/托盘包装**另议**」。
- `contracts/M1-桥接契约冻结.md L20`：`| 端口 | 启动器分配，不写死；不作为 OpenCode 服务端口 |`；`L23`：token 交接**允许 0600 文件或子进程环境变量**，**禁止**进命令行、普通日志、git。
- `契约补充 C02`：密钥不得进命令行/公共临时文件/普通日志；`C11`：凭据走 Windows 凭据管理器（`TargetName=StickS3/ZhipuASR/ApiKey`）。
- `契约补充 C10`：设计冻结需 **ADR**（最小字段见补充 §C10 L144）；**不得代批**。
- 工程硬约束：`.cmd` 必须**纯 ASCII**（本机 cmd.exe 按 GBK 解析，`文档索引 L320` 有崩坏先例）；WinRT 文件用 `/std:c++17 /await`（`build_state_host.cmd L18`），无 WinRT 的用 `/std:c++20`（`build_tray.cmd L13`）。

## 2. 范围候选（本草案最大的未决项 → D1）

仓库里"启动器"至少有两种含义，**两者范围差一个数量级**：

| 候选 | 内容 | 依据 |
| --- | --- | --- |
| **A. Windows 侧拉起器** | 按顺序拉起 `state_host.exe`（可选 `tray.exe`）、单实例、崩溃重启、退出时收尾 | `M4 §8 L138`、`evidence L73`（"由启动器拉起"）、`M1 契约 L20`（端口由启动器分配） |
| **B. M08 专用启动与绑定** | 在**明确的 WSL 发行版与项目目录**启动 OpenCode、显式传目标 session、生成随机 `tuiEpoch`、把桥接凭据交接给托盘 | `设计文档 L111—114`、`HLD §11.3/OC-03 L339`、`R03` |
| **C. A+B 合一** | 先起 WSL/OpenCode 与桥接，再起 Windows 侧 | 需求上最完整，工程量最大 |

`HLD §11.3 L431` 对 B 仍标**待定**：「精确管道名称、端口分配、凭据交换与 Windows/WSL 启动握手**仍需 M1 验证**」；`HLD OC-03 L339`：「具体启动流程**二选一待 LLD 冻结**；不使用 `--auto` 审批」。

## 3. 设计选项（按待决项分组）

### 3.1 进程模型（→ D2）
| 选项 | 说明 | 代价 |
| --- | --- | --- |
| A（独立 `launcher.exe` 监督） | 新 exe 负责拉起/监督/收尾；tray 与 host 保持独立 | 需新件 + 单实例/重启策略 |
| B（`tray.exe` 内嵌拉起） | 托盘启动时自行 spawn 并持有 host 句柄 | 与"托盘退出不停 host"矛盾，需改 tray 生命周期 |
| C（Windows 服务 SCM） | 开机即起、无用户会话 | 需**管理员安装**，与"普通用户可安装运行、无日常管理员"（`M4 §8 L137`）冲突；且用户裁定"SCM 另议" |

### 3.2 自启机制（→ D3，**仓库内无任何要求**）
全仓检索：**没有**任何关于登录自启 / Run 键 / 任务计划 / 开机启动 host 的要求；"开机"命中均为设备硬件上电。→ 自启属**产品选择**，必须由用户裁定；不得默认引入。

### 3.3 停止/重启语义（→ D4）
现状：tray 退出不停 host；host 无 shutdown 端点；无单实例锁。
可选：①由 launcher 持有并终止子进程；②给 host 加 `POST /v1/shutdown`（走现有 Bearer + loopback 模式，最小增量）；③只做单实例锁 + 端口占用探测（不改协议）。
注意：`M4 §9 L160—161` 的教训是"**避免在未定形态上编码**"——本项须先冻结再实现。

### 3.4 端口归属（→ D5，**已发现契约偏离**）
`M1 契约 L20`（已冻结）要求 **端口由启动器分配**；但现实现由**插件**取 `port ?? 0`（OS 随机分配，`plugin/src/bridge/server.ts L206-209`，实测 34901）。两者不一致 → 需裁定：改实现以符合契约，还是登记契约偏离并修订契约（走 ADR）。

### 3.5 配置发现（→ D6）
现状事实：`tray.cpp L62-65` 与 `state_host.cpp L100-104` **各自硬编码** `\\wsl.localhost\Ubuntu\…` 与 `\\wsl$\Ubuntu\…`（**发行版名写死**），另有 `STICKS3_BRIDGE_JSON` / `STICKS3_HOST_JSON` 覆盖。→ 规范化顺序与"发行版如何确定"需裁定。

### 3.6 日志与安全（→ D7/D8）
- 日志：现全部走 **stdout**（`tray.cpp L334`、`state_host.cpp say()`）。分离式启动（无控制台）后日志去向未定。
- 安全：`state_host.cpp L669-685` 用 `CreateDirectoryA` + 普通 `fopen` 写 `host.json`，**未显式设 0600 ACL**；且默认 token 为常量 `"sticks3-host"`（`L690-691`），而已批 D5 的推荐是"**复用 bridge token 语义**"——**属"模式一致、取值不同"，需裁定是否算偏离**。

## 4. 需用户裁定的决策（不得代批）

**D1—D9 已全部裁定冻结（2026-09-26）：D1=A，D2—D9 全部取推荐项 a（D3 取"不自启"）。** ADR：[contracts/ADR-2026-09-26-启动器设计冻结与端口归属.md](./contracts/ADR-2026-09-26-启动器设计冻结与端口归属.md)。范围 = A（Windows 侧拉起器）：只覆盖"把 `state_host.exe`（与可选 `tray.exe`）拉起、看住、收尾"；**不含** WSL/OpenCode 启动与 session 绑定（HLD M08，握手仍待定；如需另立 LLD）。**D1=A 直接限定了 D5 的可选空间**（见 4.4）。

### 4.1 D2 进程模型
| 选项 | 做法 | 代价/风险 |
| --- | --- | --- |
| **a（推荐）** | 常驻 `launcher.exe`：拉起 `state_host`(+`tray`)、存活检测、崩溃重启、退出收尾 | 多一个常驻进程；需定义重启退避与停止语义（D4）。依据：`evidence L4` 用户裁定 host＝"独立常驻可执行" |
| b | 一次性引导：拉起后即退出，不监督 | 崩溃无人管，"谁在管"落空 |
| c | `tray.exe` 内嵌拉起/持有 host | 托盘退出即断 host；与 `SEC-06`（托盘退出需正确清理且不误伤他者）纠缠 |
| d（不建议） | Windows 服务 SCM | **与两条已批判据冲突**：`REL-01`「不修改全局安全策略」、`UI-01`「不需要常驻管理员」；`evidence L4` 亦已列"Windows 服务"为**另议** |

### 4.2 D3 是否自启 / 机制
> 事实：全仓**无任何**自启要求（"开机"命中均指设备上电）→ 纯产品选择。
| 选项 | 做法 | 说明 |
| --- | --- | --- |
| a | 不自启（手动运行 launcher） | 零系统侵入；每次开机需手动 |
| b | 登录启动项（HKCU `Run` 键） | 当前用户、**无需管理员**；最简 |
| c | 任务计划（Task Scheduler 当前用户任务） | 可延迟启动/失败重试/条件触发，比 `Run` 键更可控 |
| d | 启动文件夹快捷方式 | 与 b 类似，可见性高、易被发现 |
| e（不建议） | SCM 服务 | 需管理员，冲突同 4.1-d |

### 4.3 D4 停止 / 重启语义
| 选项 | 做法 | 代价 |
| --- | --- | --- |
| a | launcher 持有子进程句柄，退出时终止（可先优雅后强杀） | 最直接；需处理"host 被别处启动"的情形 |
| b | host 增 `POST /v1/shutdown`（loopback+Bearer，最小协议增量）→ launcher 调它 | 干净可审计；改协议，需 ADR + 契约更新 |
| c | 只做单实例锁 + 端口占用探测，不主动停止 | 最小；"停止"仍靠用户各自退出 |
| d | tray 退出连带终止 host | 与 4.1-c 同源问题（`SEC-06` 清理语义） |

### 4.4 D5 端口归属（**受 D1=A 约束**）
> 事实：`M1 契约 L20`（已冻结）「端口 \| **启动器分配**，不写死」；实现是**插件**取 `options.port ?? 0`（OS 随机，`server.ts L206`，实测 34901）。**但 D1=A 下 launcher 无法给插件分配端口**（插件在 WSL 的 OpenCode 内先启动，只有选 B 才能干预）→ 契约文字与 D1 不相容。
| 选项 | 做法 | 说明 |
| --- | --- | --- |
| **a（推荐）** | 走 **ADR 修订契约**：端口由**桥接侧**分配并经 `bridge.json` 发布，launcher 只读 | 与现实现一致；诚实反映 D1=A 的能力边界 |
| b | 维持契约文字，待将来选 B 再实现"启动器分配端口" | 契约与实现长期不一致（须登记为已知偏离） |
| c | launcher 读 `bridge.json` 后"转授"端口（形式化包装） | 换汤不换药，徒增复杂度 |

### 4.5 D6 配置发现与发行版
| 选项 | 做法 |
| --- | --- |
| **a（推荐）** | `STICKS3_BRIDGE_JSON` 优先；未设则遍历候选，且**发行版名可配置**（env/配置项），不再硬编码 `Ubuntu` |
| b | 保持硬编码 `Ubuntu` + 文档说明（最小改动；换发行版即坏） |
| c | 用 `wsl -l -q` 探测发行版并逐一遍历 `\\wsl.localhost\<distro>\…`（更通用；多发行版需定序） |
| d | 强制要求外部设 `STICKS3_BRIDGE_JSON`，否则明确报错退出（最确定；对用户要求最高） |

### 4.6 D7 日志去向
| 选项 | 做法 |
| --- | --- |
| **a（推荐）** | launcher 捕获子进程 stdout/stderr，统一写 `%LOCALAPPDATA%\sticks3\logs\`（含大小轮转） |
| b | 子进程各自写自己的文件日志（改 `tray`/`state_host` 代码） |
| c | 保持 stdout，launcher 以带控制台方式启动（简单但体验差） |
| d | Windows 事件日志（可运维，但代码量与权限要求高） |

### 4.7 D8 `host.json` 安全
| 选项 | 做法 |
| --- | --- |
| **a（推荐）** | 写文件时显式设**当前用户专属 ACL**（等效 0600），token 改为**复用 bridge token**（贴合已批 D5 语义） |
| b | 补 ACL，但保留独立 `host.json` token（本机隔离更好，需改 D5 表述） |
| c | 登记现状为可接受（与已批 D5 语义有偏差，不推荐） |

### 4.8 D9 验收映射
| 选项 | 做法 |
| --- | --- |
| **a（推荐）** | 挂靠现有 `UI-01`/`SEC-06`/`REL-01`/`REL-03`/`REL-05` + 新增 3 条 launcher 专属（单实例、崩溃重启、退出收尾） |
| b | 只挂靠现有用例，不新增（覆盖不到 launcher 特有行为） |
| c | 新立 **S4.7e** 故事卡 + 独立用例集（最规范，但需同步改 M4 计划与追踪矩阵） |

> 现成判据原文（供 D9 直接挂靠）：`UI-01`「首次启动、设备配对、Key设置、连接、退出 → **普通用户可完成，不需要常驻管理员；可见错误和退出入口**」、`SEC-06`「启动失败/托盘退出/插件卸载 → 属于本项目的监听/线程/连接清理，**现有OpenCode任务和其他插件不被杀死**」、`REL-01`「按交付 README 构建/安装 → 构建产物可用且版本锁定，文档命令可复现；**不修改全局安全策略**」、`REL-03`（卸载清理按用户选择）、`REL-05`「后台仅 OpenCode 部分故障，BLE 仍可用 → **不将蓝牙连接等同于 OpenCode 就绪**」、`E2E-01`（真板+托盘+WSL绑定会话+真实 GLM 全链路）。

## 5. 拟实现切片（**批准后**才动，仅为便于估工）

| # | 切片 | 状态（2026-09-26） |
| --- | --- | --- |
| 0 | 冻结 D1—D9（ADR + 契约更新） | ✅ 完成（ADR-2026-09-26；M1 契约 **v1.1**） |
| 1 | `launcher.exe` 最小件（单实例 + 依序拉起 + 退出收尾 + 纯 ASCII `.cmd`） | ✅ 完成并验证（另收紧句柄继承 `HANDLE_LIST`） |
| 2 | 进程监督（崩溃重启 + 退避）与日志（D7） | ✅ 完成并验证（**轮转路径**亦实测） |
| 3 | 按 D4 停止语义落地；按 D5 处理端口契约 | ✅ D4=a 经 **Job Object** 落地；D5 契约已 v1.1，launcher 解析 `bridge.json` 并**下传 `STICKS3_BRIDGE_JSON`** |
| **3b** | **D8：`host.json` 显式当前用户 ACL + token 复用 bridge token**（**本人草案原漏列，此处补上**） | ✅ **完成并验证**：ACL（`icacls` 仅本用户一条 ACE）+ token 复用（`tokens_equal=True`、新 token 200 / 旧默认 401） |
| 4 | HIL（真实设备端到端；含"关 host 后重开""端口被占""tray 与 host 各自退出"） | 🟡 **真实设备端到端已过**（`ble connected + subscribed` → `push #re` 递增）；端口占用 / `tray` 单独退出 / 退避封顶与重置路径**未验** |
| 5 | 若 D1 含 B/C：另起 M08 LLD | 不适用（D1=A） |

**D8 实现注记（已按此实现，保留备查）**：`state_host.cpp` 的 `write_host_descriptor()` 在 `run_host()` 内**先于**读取 `bridge.json` 执行——首写时 `g_host_token` 仍是默认 `"sticks3-host"`，bridge token 要到主循环里才取到。**实际采用方案**：主循环取得 bridge token 后，若未显式设置 `STICKS3_HOST_TOKEN`，则把 `g_host_token` 换成 bridge token 并**重写 `host.json`**（复用 ACL 路径）；`bearer_ok()` 的比较加 `g_voice_mu` 锁以消除与 http 线程的竞争。ACL 侧用 `SetNamedSecurityInfoW` + `PROTECTED_DACL_SECURITY_INFORMATION` 仅放当前用户 SID。验证见 [证据 §11](evidence/M4/2026-09-26-S4.7-启动器Slice1-2.md)。

## 6. 不覆盖
- WSL/OpenCode 的**启动握手细节**（`HLD §11.3 L431` 仍待定）——若 D1 选 B/C，另立 LLD。
- 设备侧（固件）任何改动。
- M6 的**安装器/发布布局**（`REL-01`；exe 最终摆放路径在 M6 定义）。
- 自动重试/无人值守重发（`S4.7d L70` 明确排除）。

## 7. 事实附录（写 LLD 时的可核证据）
- `state_host` 控制面（已实现）：`127.0.0.1:${STICKS3_HOST_PORT:-45910}`，Bearer `${STICKS3_HOST_TOKEN:-sticks3-host}`；`GET /v1/retry/status`、`POST /v1/retry/{recognize,append}`；无鉴权 401、未就绪 409（`M4 §9 L170`）。
- 端点发现：host 启动时写 `%LOCALAPPDATA%\sticks3\host.json = {"url":…,"token":…}`（`state_host.cpp L669-685`），tray 读该文件（`tray.cpp L115-126`）。
- 桥接描述文件：`~/.local/share/sticks3-opencode/bridge.json`（`STICKS3_DESCRIPTOR_DIR` 可覆盖），随机 32 字节 token，`plugin/src/index.ts L48-62`。
- 构建：`build_tray.cmd`（`/std:c++20`，无 WinRT，链 user32/shell32/winhttp）；`build_state_host.cmd`（需 `%LOCALAPPDATA%\sticks3\winrt-headers`，`/std:c++17 /await`，链 windowsapp/advapi32/bcrypt + 引用 `firmware/…/s3p_state.c`）；两者均 vcvars 回退 2022 BuildTools→2022 Community→2019 BuildTools，找不到即 `VCVARS_NOT_FOUND` 退出 1。
- 现状 exe 体量（文档登记）：`tray.exe` 161,792 B（S4.7c）；`state_host.exe` 常驻无自退（生产）。
- 承诺交付件：`交付节点与完成定义.md L140/L143` 把「启动器源码」与「README 专用启动」列入交付集。
- 已批常驻义务：`契约补充 C05 L70/L71`（锁屏/休眠即取消并清理，即使未满 5 分钟）——当前未实现（见 §1 缺口 6）。
- 相关验收判据原文：`测试矩阵 L147/L148/L154/L156/L158/L164`（SEC-06 / UI-01 / REL-01 / REL-03 / REL-05 / E2E-01）。
- 安全 IPC 未验证：`可行性核查 L208`。
