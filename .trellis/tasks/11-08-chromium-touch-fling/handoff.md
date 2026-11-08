# 续接交接（2026-11-08）

## 当前结论

用户明确确认当前版本没问题，并授权提交本轮改动，但不结束任务。当前鼠标/Touch手感修复是后续工作的保留基线，不回退到7b逐字Chrome策略。任务继续in_progress，不归档、不push。

已完成：回调级导航历史/Down水位、Stop与overflow隔离、可靠尾段与反向保护、向量摩擦、当前残余投影连甩、跨来源隔离、独立CanvasPan控制台开关。生产合同见 `.trellis/spec/native-desktop/canvas-pan.md`，实施范围见implementation-scope.md，验证证据见validation.md。

用户确认是当前版本的体验反馈，不代表已经完成多种屏幕、不同触屏硬件或所有边界场景验收。

## 后续顺序

1. **先设计并评审设备尺度阶段**：读取repair-plan.md的设备补充及research/display-scale-audit-2026-11-08.md。保留当前默认手感作对照；把物理单位转换与响应参数分开实施，避免一起调估速/摩擦/尺度。
2. **复用已有标尺和来源校验**：DisplayScale、InputSource、monitor/输出映射、旋转、EDID可信度及DIP回退。直接Touch可使用真实物理尺度；鼠标/数位板不能用显示器尺寸冒充动作表面。手势锁存尺度，实际显示输出变化停止旧运动，不重新解释历史坐标。
3. **桌面/大屏独立响应**：沿用笔速橡皮的来源分类和物理长边/显式模式约束思路，平移独立定义释放门限、摩擦、残余增强和上限；不直接搬橡皮尺寸/速度系数。先验证同profile下分辨率/DPI变化不改变响应，再分别标定桌面/大屏。
4. **笔的边界保持**：当前笔hover/落笔仍停止画布，不参与平移。屏幕笔混合响应与外置数位板间接响应需分开设计；若要新增笔平移，先明确产品需求，不能作为尺度适配附带启用。
5. **输入保真后续**：本轮仍RTS每回调批末样本、arrival QPC。探测真实packet timer/metrics、回卷/epoch及批内全样本可用性；有证据再接入，不均分虚构时间、不切换输入后端。此项独立于尺度适配，若当前设备日志稳定，不以它阻塞尺度设计。
6. **分段验证和用户实测**：保留现有鼠标/Touch回归、源切换/短反向/高频量化/overflow保护；新增跨DPI/横竖屏/EDID失败/映射冲突测试。每段确认当前电脑手感不退化，再做大屏对照；未获GUI授权时不自动开窗口。

## 诊断与构建

设置→软件调试→控制台输出→白板平移与惯性诊断；默认关闭，开启重启生效。保留异常前后完整[CanvasPan]日志及输入类型；区分输出队列丢弃和输入历史丢弃。

已通过：ARM64完整solution、完整Headless --no-window、平移专项、control-fence、2112行pan-feel、真实diagnostic-output测试、20语i18n及格式检查。用户本轮已确认当前体验没问题。

本机构建注意：动态vswhere找原生ARM64 MSBuild；同次PowerShell规范Path/PATH并禁node reuse。本轮沙箱多节点启动异常，正常执行环境/m:1可用；曾遇LNK1000 IncrBuildImage，清Build/ARM64/Debug/Inkeys.ilk生成缓存后成功，不改项目/SDK/签名配置。完整构建至少5分钟预算；失败先看第一个有意义错误。

## 交接约束

本轮commit授权仅本次有效，后续任务不得据此继续自动commit。用户未授权push。不要运行finish-work/archive或自动提交日志；下一会话续接此任务，先更新下一阶段scope/design/implement再实施。当前阶段没有已知未解决的自动回归失败。
## 鼠标延迟修复续接（2026-11-08，优先于前述阶段计划）

用户先暂停屏幕适配，反馈Mouse拖动画布延迟、Touch正常；本轮已在CanvasPanMode纯Mouse位置发布处移除多余cursor control wake，保留状态边沿/clear/模式变化与非Pan重绘，并新增生产接线回归及render/Present诊断。实施与证据见mouse-latency-scope.md、mouse-latency-analysis.md、mouse-latency-validation.md及research/mouse-latency-*.md。

ARM64完整solution、完整Headless无窗口回归、control-fence、2112行pan-feel、diagnostic-output和平移专项全部通过。用户现明确确认鼠标正确并要求commit，但不结束任务。只提交本轮修复及相关记录；已有scale-stage规划与相关PRD/design/jsonl/task.json脏改动留存，不混入本次commit。任务继续in_progress，不archive/finish-work、不push；后续阶段等待用户继续指令，本次授权不延续。
