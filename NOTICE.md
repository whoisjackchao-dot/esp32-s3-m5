# NOTICE — 第三方组件与许可

> 状态：**审查已按实际交付状态推进（2026-09-26）**——§2 区分「已引入 / 未采用 / 仅参考」，各条许可均**实读校验**（附依据）。
> 依据 [可行性核查 §5.4](./StickS3_OpenCode_可行性核查.md)：根 MIT 不覆盖第三方原有许可；第三方组件与字体须按实际文件保留授权。

## 1. 本项目原创代码

| 范围 | 许可 | 状态 |
| --- | --- | --- |
| 本项目原创源码（固件/主机/插件/测试） | MIT | 已批准（2026-09-15，见可行性核查 §8 B07） |

## 2. 第三方组件（按实际交付状态核）

### 2.1 已引入 — 固件（`firmware/sticks3`）

| 组件 | 版本 | 许可 | 依据（实读） |
| --- | --- | --- | --- |
| ESP-IDF | **v5.5.4** | Apache-2.0（含组件例外） | `idf.py --version` → `ESP-IDF v5.5.4-dirty` |
| `78/esp-opus`（IDF 组件） | `^1.0.5`（已装 `managed_components/78__esp-opus`） | **BSD-3-Clause**（libopus `COPYING`）；另含 `LICENSE_PLEASE_READ.txt`（Opus 专利/使用须知，**须随交付包保留**） | 实读 `COPYING` 首行 `Copyright 2001-2023 Xiph.Org, Skype Limited, Octasic, …` |
| `espressif/es8311`（IDF 组件） | 已装 `managed_components/espressif__es8311` | **Apache-2.0** | 实读其 `LICENSE` 首行 `Apache License Version 2.0` |
| `font8x8_basic.h`（状态屏字体） | 随源码 | **Public Domain** | 实读文件头 `Author: Daniel Hepper … License: Public Domain` |

### 2.2 已引入 — 插件（`plugin/`，`package-lock.json` 锁定）

| 组件 | 版本 | 许可 | 状态 |
| --- | --- | --- | --- |
| `@opencode-ai/plugin` | 1.18.31 | MIT | ✅ 宽松 |
| `@opencode-ai/sdk` | 1.18.31 | MIT | ✅ 宽松 |
| `typescript` | 5.9.3 | Apache-2.0 | ✅ 宽松 |
| `@types/bun` | 1.4.2 | MIT | ✅ 宽松 |

间接依赖许可扫描：全部 MIT / Apache-2.0 / ISC / BSD-3-Clause / (AFL-2.1 OR BSD-3-Clause)，**无 copyleft**（`package-lock.json` 当前 37 条 `resolved`）。
安装方式说明：`bun install` 在本机公司代理下会卡死（见 README「已知限制」），故改用 `npm install` 生成锁文件；`bun test` 可正常使用该 `node_modules`。

### 2.3 未采用（曾评估，**不进入交付**）

| 组件 | 原因 |
| --- | --- |
| **LVGL** | 未采用——状态屏为**自绘** `display.c`（源码内无 lvgl 引用，已核） |
| **中文字体**（Noto Sans SC / OFL 等） | **未引入**——用户裁定界面保持**英文**（中文暂缓；相关实现曾建立后回滚，不在交付范围内） |

### 2.4 仅参考（借鉴寄存器语义/设计，**不继承代码**）

| 组件 | 许可 | 用法 |
| --- | --- | --- |
| M5Unified / M5GFX / M5PM1 | MIT（M5Stack 官方仓库声明） | 板级驱动**参考**：`pm1.c` 的 M5PM1 寄存器位语义与官方 PinMap；实现为本项目自写 |

## 3. 参考项目（仅借鉴设计，不继承代码）

| 项目 | 提交 | 许可 | 使用方式 |
| --- | --- | --- | --- |
| 78/voicestick | `e865d68c…` | 根 MIT | 仅参考；不继承其 `sm_bonding=0` 等不安全配置 |
| zoe-huang1/sticks3-voice-input | `166e7b30…` | 根 MIT | 仅参考；不继承 HID/OTA |
| sheepxux/Taskhub-for-StickS3 | `ecef8e05…` | 根 MIT | 仅参考状态信息组织（其通信用 Wi-Fi） |

## 4. 规则

- 每个依赖在**引入前**登记精确版本、许可类型与来源，并写入锁文件。
- 不可再分发许可的字体/素材不得进入交付包。
- 发布前执行 REL-04：自动检查产物哈希与来源，人工复核许可与敏感内容。
- 不擅自向 GitHub 推送（Q07：先本地完整交付）。

## 5. 仍未完成（诚实登记）

- ESP-IDF 的**组件例外清单**（Apache-2.0 之外部分组件的附加条款）**未逐组件复核**；`78/esp-opus` 的 `LICENSE_PLEASE_READ.txt` **全文未逐条审阅**。
- 交付包内**许可文件的归集方式**（是否随包附 `COPYING`/`LICENSE` 副本）**未定**——属 M6 打包（REL-04）项。
- M5Unified/M5GFX 的许可依据为**官方仓库声明（MIT）**，本仓未留许可副本。
