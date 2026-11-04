# Design：DPI 与按设备小圆点接触隐藏

## 改动边界

行为缺口位于 Draw3 生产光标解析和产品显示上下文接线，而非设置预览。设置只编辑配置草稿，沿已有 deferred `SyncDraw3State()` 与冻结 `QueueConfigWrite()` 发布；不在 ImGui 锁内访问 Controller/HWND。

实测确认固定样式已经只乘一次 `dpiScale`，Controller 的 `ProductTargetScale(currentProductVisualStyle)` 来自 `PenWidth::Context.effectiveDpi / 96`；正式和完整 guarded PS 均正确跟随 96/144/192/264 DPI，没有发现旧链漏乘。192 DPI 下 Windows 实际箭头可见 bbox 为 25×39 px，而自绘真实 GPU 为 23×38 px（原 appearance 24×38 px），因此在共享常量处将箭头参考大小校准为 13×19.5 DIP：宽度计入既有轮廓最右仅占参考盒 95% 的留白，高度取实测 39/2。保留 HLSL 的 12×19 归一轮廓与尖端热点，不改 shader，不额外乘 DPI。设置预览使用同一常量并按原参考盒归一。小圆点沿用已换算的笔宽与最小直径，不重复缩放墨迹。

## 数据与解析

- 在现有 `Drawing.Cursor` schema 增加 NoUpload 整数字段，四态 0=关闭、1=仅屏幕笔、2=仅数位板、3=两者，默认 1；严格拒绝 bool/float/超宽/越界输入。配置归一与 CursorSettings 使用统一枚举/还原规则。
- 扩展 `CursorSettings` 值快照及单原子打包；保留现有四设备样式、注入识别位与有效位语义，额外两位存新模式。Bridge 的复制/相等判断须包含新值，不能静止时修改无效。
- 先按既有规则解析所用设备行与样式，再决定接触可见性。产品接入路径取消旧“笔接触一律隐藏”的门控；仅软/硬笔的 Dot/HighlightDot 且设备命中模式时隐藏。nullptr 独立接口保留旧 contact 开关语义，不无故改变独立 demo。
- 主光标不替代 Touch 的逐 runtime 路径。Unknown/Conflict 和注入鼠标沿数位板行；真实鼠标即使选 Dot 也不受新模式影响。倒转笔橡皮优先于样式隐藏；Highlighter/Shape 专用外观不套 Dot 隐藏条件。
- 不新增线程、全局 ShowCursor 计数、输入过滤或 contact 身份变更。

## 设置与国际化

`CursorPageDraft/Events` 增加模式索引/变化事件；沿当前设备设置写回方式，只写变化字段并复用一次同步与保存。信息条继续紧邻数位板识别开关，其后增加“精细控制”区及 ComboRow。新字段名称/说明/四选项完整翻译 20 语言，运行现有生成链，保持源文件编码和换行。

## 预期必要修改面

- Draw3.CursorSettings.h、Draw3.PenCursor.cpp：模式、快照打包与可见性；Controller/Window/Host 的修改仅限 DPI 调研证实的缺口。
- Other.Config.cppm/.cpp、IdtState.cpp：schema、严格校验与产品快照加载。
- Setting.Pages.h/.cpp、Setting.cpp：草稿、事件、控件、位置与 deferred 写回。
- 对应 Headless 光标/配置/Bridge/设置测试；i18n 各语言与生成头/快照；draw3-cursors.md：新可执行合同。
- 不计划改 shader；只有生产尺寸问题的证据要求时才同步 CPU/GPU，并明确记录必要性。

## 风险与回滚

默认大小校准由 Windows 实际箭头和生产 GPU 可见像素差异支持；GPU fixture 在 192 DPI 严格检查 25×39 bbox，不能把 appearance 理论盒或透明位图画布当作结果。真实同屏观感仍须用户回核，不宣称发现了旧 DPI 漏乘。保留既有 Cursor 诊断 Enabled/100ms 分支，新增短 frame-scale 记录当前 Context DPI/比例/版本、Display 标尺 DPI 和 viewport，以便真实运行定位；不在关闭诊断时新增采样。来源分类与 Pointer/RTS mailbox 生命周期不变。新增配置可忽略回退，不迁移旧样式；回滚本轮文件即可恢复原行为。GUI 与真实 DPI/设备表现由用户明确授权后验证或自行验收。

## 实测收敛（2026-11-04）

用户确认同屏自绘“鼠标”箭头比系统箭头小。生产与 guarded PS 实测均正常随 DPI 换算；192 DPI 自绘实际可见 23×38 px/429 像素，当前系统实际箭头为 25×39 px/493 像素。故修复定位为自绘轮廓的默认可见大小校准，而非再乘 DPI：参考轮廓最右点占盒宽 0.95，共享尺寸盒改 13×19.5 DIP，预览按同一参考映射，要求真实 GPU 在192 DPI输出25×39。保留尖端热点与shader数学，笔宽、其它样式不变。原有光标诊断在既有门禁/100ms分支新增短frame-scale记录供实机核对。

## 实机反馈修正（revision 2）

用户已确认大小正确，实际差异为自绘黑边按1 DIP在200%变成2像素。只给 Arrow 增加共享 `CursorArrowOutlineWidthPx(scale)`，名义0.5 DIP、至少1物理像素，200%为1像素；renderer与设置预览同源。描边仍在原轮廓内部，保持外框、热点、shader数学、其它样式。用实际GPU黑边/白填充像素断言区分旧2px与新1px，不能只检查理论参数。

下拉缺陷位于 Setting.Controls.cpp 的共享 ComboBox：普通路径固定位置+AutoResize越出客户端，显式motion路径只按below截断。最小改动为统一当前viewport工作区的上下预算，默认向下，below小于所需高度且above更多时向上；选中侧高度不足则滚动。保持按钮X对齐，不能侧向盖住左侧说明。动画偏移按方向朝入口收拢，退出装饰仍不接收输入，反向/resize始终不越viewport。延续 BelowComboMotion 名称，不为修复引入新窗口或重新设计状态。

预期必要变更：CursorSettings/PenCursor的Arrow边宽、Controller/Renderer与contact测试预期、Setting.Pages预览；Setting.Controls cpp/h及setting_design_tests；对应spec与任务记录。本轮不新增i18n文案、不改持久化或设备分类。只运行受影响的无窗口设置专项和生产像素/DPI检查，并完整solution Debug|ARM64构建；必要时因失败扩大回归，不重复不受影响的耗时性能基准。

## 共享消费者核对

用户补充要求所有同类下拉统一修复。已盘点当前产品设置源码：教学安全 `RenderGeneral(Interaction)` 的 `ComboRow("teaching-safety")` 与其它 ComboRow/ComboPreviewRow 均调用共享 Design::ComboBox；直接使用的手掌橡皮比例、PPT闲置时长、快捷方式预设、诊断页下拉也调用同一入口。当前产品没有绕过该封装直接使用ImGui/ImFluent的Combo/BeginCombo入口。已有产品修复已覆盖，新增真实教学安全位于客户区底部后向上并可点击末项的回归证明通用性，不追加页面特判。
