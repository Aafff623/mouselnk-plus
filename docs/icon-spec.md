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

## 2. 造型方向（owner 2026-10-09 选定：手势弧线）

- **一个实心圆**作为底，圆内**一道粗圆头弧线**，弧线自右下扫向左上——象征鼠标快速划过的轨迹。
- 圆与弧线是两个独立形状，**不加内环、不加点、不加箭头、不加第二条线**。
- 弧线与圆之间保留明确留白，弧线不得触碰圆边。

**方向选择的理由**：参考程序 MouseInc 用圆角矩形轮廓、Aitiy 用紫色圆角方块 + 白色指针。本方向用**圆形 + 弧线**，与两者在构图层面就不同，避开相似性风险。

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
| 3. **几何重绘** | 按选定造型用几何方式重绘成矢量图形（圆 + 圆弧），程序化输出 | **AI 栅格图直接缩到 16px 会糊、边缘发虚**；tray 图标的主要尺寸就是 16px，必须由几何图形直接栅格化，才能在每个尺寸都锐利 |
| 4. 多尺寸 ico | 16 / 24 / 32 / 48（可加 256）各尺寸独立栅格化，非缩放 | 每个尺寸单独优化笔宽与留白 |
| 5. 落地 | 替换 `assets/app.ico` 与 `assets/app_paused.ico`，并把生成脚本收进 `temp/scripts/` | 保持可重新生成 |

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
