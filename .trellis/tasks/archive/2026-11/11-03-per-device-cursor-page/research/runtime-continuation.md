# 运行时续作最终实现记录（2026-11-04）

## 已完成的运行时改动

- `Draw3.CursorSettings.h` 新增四态 `DotCursorHideMode`、严格整数还原和 `ShouldHideDotCursorOnContact`。默认仅屏幕笔；模式占原子快照14–15位，原有效位13及四设备/注入位不变。
- `Other.Config.cppm/.cpp` 新增 `Drawing.Cursor.DotHideOnContact` NoUpload整数（默认1），严格接受JSON int/uint且可表示为int的0..3，其余恢复默认。
- `IdtState.cpp` 将归一模式与完整CursorSettings一起发布。原Host/Controller快照复制、相等比较与单原子打包继续使用，不新增跨线程状态。
- `Draw3.PenCursor.cpp` 先解析设备行和样式，再决定接触可见性。产品仅隐藏命中模式的软/硬笔Dot/HighlightDot；其它笔光标忽略旧统一Contact开关并保持显示。Unknown/Conflict、启用识别的注入鼠标走Tablet；真实鼠标、逐Touch、倒转橡皮和专用工具外观保持合同。nullptr独立接口保留旧Contact开关。
- 自绘Arrow默认参考大小校准为 **13×19.5 DIP**，保留HLSL的12×19归一轮廓、尖端热点和一次DPI转换；不改shader。主代理同步了设置预览归一。

## DPI 调研与校准依据

取证初期没有改尺寸，当时“未修复/尚未运行”的记录属于历史阶段；最终已有下面的真实构建和GPU结果。

1. Host::PumpDisplayScale从Display同代目标monitor.effectiveDpi建立PenWidth::Context，通过同步penWidthContextChanged回调给IdtMain。
2. IdtMain在Host启动前以Display Primary有效DPI初始化；回调再经ApplyPenWidthContext、IdtState ProductState与Product.cpp完整merge到Host。Host先PumpDisplayScale再取bridge新快照，然后Window::SetProductVisualStyle。
3. Controller每帧读取Window样式邮箱，Context改变后更新当前样式；ProductTargetScale调用effectiveDpi/96，resolver对固定样式与描边仅乘一次。Dot保持已换算笔宽和最小5 DIP，不重乘墨迹。
4. Renderer将appearance尺寸各除2上传，生产VS/PS以物理像素halfSize绘制。Window workspace按实际Drawpad RECT选目标显示器（白板Primary）；工作区恢复保留当前Context，不回滚旧DPI。其它整份stateMode恢复仅在Testing scope。
5. 正式与完整guarded PS的校准前真实GPU均正确缩放：96/144/192/264 DPI分别12×19、17×29、23×38、32×53可见像素。未发现旧链漏乘DPI或固定退回96的源码缺口。
6. 本机原生实际192 DPI箭头为25×39/493像素，而自绘为23×38/429像素。主代理决定基于此差异做最小默认大小校准；宽度13计入轮廓95%横向占用，高度19.5取39/2，不能描述为修复了旧DPI漏乘。

完整原生数据与GPU基准/最终结果见 `native-arrow-readback.md`、`native-arrow-measurements.json`、`cursor-gpu-readback.md`。透明64×64画布、appearance盒与实际可见bbox分开记录。

## 有界诊断

保留Controller既有CursorDiagnosticsEnabled和100ms frame分支，新增短frame-scale记录cursorDpi/cursorScale/contextRevision/displayDpi/viewport。只读已有Context、Display标尺和renderer尺寸；不新增API查询、锁或循环。RecordCursorDiagnostic内部仅320字节，故独立短记录避免挤掉原frame来源字段；原frame与visual(width/height)不变。关闭Cursor诊断时不采样或格式化新增字段。

## 新增/调整测试

- `draw3_contact_tests.cpp`：四态×五样式×六来源行，软/硬笔hover/contact，Highlighter/Shape专用几何与接触显示、倒转橡皮优先；完整6000个快照打包往返、非法模式还原。原nullptr Contact/Laser/Touch回归保留。
- `config_metadata_tests.cpp`：默认、四态ReadMini/写盘重启ReadAll、缺失键默认，bool/float/string/null/容器/越界/超宽整数严格拒绝，冻结副本及NoUpload。
- `draw3_bridge_tests.cpp`：仅新模式变化更新revision且旧快照不变，工具投影保留该模式。
- `RunPenWidthProductionProbe`：真实Window样式邮箱连续96→144→192→264→96 DPI更新，经生产ProductTargetScale→resolver验证Arrow/Pen/HighlightPen/Dot尺寸、描边转换一次、热点固定；Arrow独立预期13/19.5，未仅镜像常量。
- `RunRendererUploadValidation`：复用无HWND WARP离屏资源，调用真实resolver、DrawTransientDrawingCursor与正式/完整guarded PS，逐像素统计alpha bbox。192 DPI严格断言25×39与尖端(8,8)。没有运行会创建隐藏HWND的旧renderer-pixel-test。

## 最终已取得的验证结果

主代理最终完整 `InkeysRepo.sln Debug|ARM64` 构建退出0。`Build/cursor-continuation/test-results.json` 已记录cursor-gpu、cursor-ui、pen-width全部退出0。

| DPI | 最终正式/guarded可见bbox(px) | 非零alpha像素 |
| --- | --- | --- |
| 96 | 13×20 | 128 |
| 144 | 19×30 | 276 |
| 192 | **25×39** | 469 |
| 264 | 34×54 | 875 |

正式和guarded结果一致，热点均保持(8,8)。GPU末行 `PASS failures=0 no_hwnd=1 no_swapchain=1`；production pen-width probe也PASS。原生与自绘可见外框对齐，未要求两种轮廓/AA的像素数量完全相同。

运行时修改文件的diff --check和原始BOM/EOL类型检查通过。产品源码冻结，未提交commit，未修改工程、shader或显示器策略。完整headless由主会话继续执行并补最终退出码，本记录不提前声明通过。

## 尚待用户验收

默认大小校准与自动验证已完成；同屏主观观感、真实屏幕笔/数位板、Win7和自定义pointer主题仍未GUI验收。保留frame-scale+frame+visual日志供真机需要时定位。未启动GUI、未操控桌面，不把Win11 WARP通过当作Win7/硬件实测。

## 主会话最终闭环

最终完整 `InkeysHeadlessTests.exe --no-window` 在沙箱外使用同一冻结源码/二进制退出0，末行 `PASS animation correctness`，stderr无失败；最终ARM64 solution与GPU/UI/pen-width专项均退出0。37项源码SHA与source-state一致，最终完整Git状态（含子模块）和diff检查仅含本轮预期改动。验证汇总见 `research/validation-continuation.md` 与 `Build/cursor-continuation/validation.json`。未启动GUI，未commit/push/归档；任务保留in_progress等待用户实机验收。
