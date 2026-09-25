#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 ESP32-S3M5 项目 Token 消耗 Lessons Learnt 汇报 PPT。

数据口径: evidence/token-stats/2026-09-24-token消耗统计.json
(窗口 2026-09-11 ~ 09-24, 已通过 DB cost 对账与归类抽查)

用法: python3 tools/make_lessons_ppt.py
输出: evidence/token-stats/2026-09-24-Token消耗LessonsLearnt.pptx
"""

from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.dml.color import RGBColor
from pptx.enum.text import PP_ALIGN
from pptx.oxml.ns import qn
from lxml import etree

# ---- 主题色
NAVY = RGBColor(0x1F, 0x3B, 0x57)   # 标题深蓝
ACCENT = RGBColor(0xC0, 0x39, 0x2B) # 警示红
GREEN = RGBColor(0x1E, 0x7A, 0x46)  # 行动绿
GRAY = RGBColor(0x55, 0x55, 0x55)
LIGHT = RGBColor(0xF2, 0xF5, 0xF8)
WHITE = RGBColor(0xFF, 0xFF, 0xFF)

FONT = "Microsoft YaHei"


def set_font(run, size=18, bold=False, color=GRAY, name=FONT):
    run.font.size = Pt(size)
    run.font.bold = bold
    run.font.color.rgb = color
    run.font.name = name
    rPr = run._r.get_or_add_rPr()
    ea = rPr.find(qn("a:ea"))
    if ea is None:
        ea = etree.SubElement(rPr, qn("a:ea"))
    ea.set("typeface", name)


def box(slide, x, y, w, h, fill=None, line=None):
    from pptx.enum.shapes import MSO_SHAPE
    shp = slide.shapes.add_shape(MSO_SHAPE.RECTANGLE, Inches(x), Inches(y), Inches(w), Inches(h))
    shp.shadow.inherit = False
    if fill is None:
        shp.fill.background()
    else:
        shp.fill.solid()
        shp.fill.fore_color.rgb = fill
    if line is None:
        shp.line.fill.background()
    else:
        shp.line.color.rgb = line
    shp.text_frame.text = ""
    return shp


def text(slide, x, y, w, h, lines, align=PP_ALIGN.LEFT, valign="t"):
    """lines: [(content, size, bold, color, space_after_pt)]"""
    tb = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = tb.text_frame
    tf.word_wrap = True
    tf.vertical_anchor = {"t": 1, "m": 2, "b": 3}[valign]  # MSO_ANCHOR: 1=top? 用字符串映射
    for i, (content, size, bold, color, after) in enumerate(lines):
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.alignment = align
        p.space_after = Pt(after)
        run = p.add_run()
        run.text = content
        set_font(run, size, bold, color)
    return tb


def slide_new(prs, title=None, page=None, total=None):
    s = prs.slides.add_slide(prs.slide_layouts[6])
    box(s, 0, 0, 13.333, 7.5, fill=WHITE)
    if title:
        box(s, 0.55, 0.38, 12.2, 0.75, fill=LIGHT)
        box(s, 0.55, 0.38, 0.12, 0.75, fill=NAVY)
        text(s, 0.85, 0.42, 11.6, 0.68, [(title, 26, True, NAVY, 0)], valign="m")
    if page:
        text(s, 12.3, 7.05, 0.9, 0.35, [(f"{page} / {total}", 11, False, GRAY, 0)], align=PP_ALIGN.RIGHT)
    return s


def bullets(slide, x, y, w, h, items, size=16, gap=8):
    """items: [(text, bold, color)] 或 (text, bold, color, indent_level)"""
    tb = slide.shapes.add_textbox(Inches(x), Inches(y), Inches(w), Inches(h))
    tf = tb.text_frame
    tf.word_wrap = True
    for i, it in enumerate(items):
        content, bold, color = it[0], it[1], it[2]
        lvl = it[3] if len(it) > 3 else 0
        p = tf.paragraphs[0] if i == 0 else tf.add_paragraph()
        p.space_after = Pt(gap)
        p.level = lvl
        prefix = "▪ " if lvl == 0 else "– "
        run = p.add_run()
        run.text = prefix + content
        set_font(run, size if lvl == 0 else size - 2, bold, color)
    return tb


prs = Presentation()
prs.slide_width = Inches(13.333)
prs.slide_height = Inches(7.5)
TOTAL = 10

# ============================================================ P1 封面
s = prs.slides.add_slide(prs.slide_layouts[6])
box(s, 0, 0, 13.333, 7.5, fill=NAVY)
box(s, 0, 5.05, 13.333, 0.06, fill=WHITE)
text(s, 1.0, 2.0, 11.3, 1.6, [
    ("ESP32-S3M5 项目 Token 消耗", 44, True, WHITE, 6),
    ("Lessons Learnt · 复盘报告", 44, True, WHITE, 0),
])
text(s, 1.0, 4.0, 11.3, 0.8, [
    ("多模型切换协作项目的资源消耗评估与改进依据", 20, False, RGBColor(0xCF, 0xDD, 0xEA), 0),
])
text(s, 1.0, 5.35, 11.3, 1.4, [
    ("统计窗口: 2026-09-11 ~ 2026-09-24 (14 天)", 16, False, WHITE, 4),
    ("数据源: OpenCode 会话数据库 · 83 个项目会话 · 8 个模型 · 费用已与账面计费对账", 16, False, WHITE, 4),
    ("生成: 2026-09-24 · tools/project_token_stats.py", 14, False, RGBColor(0x9F, 0xB4, 0xC8), 0),
])

# ============================================================ P2 TL;DR
s = slide_new(prs, "核心结论一览 (TL;DR)", 2, TOTAL)
cards = [
    ("7.73 亿", "tokens 总消耗", "input 0.19亿 + output 0.011亿\n+ reasoning 0.02亿 + cache重读 7.51亿", NAVY),
    ("$5.27", "费用(真实+估算)", "真实计费 $2.82 (DeepSeek直连账面)\n包月/中转牌价估算 $2.46", GREEN),
    ("83", "项目会话", "主会话 32 + 子代理 51\n覆盖 8 个模型、4 家供应商", NAVY),
    ("97.5%", "缓存命中率", "消耗主体是长上下文缓存重读\n真实成本远低于表面 token 数", ACCENT),
]
for i, (big, label, desc, c) in enumerate(cards):
    x = 0.55 + i * 3.16
    box(s, x, 1.55, 2.95, 2.6, fill=LIGHT)
    box(s, x, 1.55, 2.95, 0.09, fill=c)
    text(s, x + 0.15, 1.8, 2.65, 0.9, [(big, 32, True, c, 0)])
    text(s, x + 0.15, 2.62, 2.65, 0.5, [(label, 15, True, GRAY, 0)])
    for j, ln in enumerate(desc.split("\n")):
        text(s, x + 0.15, 3.1 + j * 0.42, 2.7, 0.45, [(ln, 11.5, False, GRAY, 0)])
box(s, 0.55, 4.55, 12.25, 2.4, fill=RGBColor(0xFD, 0xF3, 0xF2))
box(s, 0.55, 4.55, 0.1, 2.4, fill=ACCENT)
text(s, 0.9, 4.75, 11.7, 0.5, [("三条最贵的教训", 18, True, ACCENT, 0)])
bullets(s, 0.9, 5.25, 11.7, 1.7, [
    ("上下文丢失后靠“查找历史会话”恢复, 烧掉 1.65 亿 tokens (占总消耗 21%) —— 文档化交接是唯一解药。", True, ACCENT),
    ("单会话无限累积: 最大单会话 2.54 亿 tokens。里程碑处开新会话 + 文档外部记忆, 比长会话更省。", True, ACCENT),
    ("计费口径混杂: 只有 DeepSeek 直连有真实账单, 包月/中转 cost=0 —— 不分开口径的费用统计会失真。", True, ACCENT),
], size=14.5, gap=7)

# ============================================================ P3 方法论
s = slide_new(prs, "统计方法: 数据从哪来, 怎么算可信", 3, TOTAL)
text(s, 0.6, 1.45, 6.0, 0.5, [("数据源与口径", 19, True, NAVY, 0)])
bullets(s, 0.6, 2.0, 6.1, 4.6, [
    ("OpenCode SQLite 库 session 表: 每会话自带 tokens_input/output/reasoning/cache_read/cache_write、cost、model、parent_id —— 无需解析聊天记录。", False, GRAY),
    ("窗口: 项目首个会话实为 09-11 16:49 (硬件选型调研), 比文档记载的 09-15 早 4 天 —— 统计窗口必须以数据为准, 不能只信文档。", False, GRAY),
    ("镜像存储 (如 ~/.claude/transcripts) 与无关平台 (Codex) 不重复计入。", False, GRAY),
    ("费用口径三分: billed=DB 真实账面; estimated=包月/中转按牌价估算; unknown=无单价只计 token。", False, GRAY),
], size=14, gap=9)
text(s, 7.0, 1.45, 6.0, 0.5, [("会话归属: 四级归类 + 人工兜底", 19, True, NAVY, 0)])
bullets(s, 7.0, 2.0, 5.8, 3.2, [
    ("① 人工 override (配置文件, 含复核理由)", False, NAVY),
    ("② parent 链继承 —— 子代理跟主会话走", False, NAVY),
    ("③ 标题关键词命中 (项目词/排除词)", False, NAVY),
    ("④ 首条用户消息内容判定; 仍不明 → 待复核清单, 绝不瞎猜", False, NAVY),
], size=14, gap=8)
box(s, 7.0, 5.35, 5.8, 1.5, fill=RGBColor(0xEE, 0xF6, 0xF0))
text(s, 7.25, 5.5, 5.35, 1.2, [
    ("验证: billed 与 DB 直查 SUM(cost) 对账一致;", "X", False, GREEN, 0),
    ("归类经 SQL 溯源 + 人工复核 + 独立重跑三重交叉确认。", False, GREEN, 0),
])

# ============================================================ P4 数据全景
s = slide_new(prs, "数据全景: 按模型与按日分布", 4, TOTAL)
text(s, 0.6, 1.4, 6.0, 0.4, [("按模型 (tokens / 占比 / 费用口径)", 17, True, NAVY, 0)])
rows = [
    ("deepseek/deepseek-flash", "3.49亿", "45%", "$2.04 真实+估算"),
    ("zhipu glm-5.3-flash (包月)", "1.61亿", "21%", "$1.03 估算"),
    ("deepseek-v4-flash (直连)", "1.43亿", "18%", "$0.78 真实"),
    ("deepseek-v4-flash (火山中转)", "1.03亿", "13%", "$0.94 估算"),
    ("nexus-staging gpt-5.6 (中转)", "0.13亿", "1.6%", "无单价, 只计token"),
    ("其余 (glm-5.3 / flash 变体)", "0.05亿", "0.7%", "$0.48 估算"),
]
y = 1.95
for name, tk, pct, cost in rows:
    box(s, 0.6, y, 6.3, 0.62, fill=LIGHT if rows.index((name, tk, pct, cost)) % 2 == 0 else WHITE)
    text(s, 0.75, y + 0.08, 3.0, 0.45, [(name, 12.5, False, GRAY, 0)])
    text(s, 3.7, y + 0.08, 0.95, 0.45, [(tk, 12.5, True, NAVY, 0)])
    text(s, 4.62, y + 0.08, 0.75, 0.45, [(pct, 12.5, False, GRAY, 0)])
    text(s, 5.32, y + 0.08, 1.6, 0.45, [(cost, 11.5, False, GRAY, 0)])
    y += 0.66
text(s, 7.35, 1.4, 5.5, 0.4, [("按日 (亿 tokens) — 消耗极不均匀", 17, True, NAVY, 0)])
days = [("09-11 选型调研", 1.21), ("09-15 设计+验证", 1.75), ("09-21 构建波次", 1.07),
        ("09-22 dayu流程", 2.62), ("09-23 M3测试+修插件", 0.96), ("09-24 上午", 0.10)]
y = 1.95
maxv = 2.62
for name, v in days:
    w = max(v / maxv * 3.1, 0.08)
    box(s, 7.35, y, w, 0.4, fill=NAVY)
    text(s, 7.35 + w + 0.1, y + 0.02, 1.8, 0.35, [(f"{name}  {v:.2f}", 11.5, False, GRAY, 0)])
    y += 0.62
text(s, 7.35, y + 0.15, 5.5, 1.4, [
    ("• 09-16/18/19 零消耗: 阶段间空转日", 13, False, GRAY, 4),
    ("• 单日峰值 09-22 = 总量 34%: 流程性会话是最大变量", 13, True, ACCENT, 0),
])

# ============================================================ P5 Lesson 1
s = slide_new(prs, "教训 1: 上下文丢失的恢复代价 = 总消耗的 21%", 5, TOTAL)
box(s, 0.55, 1.5, 12.25, 1.35, fill=RGBColor(0xFD, 0xF3, 0xF2))
box(s, 0.55, 1.5, 0.1, 1.35, fill=ACCENT)
text(s, 0.9, 1.62, 11.7, 1.1, [
    ("“查找 ESP32-S3-M5 历史会话” × 3 个会话 = 1.65 亿 tokens、$1.37 费用", 20, True, ACCENT, 4),
    ("会话中断/重启后, agent 重新扫描全部历史来恢复上下文 —— 单次恢复成本就相当于数天正常开发。", 14, False, GRAY, 0),
])
text(s, 0.6, 3.15, 6.0, 0.45, [("根因", 18, True, NAVY, 0)])
bullets(s, 0.6, 3.65, 6.0, 3.0, [
    ("项目状态只存在于会话上下文里, 不在可检索的外部存储中", False, GRAY),
    ("历史会话无结构化索引, 恢复只能靠暴力全量扫描 (cache_read 2.5亿/次)", False, GRAY),
    ("多会话并行时, “哪个会话是最新状态”需要靠人肉查找", False, GRAY),
], size=14, gap=9)
text(s, 7.0, 3.15, 6.0, 0.45, [("本项目已有的对策 (继续坚持)", 18, True, GREEN, 0)])
bullets(s, 7.0, 3.65, 5.9, 3.0, [
    ("《文档索引》收工交接节: 每日收工写“当前状态与下一步”", False, GRAY),
    ("新会话先读索引文档即可恢复关键状态, 无需扫历史", False, GRAY),
    ("证据目录 (evidence/) 按日期落盘, 结论可追溯到原始会话", False, GRAY),
], size=14, gap=9)

# ============================================================ P6 Lesson 2
s = slide_new(prs, "教训 2: 单会话累积膨胀 —— 长会话不是勋章", 6, TOTAL)
box(s, 0.55, 1.5, 12.25, 1.25, fill=LIGHT)
box(s, 0.55, 1.5, 0.1, 1.25, fill=NAVY)
text(s, 0.9, 1.62, 11.7, 1.0, [
    ("最大单会话 2.54 亿 tokens (dayu 流程主控), 第二名 1.48 亿 (设计审阅日)", 20, True, NAVY, 4),
    ("每轮对话都要重读全部历史上下文: 会话越长, 每一步的边际成本越高, 且呈超线性增长。", 14, False, GRAY, 0),
])
text(s, 0.6, 3.05, 12.2, 0.45, [("实践准则", 18, True, NAVY, 0)])
bullets(s, 0.6, 3.6, 12.1, 3.2, [
    ("以里程碑/故事为会话边界: 契约冻结、M 节点验收后开新会话, 而非无限续命同一会话", True, NAVY),
    ("新会话冷启动包 = 文档索引 + 当前契约 + 上次交接节, 读取成本 < 一次缓存重读的零头", False, GRAY),
    ("子代理 (subagent) 是天然的“用完即弃”上下文: 51 个子代理共 3.4 亿 tokens, 但不污染主会话", False, GRAY),
    ("反例警示: 把“一个会话干完全部”当成单会话原则来执行 —— 该原则的本意是“单一事实源”, 不是“物理上不换会话”", False, ACCENT),
], size=14.5, gap=10)

# ============================================================ P7 Lesson 3
s = slide_new(prs, "教训 3: 缓存命中率才是真正的成本杠杆", 7, TOTAL)
cards2 = [
    ("97.5%", "cache_read 占比", "7.51亿/7.73亿 tokens\n都是长上下文缓存重读", NAVY),
    ("≈1/10", "缓存命中价/正常输入价", "deepseek cache_read $0.0003/M\nvs input $0.066/M (拟合值)", GREEN),
    ("$0.0004", "实际每百万token均价", "表面 7.73亿 tokens 看着吓人\n账面 $2.82 + 估算 $2.46", GREEN),
]
for i, (big, label, desc, c) in enumerate(cards2):
    x = 0.55 + i * 4.18
    box(s, x, 1.55, 3.95, 2.2, fill=LIGHT)
    box(s, x, 1.55, 3.95, 0.09, fill=c)
    text(s, x + 0.2, 1.75, 3.6, 0.8, [(big, 30, True, c, 0)])
    text(s, x + 0.2, 2.5, 3.6, 0.45, [(label, 14, True, GRAY, 0)])
    text(s, x + 0.2, 2.95, 3.65, 0.8, [(desc.replace("\n", "  "), 11.5, False, GRAY, 0)])
text(s, 0.6, 4.05, 12.2, 0.45, [("怎么做对", 18, True, NAVY, 0)])
bullets(s, 0.6, 4.55, 12.1, 2.4, [
    ("系统提示词 / skill 注入 / 文档集保持稳定 —— 缓存按前缀匹配, 前缀越稳定命中越高", True, NAVY),
    ("把“每次都变的内容” (时间戳、随机摘要、动态菜单) 挪到提示词尾部", False, GRAY),
    ("评估模型成本时看“计费口径下的单故事成本”, 不要看原始 token 总量 —— 缓存重读不等于重付费", False, GRAY),
    ("警惕: 命中率掉到 90% 以下通常意味着前缀被反复破坏 (换模型、换 provider、动态注入)", False, ACCENT),
], size=14.5, gap=9)

# ============================================================ P8 Lesson 4
s = slide_new(prs, "教训 4: 多模型协作 —— 按计费模式分工, 而非按能力喜好", 8, TOTAL)
text(s, 0.6, 1.45, 12.2, 0.5, [("本项目实际形成的分工 (数据实证)", 17, True, NAVY, 0)])
hdr_y = 2.0
for x, w, t in [(0.6, 2.6, "角色"), (3.3, 2.9, "模型/通道"), (6.3, 3.4, "实际消耗"), (9.8, 3.0, "计费模式")]:
    text(s, x, hdr_y, w, 0.4, [(t, 13, True, NAVY, 0)])
box(s, 0.6, hdr_y + 0.38, 12.15, 0.03, fill=NAVY)
rows4 = [
    ("流程主控 (dayu 全生命周期)", "deepseek-flash 直连", "2.54亿 tokens · $1.03", "按量付费 —— 本项目最贵单项"),
    ("日常编排/进度检查", "zhipu glm-5.3-flash", "1.61亿 tokens", "包月 —— 边际成本≈0"),
    ("批量子代理 (TDD红绿波次)", "volcengine glm-5.3", "510万 tokens", "中转包月"),
    ("早期调研/规格验证", "deepseek-v4-flash", "2.46亿 tokens", "按量付费 $0.78 真实"),
]
y = hdr_y + 0.55
for r in rows4:
    for x, w, v, b in [(0.6, 2.6, r[0], True), (3.3, 2.9, r[1], False), (6.3, 3.4, r[2], False), (9.8, 3.0, r[3], False)]:
        text(s, x, y, w, 0.6, [(v, 12, b, GRAY if not b else NAVY, 0)])
    y += 0.62
box(s, 0.55, y + 0.25, 12.25, 1.9, fill=RGBColor(0xEE, 0xF6, 0xF0))
box(s, 0.55, y + 0.25, 0.1, 1.9, fill=GREEN)
bullets(s, 0.9, y + 0.42, 11.7, 1.6, [
    ("重活、流程性、大批量子代理 → 包月/中转通道; 按量付费留给真正需要其能力的关键决策", True, GREEN),
    ("真实账单的 37% 来自一次流程性主控 —— 流程编排这类“通用劳动”不该用最贵的计费通道", False, GRAY),
    ("后续动作: 价目表 (tools/token_prices.json) 按真实账单修正一次, 估算即变精确", False, GRAY),
], size=14, gap=7)

# ============================================================ P9 Lesson 5
s = slide_new(prs, "教训 5: 统计方法论本身踩过的坑 (可复用的治理经验)", 9, TOTAL)
bullets(s, 0.6, 1.6, 12.2, 5.3, [
    ("排除词误杀最大会话: title 含 “dayu” 的项目流程会话 (2.54亿 tokens) 被排除词干掉 —— 关键词归类必须有人工 override 兜底 + 消耗排名 Top10 的人工抽检", True, ACCENT),
    ("时间窗口想当然: 以文档记载的 09-15 为起点会漏掉 09-11~14 的选型调研 —— 统计窗口用数据定, 不用文档定", False, GRAY),
    ("镜像数据重复计数风险: 同一批会话被多平台检出 (OpenCode + 镜像 transcripts), 计数前先识别镜像关系", False, GRAY),
    ("billed/estimated 混桶: cost=0 的会话用拟合价补算后若混入“真实费用”, 对账立刻失真 —— 口径分离 + 自动对账是底线", False, GRAY),
    ("包月成本不可见性: cost=0 ≠ 零成本。不补估算价, 包月模型的消耗就会被系统性低估, 团队会误以为它“免费”", False, GRAY),
    ("占位符驱动的估算: 拟合价标注误差与来源 (如 “平均误差 54%, 勿直接引用”), 让下一个使用者知道能信到什么程度", False, GRAY),
], size=15, gap=12)

# ============================================================ P10 行动清单
s = slide_new(prs, "给下一个项目的行动清单 (开工即执行)", 10, TOTAL)
acts = [
    ("①", "开工基线", "项目启动当天跑一次统计工具, 确定“归零基线”; 统计窗口以首会话时间为准"),
    ("②", "交接纪律", "每日收工更新文档索引的“当前状态与下一步”节 —— 这是防 1.65亿级恢复成本的唯一保险"),
    ("③", "会话切分", "契约冻结 / 里程碑验收后开新会话; 冷启动包 = 索引 + 当前契约 + 交接节"),
    ("④", "通道分工", "批量/流程性工作走包月或中转通道; 按量付费留给关键决策; 中途不换模型保缓存前缀"),
    ("⑤", "月度复核", "定期跑 --review 人工复核待定会话, 补 overrides; Top10 消耗会话抽查归类"),
    ("⑥", "价目维护", "发现 unknown 模型即补价目表; estimated 价格按账单周期性校准, 标注来源与误差"),
]
y = 1.55
for tag, t, d in acts:
    box(s, 0.6, y, 12.15, 0.78, fill=LIGHT)
    box(s, 0.6, y, 0.55, 0.78, fill=GREEN)
    text(s, 0.6, y + 0.13, 0.55, 0.5, [(tag, 16, True, WHITE, 0)], align=PP_ALIGN.CENTER)
    text(s, 1.35, y + 0.08, 1.85, 0.6, [(t, 14, True, NAVY, 0)], valign="m")
    text(s, 3.25, y + 0.08, 9.4, 0.62, [(d, 12.5, False, GRAY, 0)], valign="m")
    y += 0.88
text(s, 0.6, y + 0.12, 12.15, 0.5, [
    ("工具: python3 tools/project_token_stats.py [--html 报告.html] [--json 数据.json] [--review]   ·   报告与数据: evidence/token-stats/", 12.5, False, GRAY, 0),
])

OUT = "/mnt/c/Users/home/esp32-s3-m5/evidence/token-stats/2026-09-24-Token消耗LessonsLearnt.pptx"
prs.save(OUT)
print(f"PPT 已生成: {OUT}  ({TOTAL} 页)")
