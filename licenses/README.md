# licenses/ — 交付许可文件归集

> REL-04 归集（2026-09-27）。本目录把交付所依赖的第三方许可**全文**随包提供（原文本位于 `.gitignore` 的 `managed_components/`，不入库，故此处显式归集）。
> 对照 [NOTICE.md](../NOTICE.md) §2 与 [SEC-04/REL-04 审查](../evidence/M6/2026-09-27-SEC-04隐私报告与REL-04审查.md) §3。

| 文件 | 组件 | 版本 | 许可 |
| --- | --- | --- | --- |
| `esp-opus-COPYING.txt` | `78/esp-opus`（libopus） | 1.0.5 | BSD-3-Clause |
| `esp-opus-LICENSE_PLEASE_READ.txt` | `78/esp-opus` | 1.0.5 | 贡献者许可声明（BSD/Clear BSD）+ IETF IPR 链接；**须随包保留全文** |
| `es8311-LICENSE.txt` | `espressif/es8311` | 1.0.0~1 | Apache-2.0 |

## 未包含（诚实登记）
- **ESP-IDF v5.5.4 顶层许可**（Apache-2.0）未复制入本目录；其**组件例外清单**见 [ESP-IDF-v5.5.4-component-exceptions.md](./ESP-IDF-v5.5.4-component-exceptions.md)（本机实测，2026-09-27）。交付时请连同 ESP-IDF 发行版一并提供。
- 本项目原创代码见仓库根 [LICENSE](../LICENSE)（MIT）。
- 插件 npm 依赖（37 条锁定）许可：见 `plugin/package-lock.json` 与 NOTICE §2.2，无 copyleft。
- 字体 `font8x8_basic.h`：Public Domain（见文件头）。
