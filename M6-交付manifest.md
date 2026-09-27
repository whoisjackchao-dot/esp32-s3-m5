# M6 交付 manifest（本地目录交付）

> 生成日期：**2026-09-27**。交付形态：**本地目录**（用户 2026-09-27 裁定）。
> 本文件是 **S-M6-2（可复现构建 + manifest）** 的产物，回答"交付的是哪个版本、能否据此重建"。
> 关联：[M6 计划与就绪核查](./M6-可复现完整本地交付-计划与就绪核查.md) · [USB 刷写清单](./tools/USB刷写清单.md) · 证据 [evidence/M6/2026-09-27-S-M6-2-可复现构建与manifest.md](./evidence/M6/2026-09-27-S-M6-2-可复现构建与manifest.md)。
> **本文件不关闭 M6**；M6 另有 NOTICE 残余 / 隐私报告(SEC-04) / REL-01—04 / 安装后真实 E2E 未闭合。

## 1. 版本标识

| 项 | 值 |
| --- | --- |
| **源码版本** | git tag `m6-candidate-20260927-3`（commit `6635576`；含 S4.4+ 双声提示） |
| 工作树 | **干净**（`git status --porcelain` 为空 → HEAD == 构建所用工作树） |
| 交付形态 | **本地目录**（未建仓 / 未 push / 未 Release；Q07 未授权） |
| 逐字节可复现 | **未声称、未批准**（见 §6） |

## 2. 构建环境（精确版本，2026-09-27 实测）

| 组件 | 工具 | 实测版本 | 备注 |
| --- | --- | --- | --- |
| 固件 | ESP-IDF | **v5.5.4-dirty** | README 原写 5.5.5；见 §7 漂移 |
| 固件 | xtensa-esp-elf-gcc | **14.2.0_20260121** | crosstool-NG esp-14.2.0_20260121 |
| 固件 | target / Python venv | **esp32s3** / `idf5.5_py3.14_env` | 经 `tools/idf-env.sh` 注入 |
| 主机 | MSVC cl | **19.29.30159.0**（toolset 14.29.30133） | VS2019 BuildTools `vcvars64` |
| 主机 | cppwinrt | modern headers `%LOCALAPPDATA%\sticks3\winrt-headers` | `/std:c++17 /await` |
| 插件 | Bun | **1.4.2** | README 原写 1.2.23；见 §7 |
| 插件 | Node.js | **v24.15.0** | README 原写 v24.19.0；见 §7 |
| 插件 | npm | **11.12.1** | |
| 插件 | `@opencode-ai/plugin` | **1.18.31** | 锁定，随 `package-lock.json` |
| 通用 | Git | (WSL) | |

## 3. 交付件与 SHA-256

### 3.1 固件产物（`flash-artifacts/`，已更新为无 `0x7F` 版本，2026-09-27）

| 文件 | 大小 | SHA-256 |
| --- | --- | --- |
| `bootloader.bin` | 20832 | `09e4c0e970f5bf08d48d63cbeb2853af1b9bd2d0e42279ed6680668127208b9c` |
| `partition-table.bin` | 3072 | `7f00b6c042a89b15b0cac534f82ed988caf29278ff5700b0c511eb1b5bb7c820` |
| `sticks3.bin` | 739520 | `44a38f513aad1c04805199273bae59a8fa0656412aa78f9ec275fd3248d7545e` |

> 2026-09-27 更新：加入 S4.4+ 双声提示后，于 tag `m6-candidate-20260927-3` **干净重建**（内嵌 `App version=m6-candidate-20260927-3`）并重刷。**观察**：bootloader 在不同构建间**哈希不同** → 佐证"逐字节可复现未声称"。

> 产物清单与哈希另有 `flash-artifacts/SHA256SUMS`（单一真相源）。分区/偏移/恢复见 `tools/USB刷写清单.md`。

### 3.2 主机可执行（`host/src/`，2026-09-27 重建）

| 文件 | SHA-256 |
| --- | --- |
| `launcher.exe` | `09beae3ee5a99e0fca689a68f279e51c63d6121498c8f3462cff4a09ff90f9e3` |
| `state_host.exe` | `663aa58e514a3b98238acc07c31256ab4d5ed6219e23cfee206d263569076cc2` |
| `tray.exe` | `36e09c53e60852472425e8a7685ef85d0736474010569bb1f1ddfafccf9ae01b` |
| `ble_client.exe` | `ec9ad6b9343cd0dbc8459bf39d0592aae12816ba7a7b087805b0cd3c7cbfd224` |

> 另有 16 个主机测试/工具 exe（`host/src/*_test.exe`、`asr_smoke.exe` 等），共 20 个 exe；全部经 §5.3 的 19 个 `build_*.cmd` 重建。

### 3.3 源码内容指纹（组件级，用于识别"是否同一份源码"）

| 组件 | 范围 | 聚合 SHA-256 |
| --- | --- | --- |
| 固件源码 | `firmware/sticks3/main/**`（`*.c`/`*.h`，排除生成字体表） | `643ea4910517b99aa326d721bf3bd322d770c4c65765e0c4a7385631087ea60a` |
| 主机源码 | `host/src/*.{c,h,cpp}` | `ba7e40bc50e629272ed6804f1ecd97fbf86301b0648c1d693c93daa7fa901faf` |
| 插件源码 | `plugin/{src,test}/**/*.ts` | `d14e771377694a92591a39f3f47aa9b1d7feafaf11d65dc6b006e09c83be30b0` |
| 契约 | `contracts/*.md` | `2093a14975d85ffaf72c04c9be7cbe6ef1dfb5ccf69e1c1efc85f9959d414e92` |
| 测试与 fixture | `tests/**`（`*.ts`/`*.json`/`*.md`） | `9374bcf97dc553ba9b9ee1002aab253a6f649b8e54b92b711bacbb9b5b14f107` |

> 聚合方式：对范围内文件 `sha256sum` 后按路径排序，再对结果整体取 SHA-256（可复算，命令见证据文件）。

### 3.4 依赖 / 配置锁定

| 文件 | SHA-256 |
| --- | --- |
| `firmware/sticks3/dependencies.lock` | `58eae5187498bcc335cc1bea2f93b36c2473316c799293279ed0544fe5c6b6bc` |
| `plugin/package-lock.json` | `0f95650f05706fbbdf696cc97a7ea7fb83632bff8a357154847f3a20949f358d` |
| `firmware/sticks3/sdkconfig` | `95099721270d4a3fd6e1d99dd7d6cc38b1fc283bbe11d06bc6cd6652bc91dc74` |

固件外部组件（`dependencies.lock`）：`78/esp-opus` 1.0.5、`espressif/es8311` 1.0.0~1、`idf`。

## 4. 交付件清单覆盖（对照 M6 §2 十项）

| # | 交付件 | manifest 对应 | 状态 |
| --- | --- | --- | --- |
| 1 | 固件源码 + 镜像 + 分区/刷写清单 | §3.1 / §3.3 / `tools/USB刷写清单.md` | 🟡 源/镜像/清单有；设备**未刷**（见 §6） |
| 2 | Windows 主机源码 + 运行包 | §3.2 / §3.3 | ✅ 源码 + 20 exe 重建 |
| 3 | 插件 + 启动器源码（含锁文件） | §3.3 / §3.4 | ✅ |
| 4 | 自动化测试 + fixture | §3.3（tests 指纹） | ✅ |
| 5 | 工具链 / 依赖精确锁定 | §2 / §3.4 | 🟡 版本已记；源/依赖未打 tag |
| 6 | 协议版本 / schema / 黄金向量 | `contracts/`（指纹 §3.3） | ✅ |
| 7 | 字体与图像许可来源 | 见 NOTICE | 🟡 `font8x8` PD；`¥` 自定义字形 |
| 8 | LICENSE / NOTICE / 第三方清单 | `LICENSE` / `NOTICE.md` | 🟡 **NOTICE 残余** |
| 9 | README：安装/配对/Key/启动/使用/退出/USB 恢复 | `README.md` | 🟡 部分 |
| 10 | **manifest + SHA-256** | **本文件** | ✅ 本次建立 |

## 5. 可复现构建步骤（本次实测通过，2026-09-27）

### 5.1 固件（WSL）

```bash
cd /mnt/c/Users/home/esp32-s3-m5/firmware/sticks3
source /mnt/c/Users/home/esp32-s3-m5/tools/idf-env.sh
idf.py -B ~/esp-build/m6-repro build     # 全新目录 clean 构建
```
结果：**exit 0**，1352/1352 目标；产物见 §3.1（构建目录必须放 WSL 原生盘）。

### 5.2 插件（WSL）

```bash
cd plugin && npm install
bun test                # 结果：111 pass / 1 skip / 0 fail（112 tests / 10 files）
./node_modules/.bin/tsc --noEmit   # 结果：exit 0
```

### 5.3 主机（Windows / MSVC）

逐文件构建脚本（`host/src/build_*.cmd`，各含 vcvars 回退 + 现代 cppwinrt 头）：
**19/19 脚本 exit 0**。
> `build_asr_transport.cmd` 需先起 mock-asr：`cd tests/tools/mock-asr && bun serve.ts 18765 0.0.0.0` → 测试 `32 checks / 0 failures`。
> `launcher/state_host/tray` 三者链接需其**不在运行**（否则 `LNK1104`）；本次先停后建再重启。

## 6. 效力边界与未闭合项（不得当达标）

| 项 | 说明 |
| --- | --- |
| **逐字节可复现** | **未声称**。DoD 要求逐字节可复现须另行批准；本次只证"按文档从源码构建出功能等价产物"。 |
| **设备固件** | `flash-artifacts/` 已更新为无 `0x7F` 版本，但**设备仍运行旧固件**；刷写需用户授权 + 物理操作（M6 §4 第 2 项）。 |
| **版本冻结** | 源码/依赖**未打 tag**；版本以 §1 git 提交 + §3 内容哈希标识。 |
| **源文件逐文件哈希** | 本 manifest 给组件级聚合指纹，未逐文件列全量清单（可后续生成 `SHA256SUMS.all`）。 |
| **NOTICE / 隐私报告** | NOTICE 残余、SEC-04 隐私报告**未做**（后续 S-M6）。 |
| **REL-01—REL-04 / 安装后真实 E2E** | **未执行**。 |

## 7. 已登记漂移（README 环境表与实际不符；本 manifest 以实测为准）

| 项 | README 原值 | 实测值 | 处置 |
| --- | --- | --- | --- |
| ESP-IDF | v5.5.5 | **v5.5.4-dirty** | 登记；README 已更正 |
| Bun | 1.2.23 | **1.4.2** | 登记；README 已更正 |
| Node.js | v24.19.0 | **v24.15.0** | 登记；README 已更正 |
| MSVC | 14.44.35207（VS2022） | **14.29.30133（VS2019 BuildTools）** | 登记；README 已更正 |

> 依据 dayu §9 防漂移：以实测为准，同步修订受影响文档。
