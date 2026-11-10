# 优化 README 与相关说明文档

## Goal

在 `dev` 分支优化 `README.md` 及相关说明文档（编译说明等）。先完成简体中文版本，用户认可后再翻译为英文版本（`README_EN.md` / `*_en-US.md`）。

## 范围（待确认）

- `README.md`（zh-CN，主）
- `README_EN.md`（en，最后翻译）
- `GithubRes/CompilationProcess_zh-CN.md` / `_en-US.md`
- 可能涉及：`NOTICE` 中的组件清单一致性（仅在用户同意时）
- 不涉及：`TOS/*`（法律文本，除非用户要求）、任何源码 / 构建配置

## 现状问题清单（2026-11-09 调查）

### A. 事实性错误 / 过时信息
1. 版权年份 `2023-2025`，已到 2026。
2. 徽章仍指向旧仓库 `Alan-CRL/IDT`（issues / stars），而非 `Alan-CRL/Inkeys`。
3. 分支说明过时：文中写 `main / insider / dev`；远端实际为 `main / dev / canary / feature/* / inkeys1-final / inkeys2-final`，没有 `insider`。
4. 第三方组件表与实际依赖不一致：
   - vcpkg 中有 `freetype`、`msgpack`，表中未列；`HiMsg`（子模块）未列。
   - `HiEasyX` 在 `Inkeys/additional` 中已不存在，表中仍列出（需确认是否仍使用）。
   - `hashlibpp` 作者名乱码 `Gr¸delbach`（应为 Gründelbach），NOTICE 中同样。
5. 介绍视频为 25H2，需确认是否有更新版本；"常见问题"无链接。
6. Windows 7 / 32 位 / ARM64 支持声明需与当前版本核实。

### B. 中英文不一致
1. 英文版 TOS 链接指向中文条款（`/tos/zh-cn`、`TOS/zh-CN.md`），而仓库已有 `TOS/en-US.md`。
2. 英文版"反馈"缺少问卷与 QQ 群；下载链接顺序与内容不同。
3. 作者名：中文 `陈润林`，英文 `Chen YunLam`。→ 已确认以 i18n `HomeCopyright` 为准：`AlanCRL (Chen Runlin / Chen Yunlam) Studio`。
4. 英文 cover 两张图写在同一行，中文分两行。

### C. 结构与可读性
1. 顶部是版权 + 双仓库地址，项目名/简介被挤到下方；链接写法 `[Github](url)(url)` 重复。
2. 缺少功能亮点（Features）、截图/动图之外的功能说明、快速开始。
3. "集锦(软件介绍)"命名不直观；"其他内容"中的旧链接可收纳。
4. 代码签名、编译说明、分支说明、第三方许可等开发者信息与用户信息混排，无清晰分区。
5. 第三方许可大段表格占 README 大半篇幅，可考虑折叠（`<details>`）或移至 NOTICE。
6. 小错误：`certficate` 拼写；"本使用条款"出现在 README 引用块中（语境来自 TOS 复制）。

### D. 编译说明（CompilationProcess）
1. 子模块路径写 `.\vcpkg\bootstrap-vcpkg.bat`，实际目录为 `Vcpkg`（Windows 不区分大小写，但应统一）。
2. 仅提及 `Release | Win32` 示例；未说明 solution 中测试项目（`PptCOM.Tests`、`InkeysHeadlessTests`）的作用。
3. `PptCOM` 部分说明较长、面向维护者，可与普通贡献者流程分开。

## 已确认决策（2026-11-09）

- 范围：README 中/英、编译说明中/英、NOTICE 同步。
- 改动幅度大；由 AI 先给推荐结构，用户逐项指挥定稿。
- 分支说明内容由用户后续提供，AI 仅给结构位置。
- 功能亮点要点由用户提供，AI 负责组织文字。

## 推荐 README 结构（草案，待用户审定）

1. 头部（居中）：Logo → 项目名 → 一句话简介 → 徽章（Release/下载/Issues/Stars/License/QQ 群）→ 语言切换 → 下载 | 官网 | 常见问题
2. 封面图（深/浅色）
3. 简介：2~3 句定位 + 原名 IDT 说明
4. 功能亮点：分组列点（用户提供）+ 介绍视频
5. 下载与系统要求
6. 反馈与社区
7. 使用条款
8. 开发者：编译（链接编译说明）/ 分支说明（用户提供）/ 代码签名策略
9. 许可证与致谢：GPL-3.0；第三方组件表放入 `<details>` 折叠，完整清单链接 NOTICE / ThirdpartyLicenses
10. 页脚：版权 + 仓库地址（GitHub / GitCode）+ 历史资料（旧 README、旧版视频）

## Requirements

- 按上述（审定后的）结构重写 `README.md`，修正现状问题清单 A~C。
- 修正编译说明问题 D。
- `NOTICE` 与 README 组件表同步（以实际依赖为准）。
- 中文定稿后再翻译英文版，英文版链接指向英文资源（TOS en-US、en 官网）。

## Acceptance Criteria

- [x] 中文 README 经用户审阅认可
- [x] 英文版与中文版结构、链接、事实一致
- [x] 所有链接有效、指向正确仓库与语言版本
- [x] 保持原文件编码与换行符；不改动源码与构建配置

## 完成记录（2026-11-10）

- README 改为三语：`README.md`（en-US，默认）、`README_zh-CN.md`、`README_zh-TW.md`；结构为顶部块 → 五段简介 → 图片集锦 → 时间线卡片图 + AI Agent 说明 → 下载与安装 → 反馈与交流 → 参与开发 → 许可与声明 → 项目引用（表格折叠）→ 版权声明 → 其他内容。
- 编译指南合并为英文 `BUILDING.md`，贡献指南为英文 `CONTRIBUTING.md`；删除 `README_EN.md` 与 `GithubRes/CompilationProcess_*.md`。
- 非简中文档的 TOS 链接统一指向 en-US；TOS 文件未改动；SignPath 框逐字保留。
- 时间线图由 `website/media/inkeys-readme-timeline/tools/render-timeline.cjs` 生成（三语 × 浅/深）。
- 用户决定保持现状的遗留项：「常见问题」暂无链接；图片集锦后三组暂无配图；Social Preview 由用户手动上传；`icomoon.ttf` 未列入字体声明；`PptCOM/PptCOM.cs` 头部旧说明未改；`.trellis/spec/native-desktop/directory-structure.md` 仍引用 `CompilationProcess_*.md`。
