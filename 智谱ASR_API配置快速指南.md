# 智谱ASR API配置快速指南

> 适用项目：StickS3 × Windows C++托盘 × WSL OpenCode。
> 核对日期：2026-09-16；2026-09-20 复核官方模型页，用户确认充值已完成，首次真实调用仍未执行。本文是快速上手版，费用/合规/安全细节见[详尽操作手册](./智谱ASR_API申请与配置操作手册.md)。
> ASR 是语音识别接口，不是聊天模型 API。真实调用可能计费，音频会上传云端。

## 1. 申请 API Key（约5分钟）

```text
① 注册/登录 bigmodel.cn → ② 按需完成实名认证 → ③ 控制台创建 API Key
```

| 步骤 | 操作 | 官方入口 |
| --- | --- | --- |
| ① | 打开智谱开放平台，手机号注册/登录 | <https://open.bigmodel.cn> |
| ② | 按页面提示完成个人/企业认证，确认新用户额度 | 平台内提示 |
| ③ | 个人中心 → API Keys → 添加新的 API Key | <https://bigmodel.cn/usercenter/proj-mgmt/apikeys> |

**注意事项**：

- 建议为 Key 添加备注（如 `StickS3-OpenCode-ASR`），方便日后轮换与撤销。
- Key 只在部分页面显示一次，丢失只能重建，不要截图保存。
- 不要把 Key 写进源码、Markdown、环境变量或发给别人；本项目交付后存入 Windows 凭据管理器。
- 不从未明"API代理/免费Key"网页申请；不要把中国站 Key 与国际站 `api.z.ai` 混用。

## 2. 本项目配置参数（速查表）

| 配置项 | 值 |
| --- | --- |
| 服务区域 | 智谱 BigModel 中国站 |
| 接口地址 | `https://open.bigmodel.cn/api/paas/v4/audio/transcriptions` |
| 模型 | `glm-asr-2512` |
| 鉴权 | HTTP Header：`Authorization: Bearer <API_Key>` |
| 请求格式 | `multipart/form-data`（由 HTTP 客户端自动生成 boundary） |
| 音频字段 | `file`（本项目上传内存 WAV） |
| 流式开关 | `stream=false` |
| 音频格式 | `.wav / .mp3`，≤25 MB、时长 ≤30 秒 |
| 本项目音频规格 | 16 kHz、单声道、16 位 PCM WAV |
| 成功字段 | 响应 JSON 中的 `text` |
| 热词（可选） | `hotwords` 数组，最多 100 项 |
| 上下文（可选） | `prompt`，长文本场景传入之前转录结果，建议 <8000 字 |

**模型能力（官方模型页 2026-09-20 复核）**：支持普通话及四川话、粤语、闽南语、吴语等主要方言；英语支持美式/英式等多口音；另支持法、德、日、韩、西、阿等数十种语言。厂商公布字符错误率 CER 0.0717，此为厂商口径，不作为本项目验收依据——验收仍按固定 15 条普通话用例执行（2026-09-24 修订）。

**不要误用**：

- `glm-asr`（旧版）、`GLM-ASR-Nano-2512`（开源版）不是本项目锁定的云模型 ID。
- `/chat/completions` 是聊天端点，不是音频上传接口。
- 不要仅为 ASR 购买 GLM Coding Plan——编码套餐与 ASR 调用权益不通用。
- 把 Opus 文件改扩展名为 `.wav` 不是格式转换。

## 3. 调用示例

### 3.1 cURL（Windows PowerShell 可直接用 `curl.exe`）

```bash
curl -X POST "https://open.bigmodel.cn/api/paas/v4/audio/transcriptions" \
  -H "Authorization: Bearer YOUR_API_KEY" \
  -F model=glm-asr-2512 \
  -F stream=false \
  -F file=@speech.wav
```

流式转录（`stream=true`）通过 Event Stream 逐块返回，结束时收到 `data: [DONE]`。

### 3.2 Python（SDK）

新版 SDK：

```python
from zai import ZhipuAiClient

client = ZhipuAiClient(api_key="YOUR_API_KEY")

with open("speech.wav", "rb") as f:
    resp = client.audio.transcriptions.create(
        model="glm-asr-2512",
        file=f,
        stream=False,
    )
print(resp.text)
```

旧版 SDK（`pip install zhipuai`）：

```python
from zhipuai import ZhipuAI

client = ZhipuAI(api_key="YOUR_API_KEY")

with open("speech.wav", "rb") as f:
    resp = client.audio.transcriptions.create(
        model="glm-asr-2512",
        file=f,
        stream=False,
    )
print(resp.text)
```

### 3.3 成功响应结构

```json
{
  "id": "任务ID",
  "created": 1758000000,
  "model": "glm-asr-2512",
  "text": "这是语音识别测试，请保留英文 API。"
}
```

### 3.4 首次验证建议

1. 准备 3～5 秒非敏感普通话录音（如"这是语音识别测试，请保留英文 API。"），不含密码、客户信息或机密。
2. 优先用官方接口页的 "Try it" 入口：<https://docs.bigmodel.cn/api-reference/模型-api/语音转文本>；也可用官方体验中心：<https://bigmodel.cn/trialcenter/modeltrial/voice>
3. **点击运行前确认：可能计费**。只点一次，页面慢不要连点。
4. 核对 HTTP 200 且 `text` 非空，再到控制台用量/账单页核对这次请求。
5. 带人工确认与隐藏输入的 PowerShell 安全验证脚本见详尽手册第 7 节。

## 4. 常见错误速查

| 现象/业务码 | 可能原因 | 处理 |
| --- | --- | --- |
| HTTP 401，1000/1001/1003 | Key 无效/缺认证头/Token 失效 | 核对 Key、域名、Bearer 格式 |
| HTTP 403，1220 | 无权访问 API | 检查账号/模型权限，必要时官方工单 |
| HTTP 400，1210/1211/1213/1214 | 参数/模型 ID/字段格式错误 | 核对模型 ID、file 字段、音频限制 |
| HTTP 429，1113 | 账号欠费 | 核对余额，确认后再充值 |
| HTTP 429，1302/1305 | 速率限制/服务繁忙 | 稍后手动重试 |
| HTTP 429，1311/1315 | 套餐未开放该模型 | 核对产品类型，Coding Plan 不覆盖 ASR |
| HTTP 200 但 `text` 为空 | 音频无有效识别结果 | 检查音频可听性与格式 |
| 超时（curl 退出码 28） | 网络或代理问题 | 先查调用记录再决定重试，避免重复计费 |

完整错误码见官方文档：<https://docs.bigmodel.cn/cn/faq/api-code>

## 5. Key 安全与轮换（摘要）

- **存储**：交付后由托盘程序存入 Windows 凭据管理器；之前临时验证用隐藏输入，不落盘。
- **轮换**：控制台建新 Key → 托盘替换凭据 → 验证一次 → 撤销旧 Key。
- **疑似泄露**：立即在控制台撤销，再检查异常用量；不要只删本地文件。
- **日常核查**：识别用量与 OpenCode 聊天模型用量分别核对。

## 6. 相关文档

| 文档 | 关系 |
| --- | --- |
| [智谱ASR API申请与配置操作手册](./智谱ASR_API申请与配置操作手册.md) | 详尽版：费用纪律、安全验证脚本、错误全表、托盘设计要求 |
| [功能模块HLD](./StickS3_OpenCode_功能模块HLD.md) | ASR 模块在整体架构中的位置与计费关系 |
| [测试用例与追踪矩阵](./StickS3_OpenCode_测试用例与追踪矩阵.md) | 15 条识别质量 / 15 次时延验收（2026-09-24 修订） |
| [StickS3_OpenCode_文档索引](./StickS3_OpenCode_文档索引.md) | 项目文档总索引 |

## 7. 配置完成清单

- [ ] 账号注册/认证完成，确认 `glm-asr-2512` 权限与额度
- [ ] 专用 Key 已创建并妥善保管（未写入源码/截图/聊天）
- [ ] 已准备 ≤30 秒、≤25 MB 的非敏感 WAV/MP3
- [ ] 明确同意一次真实调用可能收费
- [ ] 实际响应 HTTP 200 且 `text` 非空，用量记录已核对
- [ ] 托盘交付后 Key 迁入 Windows 凭据管理器
