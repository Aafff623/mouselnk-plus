# 图标规范（mouselnk-plus）

本文件定义产品图标的造型方向、出图规范、产出流水线与验收标准。

出图流水线参考 logocreator（https://github.com/Nutlope/logocreator ）的做法：用文生图模型批量出极简扁平的概念候选 → 人工选定 → 再产出最终资产。注意 logocreator 只导出 PNG 且**尚无 SVG**，所以**最终图标由几何重绘产出**，不走「AI 成图直接缩成 ico」这条路（见第 4 节的原因）。

---

## 1. 用途与形态

| 用途 | 尺寸 | 背景 |
|---|---|---|
| 系统托盘（常驻显示，最苛刻） | 16 / 20 / 24 px | 透明 |
| 任务栏 / 窗口 / exe 图标 | 32 / 48 / 256 px | 透明 |
| 设置界面标题 | 24–32 px | 透明或浅底 |

需要**两态**：**正常**与**暂停**（暂停态必须能从图标本身看出来，不能只靠提示文字）。

## 2. 造型方向（owner 2026-10-09 定为「在 MouseInc 图标基础上创新一点点」）

**最终设计（G4，已装入 `assets/`）**——owner 从 8 张 AI 概念稿里选定 **M-08**，G4 是它的几何重绘版：

- **中蓝圆角方块**底（`#4069C2`）
- 方块内是**纯白的蛋形鼠标剪影**（俯视、无描边）
- 鼠标**顶部中央有一道向下开口的缝**，约占鼠标高度的三分之一

**比例取自对 M-08 的实测**（脚本 `temp/scripts/make-icon-m08.py`）：

| 量 | 实测值 |
|---|---|
| 底形圆角半径 / 底形宽 | 0.253 |
| 鼠标宽 / 底形宽 | 0.462 |
| 鼠标高 / 底形宽 | 0.670 |
| 底形四边留白 | 0.045 × 画布 |
| 方块主色 | M-08 的渐变是 `#5380D1`（上亮）→ `#2E52B3`（下暗），取中间调 `#4069C2` |

**有意做的三处简化**（16px 必须取舍，写在这里避免以后被当成遗漏）：

1. **去掉渐变**——M-08 的方块有明暗渐变，扁平化后小尺寸更干净，也让两态（正常/暂停）切换时只有颜色变化。
2. **去掉投影**——原稿鼠标带投影，16px 下只会糊成脏边。
3. **去掉鼠标顶部那道斜向按键分缝**——M-08 顶部有一条把左右按键分开的斜缝，属于「16px 一定糊」的细节层次。

另外**缝改成从顶部开口**（而非封闭细槽）：开口缝改变的是外轮廓，比内部封闭槽更抗小尺寸；原稿的细直缝在 16px 下会直接消失。**代价**：弧线（手势语义）在 16px 下基本不可见，只有 24px 以上才体现——正因如此，下面 G5 与 V3 这类「内部挖空弧」的方案在托盘尺寸下并不占优。

**与 MouseInc 图标的关系**：保留「深色底 + 白色鼠标」这个**品类符号**，让它一眼看得出是同类工具；差异在——① 实心剪影而非描边；② 我方实测比例；③ 中蓝而非黑白单色；④ 取消滚轮槽的封闭造型、改为顶部开口缝。

**边界声明**：只借用了「鼠标」这个**通用图形符号**与「深色底 + 白色图形」的通用做法，**没有照搬**参考程序图标的比例、线宽、构图细节与配色；参考程序的图标文件本身未被复制、未被反编译提取。这条已在 `AGENTS.md` 的边界里收窄为「不得照搬参考程序图标的具体设计」。

其余候选（保留以便回退）：

| 候选 | 造型 | 小尺寸表现 |
|---|---|---|
| G5 | 同 G4，但把缝换成**内部挖空的手势弧** | 弧线 16px 下基本不可见，24px 以上才有效果 |
| V1 | 圆角方块底 + **描边风格**鼠标轮廓 + 弧 | 细描边在 16px 会虚成灰线，偏弱 |
| V2 | **正圆**底 + 描边鼠标轮廓 + 弧 | 同上；圆形底区分度最高 |
| V3 | 圆角方块底 + 实心剪影 + **内部挖空弧** | 实心剪影清晰，但内部弧在 16px 消失 |

所有候选（正常/暂停两态、16/24/32/48 各尺寸）与放大 8 倍对照图在 `temp/preview/icon-geometric/`；
可视化对比页：`temp/preview/图标候选对比.html`（本地预览，均不入库）。

## 3. 配色

| 角色 | 颜色 | 说明 |
|---|---|---|
| 圆底（主色） | 深靛蓝 **`#3F3D80`** | 避开 MouseInc 的橙（`#E47542`）与 Aitiy 的紫（`#8b5cf6`） |
| 弧线 | 白 **`#FFFFFF`** | 与主色形成高对比，16px 下仍分离 |
| 暂停态 | 主色保留，弧线换成**两条粗圆头竖条**（暂停符号） | 两态共用同一圆底，切换时位置不跳 |

暂停态也可选择「整圆降饱和为中性灰 + 保留弧线」的变体，最终以 16px 实际观感为准。

## 4. 产出流水线

| 步骤 | 做法 | 为什么 |
|---|---|---|
| 1. 概念稿 | `mmx image generate`（image-01），1:1，多张候选 | 沿用 logocreator 的批量候选思路 |
| 2. 选定 | owner 从候选中挑 | 造型取向由 owner 定 |
| 3. **几何重绘** | 按选定造型用几何方式重绘成矢量图形，程序化输出 | **AI 栅格图直接缩到 16px 会糊、边缘发虚**；tray 图标的主要尺寸就是 16px，必须由几何图形直接栅格化，才能在每个尺寸都锐利 |
| 4. 多尺寸 ico | 16 / 24 / 32 / 48（可加 256）各尺寸独立栅格化，非缩放 | 每个尺寸单独优化笔宽与留白 |
| 5. 落地 | 替换 `assets/app.ico` 与 `assets/app_paused.ico`，并把生成脚本收进 `temp/scripts/` | 保持可重新生成 |

### 实测结论：AI 只适合探索概念，不适合产出最终图标

两次批量共 **8 张**生成结果（`temp/preview/icon-concepts/`、`icon-concepts2/`）显示同一类失败：`image-01` **无法执行「鼠标轮廓 + 内部负空间弧线」这种精确几何构造**。它给出的分别是——

- 3D 写实渲染的鼠标（带投影、接缝，缩到 16px 完全不可用）
- 与鼠标无关的色块
- 细直缝或细月牙（与 MouseInc 的滚轮槽反而更像）
- 并且**会无视提示词里「不要阴影／不要渐变／不要 3D」的排除项**

因此：**继续微调提示词属于低信息量重试**，最终资产改由几何绘制产出（第 3 步），AI 仅用于早期概念探索。这条结论对后续「需要精确几何的图形资产」都适用。

## 5. 出图规范（写给文生图模型的提示词规则）

规则来源：`ip-as-logo` 的提示词纪律 + logocreator 的极简扁平取向，针对**几何标记**（非 IP 角色）做了必要调整——几何标记应居中对称，不适用「角色从角落探出」那条。

1. **不要把用途说出来**：提示词里不出现 `logo`、`icon`、`app icon`、`brand mark` 这类词，只描述画面本身。这是为了避免模型激活「logo 模板」式的构图偏见（带边框、卡片、文字）。
2. **语义颜色最多三种**：主色 + 辅助色（弧线）+ 底色，不用第四种。
3. **形状预算 2–4 个**：一个圆 + 一道弧，仅此而已。
4. **明确排除**：文字、水印、边框、卡片、多余形状、场景、写实材质、3D 斜面、投影、高光、纹理、暗角、光照渐变。
5. **1:1 正方形，画面填满**；标记居中，占画面约 70–75%。
6. 生成时的底色用一个**浅中性色**（便于人眼评估造型）；最终图标是透明背景，底色不进入产物。

### 生成用提示词（英文，当前版本）

```text
Create one complete full-bleed 1:1 square image.
Background: fill the entire square with solid soft off-white #EDEFF3, perfectly uniform, no texture, no vignette, no lighting variation.
Subject: one single flat graphic mark centered on the background. It consists of exactly two shapes: a solid deep indigo #3F3D80 circle, and one thick white arc stroke with fully rounded ends lying inside the circle, sweeping from the lower right up toward the upper left, like a trail left by a quick flick. The arc keeps a clear even margin from the circle edge and never touches it.
Complexity: only these two shapes. No inner ring, no dots, no arrowhead, no second line, no outline around the circle.
Style: ultra-clean flat graphic, thick rounded stroke, crisp edges, even line weight, no gradient on the arc, no shading.
Composition: the mark occupies about 72% of the square, perfectly centered, upright, with the square image's own corners left square.
Constraints: no text, no watermark, no border or frame, no card or presentation mask, no extra shapes, no scenery, no photorealism, no 3D bevel, no cast shadow, no glossy highlight, no texture, no vignette.
```

> 提示词是**版本化的资产**：改动造型方向时必须同步更新本节，并保留旧版本于 `temp/`，以便对照。

## 6. 验收标准

1. **16×16 下弧线与圆底仍能分离**，弧线不被糊成一个色块——这是最重要的门槛。
2. 24 / 32 / 48 各尺寸目视锐利，无锯齿、无半透明残留。
3. **与 MouseInc、Aitiy 的图标并排看不出构图相似**。
4. 圆内不含文字。
5. 正常态与暂停态在托盘里**一眼可分**。
6. 托盘实际显示效果确认（不能只看放大图）。

## 7. 待定

- 产品对外名称（图标不含文字，故不阻塞）。
- 暂停态的最终取舍（两条竖条 vs 整圆降饱和），以 16px 实测观感决定。
