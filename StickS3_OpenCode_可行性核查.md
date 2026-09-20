# StickS3 × OpenCode 可行性核查与证据记录

> 核查日期：2026-09-15。  
> 结论范围：文档／源码／本机隔离接口验证，不是设备和云服务端到端验收。  
> 关联：[设计文档](./StickS3_OpenCode_语音状态屏_设计文档.md)、[硬件知识文档](./M5StickS3_硬件知识文档.md)。

> 本文件保留前序核查事实；E01—E10不是新产品测试通过记录。当前开发与验收方法见[文档索引](./StickS3_OpenCode_文档索引.md)中的Selective TDD、契约与交付定义。

## 1. 证据等级

| 标记 | 含义 |
| --- | --- |
| 实测 | 本次实际运行命令、调用隔离服务或观察TUI所得 |
| 类型/源码 | 读取匹配版本类型或固定提交源码，但未运行对应功能 |
| 官方文档 | 厂商明确提供的接口或约束，不等于账户/设备已验证 |
| 拟定设计 | 本项目新规则，不能标成现成功能 |
| 未验证 | 尚需账号、设备或实现验证，明确列为门槛 |

## 2. 环境基线

前序只读硬件检测：HP EliteBook 840 G10，Windows11 Enterprise Build26100，i7-1365U 10核12线程，约32GB物理内存；Intel Iris Xe，无枚举到NVIDIA/AMD独显；WSL可见12逻辑CPU、约15GiB内存。空闲内存随运行负载变化，不作为性能承诺。

本次再次确认：

| 项目 | 结果 |
| --- | --- |
| OpenCode可执行文件 | `/home/shiwenc/.opencode/bin/opencode` |
| `opencode --version` | `1.18.30` |
| 二进制SHA-256 | `87bd160e053af86b5b409daabf71f8dc05bbc3a2a3a5f563f36011cdf706a999` |
| Windows蓝牙枚举 | `Intel(R) Wireless Bluetooth(R)`、`Microsoft Bluetooth LE Enumerator` 状态OK |
| 匹配插件包 | 隔离运行生成的 `@opencode-ai/plugin@1.18.30` |
| 文档落点 | `C:\Temp\ESP32-S3M5` |

蓝牙枚举未主动扫描、配对或操作StickS3；只能证明电脑端有可见蓝牙适配器。

## 3. 隔离OpenCode实验

### 3.1 隔离与操作边界

在 `/tmp/opencode/sticks3-design-probe` 下建立专用 HOME、XDG_CONFIG_HOME、XDG_DATA_HOME、XDG_CACHE_HOME、XDG_STATE_HOME 和空project目录；使用 `--pure`，禁用项目配置与外部技能加载，在tmux中启动独立TUI，监听 `127.0.0.1:41987`。

没有修改现用OpenCode配置，没有向现用会话追加文字，没有配置模型Key，没有调用模型或发送测试提示词。独立实例会自动生成自身数据库、日志、插件包和缓存；这些属于临时实验数据，不应称为“完全无磁盘写入”。

实验只创建两个空会话、查询接口、切换它们和填入无敏感测试字符串；未调用提交、Shell、审批或删除用户会话接口。

### 3.2 观察结果

| ID | 操作 | 观察 | 可得结论 |
| --- | --- | --- | --- |
| E01 | WSL curl查询 `/global/health` | healthy=true，version=1.18.30 | 本机独立服务正常 |
| E02 | Windows PowerShell查询同一localhost地址 | 同样返回1.18.30 | 当前机器Windows→WSL localhost链路可用 |
| E03 | 读取 `/doc` | append请求只有text，query只有directory/workspace | 不能宣称该接口原生支持按会话追加 |
| E04 | POST append `{text:测试串}` | HTTP200，true；tmux中看到草稿 | 普通追加路径实际有效，不表示安全绑定 |
| E05 | POST append `{}` | HTTP400，Missing key at text | 缺失必需字段被拒绝 |
| E06 | 创建空会话A与B，select A再select B | 会话选择器中B前显示选中标记 | 实际当前页面为B |
| E07 | 当前B时POST append，额外带sessionID=A | HTTP200，true；文字出现在当前草稿 | 额外sessionID没有保护目标，不可依赖 |
| E08 | 查询A与B的message列表 | 均为空数组 | 测试文字没有提交成消息 |
| E09 | 查询 `/api/session/active` | data为空对象，TUI仍位于B | active不是当前可见会话指示器 |
| E10 | 关闭本次tmux会话，再查健康地址 | 连接拒绝 | 实验服务和端口已关闭 |

TUI依次显示的可见测试草稿为：

```text
STICKS3_DESIGN_PROBE_NOT_SUBMITTEDA_DRAFT TARGET_A_BUT_CURRENT_B
```

测试A：`ses_f5c40a00dffe3rnvYYAnyZibte`；测试B：`ses_f5c406d36ffez1jLDTI3hRiVZ3`。这些仅为隔离实验ID，不是用户真实任务ID。

### 3.3 实时schema摘录

```text
POST /tui/append-prompt
  body properties: text:string
  required: text
  response: boolean

EventTuiPromptAppend
  type: tui.prompt.append
  properties: { text:string }

GET /api/session/active
  description: foreground Session drains owned by this process
  absent sessions: inactive

SessionStatus
  idle | retry(attempt,message,next,...) | busy

Session
  optional: cost, tokens(input/output/reasoning/cache.read/cache.write), model
```

OpenAPI中声明额外字段不允许，但本次请求额外携带sessionID仍被接受，说明不能凭schema假设运行时会拒绝未知字段。最关键的事实是该字段没有产生目标保护。

未验证：忙碌时插件写入、附件保留、会话切换竞态、ACK幂等、TUI崩溃恢复。没有为了触发busy而调用付费模型。

## 4. 匹配版本TUI插件契约

直接读取安装版本文件：

```text
/tmp/opencode/sticks3-design-probe/config/opencode/node_modules/
  @opencode-ai/plugin/package.json
  @opencode-ai/plugin/dist/tui.d.ts
  @opencode-ai/plugin/dist/index.d.ts
```

| 证据 | 位置 | 含义 |
| --- | --- | --- |
| version=1.18.30，exports含`./tui` | package.json | 不是仅凭官网泛泛猜测TUI插件存在 |
| TuiRouteCurrent为**判别联合**：`{name:"home"}` \| `{name:"session";params:{sessionID;prompt?}}` \| `{name:string;params?}` | tui.d.ts:13-24（1.18.31 复核，2026-09-20） | 可取当前会话ID，但**仅**在 `name==="session"` 分支；sessionID **不是**顶层字段。原表述"包含sessionID"不够精确 |
| TuiPromptRef.current/set | tui.d.ts:128 | 草稿含input/mode/parts，有同步set接口 |
| TuiState.session | tui.d.ts:287 | get/todo/messages/status/permission/question可用类型 |
| session_prompt slot | tui.d.ts:363 | 含session_id、visible、disabled、on_submit和ref |
| route.current、ui.Prompt、dialog、lifecycle | tui.d.ts:459 | 有组合最小TUI桥接插件的候选能力 |
| PluginModule server和TuiPluginModule tui分离 | index.d.ts:52；tui.d.ts:505 | 普通服务端插件不能假设拥有TUI上下文 |

官方 [TUI schema](https://opencode.ai/tui.json) 的 `plugin` 字段支持字符串或包名/选项元组。本次未创建或加载项目TUI插件。

**结论**：API存在已确认；“插件能正确复用Prompt并原子保护写入”仍为G1/G2验证目标。不能把E04普通API追加实验冒充插件路径成功。

可复核的公共包入口：[plugin@1.18.30](https://www.npmjs.com/package/@opencode-ai/plugin/v/1.18.30)。上游远程源码版本若无法与本地二进制完全对应，以本机类型和隔离行为为当前基线，不无条件引用dev分支作为1.18.30事实。

### 4.1 后续取得的tag源码交叉核对

本轮研究取得上游tag `v1.18.30`，提交 `3104c1428ec91f809e5ab86631300de41eb6952e`，本地只读研究副本位于 `/tmp/opencode-src`。父任务已回读以下源码：

- [TUI HTTP handler](https://github.com/anomalyco/opencode/blob/3104c1428ec91f809e5ab86631300de41eb6952e/packages/opencode/src/server/routes/instance/httpapi/handlers/tui.ts#L34)：publish后直接返回true，与E03/E07一致。
- [Prompt ref/set](https://github.com/anomalyco/opencode/blob/3104c1428ec91f809e5ab86631300de41eb6952e/packages/tui/src/component/prompt/index.tsx#L582)：current返回实时store；set同步执行setText、setStore、恢复parts extmarks并移动到末尾。
- [session_prompt槽位](https://github.com/anomalyco/opencode/blob/3104c1428ec91f809e5ab86631300de41eb6952e/packages/tui/src/routes/session/index.tsx#L1312)：replace模式、session_id、ref={bind}，内置Prompt还保留session_prompt_right子槽位。
- [Prompt卸载](https://github.com/anomalyco/opencode/blob/3104c1428ec91f809e5ab86631300de41eb6952e/packages/tui/src/component/prompt/index.tsx#L615)：模块级stashed保存卸载前草稿，cleanup调用ref(undefined)。这说明原生草稿可能跨组件重挂载延续，插件不得擅自清空。

这属于tag源码与本机行为相符的证据，未做二进制可复现构建校验，不宣称二进制逐字节来自该提交。研究还发现用户另一个全局环境使用plugin 1.18.14；本项目应独立锁定与目标CLI匹配的1.18.30，不修改用户现有依赖。

### 4.2 会话用量源码口径

[SessionProjector](https://github.com/anomalyco/opencode/blob/3104c1428ec91f809e5ab86631300de41eb6952e/packages/core/src/session/projector.ts#L89) 按sessionID更新累计；part更新时先减旧值再加新值，消息或part删除会扣减。根会话与子会话按不同sessionID分别处理。因此累计不是永不下降的计费账本。

[processor](https://github.com/anomalyco/opencode/blob/3104c1428ec91f809e5ab86631300de41eb6952e/packages/opencode/src/session/processor.ts#L452) 同时更新助手消息和step-finish；重复相加会重计。[getUsage](https://github.com/anomalyco/opencode/blob/3104c1428ec91f809e5ab86631300de41eb6952e/packages/opencode/src/session/session.ts#L340) 对供应商用量和价格配置进行归一化/估算，缺失值可能归零。设计不把这些零当成已证明真实免费。

真实供应商会话、fork/revert/compaction边界和已有全局插件组合尚未运行测试，仍保留G6验收要求。

## 5. 参考项目与可复用范围

### 5.1 固定提交

| 项目 | 本次核查提交 | 许可与用途 |
| --- | --- | --- |
| [78/voicestick](https://github.com/78/voicestick) | `e865d68c1d96411571cbe1501a301ebe3c98f3b3` | 根代码MIT；板级、音频流水线、END及WinRT参考 |
| [zoe-huang1/sticks3-voice-input](https://github.com/zoe-huang1/sticks3-voice-input) | `166e7b30a46649151731a4044475af4417bf8697` | 根代码MIT；单设备配置、BLE分片、Windows接收端 |
| [sheepxux/Taskhub-for-StickS3](https://github.com/sheepxux/Taskhub-for-StickS3) | `ecef8e053df89830d9b4d029cba65f33ace14d55` | 根代码MIT；状态信息组织；运行通信为Wi-Fi而非BLE |

这些项目明确指向StickS3，不是仅凭ESP32-S3芯片名称推断适配。上游作者的实机声明不等于本次替用户完成实机验收。

### 5.2 BLE安全的具体证据

- voicestick [voice_ble.c](https://github.com/78/voicestick/blob/e865d68c1d96411571cbe1501a301ebe3c98f3b3/firmware/components/voice_ble/voice_ble.c#L693)：`sm_bonding=0`，key distribution为0；应用层配对列表不等于链路绑定。
- zoe [sdkconfig.defaults](https://github.com/zoe-huang1/sticks3-voice-input/blob/166e7b30a46649151731a4044475af4417bf8697/firmware/sdkconfig.defaults#L14)：MAX_CONNECTIONS=1、MAX_BONDS=1、NVS持久化、MTU偏好247。
- zoe [ble_hid.c](https://github.com/zoe-huang1/sticks3-voice-input/blob/166e7b30a46649151731a4044475af4417bf8697/firmware/main/ble/ble_hid.c#L297)：bonding=1、SC=1、MITM=0、NO_IO。
- zoe [ble_audio.c](https://github.com/zoe-huang1/sticks3-voice-input/blob/166e7b30a46649151731a4044475af4417bf8697/firmware/main/ble/ble_audio.c#L80)：data仅NOTIFY，state READ/NOTIFY，ctrl READ/WRITE，未指定强制加密权限。

父任务已逐段回读以上本地源码。**不能因此说所有上游运行时音频必定明文**，主机可能建立了加密链路；正确结论是自定义服务本身没有形成可依赖的强制安全契约。删除HID后必须独立验证配对与通知门禁。

### 5.3 音频与生命周期

- 两项目的参考音频为16kHz单声道、60ms Opus、20kbit/s CBR；实际项目新参数需锁定并测试。
- zoe有按MTU分片的音频头；voicestick有录音ID/序号及显式END思路。双方都不能被当作已提供可靠应用级音频ACK/重传。
- zoe参考序号为8位；30秒500帧会回绕，新协议用更宽序号和录音标识避免歧义，不声称上游所有回绕必定出错。
- 上游的显示变暗、深睡或5分钟自动关机不能直接满足“电池息屏但BLE不断”。
- 30秒固件硬上限、失败TTL、取消迟到结果隔离、无自动Enter均需本项目实现，不能从上游功能表推断已经具备。
- zoe关闭PSRAM启动初始化的配置不直接照搬；最终固件必须验证可用内存和所有任务栈。

更多来源：[voicestick音频流水线](https://github.com/78/voicestick/blob/e865d68c1d96411571cbe1501a301ebe3c98f3b3/firmware/components/audio_pipeline/audio_pipeline.c)、[zoe音频分片](https://github.com/zoe-huang1/sticks3-voice-input/blob/166e7b30a46649151731a4044475af4417bf8697/firmware/main/ble/ble_audio.c)、[Windows子目录限制](https://github.com/78/voicestick/blob/e865d68c1d96411571cbe1501a301ebe3c98f3b3/desktop/windows/README.md)。

### 5.4 许可边界

根MIT不覆盖第三方原有许可；Opus、LVGL、Espressif组件、字体需按实际文件保留授权。未知来源图片不复用。CodeBuddy整体授权范围不明确，因此不作为本项目代码基础。

5分钟失败保留位于Windows，保留对象只是一段≤30秒音频。把20kbit/s乘以300秒来计算录音缓冲是错误口径；应乘以30秒。

## 6. GLM-ASR核查

官方资料：[API](https://docs.bigmodel.cn/api-reference/模型-api/语音转文本)、[模型说明](https://docs.bigmodel.cn/cn/guide/models/sound-and-video/glm-asr-2512)、[HTTP接口指南](https://docs.bigmodel.cn/cn/guide/develop/http/introduction)。

已核实：

- `POST https://open.bigmodel.cn/api/paas/v4/audio/transcriptions`。
- `model=glm-asr-2512`，WAV/MP3，单文件≤25MB，音频≤30秒。
- `hotwords`最多100项；`prompt`可选。本项目不用项目上下文prompt。
- `stream=true`为SSE文本输出，不是已经证明可以实时上传无限音频的WebSocket接口。
- 成功非流式响应schema为`text`等字段；不照搬旧页面chat.completion输出示例。
- 没有用Key发送任何云识别请求；账户权限、TLS代理、准确率、费用和5秒目标均未实测。

开源GLM-ASR-Nano与上述云API不是同一交付物；用户已选择云端方案，首版不安装本地推理引擎，也不增加DeepSeek文本润色。

### 隐私核查

读取 [智谱大模型开放平台隐私政策](https://docs.bigmodel.cn/cn/terms/privacy-policy)，其中存储期限按最短必要期限、业务/安全/法律等规则确定，删除可能受法定保留和备份限制。未发现针对本项目ASR请求的精确零留存承诺。

此处不据泛化政策断言每份ASR音频必然用于训练，也不承诺绝不用于训练。正式上线前以适用API服务协议、账号设置及供应商确认为准。

## 7. Windows平台依据

- [GATT Client](https://learn.microsoft.com/en-us/windows/apps/develop/devices-sensors/gatt-client)：提供发现、读写和通知接口；连接与服务发现时间可变化，不能承诺瞬时重连。
- [设备配对](https://learn.microsoft.com/en-us/windows/apps/develop/devices-sensors/pair-devices)：系统配对可能显示用户确认；“普通用户运行”不等于“无需配对确认”。
- [CredWriteW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-credwritew)：写当前用户凭据集，与登录会话相关。
- [Shell_NotifyIconW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shell_notifyiconw)：原生托盘API。
- [WSL网络](https://learn.microsoft.com/en-us/windows/wsl/networking)：Windows访问WSL服务可使用localhost；本机E02验证了这一方向，不保证所有VPN/WSL配置都一样。
- [WSL互操作](https://learn.microsoft.com/en-us/windows/wsl/filesystems)：支持WSL与Windows命令互操作。未来启动器的安全IPC尚未实现验证。

首版不改变防火墙、代理、驱动或系统安全策略；公司网络异常时显示明确错误，不自动关闭证书验证。

## 8. 已知缺口与下一阶段证据要求

| ID | 缺口 | 需要的证据 | 当前处理 |
| --- | --- | --- | --- |
| B01 | TUI插件运行中安全封装Prompt | 加载、引用生命周期、原稿附件/撤销、busy、切换会话测试 | 语音写入首要阻断门槛 |
| B02 | 相同用户下多实例/ACK/epoch | 重启、第二客户端、写后ACK丢失不重填 | 不提供跨崩溃exactly-once承诺 |
| B03 | BLE安全及30秒连续传输 | 实机加密、可信主机、MTU、500帧完整性 | 不直接继承上游安全配置 |
| B04 | GLM账号及5秒体验 | 真实授权样本5/15/30秒、延迟和准确率记录 | 保留用户目标，不虚报达标 |
| B05 | Token/cost精确口径 | 匹配版本累计实现与有用量会话验证 | 只显示已验证字段或未知 |
| B06 | 电源和中文显示 | 息屏BLE保连、电池测量、中文字体与长标题 | 不承诺续航数字 |
| B07 | 开源发布许可/依赖 | 锁定依赖、逐项许可证及资产来源 | 原创MIT已于2026-09-15批准；依赖审查及发布授权仍未完成 |

## 9. 工作审视报告

### 原定目标

确认已选需求具备可信技术路径，并交付有依据的设计文档，而非开始实现。

### 完成情况

- [x] 已确认本机OpenCode/Windows/WSL接口基线。
- [x] 已现场复现普通append API不保护sessionID。
- [x] 已核对匹配版本TUI插件类型和官方schema。
- [x] 已区分上游功能、用户需求与本项目新增设计。
- [x] 已输出设计与证据文件，且停止实验服务。
- [ ] 尚未进行项目插件、BLE设备和云ASR端到端测试：用户当前仅授权设计，未进入实现。

### 发现的问题与修正

| 严重程度 | 具体问题 | 原因 | 本次修正 |
| --- | --- | --- | --- |
| 必须改正 | 把`/tui/append-prompt`当成按会话安全写入 | 只凭接口名称推断语义 | 隔离TUI复现并改为插件内事务验证门槛 |
| 必须改正 | 把MAX_BONDS=1当成音频安全 | 未区分存储容量、配对及通知门禁 | 分别规定绑定、认证、加密和发送检查 |
| 必须改正 | 把5分钟TTL误算成5分钟音频 | 混淆保留时间与录音时长 | 明确只保留≤30秒音频 |
| 应当改正 | 把30秒录音视为必定无法5秒转写 | 没有测量的性能推断 | 保留5秒目标，以分档实测裁决 |
| 应当改正 | 用“只在RAM”承诺整个系统无落盘 | 忽略OS、TUI和云服务边界 | 限定本组件不主动持久化 |

### 下次重点关注

先通过B01/B02，再进入硬件与云联调。若新会话继续，请将这两份文档作为上下文；不能依赖跨会话记忆代替设计约束。
