# 测试矩阵：简化 Startup Preview

本矩阵只描述最终程序化占位合同。旧 BIN、CRC、图片 cache、blur/proxy、capture 和三类 cache 交接测试已删除，不得以历史结果代替本轮证据。

## 1. 纯逻辑/headless

| 场景 | 必须断言 |
| --- | --- |
| 默认几何 | 当前按钮规则得到 MainBar `380 DIP`、完整外包络 `470 DIP`；Divider 不增加宽度 |
| 缓存值校验 | 缺失、NaN、Inf、零、负值、过小和超过合理上限均回退 `470.0`；合法 double 原样保留为 DIP |
| DPI/zoom | DIP×DPI×zoom 使用既有一致舍入；不把像素值写回配置 |
| 首帧宽度发布 | 使用 `mainButton->GetW()` target + `10.0` + target `layoutTotalWidth`；不读动画 `w.val`、crop 或 padding |
| 去重写回 | 相同/无效发布不重复写；只有有限且实质变化的值交给主线程配置路径 |
| Progress plan | Preview 开启含 `PreviewGeometryReady` 的 20 units；禁用时删除 Preview 专属单位；无 skipped-as-complete |
| Progress gate | milestone 乱序/重复/并发只计一次；Bar 首个 committed frame 前不达 100%；失败冻结 |
| Preview alpha | 首次 ULW committed alpha 为 0；之后 fade-in 单调、不越界 |
| Handoff reducer | Preview committed 到 0 前 Bar 不上升；之后 Bar committed 单调到 255；255 committed 后才 stop/destroy |
| Shimmer endpoint | 默认方向含 X/Y 两个分量；任意测试宽度、DPI、zoom 下 phase 0/1 的全部非零 soft-tail 支撑在窗口外，中点穿过 mask，wrap 两侧输出等于 base-only |
| Shimmer time | epoch 取 Preview 首次显示本地 `steady_clock`；两端慢、中间快；不由 frame timestamp 取模 |
| Progress visibility | Preview shown 后 2999ms 不可见，满 3s 才开始约 180ms 渐显；3s 内完成不出现 |
| Completion | 已显示时真实 100% 直接进入整窗 fade；无旧 300ms progress-only fade/停顿；进度不超过 actual |
| Failure/retry | 首次自动重试保持普通颜色并先整窗渐隐；最终 fatal 才红，红帧 committed/有界等待后才弹窗 |
| Recovery | Preview disabled/bypass/device loss/ULW failure 最终均恢复 Bar committed alpha 255 可见 |
| Settings compatibility | 默认 Enable=true；旧 main.json 缺少新宽度字段仍可读取，旧图片字段忽略 |

## 2. Window/ULW 静态与人工

- 检查样式为 `WS_POPUP` + `WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`，没有 `WS_EX_TRANSPARENT`。
- `WM_MOUSEACTIVATE` 返回 `MA_NOACTIVATEANDEAT`；左右/中键按下、抬起、双击被吞掉；可见像素阻挡点击，透明圆角外遵循 ULW 原生 hit-test。
- owner thread 执行 create/SetWindowPos/show/hide/destroy；late post 不造成 use-after-free。
- 每次 ULW 显式提供 `pptDst`、`psize`、`pptSrc`；owner geometry 与 render thread 通过 presentation mutex 串行。
- 视觉检查深色/浅色背景：连续灰色 `80×cachedWidth DIP` 外包络、8 DIP 圆角、无文字/图标/分隔/假内容；内描边仅低调向内。

## 3. RenderPipeline/device

- 只有一个 RenderPipeline/device/render thread；dispatch order 为 Bar -> StartupPreview。
- Preview D2D target、mask、brush 在 render thread 创建；device generation 变化后全部重建，不使用旧资源。
- `FillOpacityMask` 前切 `D2D1_ANTIALIAS_MODE_ALIASED`，成功/失败均恢复；progress 在 shimmer 后最后绘制。
- D2D/DIB 为 premultiplied BGRA8；ULW blend 为 `AC_SRC_OVER`, `BlendFlags=0`, `AC_SRC_ALPHA`。
- 不新增 GDI+、WinUI Runtime、DirectComposition、Win10-only effect 或静态 Shcore 依赖。

## 4. Smoke/static/build

- `--startup-preview-smoke` 报告至少：total width DIP、first preview alpha-0 committed、preview fade-out committed、Bar alpha-0/255 committed、owner exit、preview inactive 和最终 recovery 状态。
- 人工观察可显式加 `--startup-preview-manual-delay` 或设置 `INKEYS_STARTUP_PREVIEW_MANUAL_DELAY=1` 打开分阶段延迟；默认启动和 smoke 不得被这个 hook 拖慢。
- `rg` 确认 Startup Preview 路径不再引用 BIN、image cache、CRC/signature/layout epoch、Gaussian blur、live proxy、CPU staging、`--capture-startup-preview` 或 capture validator。
- RC、resource.h、vcxproj、filters 无悬挂 Startup Preview 图片资源/文件引用；PptCOM resource 221 保持不变。
- `git diff --check`；检查修改文件的原编码/换行。
- 按仓库要求运行完整 Solution `Debug | 当前设备原生架构`；可行时按任务风险补 Release、非当前原生架构和 `InkeysHeadlessTests.exe --no-window`。不能执行的架构/平台明确记录为未执行。

## 5. 平台与人工待执行项

以下需硬件、VM 或交互环境，不得提前标 PASS：Win7 SP1 + KB2670838/WARP/FL11.0、96/120/144/192 DPI、混合 DPI/多显示器/负坐标、SuperTop helper/UIAccess、真实 device loss/ULW failure、topmost 竞争、焦点/Alt-Tab/点击吞噬和首次重试/最终确认对话框人工观察。

## 6. 2026-09-04 执行结果

### 纠偏补丁（最新）

| 项目 | 结果 |
| --- | --- |
| Solution Debug x64 | PASS，完整 `InkeysRepo.sln` 退出码 0 |
| x64 Debug `--no-window` | PASS，`PASS animation correctness` |
| `git diff --check` | PASS，无空白错误 |
| 修改文件换行 | PASS，工作区均为 `w/crlf` |
| Trellis context | PASS，任务重新设为当前会话；Startup Preview 任务规范、context 与 jsonl 已补充当前约束，避免大文件截断 |
| x64 Debug `--startup-preview-smoke` | NOT RUN，本轮按项目约束未启动可见应用窗口 |
| Win7/DPI/多屏/输入/视觉/真实故障注入 | NOT RUN，需专用环境或人工交互 |

### 初版简化提交（历史基线）

| 项目 | 结果 |
| --- | --- |
| Solution Debug/Release x64 | PASS，退出码 0 |
| Solution Debug/Release Win32 | PASS，退出码 0 |
| Solution Debug/Release ARM64 | PASS，退出码 0 |
| x64 Debug/Release `--no-window` | PASS，`PASS animation correctness` |
| Win32 Debug/Release `--no-window` | PASS，`PASS animation correctness` |
| ARM64 `--no-window` runtime | NOT RUN，x64 宿主不能执行 ARM64 PE |
| x64 Debug `--startup-preview-smoke` | PASS，470 DIP 与全部 committed/exit 字段为预期值 |
| Win7/DPI/多屏/输入/视觉/真实故障注入 | NOT RUN，需专用环境或人工交互 |
