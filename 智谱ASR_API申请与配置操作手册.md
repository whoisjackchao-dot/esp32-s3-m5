# 智谱ASR API申请与配置操作手册

> 适用项目：StickS3 × Windows C++托盘 × WSL OpenCode。  
> 核对日期：2026-09-15；2026-09-20 复核官方模型页。ASR是语音识别，不是聊天模型API。  
> 已核对官方公开文档。**充值已由用户于2026-09-20确认完成**；专用Key申请、账号侧价格/额度核对与首次真实识别调用仍未执行。控制台具体布局/权益以登录后页面为准。

## 1. 先看操作顺序与费用

```text
注册/登录官方平台
    → 按账号要求完成认证
    → 确认glm-asr-2512权限、计费和可用额度
    → 创建本项目专用API Key
    → 准备非敏感测试音频
    → 手动确认后调用一次ASR【可能收费，音频上传云端】
    → 核对返回文字及控制台调用记录
    → 项目托盘完成后，把Key存入Windows凭据管理器
```

| 操作 | 会否调用付费模型 | 注意事项 |
| --- | --- | --- |
| 阅读文档、注册登录、创建Key | 这些动作本身不等于发起ASR识别 | 实名/企业认证、账号权益按平台要求 |
| 充值或购买服务 | **涉及付款** | 由你确认金额和产品，不代购、不假定必须买Coding Plan |
| 官方体验中心上传音频 | **可能消耗额度或计费** | 不假设体验中心永久免费；点击运行前核对提示 |
| API首次测试与手动重试 | **可能计费** | 每次都是新的请求；超时/取消也不保证免计费 |
| 本项目“重试填入” | 无新增ASR识别费 | 只重试本地草稿追加，不能与“重试识别”混淆 |
| 本地mock自动化测试 | 无智谱识别费 | 不能证明真实模型可用或准确率达标 |

**开源代码不代表托管API免费。** 本项目采用智谱托管`glm-asr-2512`，不是自行部署开源`GLM-ASR-Nano-2512`。

## 2. 官方入口

| 用途 | 官方地址 |
| --- | --- |
| 注册/登录 | <https://open.bigmodel.cn> |
| 平台首页 | <https://bigmodel.cn> |
| API Key管理 | <https://bigmodel.cn/usercenter/proj-mgmt/apikeys> |
| 当前价格入口 | <https://open.bigmodel.cn/pricing> |
| 官方体验中心 | <https://bigmodel.cn/trialcenter/modeltrial/voice> |
| GLM-ASR-2512模型说明 | <https://docs.bigmodel.cn/cn/guide/models/sound-and-video/glm-asr-2512> |
| 语音转文本接口 | <https://docs.bigmodel.cn/api-reference/模型-api/语音转文本> |
| 官方快速开始 | <https://docs.bigmodel.cn/cn/guide/start/quick-start> |
| 错误码 | <https://docs.bigmodel.cn/cn/faq/api-code> |
| 隐私政策 | <https://docs.bigmodel.cn/cn/terms/privacy-policy> |

直接访问上述官方域名，不从不明“API代理/免费Key”网页申请。不要把中国站Key与国际站`api.z.ai`地址混用，本手册只配置中国站BigModel。

## 3. 申请账号与API Key

### 步骤一：注册或登录

1. 打开官方平台，使用页面提供的注册/登录入口完成操作。
2. 需要手机验证码或实名认证时，直接在官方页面提交。具体材料依个人/企业账号而异，本文不假定所有账号流程相同。
3. 公司账号按公司规范办理，不将身份证件、验证码、密码或Key发给聊天助手。

**检查点**：能进入自己的控制台，并确认当前账号/项目归属。若进入企业空间，确认你有创建Key与调用模型的权限。

### 步骤二：先确认模型权限和计费

1. 查找模型 **`glm-asr-2512`**，不是`glm-5.*`聊天模型，也不是开源Nano模型。
2. 在当前价格与账号额度/账单页面确认：计费单位、价格、可用余额或试用额度、额度到期时间。
3. 如果页面要求开通服务或认证，先完成所需流程；若没有清晰说明，向官方工单确认，不凭本文推测“默认已开通”。
4. 若有预算/限额/告警功能，按你的账号权限设置；本文未验证所有账号都提供相同入口。

**不要仅为ASR购买GLM Coding Plan。** 编码套餐与通用ASR调用不是同一端点或必然共享权益；需平台明确确认。旧版`glm-asr`页面的价格不能套到`glm-asr-2512`。

建议记录一份不含秘密的配置清单：模型、核价日期、计费单位、可用额度、测试预算和负责人。本文不填未经核实的人民币单价或“永久免费”承诺。

### 步骤三：创建专用Key

1. 打开[API Keys管理页](https://bigmodel.cn/usercenter/proj-mgmt/apikeys)。
2. 使用页面提供的创建入口。如果支持备注，可命名为`StickS3-OpenCode-ASR`，方便之后轮换和撤销。
3. 若可选择项目或限制范围，确认与模型权限匹配；不猜测当前控制台支持哪些限制选项。
4. 把Key保存到公司允许的凭据工具。当前项目托盘尚未实现，不要为等待开发而把Key写入仓库或Markdown。
5. 控制台是否只显示一次Key以实际页面提示为准。丢失时按平台能力重新创建/轮换，不尝试从截图或日志找回。

**检查点**：你已拥有有效Key，但尚未因创建Key本身验证ASR调用成功。

## 4. 本项目需要的配置

| 配置 | 正确值/方式 |
| --- | --- |
| 服务区域 | 智谱BigModel中国站 |
| API完整地址 | `https://open.bigmodel.cn/api/paas/v4/audio/transcriptions` |
| 模型 | `glm-asr-2512` |
| 鉴权 | HTTP `Authorization: Bearer <Key>` |
| 请求格式 | `multipart/form-data`，由HTTP客户端生成boundary |
| 音频字段 | `file`，本项目上传内存WAV |
| 流式 | `stream=false` |
| 超时 | 本项目请求超时20秒，不是厂商SLA |
| 成功文本 | JSON中的`text` |
| 热词 | 最多100项；本项目只用通用技术词，不从代码自动提取 |
| 日常Key存储 | 项目托盘完成后使用Windows凭据管理器 |

不要误用：

- `/chat/completions`：聊天端点，不是本项目音频上传接口。
- Coding Plan专属端点：不能未经证实当作ASR端点。
- `glm-asr`、`GLM-ASR-Nano-2512`：不是本手册锁定的云模型ID。
- 将Opus文件重命名成`.wav`：不构成音频转换。
- 手工只写`Content-Type: multipart/form-data`却漏boundary：让客户端自动生成更可靠。

## 5. 准备首次测试音频

推荐一段3～5秒、你有权上传的普通话测试录音，例如：“这是语音识别测试，请保留英文 API。”不要包含客户信息、密码、项目机密或真实工作指令。

官方接受WAV/MP3，文件≤25MB、时长≤30秒。本项目最终使用16kHz、单声道、16位PCM WAV。文件扩展名不能证明编码与时长正确，上传前用音频工具核对。

测试录音可使用已有的合法WAV/MP3，不要求先刷StickS3。此处验证的是**账号/接口**，不是设备链路，也不代替30条识别质量或60次时延验收。

如果需要格式转换，使用你已经安装且信任的音频工具；不要为了首次API验证上传未确认时长的大文件。手工测试文件属于你主动准备的测试资料，与最终产品“不主动保存日常录音”的规则不同。

## 6. 首次调用：优先使用官方接口页面

这是当前无需项目程序即可操作的验证方式。

1. 打开[官方语音转文本接口页](https://docs.bigmodel.cn/api-reference/模型-api/语音转文本)。官方文档提供“Try it”交互调用入口；若当前页面没有显示，使用其提供的SDK/cURL示例或咨询平台。
2. 在官方页面的认证输入处填写Key，确认浏览器地址仍是官方域名。不要截图分享认证区域。
3. 模型选择`glm-asr-2512`，上传准备好的音频，`stream=false`；首次无需填写热词或上下文。
4. **点击运行前再次确认：音频会上传智谱，可能计费。** 点击一次，不因页面慢连续点击。
5. 检查HTTP状态和JSON响应。有效成功应有非空`text`；以下只是结构示意，不是实际调用结果：

```json
{
  "model": "glm-asr-2512",
  "text": "这是语音识别测试，请保留英文 API。"
}
```

6. 回到账号调用记录/用量或账单页面核对这次请求。记录请求ID、时间、模型、是否成功与实际计费；账单可能有延迟，不把暂未出现费用当成免费。
7. 完成后关闭测试页，按公司要求清理浏览器中保存的秘密和测试文件。不承诺关闭页面就删除云端数据。

若平台交互页不可用，下一节给出Windows命令行方式。不要为了检查Key而先调用聊天模型，这既不能证明ASR权限，也可能额外收费。

## 7. 可选：Windows命令行最小验证

### 7.1 无Key、无音频的连通性检查（不调用模型）

在Windows PowerShell运行：

```powershell
Test-NetConnection open.bigmodel.cn -Port 443
```

这只检查TCP连通，不证明TLS、代理鉴权、Key、模型权限或ASR有效。在必须经公司HTTP代理联网的环境，直连TCP失败也不一定代表合规代理路径不可用；按公司网络规范排查，不关闭防火墙/证书校验。

### 7.2 上传一次测试音频【可能付费】

下面是供人工参考的Windows PowerShell＋`curl.exe`示例。**未使用真实Key执行端到端验证**。它不需要安装项目程序，Key通过隐藏输入临时进入当前PowerShell进程内存，不保存到环境变量或文件；这是一次性验证，不替代最终Windows凭据管理器设计。

先确认系统可用：

```powershell
curl.exe --version
```

确认音频≤30秒/≤25MB并同意费用后，再执行以下完整代码块。脚本还会要求输入`YES`，不会自动重试，也不会跟随重定向。

```powershell
$audioPath = Read-Host '测试音频完整路径（WAV或MP3）'
$audioFile = Get-Item -LiteralPath $audioPath -ErrorAction Stop
if ($audioFile.PSIsContainer -or $audioFile.Length -eq 0) {
    throw '必须选择非空音频文件'
}
if ($audioFile.Extension.ToLowerInvariant() -notin @('.wav', '.mp3')) {
    throw '只接受WAV或MP3；还需自行核对实际编码与时长'
}
if ($audioFile.Length -gt 25000000) {
    throw '请使用小于25MB的短测试音频'
}
if ($audioFile.FullName.Contains('"')) {
    throw '文件路径含不支持的引号'
}
if ((Read-Host '已确认音频不超过30秒，允许上传智谱并承担可能费用？输入YES') -cne 'YES') {
    throw '已取消，未发送请求'
}
$secret = Read-Host '请输入智谱API Key（隐藏输入）' -AsSecureString
$ptr = [IntPtr]::Zero
$key = $null
$curlConfig = $null
try {
    $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secret)
    $key = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
    if ([string]::IsNullOrWhiteSpace($key) -or $key -match '[\r\n"\\]') {
        throw 'Key为空或含异常字符；请在官方控制台重新核对'
    }
    $curlConfig = 'header = "Authorization: Bearer ' + $key + '"'
    $upload = 'file=@"' + $audioFile.FullName.Replace('\', '/') + '"'
    $curlConfig | & curl.exe --config - --silent --show-error `
        --connect-timeout 10 --max-time 20 `
        --request POST `
        --url 'https://open.bigmodel.cn/api/paas/v4/audio/transcriptions' `
        --form 'model=glm-asr-2512' `
        --form 'stream=false' `
        --form $upload `
        --write-out '\nHTTP %{http_code}\n'
    $curlExit = $LASTEXITCODE
    Write-Host ('curl退出码：' + $curlExit)
} finally {
    if ($ptr -ne [IntPtr]::Zero) {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr)
    }
    $key = $null
    $curlConfig = $null
    $secret.Dispose()
}
```

这里使用标准输入向curl传认证配置，避免把明文Key写在curl进程参数中。不得添加`--verbose`、`--trace`、`--insecure`或输出认证配置。PowerShell/系统仍可能有内存副本、进程转储或企业审计；此方式不是零痕迹保证。返回文本会显示在终端，故只用非敏感样本。

**企业网络实测（2026-09-22）**：公司网络下首跑报 `curl: (35) schannel: CRYPT_E_NO_REVOCATION_CHECK`、`HTTP 000`——TLS 证书吊销检查（CRL/OCSP 端点）被网络策略阻断，请求未发出（无计费）。处理：脚本（`tools/zhipu_asr_verify.ps1`）已加 `--ssl-revoke-best-effort`——证书链仍完整校验，仅当吊销服务器"连不上/查不到"时容忍；若证书真被吊销仍会拒绝。**这不等于 `--insecure`（完全跳过校验），后者仍被禁止。**

**成功检查**：HTTP200且有可用`text`；curl退出码0本身不能证明HTTP业务成功。HTTP401/429等需按下一节检查。20秒超时后不要自动重试，先确认调用记录，避免重复费用。

## 8. 错误排查

官方错误同时包含HTTP状态和业务错误码，不能只看HTTP429就认定“请求太快”。

| 现象/业务码 | 含义或可能原因 | 操作 |
| --- | --- | --- |
| HTTP401，1000/1001/1003 | 认证失败、缺认证头、Token失效等 | 核对Key/域名/Bearer格式；不要公开Key |
| HTTP403，1220 | 无权访问API | 检查账号/项目/模型权限，必要时官方工单 |
| HTTP400，1210/1211/1213/1214 | 参数、模型ID、必需字段或字段格式错误 | 核对`glm-asr-2512`、file、multipart与音频限制 |
| HTTP429，1113 | 账号欠费 | 核对账单/余额，确认后再充值，不盲目循环调用 |
| HTTP429，1302/1305 | 速率限制/服务访问量高 | 停止连点，稍后手动重试；可能再次计费 |
| HTTP429，1311/1315 | 套餐未开放模型或Key限特定企业编程场景 | 核对正确产品类型Key，不假设Coding Plan覆盖ASR |
| HTTP500或网络错误 | 服务或网络问题 | 保存脱敏request_id和错误码，先查记录再决定重试 |
| curl退出码28/HTTP000 | 请求超时/未取得完整HTTP响应 | 不能认定请求没到云端；检查代理/网络和平台调用记录 |
| HTTP200但text为空 | 音频无有效识别结果或响应异常 | 检查可听性、格式与模型响应；不直接提交空文本 |
| 识别成功但术语错误 | 模型质量/音质/口音因素 | 使用非敏感通用热词；按项目30条验收，不用聊天模型偷偷改写 |

实际错误语义以[官方错误码](https://docs.bigmodel.cn/cn/faq/api-code)为准。给支持人员的信息应包括时间、模型、request_id、HTTP/业务错误码；不要附Key、Authorization头或敏感录音。

## 9. 接入本项目托盘：设计要求，尚不可执行

**当前还没有实现托盘程序，因此下面是将来交付后的配置流程，不是现有菜单截图或已可用功能。**

1. 运行交付的Windows C++托盘程序，打开届时提供的ASR配置入口。
2. 选择BigModel中国站、`glm-asr-2512`，核对固定接口地址。
3. 通过隐藏输入填写Key，保存到当前Windows用户凭据管理器。项目将自行管理准确的凭据目标名；现在不要猜名称并用`cmdkey`手工写入。
4. 界面仅显示“已配置/未配置”，不回显完整Key。Key不写`opencode.json`、WSL环境、设备固件或Git仓库。
5. 若提供“测试连接/识别”按钮，必须明确区分：只检查本地配置不调用模型；真实识别测试需提示上传与可能费用。
6. 配对StickS3，通过专用命令启动并绑定OpenCode，再按住设备录音。识别结果只追加到输入框，用户检查后手动发送。

本项目无自动ASR重试：失败最多在电脑内存保留5分钟；托盘“重试识别”会重新调用付费API，“重试填入”不会。锁屏/睡眠/休眠取消清理，原有已填入草稿不会被删除。

最终菜单名称、凭据目标名、安装路径和启动命令应由实际交付文档给出，不能把本手册设计说明当作已安装软件。

## 10. Key轮换、撤销与费用检查

### 正常轮换

1. 在官方控制台创建新Key并确认相同ASR权限。
2. 通过托盘配置替换凭据；如需真实测试，只执行一次获授权调用。
3. 验证正常后撤销旧Key，并核查旧Key后续调用是否停止。

### 疑似泄露

立即在官方控制台撤销受影响Key，再更换凭据并查看异常用量；不要只从本地删除文件就认为Key失效。不要等待新Key测试成功才撤销已泄露Key。

### 日常核查

- 查看真实模型与请求量是否符合使用频率。
- 失败请求是否重复发起；自动化真实云测试是否有独立授权与预算。
- 识别用量与OpenCode聊天模型用量分别核对，状态屏费用不含ASR账单。
- 取消本地录音或清理缓存不等于供应商删除已经收到的音频；云端以服务协议和隐私政策为准。

## 11. 申请与配置完成清单

- [ ] 账号及必要认证完成，当前项目/账号归属正确。
- [ ] 已核对`glm-asr-2512`权限、当前价格、额度和测试预算。
- [ ] 专用Key妥善保管，未写进源码、截图、日志或聊天。
- [ ] 已准备有权上传的≤30秒、≤25MB非敏感WAV/MP3。
- [ ] 明确同意一次真实识别调用可能收费。
- [ ] 实际HTTP响应和非空text已核对，平台用量记录已查看。
- [ ] 若尚无托盘程序，只标记“云API准备完成”，不声称StickS3/OpenCode集成完成。
- [ ] 交付后Key进入Windows凭据管理器，并按真实设备用例验收。

本手册编写时上述账号/付费操作均未代你执行。相关模块与计费关系见[功能模块HLD](./StickS3_OpenCode_功能模块HLD.md)。
