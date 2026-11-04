# Implement — 统一主栏与组件 SVG 图标设计语言

依次执行；每个评审门未通过不得进入下一阶段。

## 阶段 A：设计稿（仅 `research/` 下产出，不改产品文件）

- [x] A1 在 `research/icons/` 下按 `design.md` §2 规范绘制全部新/重绘 SVG（§3.1–3.4 共 5 + 3 + 16 + 2 + 1 + 9 个）。
- [x] A2 编写静态校验脚本 `research/lint_icons.py`：断言 24 画布、仅 `rgba(10,0,7,0)`、无 stroke/transform 等禁止元素与属性、坐标不越界（线宽由设计源常量保证）。对 A1 全部文件运行通过。
- [x] A3 生成预览页 `research/icon-preview.html`：新图标与基准图标（select/brush/eraser/clean/setting/recall）并排；浅色（Surface `#F7F8FA` / IconPrimary `#474D52`）与深色两种主题；14/18/20/28 DIP 四种尺寸；选中态用 Accent。
- [x] **评审门 1**（2026-11-03 开发者确认全部通过）：开发者在预览页确认图形与一致性；按反馈迭代 A1–A3。

## 阶段 B：资源落地

- [x] B1 将确认后的 SVG 写入 `Inkeys/src/UI/`：重绘项覆盖原文件，新增项新建文件。
- [x] B2 `Inkeys/Inkeys.rc`：新增 `BARRETURNSHOW`、`BARRESUMEWHITEBOARD`、`BARMOVE`、`BARERASERCLEARCANVAS`、`BARERASERSPEEDSETTING`、16 个 `COMP*`；删除 `BARDISMISS`、`BARDIVIDER`、`LOGO2`、`CUSTOMIZEICO1`–`11`。保持 UTF-8 + CRLF。
- [x] B3 `Inkeys.vcxproj` / `.filters` 同步 `<None Include>` 增删。
- [x] B4 复核后删除不再引用的源文件（`barDismiss.svg`、`barDivider.svg`、`logo2.svg`、组件 PNG）；复核命令：`rg -n "CustomizeIco|barDismiss|barDivider|logo2|ClassIsland\.png|IslandCaller\.png|SecRandom\.png|NamePicker\.png" Inkeys --glob "!Cache/**"`，仅允许 `IdtFloating.cpp`（未编译）命中。

## 阶段 C：代码

- [x] C1 `Bar.Button.cpp` `registerComponent`：PNG → SVG（design §4.1），16 处调用改为 `comp*` 资源名。检查 `iconKind == Png` 相关分支是否仍有使用者。
- [x] C2 `Bar.Button.cpp` 白板按钮状态图标（`barReturnShow` / `barResumeWhiteboard`）与选择按钮「移动」（`barMove`）。
- [x] C3 `Bar.EraserAttribute.cpp:122` 显式映射新资源。
- [x] C4 测试迁移：`Bar.EraserAttribute.Test.cpp:1244-1245`；`Bar.Presentation.Test.cpp` SVG 绑定探针。
- [x] C5 文件保持原编码与换行（UTF-8 BOM/CRLF 按原文件），不格式化无关代码。

## 阶段 D：验证

- [x] D1 `research/lint_icons.py` 对 `Inkeys/src/UI/` 中本任务涉及文件全部通过。
- [x] D2 构建：`MSBuild.exe InkeysRepo.sln /p:Configuration=Debug /p:Platform=<当前原生架构>`（超时 ≥5 分钟），零错误。
- [x] D3 headless 测试（含 Bar/Presentation/EraserAttribute 相关测试入口）通过。
  - 2026-11-03：`InkeysHeadlessTests.exe --no-window` 退出 0；`Inkeys.exe --bar-eraser-offscreen-test` 仅 1 项失败「PageSurface dark/light pixels follow the same UI3 surface color as the main bar」，换回原分页图标重建后同样失败，属既有问题，与本任务无关。UI3 Presentation harness（Bar.Presentation.Test）需 GUI，未运行。
- [ ] D4 人工验证（无法执行的项须如实标注）：浅/深主题下主栏、More 面板、组件按钮、白板（打开/最小化/回到白板/返回放映/移动）、PPT 结束放映页、橡皮面板、提示气泡、关闭按钮显示正常且着色随选中变化。
- [x] **评审门 2**：trellis-check 全量检查。
  - 2026-11-03：资源名/rc/vcxproj/filters/磁盘一致，36 个图标与 `research/icons/` 字节一致且 lint 通过；Debug|ARM64 Solution 0 错误，headless 退出 0；offscreen 唯一失败（PageSurface dark/light）在 HEAD 4606b067 临时 worktree 同样失败，确认为既有问题。

## 阶段 E：收尾

- [x] E1 更新 spec：`rendering-and-ui.md` 中 EndShow 资源描述（实心规则）；在 `bar-theme-material.md` 或新增小节记录图标设计语言与占位色规则。
- [x] E2 提交（不含 Claude 署名）。2026-11-04 提交 v2 图标；任务保持进行中，待 D4 人工验证与后续反馈。

## 阶段 F：v2 视觉修订（2026-11-03 开发者反馈）

- 反馈：v1 主栏/状态/组件图标比画笔、橡皮显小显轻（v1 只用 2–22 活动区，基准图标满幅、Bar 按高度缩放）；几何恢复原版；白板「移动」改手形并注意圆角；橡皮面板「清空画布」恢复垃圾桶 `barClean`。
- [x] F1 设计源改为满幅（0.5–23.5）、实心更重；产出 `research/icons_v2/` 与 `research/icon-preview-v2.html`（`preview_v2_build.py`）。
- [x] **评审门 3**（2026-11-03 开发者确认 v2，含雪花减重、A 字腔镂空、NamePicker 改人形 + 姓名牌）：把 `icons_v2/` 写入 `Inkeys/src/UI/`、几何恢复原文件、`Bar.EraserAttribute.cpp` 项 0 改回 `barClean` 并删除 `barEraserClearCanvas` 资源、测试同步、重新构建与 headless 测试。

## 回滚点

- 阶段 B 前：只有 `research/` 产出，丢弃即可。
- C1 独立可回退（恢复 PNG 组件）；B/C2/C3 随资源一起回退。
