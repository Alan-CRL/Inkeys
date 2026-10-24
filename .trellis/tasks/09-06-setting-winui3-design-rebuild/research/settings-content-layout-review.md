# 设置页内容与排版独立审查（2026-10-24）

Active task: `.trellis/tasks/09-06-setting-winui3-design-rebuild`。基于外框提交 `3a28f8ac` 之后的当前差异，只读审查产品/测试源码；仅写入本研究记录。未 build、启动 GUI/Office/Computer Use、触发外链、修改代码、commit/push。既有 CSO、旧日志不属于本轮发现。父任务保持进行中。

## 结论

当前已交接 UI 与后端源码未发现有充分证据支持的阻断问题。主会话仍在补充和运行实际 renderer、托管/原生及资源交接验证，本文不提前宣称测试通过或产品界面/Office 已实机验收。

## 逐项覆盖

1. **笔震动**：绘制/按钮两行改为 U+EDC6，仓库图标表对应 PenWorkspace；haptics.Drawing/Buttons 的原写回、业务同步保持。
2. **PPT 加载页**：保留带标题、说明与图标的 SettingRow，去掉 UI 草稿和 toggle。PptComReadSetting 在读文件前固定内存 false，随后忽略旧 ShowLoadingScreen；完整配置快照和 WriteJournal 共同序列化边界均固定输出布尔 false。原 kPptLoadingPageEnabled=false 仍阻断唯一 FreezePPT=true 入口；加载图形及流程代码未被删除。旧 true、坏类型/缺项不再重新启用，普通冻结功能没有被改动。
3. **PPT 语义图标**：仓库 ImFluent 图标表确认 bottom/middle 为 DockBottom/DockLeft，实验选项分别为 SwitchApps、RepeatOne、ExpandTile、Touch；对应控件显隐、布局更新、配置/QueueConfigWrite 路径保持。
4. **插件详情返回**：四页共用 renderPluginHeader，只删除“插件”HyperlinkButton 与其 SameLine，保留独立返回箭头、标题/版本、各页原有 GitHub 链接和内容路由。
5. **SuperTop**：启用改用 Pin；指示器改为只读说明 SettingRow，未强制修改实际 setlist.plugInSetting.superTop.indicator，原读取与保存仍在。没有把隐藏 toggle 误当成关闭指示器。
6. **DDB**：软件列表公共行使用 AppIconDefault；各行 value 指针、拦截字段与统一保存边沿保持。
7. **PPT 版本**：托管 CheckCOM 实际返回 20261024a；接口顺序、GUID、程序集 ABI 和版本格式未扩大修改。native CheckPptCom 仍调用真实 COM CheckCOM，设置总览与详情仍显示同一 pptComVersion/error 分支，没有 UI 假硬编码版本。新 DLL/TLB 同次构建、native 资源同步及包内字节一致性由主会话继续验证，源码检查不能代替产物交接。
8. **组件图标**：显示桌面为 TVMonitor，与锁屏 Lock 区分；ClassIsland 快速换课为 Edit（铅笔），对应启动/URL 配置未改变。
9. **调试与版本页**：dirty-debug/fps 行从实验页移入 Debug，原稳定控件 ID、配置字段、条件显示、SetDebugOptions 即时调用和 QueueConfigWrite 保留。更新状态/动作的 12 状态 switch 与后续动作分支原码前移到版本页头，下载进度、渠道信息、手动下载/重查/强制更新等副作用保持。版本信息转蓝色 Notice，删除旧 PNG/“版本信息”SectionHeader，保留发布版本/日期、架构、release/debug、渠道、手动/自动构建和真实 CI 详情入口。
10. **赞助说明**：感谢文字位于图片卡片 EndCard 之后；RenderSponsorThanks 复用逐行测量/居中渲染，Body 与 TextPrimary，按图片卡片宽度登记完整文字高度。未恢复次要灰色、未把文字塞回图片卡内部。

## CI/CD 与赞助 helper 审查

- RenderCicdDetails 参数为真实动态字段，不含测试 fixture 或 ShellExecute；两个 NavigationRow 返回 Run/Repository 枚举，由 Setting.cpp 使用真实 settingCICD URL 执行原外链。保留原稳定行 ID。
- CI 详情包含 branch、submitter、buildTime、OS/版本、runner OS/版本、完整 MSBuild 输出；未截断元数据或移除内部换行。
- 明确按当前可用宽减去 CardPadding 测 TextHeight，再对 BeginCard 设置当前帧高度约束/内容高度。实现与已有 RenderEdidCard 的生产模式一致，避免首次 AutoResize 取上一帧高度以及长文本困在内部小视口。
- RenderSponsorThanks 自身无业务动作/外链，只按指定 Body 字体和主文字色绘制，逐行居中并 Dummy 登记布局高度；图片与其后说明拥有独立的外层页面布局位置。
- 真正长 URL、长工具链、多语言、主题、DPI、外层滚动和链接点击是否通过现有实际 renderer 回归，由主会话完成后登记；未将只读代码审查当作像素/布局测试通过。

## 测试源码和待交接项

- 新 ppt_ui_tests 使用真实 WriteJournal CapturePositions/CaptureSettings 与 Commit writer 解析最终快照，覆盖缺项、旧 true、旧 false、错误类型；同时验证坐标、位置记忆、控件显示和未知键保留，具有实际持久化边界意义。
- 主会话的 setting_design_tests 实际 renderer 回归仍在补充/执行，不在代理并行写同一文件。
- 待主会话统一记录：i18n check、编码/BOM/CRLF、git diff --check、PptCOM 同次 DLL/TLB 同步/资源字节、完整 Debug|ARM64 Solution、适用 managed/native --no-window；无真实 Office/GUI 验证。
