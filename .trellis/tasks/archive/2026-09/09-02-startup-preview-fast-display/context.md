# 启动预览上下文与调用关系

## 现状核对

上一版实现已经建立 `Inkeys.Startup.Progress`、共享 `Inkeys.UI.RenderPipeline`、独立 Preview owner/message thread、D2D1.1 layered presentation、presentation alpha 三态、3 秒进度门、失败/重试钩子和 smoke 入口。本轮只把 Preview 的视觉输入和交接语义简化为程序化占位；这些可靠基础设施继续保留。

Startup Preview 的产品输入只有三个 mini-config 值：`StartupPreview.Enable`、`CachedStartupBarWidthDip` 和 `UI.Bar.Zoom`。不读取图片、主题、语言、按钮序列或任何视觉签名字段。

## 合法启动时序

1. `wWinMain` 完成路径、参数、单实例、崩溃和 SuperTop 父/helper 分支。
2. 最终进程越过所有会退出或替换当前进程的 SuperTop 分支后，立即用 `steady_clock` 记录唯一 T0，建立 immutable progress plan，并报告 `SuperTopCrossed`。
3. 在创建任何 Preview/Window Service HWND 或枚举 Display 前建立 DPI awareness；Win8.1+ Shcore 继续动态探测，Win7 走既有回退。
4. 读取 mini config，校验缓存总宽度 DIP；启用时计算当前 DPI/zoom 下的像素 bounds，报告 `PreviewGeometryReady`，条件化启动共享 RenderPipeline 和 Preview owner。
5. 继续日志、COM、Display、full config、i18n、PptCOM、字体、Window Service、Draw3、Setting、Whiteboard 和 topmost 等现有阶段；这些阶段只报告实际完成的 milestone。
6. Bar 以 presentation alpha 0 初始化并在 RenderPipeline 中完成首个完整 ULW frame；该 committed frame 才报告 100%，并发布本次目标总宽度供主线程回写。
7. 正常交接严格为 Preview committed fade-to-zero -> Bar committed fade-in 0 到 255 -> 确认 255 committed 后 owner hide/destroy。失败路径先恢复 Bar 可见，再按既有错误/退出语义清理。

## 默认几何证据

默认状态为 `StateModeSelectEnum::IdtSelection`、空白页、无白板/演示、旧组件开关关闭；`PresetHoming()` 隐藏 Geometry、Eraser、Recall。A1 的 Select、Draw、Clean 各占一列，A2 的 Whiteboard 与 Freeze 共用一列，More 占一列；Divider 只结束未填满列。

| 量 | 推导 |
| --- | --- |
| 标准 gap | `BarButtonGapDip = 5.0` |
| 单边按钮 | `BarButtonOneSideDip = 32.5` |
| 双边按钮 | `32.5 * 2 + 5 = 70.0` |
| 列步长 | `70 + 5 = 75 DIP` |
| MainBar 主体 | `5 + 5 * 75 = 380 DIP` |
| 完整外包络 | `80 主按钮 + 10 间隔 + 380 = 470 DIP` |

`BarMainBarWidthDip = 80` 仅是初始化/折叠目标，不是展开主体宽度。`470.0` 是 schema 默认和坏缓存回退值；它表达完整外包络，而不是像素、主体宽度或 zoom 后尺寸。

## 并行与失败边界

- Preview/占位绘制、shimmer、进度条或 owner 失败只能 bypass/recover，不得阻止正式启动。
- RenderPipeline、Window Service、Draw3、Setting、Whiteboard 或正式 Bar 的既有致命失败仍须冻结真实进度并进入现有错误流程，不得静默 return。
- 第一次自动重试不是最终 fatal：保持正常颜色，先让 Preview 完整渐隐，再按既有重启参数重试。第二次失败才绘制红色错误帧并在有界等待后显示对话框。
- Preview 尚未建立或已经失效时直接使用系统/应用 MessageBox fallback；不得为等待错误帧无限阻塞。
- `ReportStartupMilestoneForManualTest`、`RunStartupPreviewRetryFailureForManualTest`、`INKEYS_STARTUP_PREVIEW_RETRY_FAILURE`、`INKEYS_STARTUP_PREVIEW_MANUAL_DELAY=1`、`--startup-preview-manual-delay` 和分阶段延迟仅在测试条件启用；RenderPipeline/Draw3 callback thread 不 sleep。

## 线程与资源所有权

| 对象 | 所有者 | 跨线程规则 |
| --- | --- | --- |
| Progress tracker | 原子状态/短锁 | 生产者只做低开销报告；UI 只读 snapshot |
| Preview HWND/message loop | Preview owner thread | 其他线程只 post 命令；hide/destroy 回 owner |
| Preview D2D target、mask、brush | 唯一 RenderPipeline render thread | device generation 变化时在该线程重建；停止时优先通过 render-thread control task 释放，管线已停则仅作收尾兜底 |
| Bar presentation transaction | 唯一 RenderPipeline render thread | requested/attempted/committed 只在完整 present 后推进 |
| Width write | 主线程或现有配置安全路径 | render thread 只发布有限、去重的 `double`，不做 I/O |
| Owner geometry snapshot | presentation mutex | owner `SetWindowPos` 与每次 ULW 读取/提交串行 |

## 保留与删除边界

保留 ULW、per-pixel alpha、`SourceConstantAlpha`、静态形状 mask、默认斜向 `FillOpacityMask` shimmer、真实 milestone、owner 线程、shared RenderPipeline、Bar alpha 事务和人工测试入口。删除 Startup Preview 专属的图片资源/文件、图片解析或持久化、签名/CRC/epoch 分类、模糊/proxy/staging/capture 及其 CLI、诊断、工程和文档登记；普通 Cache 目录及其他功能的 Cache/CRC/SHA 代码不在删除范围。
