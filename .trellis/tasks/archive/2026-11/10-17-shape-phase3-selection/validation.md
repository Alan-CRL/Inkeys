# P3＋逐点工具＋P4限定范围 验证记录

验证环境：Windows 11 ARM64，2026-10-17～18。未启动主窗口或Computer Use；所有EXE入口均为无HWND/离屏/命令行模式，由主会话统一执行。代码保持未提交，无push。

## 构建

- 使用vswhere定位 `MSBuild/Current/Bin/arm64/MSBuild.exe`，没有硬编码VS版本。
- 同一次PowerShell invocation先删除重复Env:PATH、设置MSBUILDDISABLENODEREUSE=1，再运行现有solution。
- 完整 `InkeysRepo.sln /m /nr:false /p:Configuration=Debug /p:Platform=ARM64 /p:LinkIncremental=false`。
- build-01、02分别发现13/11项新增UI源码引用错误，已修正。build-03退出0（1:24）；之后针对UI实际像素修正增量验证。最终build-final退出0、0 Error、3项既有hashlib++ C4267 Warning，用时14.20s，PptCOM和Inkeys均成功；未为消除无关警告改代码。
- `PptCOM`依赖和产品RC/链接均成功。GPU测试通过正式renderer初始化及六份嵌入shader资源加载检查；本轮不改shader二进制合同。
- 共享 `inkStrokeModelerTest.sln /t:inkStrokeModelerTestTests Debug|ARM64`最终构建退出0，0 Error，用时16.81s。新中文头/inl采用UTF-8 BOM，已有源文件保持原编码。

原始日志：`Build/shape-p34/build-01.log`、`build-02.log`、`build-03.log`、`build-final.log`、`shared-build-final.log`。

## 已执行测试

| 入口 | 当前结果 | 验证重点 |
|---|---|---|
| `--draw3-shape-core-test` | 退出0 | 共享核心、生产创建、统一编辑历史、跨笔输入与身份门禁，全部failures=0 |
| `--draw3-shape-renderer-test` | 退出0，376 cases | 21工具相关几何、线型/覆盖并集、tile、旧图形/笔/荧光笔/Laser、同层编辑/取消/Undo/Redo/资源故障恢复 |
| `--draw3-whiteboard-storage-test` | 退出0 | 100页面cold存储/CPU及GPU缓存引用释放/隐藏redo/主题/stale及故障分支 |
| `--ui3-shape-editor-test` | 最终退出0；380场景/1,892提交/65,204检查 | 真实SVG、数字、色板、旋转圆面、触控热区、呈现失败、设备恢复、终结重试、小工作区及公共按压曲线 |
| `--ui3-geometry-attribute-test` | 最终退出0；3,024配置/1,072,340检查 | 21真实类别与15禁用、三语/主题/DPI、独立属性list/留白、完整Bar步骤提示及5DIP间距 |
| `--shape-tool-state-test` | 退出0 | 21工具/模式/样式/宽度槽/边数/队列不可变身份与FIFO |
| `--pen-width-production-test` | 退出0 | 共用粗细策略、既有精调状态及跨代取消回归 |
| `--state-mode-production-test` | 退出0 | 工具切换/穿透/主栏状态/清空事务回归 |
| `--bar-eraser-offscreen-test` | 退出0 | 公共UI3按钮/分页/捕获及既有擦除属性回归 |
| `InkeysHeadlessTests.exe --no-window` | 退出0 | 公共动画、输入、粗细及擦除合同 |
| 共享 `--shape-core-only` | 最终退出0 | typed参数/编辑约束/四方向Cube/弧与空间构造 |
| 共享 `--uink-file-only` | 最终退出0 | 236对象/240标准部件，旧文件/精确参数/可见降级/文件事务 |
| 共享 `--uink-presentation-only` | 退出0 | PPT保存文件事务 |
| 共享 `--desktop-autosave-only` | 退出0 | 桌面保存及注入失败事务 |
| `Scripts/i18n.ps1 check` | 退出0 | en-US/zh-TW各426/426 |

原始stdout/stderr及实际退出码保存在 `Build/shape-p34/`。故障注入中预期的“retaining warm page”/index-commit failed日志与测试失败区分，以进程退出码及断言汇总为准。

## 独立审查与闭环

详见research/review-renderer.md及review-history.md。已修复并以生产helper/队列/故障/negative fixture验证：

- 显示帧粗细/输入focused并发、最小化恢复与Bar位置唤醒。
- Cube角真值、旧/新箭头头部规则、真实轨迹与预测扫角分离。
- Fatal、活动期间取消屏障、同帧多接触最后真实Up、成功raster后自动编辑。
- Host重复场景前导导致会话清空；绑定PPT→isolated相同workspace仍是真实场景改变。
- Replace redo先于Clear redo、冷历史重复ID/重复映射/漏项拒绝且原子保留旧状态。
- 权威异步页面安装关闭旧会话/推进代次/清缓存。
- 队列拥塞时终结动作有界续接，过期会话隔离，清空后worker允许休眠；Close为会话级操作，不能跨新会话。
- 固定460×420像素、1.5/2倍率扩展区可达性；UI真实SVG/文字/色块内容缺口已修复并复验，实际PNG也已由主会话查看。

## 人工待验与范围

Win7/Win10实机、真实HWND/DWM层叠与穿透、实体笔/触屏、多屏实际手感未执行，本轮离屏不能替代。没有额外设置/复制删除翻转组合入口。仅当前刚完成对象可编辑，旧对象重选/多选/吸附不在本轮。

本轮启用21类，15类仍禁用；P4剩余角度/平行/垂直等条目保持未完成。桌面/PPT重开保留最终几何与参数，不承诺跨进程恢复编辑框或完整编辑journal。


## UI视觉回归与测试修正记录

- 初版Editor虽返回0，但实际PNG暴露了SVG尺寸为0、数字没有文字槽、共享画刷被后续frame颜色覆盖的问题；已修复并新增严格内容像素检查。
- 同步修正Shape RGB与COLORREF转换，沿原15DIP colorSelect徽标；旋转按钮具有主题圆面。构造提示按实际panel高度保留主栏上方5DIP间距。
- 内容增强时发现fixture打开色板后未像生产每帧Ensure当前target尺寸，导致扩展区被旧target裁切；修正测试接线，不改正确的生产Render流程。
- 0.75倍浅色下固定绿圆白勾的抗锯齿oracle原来误用父按钮背景；改用资源绿色背景/中心裁切/严格残差，保留实际空白和选中底块负例。没有为过测试改正常SVG或放宽为alpha检查。
- Root最终将几何点的局部.82/1.12缩放改为公共BarButtonPressScale、Press/ReleaseCurve与共享边缘光；重新完整构建及Editor380场景复验，退出0。
- 实际查看的成功呈现证据：`Build/shape-p34-ui/editor-10-light.png`、`editor-15-dark.png`、`editor-small-200.png`、`construction-normal.png`、`construction-small-200.png`。这些是独立UI层离屏图，图形本体由画布renderer绘制。

所有相关自动化检查已通过。任务文档与规范已同步，代码留在工作区待用户审阅；不自动提交或归档P4剩余范围。
