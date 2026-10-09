# 实施与验证步骤

## 1. 实施前检查

- 用户审查prd/design/本计划并确认进入实施后，再运行task.py start本子任务；父任务保持进行中。
- 派发trellis-implement，优先使用原生jsonl上下文注入，缺失时读取manifest引用文档后prd/design/implement。代理直接实现，不递归派发。
- 记录每个预计修改文件的原UTF-8/BOM/CRLF，与当前clean fluent基线对比；读取两份研究及旧helper原码。

## 2. 产品最小修改

1. 在中段Begin之后启用统一自动垂直滚动条，根据真实ScrollbarY增加4 DIP可用宽间距，全部根/父项/箭头/内联child命中同步；保留固定区/蓝条逻辑。
2. 将历史ScrollWhenDraggingOnVoid正文原样迁入Design内部实现，声明仅供设置内部与现有headless测试。
3. 按design在设置session本帧提交结束后部署一次唯一可滚宿主调用，覆盖正文、侧栏、浮层及Combo；保持旧调用(0,-MouseDelta.y)，恢复真实CurrentWindow/clip，避免重复父子滚动及已关闭Popup。
4. 添加简短中文注释解释额外gap不重复扣轨道、原helper出处和单宿主/上下文恢复边界；不顺手改vendor、输入消息、配置或其他渲染代码。

## 3. 必要的现有无窗口回归

- 扩展真实生产RenderNavigationPane harness，而非只改简化测试副本。覆盖无溢出/有溢出/展开折叠/窗口高度变化/紧凑展开覆盖/多倍率/宽度动画；观察实际ScrollbarY、轨道矩形、所有ItemBounds、glyph/focus ring及固定区位置。
- 实际点击/拖动thumb、轨道、间距时只滚动，不路由或业务动作；导航行/父项/箭头仍可正常点击，末项完整可达，蓝条同步。
- 旧helper原码逐行核对，测试真实输入序列：鼠标/触摸来源、空白与内容起拖、原ActiveId规则、上下/反向/边界/释放/离窗；检查单宿主、不双滚、popup关闭/输入屏障、原生产root flags以及context恢复。
- 保留全量现有DrawData/Debug/EDID/主题/汉堡/字体/动画回归；观察真实效果，不复制原算法或混色公式写测试替身。

## 4. 构建与最终检查

- 动态vswhere定位ARM64原生MSBuild，在同PowerShell调用Remove-Item Env:PATH并设置MSBUILDDISABLENODEREUSE=1，复用已有正常用户权限构建脚本策略。
- 完整InkeysRepo.sln Debug|ARM64，至少300秒timeout（采用1800秒deadline、/m:1 /p:CL_MPCount=1），结果与首个有意义错误分别记录。已知runner环境错误不通过代码/工程绕过。
- 完整InkeysHeadlessTests.exe --no-window，检查准确退出码/输出/stderr；国际化check、原编码/BOM/CRLF审计、git -c diff.ignoreSubmodules=all diff --check；不启动GUI。
- 产出research/implementation-validation.md，明确成功、失败修正和未验证的人工边界。随后派发trellis-check独立审查，在无源码变化时复用最终真实日志，不无意义重复构建。

## 5. 规范与任务收尾

- 主会话更新相关渲染/UI合同、旧helper复用/调用环境与回归要求，保持最小diff。
- 更新子任务、父任务和journal的实际验证结果；本轮新改动不自动commit/push，父任务保持in_progress。子任务不在人工验收前自动归档。

## 回退边界

只回退本子任务新增的中段条/宽度调整、旧helper部署和对应测试/文档差异；不触及此前已提交的Debug、版本色或汉堡反馈。出现环境问题保留源码与诊断证据，不升级依赖或更换构建系统。

## 后续执行（2026-10-09）

1. 已完成用户授权的上一轮 commit b8beea0b，继续沿用活动子任务。
2. trellis-research 只读核对主页遗漏、全设置实际竖直条宿主、过大间距和显隐动画部署依据，主会话将结果注入。
3. trellis-implement 负责必要设置内部源文件及现有真实 headless 回归；按最新授权优化拖动部署/算法，保持原编码/BOM/CRLF并添加简短中文边界注释。最小实现完整要求，不添加 vendor/message 适配。
4. 独立 trellis-check 核对生产 RenderHome 的真实输入、全部竖直条覆盖、实际图形与点击间距、显隐连续帧宽度/alpha、反向与动画开关，并修复必要问题。
5. 主会话动态定位原生 ARM64 MSBuild，同 PowerShell PATH workaround；完整 solution Debug|ARM64 与 --no-window 各1800秒deadline、准确退出码/输出；国际化、checkout编码/换行、tracked及新增文档 diff检查。
6. 更新相关规范与实施/审查结果，记录第一笔commit及新增验证，按用户授权再次commit；签名失败停止，不绕过，不push。父任务继续in_progress，不将自动回归表述为真实触屏/视觉验收。
