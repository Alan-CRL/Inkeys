# 主栏任务栏适配设计（已批准）

共享完整方案及状态表见父任务design.md“主栏贴底策略”至“Bar底部1物理像素”；源码锚点见父任务research/dock-geometry.md、taskbar-observation.md和bottom-edge-hit.md。后者旧画布A/B不适用于本任务范围。

## 责任文件
Inkeys/Inkeys/Display/Display.cppm与Display.cpp：逐屏结构/采样/observer。
Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h、Bar.DisplayTransition.h、Bar.RenderLoop.cpp及必要Main/State模块：统一目标线、环境元组和Y过渡。
Bar.Interaction.cpp：输入环境交接/直移保护；Bar.Rendering.cpp或现有Presentation.Source/WindowGeometry helper：最终viewport/旧区清除。
Inkeys/Inkeys/UI/StartupPlacement.h及相关现有预览入口：CenteredBottomBar一致性。
具体只改实现必要文件，不改PPT业务动作、Draw3画布、设置/缩放配置，不重构无关模块。

## 观察器到呈现
新增edge/autoHide/Hidden-Raised-Unknown/稳定anchor，厚度独立。WinEvent只排队，owner合并16–33ms，必要缓存矩形200ms兜底，既有2秒结构复查；数值用于初始策略并按验证调整。逻辑状态变化才发新generation，Shell中间rect不直接成为目标。缓存身份换代、Unknown最近有效回退不跨屏/几何复用。

统一纯函数输出dock line及相关保护，渲染/输入tracker/blocked-release/成功呈现屏障/居中启动预览消费同一完整环境，不伪造workArea。真实5DIP和最终UIzoom职责分开；顶部/左右按追加要求与底部Hidden共用屏底5真实屏幕DIP目标。

同屏任务栏目标改变复用displayCenterY与现有0.4秒PPT动画语义，仅Y变化。同目标不重播，快速反向由当前已呈现Y重新出发；稳定SetDirect路径在过渡时让位，结束后恢复。输入owner拖动/Absorbing/恢复持有Y时，动画不得成为第二写者；PPT/白板切入取消桌面过渡，退出取最新桌面目标。

桌面底部autoHide保护最终呈现viewport的bottom exclusive<=monitor.bottom-1（含directTranslation）。源/尺寸改变触发既有完整ULW替换和旧区清除，稳定帧保留partial dirty。直移若会覆盖保护行则降为渲染提交。不按DPI放大1px、不靠HTTRANSPARENT宣称跨进程穿透。

## 失败与兼容
Shell不支持EX或WinEvent丢失走现有枚举/缓存与低频结构复查。未知信息不凭空假造本屏任务栏；UI缩放厚度回退不能当本屏存在证据。hook同线程卸载、timer对称清理、shutdown拒绝迟到发布。不读私有注册表，不修改任务栏，不新建常驻线程或每帧查询。

纯几何/状态机/通知合并/单写者与呈现裁剪测试覆盖生产逻辑。真实Shell窗口动画暴露方式及性能视觉效果需另经授权GUI验证，当前已获批实施，自动验证结果见父任务research/validation.md。

## 实施授权更新
2026-10-19：用户明确回复“批准，并开始实现”。以上规划门禁的批准条件已满足；按既有计划进入实施，GUI与commit/push仍未授权。
