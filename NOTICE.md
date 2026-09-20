# NOTICE — 第三方组件与许可

> 状态：**待逐项审查**。本文件是依赖/许可登记表，不是已完成审查的结论。
> 依据 [可行性核查 §5.4](../StickS3_OpenCode_可行性核查.md)：根 MIT 不覆盖第三方原有许可；Opus、LVGL、Espressif 组件、字体需按实际文件保留授权。

## 1. 本项目原创代码

| 范围 | 许可 | 状态 |
| --- | --- | --- |
| 本项目原创源码（固件/主机/插件/测试） | MIT | 已批准（2026-09-15，见可行性核查 §8 B07） |

## 2. 计划引入的第三方组件（引入前须逐项完成许可审查）

### 2.1 已实际安装（plugin/，2026-09-20，`package-lock.json` 锁定）

| 组件 | 版本 | 许可 | 状态 |
| --- | --- | --- | --- |
| `@opencode-ai/plugin` | 1.18.31 | MIT | ✅ 宽松 |
| `@opencode-ai/sdk` | 1.18.31 | MIT | ✅ 宽松 |
| `typescript` | 5.9.3 | Apache-2.0 | ✅ 宽松 |
| `@types/bun` | 1.4.2 | MIT | ✅ 宽松 |

间接依赖（30 个包）许可扫描结果：全部为 MIT / Apache-2.0 / ISC / BSD-3-Clause / (AFL-2.1 OR BSD-3-Clause)，**无 copyleft**。
安装方式说明：`bun install` 在本机公司代理下会卡死（见 README「已知限制」），故改用 `npm install` 生成 `package-lock.json` 作为锁文件；`bun test` 可正常使用该 `node_modules`。

### 2.2 待引入（引入前逐项审查）

| 组件 | 用途 | 计划版本 | 许可 | 审查状态 |
| --- | --- | --- | --- | --- |
| ESP-IDF | 固件框架 | 5.5（release/v5.5） | Apache-2.0（含组件例外） | NOT_REVIEWED |
| Opus（libopus / esp-opus） | 音频编码 | 待锁定 | BSD 系 | NOT_REVIEWED |
| LVGL | 屏幕 UI | 待锁定 | MIT | NOT_REVIEWED |
| M5Unified / M5GFX / M5PM1 | 板级驱动参考 | 待锁定 | MIT（待确认） | NOT_REVIEWED |
| 中文字体 | 状态屏显示 | 待锁定 | 待查（须为可再分发许可） | NOT_REVIEWED |

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
