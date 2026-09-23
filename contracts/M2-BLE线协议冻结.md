# M2 BLE 线协议冻结（StickS3 ↔ Windows 主机）

> 状态：**v1.1**（2026-09-23）。v1.0 于 2026-09-21 冻结；v1.1 仅澄清 BEGIN.frameCount 的
> "预告上限"语义与 END 提前结束裁决（线格式未变，黄金向量不变）。依据
> [C07](../StickS3_OpenCode_契约与演进设计补充.md)、[HLD §10](../StickS3_OpenCode_功能模块HLD.md)、
> [测试矩阵 §4](../StickS3_OpenCode_测试用例与追踪矩阵.md)。
> 本文件解除 C07 开工闸门：UUID/帧布局/字节序/限额/摘要/尾帧超时全部给出可测试取值。
> 变更须走：改版本 → 更新两端解析与黄金向量 → 契约回归（C07 末条）。

## 1. GATT 布局

| 项 | 值 |
| --- | --- |
| 服务 UUID | `22587f80-334a-53d6-beb8-8a6ace31af07` |
| Audio Notify（设备→主机） | `54d5e361-d549-55cc-ab98-66ebc8afe4f3`，Notify，需加密 |
| Control Write（主机→设备） | `14be07f7-5cd5-5d0d-b56c-ecbbc6b162c0`，Write Without Response，需加密 |
| Device Event（设备→主机） | `a1d7042f-43a0-53b5-ad2f-a1b3ae57c79c`，Notify，需加密 |

- 连接参数：**单连接**（MAX_CONNECTIONS=1）、LE Secure Connections、Bond=1、MITM=0（Just Works 配对 + 加密）。
- 配对由物理确认（BLE-03）；旧 bond 不被同名陌生设备替换（BLE-01/02）。

## 2. MTU 策略（C07 第 4 条）

- 首选 MTU **247**；**MTU < 65 时显式拒绝录音开始**（BEGIN 失败 + ERROR 事件），**不得静默截断**。
- 依据：Audio 分片头 10 字节（§3），MTU65 → 单通知净载荷 55 字节；20kbit/s 60ms 帧约 150 字节
  需 3 片以内，30 秒 500 帧约 ≤1500 通知，时延预算内。MTU23（净载荷 20）需 17 片/帧，
  不可接受，故拒绝而非降级。

## 3. 包格式（Audio Notify，统一**小端**）

### 3.1 包头（所有包共有，2 字节）

| 偏移 | 字段 | 宽度 | 值 |
| --- | --- | --- | --- |
| 0 | packetType | u8 | 见 §3.2 |
| 1 | protocolVersion | u8 | 1 |

### 3.2 包类型与载荷

**BEGIN（0x01）** — 12 字节

| 偏移 | 字段 | 宽度 | 说明 |
| --- | --- | --- | --- |
| 2 | recordingID | u32 | 设备生成，单调递增；重启回绕允许（bootID 隔离由重连握手承担） |
| 6 | frameCount | u16 | **预告上限**（30s/60ms=500）；实际帧数以 `END.totalFrames` 为准，且须 ≤ 本值（§4） |
| 8 | sampleRate | u16 | 16000 |
| 10 | frameMs | u8 | 60 |
| 11 | reserved | u8 | 0 |

**AUDIO（0x02）** — 10 字节头 + 分片载荷

| 偏移 | 字段 | 宽度 | 说明 |
| --- | --- | --- | --- |
| 2 | recordingID | u32 | |
| 6 | frameSeq | u32 | 0 起，单调递增，**u32 无回绕歧义**（500 帧远小于 2³²） |
| 10 | fragIndex | u8 | 0 起 |
| 11 | fragCount | u8 | 本帧总分片数（≥1） |
| 12 | fragLen | u16 | 本片 Opus 字节数（不含头） |
| 14 | payload | fragLen | Opus 载荷片段，按 (frameSeq, fragIndex) 顺序重组 |

**END（0x03）** — 16 字节

| 偏移 | 字段 | 宽度 | 说明 |
| --- | --- | --- | --- |
| 2 | recordingID | u32 | |
| 6 | totalFrames | u32 | 实际帧数 |
| 10 | totalSamples | u32 | 有效样本总数（16kHz；<4800 由主机按过短拒绝，TX-18） |
| 14 | summary | u32 | FNV-1a(全部 Opus 载荷字节, seed=0x811c9dc5)，**主机独立计算黄金向量** |

**DEVICE_CANCEL（0x04，Device Event 特征）** — 6 字节：type, ver, recordingID

**ERROR（0x05，Device Event 特征）** — 8 字节：type, ver, recordingID, errorCode(u16)
errorCode：1=Opus 编码失败，2=缓冲耗尽，3=麦克风初始化失败，4=协议 violation

**HOST_STATE/READY/HEARTBEAT（Control Write，主机→设备）** — 载荷首字节 msgType：
0x10=READY{protocolVersion u8, mtu u16}；0x11=HEARTBEAT{seq u8}；0x12=STATE{快照版本 u8 + 精简状态位图 u8}
（状态位图字段在 M4 冻结；M2 只要求 READY/HEARTBEAT 可往返）

## 4. 会话与重组规则

| 规则 | 冻结值 | 用例 |
| --- | --- | --- |
| BEGIN → AUDIO → END 顺序；END 先到 = 协议 violation | ERROR(4)，丢弃整段 | BLE-10 |
| `END.totalFrames` vs BEGIN 预告 | END 为**权威实际帧数**，须 **≤** 预告；`> 预告` 或已收 `frameSeq ≥ 声明` → 整段失败；`< 预告`（松键提前结束）= 合法，但须收齐 `[0, 声明)` | BLE-12 |
| 尾帧等待 `FRAME_DRAIN_TIMEOUT` | **2000 ms**，起点=END 收到时刻 | BLE-10/11 |
| 重复 (frameSeq,fragIndex) | 忽略（仅记一次） | BLE-07 |
| 同 ID 同序号内容冲突（fragLen 或 payload 前 8 字节不同） | 整段失败 ERROR | BLE-08 |
| 缺片（FRAME_DRAIN_TIMEOUT 内未集齐） | 整段失败，无 ASR | BLE-06/11 |
| frameSeq > 宣告 frameCount-1 | 协议 violation，整段失败 | BLE-13 |
| recordingID 与 BEGIN 不符的迟到包 | 丢弃并计数 | BLE-14 |
| 重组缓冲上限 | 单 recording 256 KiB（契约已批）；超限 ERROR(2) | BLE-15 |
| 有效样本 | END.totalSamples 准；<4800 主机拒 ASR | TX-18 |

## 5. 限额汇总

30s 硬停（TX-02）；单段 Opus ≤256KiB；RECEIPT/重组缓冲如上；心跳 10s（BLE-API-05）；
15s 无有效状态过期。意外断线自动重连**已绑定**设备，不重放录音/ASR（Q05）。

## 6. 黄金向量要求（TEST-API-02，独立计算）

最小集（十六进制，手推期望字节，不从待测 serializer 生成）：
1. BEGIN 最小包（recordingID=1, frameCount=500）
2. AUDIO 单片完整帧（150B 载荷，MTU247 单通知）
3. AUDIO 三片帧（MTU65 边界）
4. END 含 summary
5. 非法：version=2、frameSeq 越界、零片数、summary 错
6. 大端变体（验证字节序判定拒绝）
7. 500 帧边界：frameSeq=499 与 500（后者越界拒绝）

## 7. 与上游差异（不继承）

不继承 zoe 8 位序号（30s500 帧会回绕）、voicestick `sm_bonding=0`（本项目强制 bond+加密）、
两者均无 END 摘要——本协议补齐。无 OTA/HID（BLE-18）。

---

## 变更记录

### v1.1（2026-09-23）：提前结束的权威帧数裁决

**背景**：v1.0 §3.2 已写"松开提前结束以 END 为准"，但主机端 `s3p_reasm_end` 实现要求
`END.totalFrames == BEGIN.frameCount`（严格相等），BLE-12 用例亦按"不符即失败"断言——
契约与实现/用例冲突（"按下录音/松开结束"是 Q06 已批准的 M3 UX，5/15s 短录音依赖提前结束）。

**裁决（用户 2026-09-23 确认，采纳建议）**：

1. `BEGIN.frameCount` = **预告上限**（非等值约定）。
2. `END.totalFrames` = **权威实际帧数**，须 ≤ 预告。
3. 三条例外一律显式失败，保留原"反静默截短"意图：
   - `END.totalFrames > 预告` → 失败（不得声多）；
   - 已收 `frameSeq ≥ END.totalFrames` → 失败（声明与实际不一致）；
   - `END.totalFrames < 预告` 但 `[0, END.totalFrames)` 有缺片 → 不 COMPLETE（转尾帧窗口，超时失败）。
4. **线格式不变**：BEGIN/AUDIO/END 字节布局与黄金向量（§6）不受影响。

**落地**：`host/src/s3p_reasm.c` 的 `!=` 判据改为 `>` + 越界/缺片检查；`host/src/s3p_reasm_test.c`
新增 3 条提前结束用例（`816 checks, 0 failures`）；`firmware/.../audio_session.c` 松开时以实际
帧数发 END；`firmware/.../button.c` 实现 BtnA(GPIO11) 按住/松开。BLE-12 用例语义同步见测试矩阵 §4。

---

## 附：测试专用钩子登记（2026-09-22）

**事项**：Control Write 特征新增 `msgType=0x7F`，语义为「设备主动 terminate 链路」。

**原因**：Q05「重连不重放」需要**设备可观测的真实断链**。Windows 会缓存 ACL，`BleakClient.close()` 不真正拆链（详见 `evidence/M2/2026-09-22-M2演示执行记录.md` §6）。故以设备主动断开作为确定性触发。

**性质与边界**：
- `0x7F` 是**保留值，不属于本契约的正式消息集**（正式集：`0x10` READY / `0x11` HEARTBEAT / `0x12` STATE）。
- 该钩子**不影响** Q05 行为本身；Q05 的通过取决于「断线时终止在途会话、重连不投递旧帧」，与钩子无关。
- 验证固件 SHA `9d1cde757bd78eb68f934a915c6d975a51e8d3cc9107010f53a79b87afac25f7` 含此钩子。

**待决（交付前须由用户裁定）**：
1. **移除**钩子（交付固件更干净；代价：Q05 复测需另建含钩子的测试固件，届时"已验证固件 ≠ 交付固件"）；
2. 或**保留并接受**为测试脚手架（则"已验证固件 = 交付固件"成立，但交付固件多一个非契约控制消息）。

**建议**：倾向 (2) 保留并登记——因为 Q05 的验证产物与交付产物一致，可追溯性更强；且 `0x7F` 为保留值，正常主机不会发送，实际风险为零。**但这是产品语义决定，须用户批准，不得由实施者代批。**

**裁定（2026-09-22，用户确认）**：采用方案 (2)——**保留** `0x7F` 测试钩子为已接受的测试脚手架。交付固件即含此钩子，与 Q05 验证固件（SHA `9d1cde75…`）一致，「已验证固件 = 交付固件」成立。
