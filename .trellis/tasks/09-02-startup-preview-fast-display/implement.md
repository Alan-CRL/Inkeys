# 实施计划：简化 Startup Preview

## 执行原则

- 只修改 Startup Preview 专属实现和其必要调用点；不还原、重置或清理无关工作树改动。
- 先建立可 headless 验证的纯几何、配置校验、shimmer 行程和交接 reducer，再改动窗口/渲染接线。
- 每个阶段保持可构建；删除旧图片方案前先确认引用，避免误删普通 Cache、CRC/SHA、Gaussian 或资源 221。
- 代码注释用简短中文说明关键合同；文档中的验证结果只能写本轮真实执行证据。

## 执行步骤

### 1. 纯几何与配置

- 将 `CacheClassified` 替换为真实 `PreviewGeometryReady` milestone；同步 tracker plan、诊断和 headless 断言。
- 增加/复用无 UI 副作用的总宽度函数与 named constants：`470.0` 默认外包络、`380.0` 默认 MainBar 主体、`80.0` 主按钮、`10.0` 间隔、`80/8 DIP` 高度/圆角。
- 校验 `CachedStartupBarWidthDip` 的 finite、正值、合理上限；缺失/NaN/Inf/过小/异常回退 470。DPI×zoom 只在本次启动换算像素并遵守既有舍入。
- 测试 target `GetW()` + 10 + layoutTotalWidth 的首帧发布，以及相同值不重复写。

### 2. 简化状态/API

- 删除 Startup Preview 专属图片输入、embedded/disk cache parser、CRC/signature/epoch 分类、proxy/staging/capture 状态和接口；保留 owner、RenderPipeline client、ULW、alpha transaction、失败恢复和测试钩子。
- mini config 只传 Enable、CachedStartupBarWidthDip、UI.Bar.Zoom；旧 JSON 字段安全忽略。
- 删除 `--capture-startup-preview` 和 developer capture 输出；保留并改写 `--startup-preview-smoke` 诊断字段。

### 3. 程序化 Preview 绘制

- 在共享 RenderPipeline render thread 创建 32-bpp premultiplied BGRA D2D target、静态圆角矩形 mask，以及复用 MainBar Dark Surface `#181818` / `0.8` 和白色边框 alpha `0.18` 的具名 brush。
- 移除图片上传、cubic、Gaussian blur、padding/effect bounds、live Bar proxy、CPU_READ staging 和 cache writer。
- 用默认左上到右下的斜向 `FillOpacityMask` 调制多段 soft-tail shimmer；4.0 秒周期内以 2.8 秒余弦缓动扫过，再完整离屏停驻 1.2 秒；调用期间切换 `D2D1_ANTIALIAS_MODE_ALIASED` 并无条件恢复。
- 以 Preview 首次显示本地 `steady_clock` epoch 驱动 phase；提取纯函数计算完整非零支撑的离屏起止点，并覆盖不同 width/DPI/zoom 的 phase 0/1、中点穿过 mask 与 wrap 测试；默认方向不得退化为纯水平/纯竖直。
- 在 shimmer 后绘制 192 DIP 居中进度条，沿用 3 秒门、约 180ms 渐显和窄窗口适配。

### 4. ULW 与 owner 合同

- 首次 Preview frame 必须以 `SourceConstantAlpha=0` 完整 ULW committed，然后 owner `SW_SHOWNOACTIVATE` 显示并渐显。
- 每次 ULW 显式提供 `pptDst`、`psize`、`pptSrc`；owner `SetWindowPos` 与 render thread 提交通过 presentation mutex 串行。
- 保留 `WS_POPUP`、`WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`、`WM_MOUSEACTIVATE -> MA_NOACTIVATEANDEAT`、client click swallow；不要加入 `WS_EX_TRANSPARENT`。

### 5. 统一交接与失败恢复

- 正常完成只实现 Preview committed fade-to-zero -> Bar committed alpha 0 到 255；删除 cache-valid/deblur/corrupt 分支和 progress-only fade/透明停顿。
- 首次自动重试先整窗渐隐，保持正常颜色；最终 fatal 在红帧 committed 或有界等待后弹窗，确认后再渐隐 Preview。
- Preview/device/ULW/handoff 失败都走有界 bypass/recovery，尽最大安全努力让 Bar committed 255 后再销毁 owner；RenderPipeline/核心启动失败维持致命。

### 6. 宽度回写与线程边界

- 在首个完整 Bar committed frame 后，从 target `mainButton->GetW()` 和目标 `layoutTotalWidth` 计算总宽度。
- render thread 只发布有限、去重的普通 `double`；主线程或现有配置安全路径比较并写 `main.json`，不在 render thread 执行 I/O。
- 写失败仅记录/忽略；不把物理像素、crop、透明 padding 或 `w.val` 写入缓存。

### 7. 删除资源与工程登记

- 删除 Startup Preview 专属 BIN、生成说明、RC/resource ID 及悬挂 project/filter 条目；保持 PptCOM activation manifest 资源 221 不变。
- 仅在确认无通用职责后删除 `StartupPreview.Format.*`、`StartupPreview.VisualConfig.*`、`StartupPreview.CacheWrite.*`；普通功能同名模块不得误删。

### 8. 文档与验证

- 更新本目录 PRD、context、design、implement、validation、test-matrix、decision-log 以及 `.trellis/spec/native-desktop/startup-preview.md`。
- 执行 `rg` 静态残留检查、`git diff --check`、headless tests、完整 Solution 构建和可行的 smoke；未执行的 Win7/ARM64 runtime/mixed-DPI/人工输入明确标记 Pending。

## 本轮完成状态

步骤 1-7 已落地，并已提交 `9e27bd26707dfeb3204ce78bf53e3b884cf4b811`。2026-09-04 纠偏补丁追加了斜向 shimmer 合同、render-thread 优先资源释放、显式 manual-delay 入口和 Trellis context 恢复；步骤 8 的最新自动验证以 `validation.md` 本轮记录为准。任务保持 `in_progress`，因为 Win7、ARM64 runtime、混合 DPI、多屏和交互视觉项目仍为 NOT RUN。
