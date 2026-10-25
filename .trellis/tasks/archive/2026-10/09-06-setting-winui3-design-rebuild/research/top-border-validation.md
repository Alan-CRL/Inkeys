# 选项窗口顶部边框修正验收

2026-09-08；基线 9ef709df。目标仅为用户红箭头指出的“智绘教 Inkeys 选项”窗口最上沿粗条。主栏/主按钮源码与行为均不在变更范围；此前主栏截图仅用于从正常入口打开选项。

## 结果

- 恢复态 top 使用 DWM 实际可见描边；左右/下保留原系统 sizing frame，最大化仍使用完整系统 frame。只在空白 caption 顶部补足 resize 高度，保护 caption/theme/version/sysmenu 与右侧 scrollbar。
- 创建后仅在 Setting 分支用 actual outer/client 差校正一次请求尺寸，保留原中心与工作区夹紧；解决创建前1px估计与实际2px DWM描边导致的客户区少1px，不猜分数DPI取整。
- 共享渲染管线、主题、页面布局、配置和其他 WindowRole 未改。

## 用户已打开选项窗口的实际读数

Windows 11 ARM64、DPI 192（200%）。本次直接读取用户打开的 Inkeys Settings（PID 22328，HWND 462270），未操作主栏。

| 项目 | 原边框基线 | 收薄顶部后实际读取 |
| --- | --- | --- |
| 顶部可见高度 | 12px | 2px |
| left/top/right/bottom client inset | 12/12/12/12px | 12/2/12/12px |
| DWM visible frame thickness | 2px | 2px |
| 顶边 / 左上 / 右上命中 | HTTOP / HTTOPLEFT / HTTOPRIGHT | 相同 |
| 空白标题正文 | HTCAPTION | HTCAPTION |
| 关闭 / 最大化按钮顶部 | HTCLOSE / HTMAXBUTTON | 相同 |
| 主题按钮 / scrollbar内外侧轨道 | HTCLIENT | HTCLIENT |

证据：Build/SettingVisualReview/top-border-before.json、options-user-restored.json。用户正在运行默认 Debug 路径的实例，包含顶部收薄修正但未载入随后1px创建校正，因此实际client为1920×1399；不声称未重启的实例已经热更新。最终完整产物另存于 Build/SettingTopBorderFinal/Inkeys.exe，保留用户正在运行的程序。

屏幕捕获/后续真实拖动时 WTS 检测到锁屏，因此没有发送输入或绕过锁屏；上述结果来自真实 HWND/DWM/WM_NCHITTEST 的只读查询。早先 GDI 白色客户区帧和 DXGI捕获超时未作为整页视觉通过证据。没有使用 Computer Use。

## 最终检查

- ARM64 host 完整 InkeysRepo.sln Debug|ARM64 + PptCOM：exit0；900秒超时；独立OutDir避免默认Debug实例引起的LNK1168，不终止用户实例。日志 Build/setting-top-border-final-build.log。
- 最终 InkeysHeadlessTests --no-window：PASS/exit0。
- 最终含 RunWindowTests 的集成运行：PASS/exit0。日志 Build/setting-top-border-final-windows.log。
- 覆盖多个DPI的薄顶/经典回退/最大化、顶部与控件互斥、整个右侧scrollbar client、顶角、创建差1px/no-op/负原点/工作区不足/底部夹紧。
- Setting窗口测试在其自己的PM-aware创建线程测量实际client。曾测出160px fixture被原生258px min-track扩大至234px客户区，仅在测试WndProc隔离该原生最小值，保持160×90精确断言；产品720×520最小尺寸与USER32 max字段不变。
- 独立审查与 UTF-8/CRLF、git diff --check 通过，详见 top-border-review.md 与 top-border-creation-correction.md。

## 待实际桌面补验

拖动过程的视觉流畅性、实际最大化/还原截图和多显示器切换仍不由纯几何或读取命中值代替。当前代码已完成修复与自动化验证；整项Trellis任务保留in_progress，不在本轮自动commit。
