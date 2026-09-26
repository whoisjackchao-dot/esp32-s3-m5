# M4 STATE 快照契约冻结（`STATE 0x12` 载荷）

> 状态：**已批准 v1.1（2026-09-27，用户确认 #3）**；前版 v1.0（2026-09-25，用户"批准"）。
> v1.1 变更：固定段 23→**31B**，新增 `tokens.reasoning`、`tokens.cacheRead`（各 u32），`snapshotVersion` 1→**2**；MTU 阈值相应调整。ADR：[ADR-2026-09-27-STATE载荷v1.1扩用量字段](./ADR-2026-09-27-STATE载荷v1.1扩用量字段.md)。
> 范围：`STATE(0x12)` 控制消息的**载荷布局**。M2 契约已冻结消息集（`0x10` READY / `0x11` HEARTBEAT / `0x12` STATE）与"快照版本 u8 + 精简状态位图 u8"骨架；本文件冻结 **M4 的完整载荷**。
> 依据：[M4 开发计划 §7](./../StickS3_OpenCode_M4开发计划.md)、[M2 BLE 线协议冻结](./M2-BLE线协议冻结.md)、[HLD §7.4 BR-03](../StickS3_OpenCode_功能模块HLD.md)、[契约 C08](../StickS3_OpenCode_契约与演进设计补充.md)。
> **不改变** M2 已冻结的线格式与黄金向量；仅细化 `0x12` 载荷。

## 1. 字段布局（小端 LE）

| 偏移 | 字段 | 类型 | 说明 |
| --- | --- | --- | --- |
| 0 | msgType | u8 = 0x12 | M2 已冻结 |
| 1 | snapshotVersion | u8 = **2** | 本载荷版本（v1.1）；不匹配则拒绝 |
| 2 | status | u8 | 0=unknown, 1=idle, 2=busy, 3=retry, 4=waiting, 5=error；其他值=unknown |
| 3 | flags | u8 | bit0 canAppend, bit1 expired, bit2 hasProject, bit3 hasTitle, bit4 hasModel, bit5 hasUsage, bit6 toolFailed, bit7 childBusy |
| 4..7 | snapshotRevision | u32 | 旧 revision 不回滚 |
| 8 | toolCount | u8 | 运行工具数；>255 截断为 255（并置 toolFailed 由 host 决定） |
| 9 | todoCompleted | u8 | 与 todoTotal 同为 0 表示无待办 |
| 10 | todoTotal | u8 | |
| 11..14 | tokens.input | u32 | `0xFFFFFFFF` = unknown；`0xFFFFFFFE` = not_reported |
| 15..18 | tokens.output | u32 | 同上 |
| 19..22 | tokens.costMilli | u32 | 费用×1000；哨兵同上 |
| 23..26 | tokens.reasoning | u32 | reasoning token 数；哨兵同上（**v1.1 新增**） |
| 27..30 | tokens.cacheRead | u32 | cache 读取 token 数；哨兵同上（**v1.1 新增**） |
| 31.. | 变长文本区 | TLV | 见 §2 |

固定段 = **31 字节**（v1.1；v1.0 为 23 字节）。

## 2. 变长文本区（TLV）

每段：`[tag u8][len u8][utf8 bytes]`（len ≤ 32）。

| tag | 字段 | 对应 flags |
| --- | --- | --- |
| 0x01 | 项目显示名 | hasProject |
| 0x02 | 会话 title | hasTitle |
| 0x03 | 模型名 | hasModel |

- 仅当 flags 位为 1 时出现；顺序按 tag 升序。
- **截断**：按 UTF-8 **字素边界**截到 ≤32 字节；截断时不加省略号（设备端渲染决定占位）。
- 文本区最坏 = 3×(1+1+32) = **102 字节**；载荷最坏 = **133 字节**（v1.1 固定段 31 + 102；v1.0 为 125）。

## 3. MTU 适配

- MTU ≥ **136** → 单个 ATT 通知载荷 ≥133，**单通知**发送完整快照（v1.1 最坏 133B；v1.0 为 125B/阈值 128）。
- MTU **65..135** → 单通知放不下完整快照：**仅发固定段（31B）+ 省略文本区**，并清 hasProject/hasTitle/hasModel。
- MTU < 65 → 违反 M2 §2 硬底线（设备拒绝录音），不发快照。

## 4. 不变量

- 未知/未报告**不得伪装为 0**（哨兵区分）。
- 旧 `snapshotRevision` 不覆盖新值（设备侧同样丢弃）。
- 文本区不含工具参数、源码、正文；仅显示名/title/模型名的有界子串。
- 本载荷不携带聊天正文、ASR 费用、账号余额。

## 5. 兼容与变更控制

- 消息集与 `0x10/0x11` 不受影响；`0x12` 旧骨架（version+bitmap）**被本载荷取代**（双方同步升级）。
- 任何字段增删/语义变更需 **ADR** + 冻结新版本 + 黄金向量回归。

### 变更记录
- **v1.1（2026-09-27）**：固定段 23→31B；新增 `tokens.reasoning`(23..26)、`tokens.cacheRead`(27..30)；`snapshotVersion` 1→2；MTU 阈值 128→136。ADR：[ADR-2026-09-27](./ADR-2026-09-27-STATE载荷v1.1扩用量字段.md)。两端同步升级；v1 解码器遇 v2 拒绝。
- v1.0（2026-09-25）：初版冻结（固定段 23B）。
- 黄金向量：`contracts/../tests/fixtures/` 提供至少 3 条（空闲/忙碌+工具失败/全字段），供两端独立校验。

## 6. 效力边界

- 本文件为**已批准契约**；实现须与之逐字节一致。
- 尚无实现时不得声称 S4.2 达标；host 发送端与固件渲染的实机验证另行记录。
