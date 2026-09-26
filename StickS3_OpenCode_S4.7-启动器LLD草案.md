# S4.7 启动器 LLD 草案（host 侧拉起与生命周期）

> 日期：2026-09-26。状态：**未批准（草案）**——本文只整理事实与选项，**产品选择一律留待用户裁定（§4），不代批**。
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

| # | 决策 | 选项/建议 | 影响 |
| --- | --- | --- | --- |
| **D1** | 启动器**范围** | A Windows 侧拉起器 / B M08 WSL+OpenCode 专用启动 / C 两者 | 决定后续全部工作量（B/C 需 WSL 启动握手，HLD 仍标待定） |
| **D2** | **进程模型** | 独立 `launcher.exe`（推荐，贴合"独立常驻"裁定）/ tray 内嵌 / SCM 服务 | 与 `M4 §8 L137` 的"无日常管理员"冲突项需排除 |
| **D3** | **是否自启**、用何机制 | 不自启（手动）/ 登录启动项 / 任务计划 / 服务 | 仓库无依据，纯产品选择 |
| **D4** | **停止/重启语义** | launcher 持有终止 / 新增 `POST /v1/shutdown` / 仅单实例锁 | 需 ADR（契约 C10） |
| **D5** | **端口归属契约偏离** | 改实现符合 M1 契约 / 登记偏离并修订契约 | 触契约变更流程 |
| **D6** | 配置发现**规范顺序**与发行版确定 | 环境变量优先 + `wsl -l -q` 探测 / 保持硬编码 + 文档化 | 影响多机可移植性 |
| **D7** | 分离启动后的**日志去向** | 文件日志（路径/轮转）/ 保持 stdout / 事件日志 | 影响可诊断性 |
| **D8** | `host.json` 的 **0600 ACL** 与 **token 取值** | 补 ACL + 改复用 bridge token / 明确登记现状可接受 | 安全项，C02/C11 相关 |
| **D9** | **验收映射** | 复用 UI-01/SEC-06/REL-01 / 新增 launcher 专属用例 | 无现成 S4.7e 用例 |

## 5. 拟实现切片（**批准后**才动，仅为便于估工）
1. **Slice 0**：冻结 D1—D9（ADR + 契约更新）。
2. **Slice 1**：若 D1=A → `launcher.exe` 最小件：单实例 + 依序拉起 + 退出收尾 + 纯 ASCII `.cmd` 构建（沿用 vcvars 回退）。
3. **Slice 2**：进程监督（崩溃重启、退避）与日志（按 D7）。
4. **Slice 3**：按 D4 的停止语义落地；按 D5 处理端口契约。
5. **Slice 4**：HIL（含"关 host 后重开""端口被占""tray 与 host 各自退出"）+ 证据。
6. 若 D1 含 B/C：另起 M08 相关 LLD（WSL 启动握手仍未定，不在本草案内解决）。

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
