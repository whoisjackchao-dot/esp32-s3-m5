# ASR一定要付费吗？免费与开源替代方案延伸思考

> 日期：2026-09-15。性质：独立方案思考，**不是原设计变更、实施计划批准或实测报告**。  
> 按用户要求只新增本文，不修改现有HLD、决策表、测试矩阵、申请手册或索引。原先已批准的智谱方案保持不变。  
> 本次只读取公开资料和既有环境记录，未安装ASR引擎、下载模型权重、申请账号、上传录音或调用付费API。

## 1. 核心结论

**不必先购买托管ASR服务，才能实现这个语音状态屏。** 已有能够在Windows CPU上运行、可通过原生API集成的开放源代码方案。

更准确的选择不是“付费才能识别，免费只能玩具”，而是：

| 路线 | 识别调用费 | 代价 | 对本项目的判断 |
| --- | --- | --- | --- |
| 本机开源引擎＋可合法使用的权重 | **无供应商按次识别费** | 本机CPU/内存/电力、模型下载、集成与维护 | 最值得独立验证 |
| 云服务免费额度 | 额度内可能免费 | 账号、限流、联网、数据上传、政策变化 | 可评估，不等于永久免费或离线 |
| 自有/租用服务器部署开源模型 | 无模型平台按次费 | 服务器/带宽/运维仍可能付费 | 若已有可用算力可考虑，不必为一台StickS3先买GPU |
| 商业托管ASR | 按平台规则付费 | 网络、隐私与供应商依赖 | 省部署成本；不是唯一可行路线 |

**当前推荐**：先用whisper.cpp的多语言模型建立Windows原生CPU基线，再以sherpa-onnx＋SenseVoiceSmall作为中文方向对照。只有拿到同一台电脑、同一套语料的质量和耗时结果，才能决定是否值得替换智谱。

不承诺免费方案必定达到现有“5s/10s/30s 各5次、每组5次5秒内填入”（2026-09-23 修订）和“15条至少14条合格”（2026-09-24 修订）的门槛，也不在没测之前断言一定达不到。

## 2. 重新审视为什么此前选择了云ASR

此前云API路线解决的是“本机推理部署与性能未知”，并不是硬件在原理上无法做本地识别。

已记录的电脑环境：i7-1365U、约32GB内存、Intel Iris Xe、没有枚举到NVIDIA独显；WSL可见约15GiB内存。这说明：

1. **没有CUDA不等于不能做ASR**。whisper.cpp明确支持CPU-only及Windows；sherpa-onnx明确支持本地推理、Windows和C/C++接口。
2. 首先比较Windows原生CPU路径，而不是直接导入依赖GPU的Docker示例。
3. 可以评估OpenVINO对Intel硬件的加速，但“支持Intel GPU”不等于已验证当前Iris Xe和驱动，也不等于整条识别链都能加速。
4. 32GB物理内存不保证速度；持续推理的功耗、风扇、温控以及与WSL编程任务争抢CPU也需要测。
5. 模型放在电脑，不放在只有8MB PSRAM的StickS3。设备仍采音、编码、BLE传输和显示，不承担大型ASR推理。

这是一项部署位置优化思考，不改变OpenCode模型调用本身：**本地免费ASR不代表OpenCode所用LLM也免费或离线。**

## 3. 有依据的开源候选

### 3.1 首选基线：whisper.cpp＋Whisper多语言模型

项目：[ggml-org/whisper.cpp](https://github.com/ggml-org/whisper.cpp)，权重来源：[openai/whisper](https://github.com/openai/whisper)。

已核实：

- whisper.cpp为C/C++实现，支持Windows、CPU推理、整数化模型和C API。
- whisper.cpp代码为MIT；OpenAI Whisper官方README明确代码和模型权重均为MIT。模型转换/分发包仍需核对具体来源和附带组件。
- 可以直接把内存里的16kHz浮点PCM交给`whisper_full`，不必上传网络或创建临时WAV。
- 官方提供`whisper-server`及`POST /inference`示例；这不是智谱API的同路径兼容服务。
- 可以从`base`、`small`多语言模型开始验证。**不要选`base.en`或`small.en`处理中文**。

官方whisper.cpp README列出的参考内存/磁盘规模：

| 模型 | 磁盘参考 | 内存参考 | 如何使用这些数值 |
| --- | --- | --- | --- |
| base | 142MiB | 约388MB | 仅用于估计模型级资源，不代表本程序总RSS |
| small | 466MiB | 约852MB | 中文质量与耗时需本机实测 |
| medium | 1.5GiB | 约2.1GB | 不因内存装得下就默认适合5秒目标 |
| large | 2.9GiB | 约3.9GB | 不作为无实测首选；量化/运行后端会改变结果 |

注意这不是OpenAI PyTorch实现的VRAM要求，也不是已测的i7-1365U消耗。[S1][S2]

**建议定位**：最小许可风险、原生C++工程基线；不是直接认定中文效果最优。

### 3.2 中文方向：sherpa-onnx＋SenseVoiceSmall

项目：[k2-fsa/sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx)，模型项目：[FunAudioLLM/SenseVoice](https://github.com/FunAudioLLM/SenseVoice)。

已核实：

- sherpa-onnx引擎为Apache-2.0，支持Windows、C/C++以及本地流式/非流式识别，包含SenseVoice模型接入。
- SenseVoiceSmall公开说明支持普通话、粤语、英语、日语、韩语，采用非自回归识别架构；中英混合编程口述仍需本项目语料验证。
- **SenseVoiceSmall本身是非流式候选**；sherpa-onnx支持流式模型，不等于把任何离线模型自动变成真流式。
- 单段≤30秒的按住说话场景适合先比较整段离线识别，不需要一开始建设会议转录/说话人分离/情绪识别系统。
- 源码许可与权重许可必须分开：SenseVoice仓库代码MIT，官方模型卡链接FunASR Model Open Source License Agreement，不能把权重直接写成MIT或Apache-2.0。
- 权重的自定义协议要求署名、保留模型名称，并包含“for reference and learning purposes only”等措辞；本文不将其解释为无条件商业分发授权。转换后的ONNX包同样需核对原始权重条款。[S4][S5][S10]
- 当前核查的SenseVoice接入不支持transducer式上下文热词增强；官方另有拼音词组替换功能，但它是文本后处理，不是英文代码词热词识别保证。本次不引入这类自动改写。[S11]

**建议定位**：中文准确率/速度对照候选，先审查具体ONNX模型包和许可再进入分发设计。不要因为仓库称“高性能”就写“你的电脑毫秒级识别”。

### 3.3 对照工具：faster-whisper CPU INT8

项目：[SYSTRAN/faster-whisper](https://github.com/SYSTRAN/faster-whisper)。

官方README提供`device="cpu", compute_type="int8"`路径，基于CTranslate2。其Python封装适合隔离评测或作为独立本地工作进程，不要求把原定C++托盘整体改成Python。

官方CPU基准使用i7-12700K、8线程和13分钟音频，不是你的i7-1365U，也不是30秒中英口述，**不能按比例推导你松键后的精确延迟**。名称叫“faster”也不保证在每个模型、线程数和场景中胜出。

批处理路径默认VAD行为需要特别核对；原方案已明确不增加VAD，评测不能无说明启用后把性能提升归因于同等处理。源码/运行时/权重分别审核许可，不因为Python可安装就认定完整分发许可已解决。[S6]

### 3.4 保留候选：GLM-ASR-Nano自部署

[zai-org/GLM-ASR](https://github.com/zai-org/GLM-ASR)提供1.5B模型说明、Transformers CPU选择及SGLang服务示例。它说明“不购买智谱托管API也能研究GLM识别”。

但当前仓库示例对依赖版本、SGLang开发镜像等有要求，本机CPU速度未验证。对这一台无NVIDIA显卡、既定C++托盘的项目，不宜只因品牌相同就优先选择。**开源Nano权重与云`glm-asr-2512`不能视为完全相同效果或接口**。[S7]

## 4. 免费托管额度与真正本地免费不同

免费套餐、试用余额、社区Space或浏览器演示可能让一次转录不付款，但它们不自动具有以下性质：

- 永久无限额度；
- API始终对你的账号开放；
- 录音不离开电脑；
- 无网络、排队、限流或区域访问限制；
- 可以合法把网页内部接口当公开API自动调用。

本项目不使用共享Key、逆向绕过收费、轮换账号薅额度或把“免费网页”包装成稳定API。若采用免费套餐，额度耗尽必须明确停止/提示，不能自动升级付费或切到另一家上传。

### 4.1 Groq：有免费档的托管转写API

- 官方转写端点：`POST https://api.groq.com/openai/v1/audio/transcriptions`，支持多语言`whisper-large-v3`和`whisper-large-v3-turbo`。不要使用会翻译成英文的`/translations`代替转写。
- 官方语音文档明确免费档单文件上限25MB；账单FAQ说明可以降回Free档，届时停止按使用量收费。这是实际API免费档，不是从免费网页推断。
- 免费档受请求频率及音频时长配额限制，实际账号上限以控制台为准。公开限额页存在Free/Developer切换，本次未登录核验，不把抓取到的数值写成你的账号保证。
- 付费档公布的模型小时单价、最低10秒计费规则不应写成Free档每次必收费。只有明确决定升级时才重新核查计费。[S12]

### 4.2 Cloudflare Workers AI：每日共享免费算力额度

- 官方定价页明确Workers Free每天有**10,000 Neurons免费额度**，UTC 00:00重置；超额需升级Workers Paid，Free档不能无限继续调用。
- 候选模型`@cf/openai/whisper-large-v3-turbo`公布消耗46.63 Neurons/音频分钟。若全部额度只用于该模型，算术折算约214分钟/天；这不是保底服务量，其他模型会共享消耗额度，实际计量与服务限制仍以平台为准。
- 需要Cloudflare账号及访问凭据，录音发送至云端。付费Workers计划超出免费额度可以产生费用，不要把Free和Paid的超额行为混写。[S13]

两者都可作为额度内免费的云端候选，不仅限于一次性试用；但公开免费档不能证明未来永久提供，也不能证明你的网络、账号及中文质量已达标。本次没有注册、上传录音或实测服务可用性。

因此，从“可长期自主控制、避免按次费用”的目标出发，**本机开源推理比依赖免费额度更值得作为主候选**；若更重视免部署、能接受上传和配额，则可单独评估上述免费云API。

## 5. 一个不购买ASR服务的独立候选架构

### 5.1 模块视图

```text
StickS3：麦克风 → ES8311 → Opus → 加密BLE
                                      │
                                      ▼
Windows C++托盘：重组完整音频 → PCM
                                      │ 本机内存/受限IPC
                                      ▼
Windows本地ASR工作进程：模型常驻 → 识别 → 完整text
                                      │
                                      ▼
已有候选WSL TUI桥接：检查session/epoch → 追加草稿
                                      │
                                      ▼
用户检查后手动发送 → OpenCode现有LLM（仍可能收费）

OpenCode状态 → Windows → BLE → 设备状态页（不变）
```

**变化集中于HLD的M06识别后端**：由WinHTTP调用智谱换成Windows本地模型工作进程。不是重新开发整套固件，也不需要增加手机、租GPU服务器或完整小智云后端。

本地worker作为单独进程是候选而非强制：

| 方式 | 优点 | 代价 |
| --- | --- | --- |
| 原生库直接嵌入托盘进程 | 接口最少、内存传递方便 | 模型异常可能影响托盘，取消/资源隔离更难 |
| **独立Windows工作进程（推荐研究）** | 模型/线程池隔离，可控重启，不阻塞BLE与TUI | 需要IPC、进程生命周期和版本匹配 |
| localhost HTTP模型服务 | 最接近现有“ASR adapter”形状，便于独立调试 | 额外端口、认证、上传/日志/临时文件与取消语义需要审查 |

### 5.2 为什么优先Windows而不是WSL跑模型

BLE和PCM已在Windows，原生C++接口有公开依据。将模型放Windows可减少Windows↔WSL传递音频，避免另占WSL有限内存配额；WSL仍负责OpenCode。

这只是工程路径比较，不是断言Windows CPU推理一定快于WSL。若现有Linux运行时更可靠，可以比较WSL sidecar，但先用同一权重/量化/线程/语料测量。

### 5.3 保持的既有行为

以下只作为对照约束，不修改原文档：

- 一台电脑、一台可信StickS3；BLE加密；按住说话，最多30秒，小于300ms丢弃。
- 完整音频才产生可提交转写；缺片不以PLC隐藏后继续输出。
- 不自动提交、不额外LLM润色、不审批权限、不自动上云回退。
- 单事务，失败/阻塞保留最多5分钟，锁屏/挂起清理；已填入草稿不删除。
- 电池60秒息屏、10秒心跳、15秒有效状态过期不受ASR替换影响。
- Q03时延和Q04质量门槛用于同条件比较，不因免费而私自降低。

30秒上限从“厂商限制兼产品约束”变为单纯产品约束，**不因本地可处理长音频就扩展首版功能**。

## 6. 候选接口：区分现有API与新设计

### 6.1 whisper.cpp已有C API（源码核实）

| 能力 | 实际API/字段 | 本项目候选用法 |
| --- | --- | --- |
| 加载 | `whisper_init_from_file_with_params` | 只加载固定、校验哈希的本地模型，启动时完成 |
| 模型配置 | `whisper_context_params.use_gpu` | 第一轮CPU基线，不假设自动识别到加速设备 |
| 识别 | `whisper_full(ctx, params, samples, n_samples)` | 输入16kHz float PCM；同context不能并发使用 |
| 结果 | `whisper_full_n_segments`、`whisper_full_get_segment_text` | 收集全部最终分段后交给安全追加，不边生成边提交 |
| 取消 | `whisper_full_params.abort_callback` | 检查generation/取消标志；实际取消响应时间需要测试 |
| 上下文隔离 | `no_context`、`initial_prompt` | 不混入上一次转录；必要通用技术词提示不等于保证热词正确 |
| 清理 | `whisper_free`、独立state释放接口 | 生命周期结束释放，持续权重与短生命周期音频分开 |

来源：官方[whisper.h](https://github.com/ggml-org/whisper.cpp/blob/master/include/whisper.h)。有API声明不等于本项目已实现或性能合格。

### 6.2 本地worker逻辑接口（仅思考草案）

| 操作 | 输入 | 输出 | 费用 |
| --- | --- | --- | --- |
| `status` | 无正文 | backend/model/hash、loading/ready/error | 无ASR调用费 |
| `transcribe` | requestID、generation、16k mono PCM、有效样本数 | 完整text或错误，模型版本和耗时 | 无ASR调用费，消耗CPU/内存 |
| `cancel` | requestID、generation | 已取消/已完成/待确认 | 无ASR调用费 |
| `shutdown` | 当前用户授权 | 释放模型进程及所有临时缓冲 | 无ASR调用费 |

这些不是已存在的软件端点。若用受限本地管道不需要HTTP；若用HTTP，仍应仅loopback、认证、限制载荷且不暴露任意`/load`路径给不可信请求。

### 6.3 HTTP也可以免费，但不能直接换base_url了事

whisper.cpp示例使用`POST /inference`；智谱使用`POST /api/paas/v4/audio/transcriptions`；请求参数、模型选择、响应及取消并不相同。应有具体adapter，不伪称所有“OpenAI兼容”都能零修改替换。

已读取whisper.cpp server源码：`--convert`分支会写临时音频文件；未启用转换的分支可从上传内容内存读取。此外示例有模型加载入口和宽松CORS设置，不能把示例直接当成符合本项目隐私/安全要求的生产服务。[S3]

如果走原生PCM API，就无需为ASR封装WAV或写临时文件；但模型权重本身持久保存是必要的，**“不保存用户录音”不等于“不保存模型文件”**。

## 7. 多想一步：怎么争取松键后5秒

### 7.1 先消除没有必要的开销

- 模型常驻，不每段录音启动Python/重新加载几百MB权重。
- 先用CPU多语言base/small或中文非自回归模型对比；线程数通过本机数据选，不盲目占满所有核。
- 不做翻译、说话人分离、词级时间戳、情绪标签展示或LLM润色。
- 使用原生PCM和有界工作队列；BLE/托盘/UI线程不承担推理。
- 记录“模型加载”和“热态识别”不同耗时，不隐藏冷启动。

### 7.2 再考虑提前计算，而不是先降低标准

候选A：完整≤30秒录音收齐后批量识别，最易保留现有完整性和取消规则；若能达标则不要增加复杂度。

候选B：选用真正的中英流式模型，边收到连续完整PCM边解码，松键后finalize；只在END/完整性校验成功后交出最终文本，中途缺片/取消则丢弃全部暂存结果。sherpa-onnx支持部分流式Zipformer/Paraformer模型，但具体模型权重、许可、热词与质量需单独验证。

**流式解码不等于开启VAD**。可继续由按键控制开始/结束；但不能默认照搬一个会自动分段/静音截断的demo。

候选C：对Whisper/SenseVoice离线模型做滑窗重复识别，属于应用层模拟流式，需要处理边界、重复和修正，可能牺牲质量。不作为第一步，也不把半句直接追加到用户草稿。

### 7.3 当前不能做的性能承诺

- 官方benchmark的桌面i7-12700K、服务器GPU或手机不能代替i7-1365U本机测试。
- 大模型“turbo”、INT8或OpenVINO标签不能直接推出5秒达标。
- 30秒录音可能以小于30秒时间识别，也可能超过5秒；需要测量而非用录音长度判断不可能。
- 本机推理会增加笔记本用电，与StickS3电池续航不是同一个指标。

## 8. 免费路线新增的风险

| 风险 | 必须核查 |
| --- | --- |
| 静音/噪音幻觉 | 原方案不做VAD，本地模型可能在无语音时生成文字；固定静音/敲键盘/风扇样本测试，不偷偷启用新过滤规则 |
| 中文与英文代码词混写 | 文件路径、否定词、数字单独计错，不能用整体低CER掩盖关键错误 |
| 取消后还在计算 | generation丢弃迟到结果，同时验证worker是否真正释放音频/停止占用；原生取消能力不保证即时 |
| 长驻模型泄漏旧上下文 | 每段重置识别state，不沿用上次转录提示或音频特征；是否需要回收worker通过测试决定 |
| 日志与临时文件 | 禁实时打印正文/调试音频，检查框架和wrapper实际写盘；OS分页/转储仍是边界 |
| 下载与更新 | 固定版本/模型哈希/许可；首次下载可以联网，运行推理不自动联系外部服务器；更新显式进行 |
| 权重许可 | C++引擎许可不等于模型许可；ONNX转换不消除原许可条款 |
| 静默退回收费 | 纯免费候选不自动调用智谱；本地失败只报告失败，不能用云端兜底隐藏不足 |
| C++托盘卡死 | 推理worker与BLE/输入框控制隔离，压力测试不能只测模型本身 |

Whisper官方模型卡明确指出可能产生未说出的文字与重复文本。保留“用户检查后手动发送”仍然必要，不能因本地部署就省掉输入确认。[S8]

## 9. 如果将来批准验证，怎么最小投入做决定

本节仅提出后续验证路线，**当前不执行、不修改现有测试文档**。

1. **准备一份共同语料**：沿用已批准15条编程口述的质量规则（2026-09-24 修订）；另准备5/10/30秒各5次时延测试，复用同一录音而非为每个模型挑有利样本。
2. **先比较两个候选**：whisper.cpp多语言small（可加base作速度下界对照），与经许可确认的sherpa-onnx＋SenseVoiceSmall CPU。不要一次安装所有ASR生态。
3. **离线真实性测试**：先显式完成模型下载，再断开外网进行ASR；OpenCode云模型另行隔离，不把其网络流量算成本地ASR联网。
4. **自动化采集**：版本/模型哈希、量化/线程、音频样本数、推理时间、进程内存、CPU、关键语义错误与取消响应。人工只审校固定参考与争议结果。
5. **验收不改门槛**：每个时长组各 5 次、需 5/5 次 ≤5 秒成功填入；15 条至少 14 条 ≤2 处局部修正且无关键语义错误（2026-09-24 修订）。不能把“模型推理完成”当成“已安全填入”。
6. **免费候选的压力测试必须跑真实本地推理**：原方案2小时mock-ASR压力试验只代表其它链路；替成本地后要覆盖模型长驻、CPU竞争和音频清理，再依用户决定是否追加。
7. **作出选择**：本地同时达质量/时延/稳定性则再申请变更正式设计；只达一项则列出差距，比较不同模型/后端，不自动上云、不擅自降低需求。

测试输出建议为一张表，所有数据必须实测后填，本文不填虚构数值：

| 候选 | 权重/版本/许可 | 质量合格条数/30 | 各时长5秒内成功次数/20 | 内存/CPU | 离线/取消 | 结论 |
| --- | --- | --- | --- | --- | --- | --- |
| whisper.cpp多语言small | 尚未下载/冻结 | 未测 | 未测 | 未测 | 未测 | 待验证 |
| sherpa-onnx＋SenseVoiceSmall | 具体转换包许可待核对 | 未测 | 未测 | 未测 | 未测 | 待验证 |

## 10. 成本思考：免费不是没有成本

可以用以下结构比较，而不是编一个未经核价的月账单：

```text
托管总成本 = 有效录音计费量 × 实际单价 + 重试/测试费用 + 接入维护
本地总成本 = 集成维护 + 模型存储 + 额外电力/资源占用 + 版本/质量回归
```

识别频率较低时，云API费用可能低于本地集成时间成本；涉及隐私、频繁输入或离线需求时，本地方案的价值可能更大。具体哪一项便宜取决于使用量与实际计费，不通过“免费”二字作结论。

本项目已确定C++托盘、BLE、TUI桥接和自动化用例，识别后端可隔离；因此**先做本地ASR小范围评测，不必先采购云额度或新硬件**是合理下一步候选。

## 11. 与现有设计的关系

- 本文未撤销已批准的智谱方案，未修改原HLD/API申请手册/索引。
- 本文不授权下载模型、安装软件、运行长时间评测、刷机、上传录音或改写已有实现。
- 若未来采纳本地方案，需要显式更新M06识别后端、费用/隐私说明、模型安装/生命周期与相关测试；其它模块尽量不变。
- 不同时保留两套后端作为默认范围，不引入未要求的自动选择/云回退。当前只是候选比较，不提前设计复杂多引擎平台。

## 12. 来源与核查边界

- **[S1] whisper.cpp官方README**：Windows、CPU、量化、OpenVINO及参考内存。<https://github.com/ggml-org/whisper.cpp>
- **[S2] OpenAI Whisper官方README**：多语言与.en区别，代码/权重MIT。<https://github.com/openai/whisper>
- **[S3] whisper.cpp官方服务文档与源码**：`/inference`、`/load`、转换写临时文件及内存读取分支。<https://github.com/ggml-org/whisper.cpp/tree/master/examples/server>
- **[S4] sherpa-onnx官方README**：Windows/C++/本地/流式与非流式模型支持。<https://github.com/k2-fsa/sherpa-onnx>
- **[S5] SenseVoice官方README**：Small的语言、非自回归、部署和许可边界。<https://github.com/FunAudioLLM/SenseVoice>
- **[S6] faster-whisper官方README**：CPU INT8、i7-12700K基准及VAD行为。<https://github.com/SYSTRAN/faster-whisper>
- **[S7] GLM-ASR官方README**：Nano、CPU示例与SGLang示例。<https://github.com/zai-org/GLM-ASR>
- **[S8] Whisper模型卡**：幻觉、重复文本及场景评估限制。<https://github.com/openai/whisper/blob/main/model-card.md>
- **[S9] whisper.cpp原生头文件**：PCM输入、取消回调、上下文与线程约束。<https://github.com/ggml-org/whisper.cpp/blob/master/include/whisper.h>
- **[S10] SenseVoice权重与协议**：<https://huggingface.co/FunAudioLLM/SenseVoiceSmall>；<https://github.com/modelscope/FunASR/blob/main/MODEL_LICENSE>。
- **[S11] sherpa-onnx热词与拼音替换说明**：<https://k2-fsa.github.io/sherpa/onnx/hotwords/index.html>；<https://k2-fsa.github.io/sherpa/onnx/homophone-replacer/index.html>。
- **[S12] Groq官方语音、免费档账单与限额说明**：<https://console.groq.com/docs/speech-to-text>；<https://console.groq.com/docs/billing-faqs>；<https://console.groq.com/docs/rate-limits>。
- **[S13] Cloudflare官方免费额度与模型消耗**：<https://developers.cloudflare.com/workers-ai/platform/pricing/>。

以上公开分支链接可能变化，真正评测前应固定提交与模型哈希。本次结论仅来自公开文档/源码及先前主机检测，**没有本机识别性能、识别准确率或真实设备测试结果**。
