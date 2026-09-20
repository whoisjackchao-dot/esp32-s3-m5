# StickS3 × OpenCode

> 一个蓝牙语音输入器 + OpenCode 状态屏：按住 StickS3 正面键讲话，转写结果**只追加**到正确的 OpenCode 草稿末尾，由用户自己按 Enter 发送。
>
> **当前状态：开发准备阶段。没有任何产品里程碑（M1—M6）已开始或验收。** 设计文档见本目录 `.md` 文件；索引见 [StickS3_OpenCode_文档索引](./StickS3_OpenCode_文档索引.md)。

## 1. 仓库结构

```
.
├── (设计文档 .md)              # 需求/HLD/契约/测试/交付/可行性/硬件
├── contracts/                  # 各里程碑开工前的契约冻结与 ADR
│   ├── M1-桥接契约冻结.md        # ✅ 已冻结（M1 开工阻塞已解除）
│   ├── M2-BLE线协议冻结清单.md   # ⏳ 清单就绪，取值待设计（M2 前）
│   └── M3-ASR验收冻结清单.md     # ⏳ 清单就绪，语料待冻结（M3 前）
├── plugin/                     # M09 安全草稿插件 + M10 状态归约（Bun / TypeScript）
├── host/                       # M04—M08 Windows 主机（C++20 / MSVC / Win32+WinRT+WinHTTP）
├── firmware/                   # M01—M03 StickS3 固件（ESP-IDF 5.5 / esp32s3）
├── tests/                      # 跨模块测试、fixtures、受控测试服务
├── LICENSE                     # MIT
└── NOTICE.md                   # 第三方组件与许可登记（待逐项审查）
```

> 设计文档保持在仓库根目录，不移动，避免破坏既有交叉链接。

## 2. 开发环境（已核实）

| 模块 | 工具 | 已装版本 | 状态 |
| --- | --- | --- | --- |
| 插件 | Bun | 1.2.23 | ✅ |
| 插件 | Node.js | v24.19.0 | ✅ |
| 插件 | TypeScript | 5.9.x（devDependency） | ⏳ 待 `bun install` |
| 插件 | `@opencode-ai/plugin` | 1.18.31（匹配已装 CLI） | ⏳ 待 `bun install` |
| 主机 | MSVC (VS BuildTools 2022) | 14.44.35207 | ✅ |
| 主机 | CMake / Ninja | 随 VS BuildTools | ✅ |
| 固件 | ESP-IDF | **v5.5.5**（release/v5.5） | ✅ |
| 固件 | xtensa-esp-elf | 14.2.0_20260121 | ✅ |
| 固件 | 目标芯片 | esp32s3 | ✅ |
| 固件 | CMake / Ninja | 4.4.3 / 1.13.2（装在 ESP-IDF venv 内） | ✅ |
| 通用 | Git | 2.53.0 (WSL) | ✅ |

**注意**：`tsc` 未全局安装（用 `bunx tsc` 或 `bun run typecheck`）；WSL 侧无 gcc/cmake/make（主机端在 Windows 用 MSVC 构建，固件端用 ESP-IDF 自带工具链）。

## 3. 构建与测试

### 插件（WSL）

```bash
cd plugin
npm install        # 见「已知限制」：bun install 在本机代理下会卡死
bun test           # Selective TDD 单元测试
./node_modules/.bin/tsc --noEmit   # 类型检查
```

> 已验证：`bun test` 4 pass / 0 fail；`tsc --noEmit` exit 0（2026-09-20）。

### 主机（Windows）

```powershell
cmake -S host -B host/build -G "Visual Studio 17 2022" -A x64
cmake --build host/build --config Release
```

> 已验证：configure/build/run 全部 exit 0，`toolchain_check.exe` 输出 `win32 link ok`（2026-09-20）。

### 固件（WSL）

```bash
source tools/idf-env.sh      # 包装脚本：python3→3.12 + 用户态 libusb + source ESP-IDF export.sh
cd firmware/toolchain-check
idf.py -B ~/esp-build/toolchain-check set-target esp32s3
idf.py -B ~/esp-build/toolchain-check build
```

> **构建目录必须放在 WSL 原生盘**（如 `~/esp-build/...`）。放在 `/mnt/c` 时实测 23 分钟仅完成 338/1078 个目标；WSL 原生约 45 秒完成 1078/1078，约 30 倍差距。

> **不要**直接 `source ~/esp/esp-idf/export.sh`：本机系统 python3 是 3.14，会让 ESP-IDF 找不到 venv 并**静默失败**（返回 0 但 idf.py 不在 PATH）。用 `tools/idf-env.sh`。

> 已验证：ESP-IDF v5.5.5 + esp32s3 构建成功，产物 `sticks3_toolchain_check.bin` 199,088 字节（2026-09-20）。

## 3.1 已知限制

| 限制 | 现象 | 规避 |
| --- | --- | --- |
| `bun install` 走不通公司代理 | 卡在 `Resolving dependencies`，CPU 近 0，无产出（WSL 原生目录同样复现） | 用 `npm install`；`bun test` 仍可用 npm 装出的 `node_modules` |
| ESP-IDF 需要 Python ≤3.13 | `~/.espressif/python_env` 缺失，`export.sh` 静默失败（返回 0 但 `idf.py` 不在 PATH） | 用 `tools/idf-env.sh`（固定 python3→3.12 + 显式 `IDF_PYTHON_ENV_PATH`） |
| openocd 需要系统 `libusb-1.0.so.0` | 系统无该库且 `sudo` 需密码；ESP-IDF 工具校验失败会**删除** openocd 目录，导致 `export.sh` 无法激活 | 已用 `apt-get download libusb-1.0-0` + `dpkg-deb -x` 解包到 `~/.local/share/sticks3-esp-libs/`，由 `tools/idf-env.sh` 注入 `LD_LIBRARY_PATH`。**如需长期稳定，建议 `sudo apt-get install -y libusb-1.0-0 cmake ninja-build`** |
| WSL 无 `cmake`/`ninja` | ESP-IDF 在 Linux 要求系统提供；`idf.py` 报 `"cmake" must be available on the PATH` | 已装入 ESP-IDF venv（`pip install cmake ninja`），`export.sh` 自动加入 PATH |
| 在 `/mnt/c` 上构建固件极慢 | 23 分钟仅 338/1078 个目标 | **构建目录放 WSL 原生盘**：`idf.py -B ~/esp-build/xxx build`（约 45 秒完成） |
| `/mnt/c` 上 `npm install` 较慢 | 约 2 分钟（WSL 原生约 36 秒） | 可接受；如需更快可在 WSL 原生目录建开发副本 |
| OpenCode CLI 为 1.18.31 | 设计文档原基线为 1.18.30 | 插件已按 1.18.31 安装并锁定（见 M1 契约第 7 节） |

## 4. 安全边界（不可违反）

- 桥接**只追加草稿，绝不提交**；不提供审批、Shell、任意文件、abort、清空草稿接口。
- 智谱 API Key 只存 Windows 凭据管理器；不进 WSL、固件、命令行、日志、git。
- 桥接仅监听 `127.0.0.1`。
- 设备不保存待重试录音；失败音频最多在主机内存保留 5 分钟。
- 不擅自 `git commit` / `git push`；发布须另行授权（Q07）。

## 5. 里程碑（当前全部未开始）

M0（部分关闭：产品决策已批，工程契约补齐中）→ M1 安全草稿通道 → M2 真实录音 → M3 真实 ASR → M4 状态屏与托盘 → M5 候选版本验证 → M6 本地完整交付。

详见 [交付节点与完成定义](./StickS3_OpenCode_交付节点与完成定义.md)。
