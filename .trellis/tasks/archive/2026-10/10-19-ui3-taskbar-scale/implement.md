# 实施顺序与验证（2026-10-19 已批准）

## 实施前门禁
- [x] 用户于 2026-10-19 批准方案并要求开始实现，已运行 task.py start。
- [x] 按 implement.jsonl 加载规范与研究，使用 trellis-implement；完成后 trellis-check 全范围检查。实现者不得自行递归派发实现/检查。
- [x] rendering-and-ui.md 与 configuration-i18n-and-assets.md 超过自动注入单文件上限；实施/检查需按相关章节显式补读完整内容，不能把截断注入当完整规范。
- [x] 记录工作树与目标文件 BOM/编码/换行；保持原格式，用户未明确要求不 commit/push、不 GUI。

## 最小实施步骤
1. Display：增加可测试的逐屏任务栏厚度解析/回退数据和差异通知，复用现有刷新及订阅，任务栏查询和纯计算分离。
2. Config：Zoom→Zoom2，新增默认 true 的 UseTaskbarHeightAsScaleBase，更新真实读写和 Mini 测试（旧键不迁移、新旧并存、默认、类型、复制、NoUpload）。
3. Bar：区分真实 DPI 与 UI 基准，初始化/显示过渡/缩放操作/启动 fit/底栏拖动与稳定几何统一 scale；检查所有 dpiZoom 消费者，画笔尺寸仍用真实 DPI。
4. StartupPreview/IdtMain：读新键/开关，复用基准，保持逻辑缓存与实际栏交接一致。
5. Ppt/PageControl：补齐四分页的全局倍率并确认 Bar 内结束放映按钮继承缩放，分离 UI 几何 scale 与屏幕 DPI，接入设置/显示变化唤醒，不破坏位置持久化和 pair 提交。
6. Setting/i18n：新增开关卡片与三语说明，复用 QueueConfigWrite 和现有布局，SettingUI 不变；执行 Scripts/i18n.ps1 sync 后 check，检查生成 diff。
7. 运行必要无窗口测试、完整构建，审阅全部 diff，并将实际确认的缩放合同补入相关 spec。最终保持任务实施状态供用户决定后续归档，不自动提交。

## 验证矩阵
- 纯算法：四个任务栏边、负屏幕坐标、96/144/192 DPI、T=48/72/96、U=0.5/1/1.5/2、开启/关闭、无任务栏/无 Shell、无效厚度、自动隐藏完整厚度、同值更新/跨屏变化。
- 主栏：80×B×U 与任务栏厚度关系；初始化→首帧→显示过渡一致；底栏几何与渲染一致；屏幕 DIP 和画笔宽度不变；基础量小于 0.5 不应被错误 clamp。
- PPT：四分页逐一检查全局倍率/局部倍率叠乘一次，Bar 内结束放映按钮跟随主栏且不重建旧退出控件，UI 参数变化即时唤醒，fit/碰撞/drag/取消/吸附/提交回归，20 屏幕 DIP 阈值不随 U 放大；共享白板控件回归。
- 配置：旧 Zoom=1.75 无 Zoom2→1.0；两键并存只认 Zoom2；bool 非法类型→默认 true；Mini/ReadAll/Reset/复制/Write/NoUpload；旧未知键处理保持原策略。
- i18n：& ./Scripts/i18n.ps1 sync；& ./Scripts/i18n.ps1 check。
- 构建：先检查 InkeysRepo.sln 和当前 VS 安装；通过 vswhere 找 ARM64 原生 MSBuild，完整 Debug|ARM64 Solution。必须在同一次 PowerShell invocation 先 Remove-Item Env:PATH -ErrorAction SilentlyContinue；$env:MSBUILDDISABLENODEREUSE='1'。允许至少 5 分钟，不因短时无输出重启。
- 运行 Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window；可按现有实现使用 Inkeys.exe --config-boundary-test（已确认在 GUI 前退出）。其余测试入口先确认不会创建 HWND。
- 检查退出码及首个有意义错误；环境问题不通过修改源码/工程绕过。测试日志保存本任务 research/validation.md。
- 当前未授权 GUI，因此实机自动隐藏/任务栏位置及视觉一致性保留为人工验收项，不能把无窗口计算测试写成 GUI 已通过。

## 回退
- 修改限定到本任务相关 Display、Config、UI3/PPT、设置/i18n 和对应测试；按本任务 diff 撤销，保留用户其他修改。
- 不破坏旧 Zoom 数据；回退旧版本可按原未知键策略读取遗留旧值。新版本不恢复旧倍率是明确需求。

## 实施结果
全部实施步骤与自动验证完成，见research/validation.md；GUI实机验收未执行。保留in_progress，不自动归档或提交。
