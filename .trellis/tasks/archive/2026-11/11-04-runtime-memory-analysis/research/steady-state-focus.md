# 1–6候选的常驻优先方案

用户最新目标：降低普通静止与用过功能返回桌面后的稳定内存，可自行回落的短时峰值次要。Laser接受选中初始化、离开销毁；本轮介绍方案，不新增产品实现。原三层画布、双device与后端分工继续保持。

## 1 保存快照：后置，先看任务完成后的持有

副本包括active/legacy、规范页和编码中间态；主要放大保存峰值。Desktop AutoSave.cpp:1186–1204已把终态大snapshot移到锁外局部释放；PresentationAutoSave.cpp:1753–1757已清save/load大payload。先确认队列/完成回执归零后是否仍保留大容器或容量；能自行回落则不优先做COW/公共数据表示重构。后续去重也应保持旧输入和编码等价。

## 2 Setting CPU恢复副本：明确的常驻候选

Setting.Base.cppm:24/165同时保留14槽解码CPU像素和纹理；204行恢复只重新上传CPU副本。方案为存稳定资源ID、目标尺寸、反预乘/遮罩转换配方，普通显示继续用现有纹理，设备恢复时临时解码/转换/上传，随后释放临时像素。HICON槽0单独处理。倍率1/2的CPU图片载荷约8.83/12.47MiB；字体/ImGui session可先保持热状态，避免扩大到整套设置首开重建。若再按页面/设置显隐创建销毁图片，需另明确首次打开体验。

## 3 Laser：选中准备，离开归还

Renderer.cpp:415–416无条件建laserCompositedColor/laserStrokeCoverage；ClearAllLaserCoverage只清色并释放scratch，主体仍在。核心收益为两幅全屏RGBA8，2880×1920示例42.19MiB；live/scratch部分已有按需机制，不能重复计算为新的启动节省。

在绘制线程消费工具切换时一次性准备完整候选资源组，成功后再启用Laser绘制；普通启动和普通笔帧允许主体不存在。离开时安全收尾活动接触/淡出/烘干及最后待提交帧，再解除SRV/RTV绑定并释放纹理、view与可选live/scratch状态。不可从UI回调直接释放，普通合成/resize/恢复也不能无条件把它们建回来。资源生命周期边界和视觉尾迹收尾须写在实施方案中，不擅自引入长期热缓存。

## 4 辅助ULW：依据真实输出状态

TransparentPresentation.cpp:1031无条件初始化selection读回目标，包含staging+DIB，示例42.19MiB。Host.cpp:2202与IdtState.cpp:615–644确认SelectionUlw参与电脑交互/选择模式；IdtState.cpp:1111按内容和auxiliaryFullFrameClean选择Primary/Presentation/Hidden，因此不能把普通静止视为不需要它。

方案为分离HWND与缓冲生命周期：目标确实需要辅助呈现时准备并完成完整帧；切到Primary/白板后等旧表面交接完回收；无内容时完成合法透明/隐藏提交并保持revision/clean证明后再回收。辅助窗仍承担可见旧墨迹时不能直接释放其仍需的工作数据；更大收益须研究GPU直接呈现或另一套可重建读回策略，不能跳过输入穿透/ready门。

## 5 Freeze、白板与隐藏UI：功能结束后回落

Freeze DIB有恢复提示及全屏暗幕消费者，不能直接当废弃资源。可先保留启动所需透明首帧提交，随后在不活跃状态释放像素，真正通知/冻结开始前在原owner线程准备，结束后提交透明/隐藏状态再归还。指向HDC的ULW结构须同步更新；白板占有共享Freeze HWND时不得额外提交透明帧清掉白板。长期方案再拆小提示与全屏遮罩。

Whiteboard.cpp:218在InactiveCommitted后只Idle，backgroundScene仍持有target；可在隐藏/退休确认且无pending submission时释放该client的全屏target，保留文档、主题、状态和共享WARP device，下次进入先完整重建再显露。Bar/分页则优先收缩关闭面板后的大backing，保留小型高频主栏，避免每帧或动画未结束时销毁重建。每幅全屏BGRA示例21.09MiB，白板属于使用后高水位。

## 6 GPU缓存：小规模少分配、空页能归还

Undo页128²×4×256=16MiB；composition页256²×6×64=24MiB。RemoveHotEntry/RemoveResident通常归还slice而不释放纹理页；Clear/slot切换已有整体Discard。启动未使用历史时不预分配整套预算，此项主要改善写过以后静止的持有。

先保持tile像素尺寸与历史能力，研究更小/渐进的每页slice数；例如Undo64slice为4MiB、composition16slice为6MiB，仅为设计例子。现有页结构已记录sliceCount且物理slot带page/slice，但仍须审查planner和重排全部引用。只缩页不缩总预算，达到预算的重度场景不会自动下降。

回收先限于完全空页，保留有用热条目；空页释放须清free-slot引用并保持稳定索引/代际，不动文档与合法Undo/Redo。不能仅clear对象而仍由context/view持有纹理，也不在每次Up后强制清光缓存。更复杂的闲置整理可把稀疏页集中后释放空页，但会增加映射切换/GPU复制复杂度，应另评估，正常绘制一旦恢复即避免同步整理。

## 观测与优先级

分别记录冷启动、进入功能、功能活跃、退出并完成清理后的稳定值；临时上涨不作为失败，但资源退出后长期不回落要定位应用持有、容器容量与驱动/堆缓存。建议先Laser，再Setting CPU副本和白板/Freeze的安全回收；辅助ULW按实际活跃状态定收益，GPU缓存针对用后高水位；保存峰值去重后置。所有载荷数字都不是已测Private Bytes节省量。

API参考：D3D11 PSSetShaderResources会持有传入view引用，资源销毁需核对绑定和所有权：[Microsoft](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-pssetshaderresources)。
