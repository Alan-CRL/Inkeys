# 双画布策略设计（已批准）

完整共享决策见父任务design.md“双画布场景策略”；证据见父任务research/canvas-scene-policy.md。旧bottom-edge-hit.md的可选A/B范围已被最新需求取代。

## 场景与状态
Desktop和PPT均为gapPx=1、NonRude=true；Whiteboard为gapPx=0、NonRude=false。策略不含taskbar edge/autoHide/exposure，因此Shell每次升降不会触发画布region或resize。Drawpad及DrawpadPresentation始终按同一个scene revision应用；selection保留固定WS_EX_TRANSPARENT，不创建第二Host或RTS。

窗口区域取完整window-local shape，减去目标屏幕[bottom-1,bottom)与窗口的交集；计算基于物理屏幕RECT到窗口外框局部坐标，适配负原点/移动。完整client/outer RECT保留，窗口实际可见/命中范围少1行。白板恢复原完整region；原有自定义region须按快照恢复，不能无条件假设不存在。

MarkFullscreenWindow(FALSE)仅撤销显式标记，Shell仍可自动检测；因此两窗显示前SetPropW(NonRudeHWND, TRUE)明确非全屏。白板移除本任务属性，仍由Freeze/root锚点调用既有MarkFullscreen(TRUE)，不为每个owned画布重复标记。当前该COM链为best effort，本次不重构它；新region/property步骤检查真实Win32结果。

## 最小接线
主要责任：Inkeys/Inkeys/Window/Window.cpp及必要的Window.cppm/Legacy边界；如现有请求不足才在IdtState.cpp/IdtMain.cpp改最小调用，不分散scene规则。Draw3模块仅作集成验证参考，region方案原则上不修改其渲染、RTS、持久化或资源resize路径。

1. CreateWindowFor建立两个角色HWND后、首次Show前落实当前scene策略，失败走既有创建清理，不提前发布ready。
2. ApplyWhiteboardWindowMode既有事务新增双窗region/属性/可见性快照；明确遍历两个画布，不能放入RequiresOverlayActivationStyleTransition分支（该分支漏过DrawpadPresentation）。
3. 必要的场景改变中暂时隐藏受影响窗口，设置region/属性，读回核对后按既有互斥surface恢复可见性，再提交mode/revision。Desktop↔PPT同策略不隐藏/重设。
4. 一窗失败则逆序恢复整对region/属性/可见性和既有style状态；不提前提交白板mode。回滚失败保留未收敛标识交现有循环重试，不新增通用事务框架。
5. 真实bounds变化重新计算region；重建按HWND generation重新应用；显隐核对已应用revision；销毁前移除本任务属性/资源。相同scene+bounds+generation无Win32重复调用。

SetWindowRgn成功时系统拥有HRGN；失败时本方释放。原region快照需复制或按纯几何重建，不能重复使用已移交HRGN。属性仅移除本任务拥有的值，RemoveProp返回NULL时结合GetProp核对，布尔属性值不CloseHandle。

## 验证重点
纯策略/区域几何、双方角色同代、单步Win32失败注入与逆序回滚、同值幂等、白板取消/退出重入、首次Show前顺序。对region策略检查client/Host尺寸和resizeCount不变，RTS及文档坐标不重标定；不以源码文本测试代替生产逻辑。
已有capture继续遵守输入owner，任务不取消笔迹；最底行保证针对新命中/可见覆盖。真实Shell是否按预期响应、DComp/ULW与旧系统兼容待授权GUI，不能从API成功推导为实测通过。

## 实施授权更新
2026-10-19：用户明确回复“批准，并开始实现”。以上规划门禁的批准条件已满足；按既有计划进入实施，GUI与commit/push仍未授权。
