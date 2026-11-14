# README 头图与 Social Preview

父任务：`11-09-readme-docs-refresh`

## Goal

制作 2:1 头图，放在 README 最顶端，并同时用作 GitHub Social Preview。
共 6 张：{浅色, 深色} × {zh-CN, zh-TW, en-US}。

## 素材来源（已调研）

- 背景配色：官网 `new-index3` 首屏（`website/docs/.vuepress/theme/components/NewHome3/NewHome3.vue:578-609`）
  - 浅色：底色 `#f6f9fb`，叠加三个径向渐变：薄荷 `rgba(136,218,195,.24)`、薰衣草 `rgba(173,163,232,.23)`、珍珠 `rgba(255,236,214,.18)`
  - 深色：底色 `#111820`，叠加三个径向渐变：薄荷 `rgba(36,102,100,.23)`、薰衣草 `rgba(80,65,137,.22)`、珍珠 `rgba(91,76,111,.09)`
  - 径向渐变位置：12%/15%、88%/45%、45%/95%（按首屏比例换算到 2:1 画布）
- 彩虹色艺术字：
  - 方案 A：手写体彩虹墨迹 `media/inkeys3-showcase-assets/overview/backgrounds/wordmark-inkeys.png`（2580×1164，由官网 softPen 绘制器离屏渲染，现有文件只有浅色）
  - 方案 B：首屏 CSS 艺术字（"Ink" 用 Google Sans Flex 650，"eys" 用 DM Serif Display 斜体），配彩虹渐变
  - 彩虹渐变：浅色 `#078b99 → #b3cd33 → #ffb500 → #ff7157 → #ce5897 → #289fca`；深色 `#55ccbc → #dedb68 → #ff9569 → #fa83af → #a194f4 → #62bcf4`
- 主栏预览：`overview/ui-{light,dark}/toolbar.png`（原生离屏渲染，6186×934，实体矩形在 `toolbar-body.json` 里，**语言是 en-US**）
- 图标：`website/docs/.vuepress/public/Inkeys.svg`（矢量）/ `Inkeys/src/icon.ico`（256px）
- 标语：用户提供的三语文案

## 推荐方案

- 画布 2560×1280 母版，导出 1280×640 PNG（GitHub Social Preview 推荐尺寸，需 < 1 MB）。
- 布局（自上而下，居中）：
  1. 左上角：Inkeys 图标 + "智绘教Inkeys"/"Inkeys" 小字品牌名
  2. 中部偏上：彩虹 Inkeys 艺术字（视觉主体）
  3. 艺术字下方：标语（中文一行；英文两行：主句 + 副句）
  4. 下部：主栏预览，带轻投影，像悬浮在桌面上
  5. 底部角标：左下 `© 2023-2026 AlanCRL 工作室`，右下 `Windows · GPL-3.0` 胶囊标签
- 生成方式：在仓库外的脚本目录用 HTML 模板 + 无头浏览器截图（可直接复用官网字体与 CSS 渐变），或用 Python/Pillow 合成；产物只提交 PNG。
- 输出路径（建议）：`GithubRes/hero/hero-{light,dark}-{zh-CN,zh-TW,en-US}.png`
- README 用 `<picture>` + `prefers-color-scheme` 切换深浅色。
- Social Preview 只能上传 1 张（不分深浅色和语言），需要选定其中一张，并由用户手动在 GitHub 仓库设置里上传。

## 已确认决策（2026-11-09）

- 艺术字：手写彩虹墨迹（softPen 离屏渲染）。现有文件只有浅色，深色版要用深色彩虹渐变重新渲染（官网 `softPen.ts createPainter`，参数见 `wordmark-inkeys.json`）。
- 主栏：每种语言单独做原生离屏渲染（zh-CN / zh-TW / en-US × 浅色 / 深色）。由 Codex 协作完成，沿用 `website/media/inkeys3-theme-promo/native/export.py` 这套管线（Inkeys-draw dev、Soft Pen、笔色 `FF1000`、BackgroundOpacity 0 / ContentOpacity 100）。
- Social Preview：浅色 · English。
- 辅助信息：左上角放图标和品牌名；底部一行放版权和「Windows · GPL-3.0」胶囊标签。
- 版权文案严格照搬 i18n 的 `HomeCopyright`（`Inkeys/src/i18n/*.jsonc:233`）：
  - zh-CN：`Copyright © 2023-2026 AlanCRL(陈润林) 工作室`
  - zh-TW：`Copyright © 2023-2026 AlanCRL(陳潤林) 工作室`
  - en-US：`Copyright © 2023-2026 AlanCRL (Chen Runlin / Chen Yunlam) Studio`
- 品牌名：zh-CN `智绘教Inkeys`，zh-TW 以 i18n 中的写法为准（实施时核对），en-US `Inkeys`。

## 实施分工

1. **Codex（Sol High，service_tier=default，workspace-write 仅限 website 媒体目录）**：扩展离屏导出，增加 locale 参数，渲染 6 份主栏 PNG 和对应的 `toolbar-state.json` / `toolbar-body.json`；Inkeys-draw 源码必须按字节还原。构建、运行 exe 由 Claude 执行（Codex 沙箱里不能起进程）。
2. **Claude**：在仓库外的脚本目录渲染深色彩虹字标；写合成脚本（Pillow，2560×1280 母版 → 1280×640 PNG）；先出 1 张浅色 en-US 样稿给用户定稿，再批量出 6 张。
3. 输出路径：`GithubRes/hero/hero-{light,dark}-{zh-CN,zh-TW,en-US}.png`。只提交 PNG，脚本留在 website 媒体目录。

## 实施记录（2026-11-09）

- 分工变更：导出管线本来就支持 `locale`（`HeroScene.inc` 解析场景文件里的 `locale` 行），只需新增 `inkeys3-theme-promo/data/native-scene-readme-{zh-CN,zh-TW,en-US}.txt`，适配器和 C++ 都不用改。Codex 沙箱里不能起进程，所以这一步由 Claude 直接完成，没有调用 Codex。
- 构建源：本仓库 `dev`（571b49ec），`Debug|ARM64`，私有 OutDir。6 次捕获都是 `failures=0`，`source-restoration.json` 为 `unchanged: true`，仓库工作区保持干净。
- 素材与脚本：`website/media/inkeys-readme-hero/`
  - `sources/`：6 份主栏（含 body/state json）、`wordmark-{light,dark}.png`（浅色与原素材逐像素一致）、`Inkeys.svg`
  - `tools/render-wordmark.cjs`：字标渲染
  - `tools/render-hero.cjs`：合成，Edge 无头 + `page.route` 虚拟源
  - `output/`：1280×640 成品，单张约 450–530 KB；`output/master/`：2560×1280 母版
- 踩坑：Edge 在 file:// 页面里不加载 @font-face；`font-family: Serif` 会被当作通用字体族关键字而失效。脚本现在会检查实际用到的字体都已加载。
- 已知：繁中标语用的是 Noto Serif SC，标点按大陆规范靠左下；本机没有 TC 衬线字体。
- 版式定稿（用户认可）：错落版式。字标放大到 900 宽、偏右上；左下是标语（中文两行错落）；右下是主栏，从 x=608 起向右超出画布截断；右上是 Windows / GPL-3.0 胶囊标签；左下角是版权。
- 交付：6 张图直接放在 `GithubRes/hero-{light,dark}-{zh-CN,zh-TW,en-US}.png`（用户要求放在 GithubRes 根目录，不建子目录）。`README.md` / `README_EN.md` 最顶端插入 `<picture>`，按深浅色切换，保持 CRLF / UTF-8 无 BOM。
- 踩坑：本机 Edge 无头实例的 `browser.close()` 偶发挂起 20 分钟以上，截图早已落盘。脚本已改为最多等 10 秒，然后直接退出。
- 待用户操作：在 GitHub 仓库 Settings → Social preview 上传 `GithubRes/hero-light-en-US.png`。

## Acceptance Criteria

- [ ] 6 张 2:1 PNG，单张 < 1 MB，1280×640 下文字清晰
- [ ] 深浅配色与官网首屏一致
- [ ] 彩虹艺术字、标语、图标、协议、版权、平台信息齐全
- [ ] 主栏预览为原生离屏渲染素材，没有手工重绘
- [ ] 用户审阅认可
