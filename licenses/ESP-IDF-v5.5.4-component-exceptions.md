# ESP-IDF v5.5.4 组件许可例外清单

> REL-04 / NOTICE §5 缺口补全（2026-09-27）。ESP-IDF 顶层为 **Apache-2.0**，但其树内**部分组件**采用其他许可。下表由**本机 ESP-IDF 树实测扫描**（`find … LICENSE*/COPYING*` + 读文件头）得到。
> **边界**：本表是 ESP-IDF 发行树的**例外清单（超集）**；实际**链接进本固件**的是其中一个子集。完整许可全文随 ESP-IDF 发行版提供（本目录不复制 ESP-IDF 全树）。

## 顶层
| 文件 | 许可 |
| --- | --- |
| `esp-idf/LICENSE` | Apache-2.0 |

## 组件例外（非 Apache-2.0）
| 组件 / 路径 | 许可 | 备注 |
| --- | --- | --- |
| `components/esptool_py/LICENSE` | **GPL-2.0** | **仅主机刷写工具**，不链接进固件应用；不构成 app 的 copyleft |
| `components/mbedtls/mbedtls/LICENSE` | **Apache-2.0 OR GPL-2.0-or-later**（双许可） | 本项目取 **Apache-2.0** |
| `components/bootloader/.../micro-ecc/LICENSE.txt` | BSD（2-clause） | 文件头 `Redistribution…`，无 "Neither the name" |
| `components/bt/common/tinycrypt/LICENSE` | BSD-3-Clause | 文件头含 "Neither the name" |
| `components/bt/host/nimble/nimble/ext/tinycrypt/LICENSE` | BSD-3-Clause | 同上 |
| `components/console/argtable3/LICENSE` | BSD-3-Clause | 文件头含 "Neither the name" |
| `components/console/linenoise/LICENSE` | BSD（2-clause） | 文件头 `Redistribution…` |
| `components/http_parser/LICENSE.txt` | MIT（NGINX 同源条款） | 文件头 `Permission is hereby granted` |
| `components/json/cJSON/LICENSE` | MIT | 文件头 `Permission is hereby granted` |
| `components/lwip/lwip/COPYING` | BSD-3-Clause | lwIP（SICS） |
| `components/openthread/openthread/LICENSE` | BSD-3-Clause | 文件头含 "Neither the name" |
| `components/protobuf-c/protobuf-c/LICENSE` | BSD（2-clause） | 文件头 `Redistribution…` |
| `components/wpa_supplicant/COPYING` | BSD-3-Clause | wpa_supplicant/hostapd |
| `components/freertos/FreeRTOS-Kernel/LICENSE.md` | MIT | |
| `components/mqtt/esp-mqtt/LICENSE` | Apache-2.0 | |
| `components/spiffs/spiffs/LICENSE` | MIT | |
| `components/unity/unity/LICENSE.txt` | MIT | 测试框架 |
| `components/newlib/COPYING.NEWLIB` / `COPYING.picolibc` | newlib 系列宽松许可（BSD 风格 + 各贡献者） | 全文随文件 |
| `tools/catch/LICENSE.txt` | Boost Software License 1.0 | 测试用 |
| `components/bt/controller/lib_esp32*`, `esp_coex/esp_phy/esp_wifi/lib`, `openthread/lib`, `bt/esp_ble_mesh/lib` | Apache-2.0 | 预编译库 |
| `components/lwip/lwip/contrib/apps/LwipMibCompiler/SharpSnmpLib/license.txt` | LGPL-2.1 | **contrib 工具**，非固件运行时 |
| `components/openthread/openthread/third_party/jlink/SEGGER_RTT_V640/License.txt` | SEGGER 许可 | 调试 RTT |

## 结论
- **链接进固件的第三方许可均为 permissive（Apache-2.0 / MIT / BSD / MIT 风格）**；**无 copyleft 进入 app**。
- **GPL-2.0（esptool_py）** = 主机工具；**LGPL（lwip contrib）** = 非运行时工具；**SEGGER** = 调试；均**不构成固件应用许可污染**。
- **边界**：本表为**发行树例外超集**；"实际链接子集"未逐符号核对（可后续用 `idf.py size-components` 精确化）。

> 依据命令：`find esp-idf -type f \( -iname "LICENSE*" -o -iname "COPYING*" \) | sort` + `head`/`grep` 文件头。生成日期 2026-09-27。
