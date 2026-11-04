# 固定手势呈现表面：方案评估

> 此文保留实施前的历史判断。固定手势表面的最终实现及本轮人工验证结果见 [实现与验收](fixed-gesture-surface-implementation-2026-10-26.md)。

## 评估结论

可行，值得作为下一阶段原型。它消除的是吸附模式变化时原生窗口的约80/40px阶跃和反向内容重基准，同时冻结source/size。即使尚未确认系统内部根因，这条机制性缓解路线仍有依据；不能提前承诺治愈闪烁。此次只评估，不实现原型、不新建任务、不提交。

上移对照已由用户确认同强跳变：真实HWND.bottom始终1600..1720，未接触1856任务栏或1920物理屏幕底边；相同50次大原点迁移仍在。排除“必须接触实际底边才闪”的必要前提，未排除软件内部虚拟layout裁剪，也未证明Windows/ULW具体缺陷。

## 对用户方案的准确解释

- HWND在拖动中跟随连续输入平移；固定的是宽高、source裁取、capacity/viewport和UI坐标系相对窗口的基准。
- 固定布局基准不等于固定形变中的每个像素。UI顶边仍保持已验证的设计：仅随有效输入移动；底边/居中远端在窗口内部独立变形以维持屏幕吸附目标。
- 本地指针偏移来自Down准备成功后的窗口与真实Down点，不重新引入已经回退的“固定归一化图像抓点+底端反推top”约束。
- 允许透明HWND越过屏幕；UI仍以真实工作区/任务栏线决定吸附和可见布局，不把屏幕尺寸扩大来假装有更多UI空间。

## 三阶段合同

### Down准备

从最后完整成功的形状和窗口记录P0、nativeOrigin0，预先计算本手势允许形状的完整union；一次确保backing容量，并确定固定viewport V0、capacityOrigin C0、size以及S0=V0.topLeft−C0。扩窗候选本身保持UI世界像素连续。准备完整成功后才允许直移，途中新输入沿现有barrier保留；失败重试不重复累计预留量。

包络包括水平40DIP/竖直20DIP捕获带、24DIP常规spring、Down成功形变及可能超过24DIP的合法seed、Floating恢复、允许的布局宽度范围、普通/居中两种提示最大文案及Back/描边/光晕/AA、尚在退场的可见菜单/属性。复用现有ResolveBarWindowSegmentRange和visual/capacity/indicator envelope；不按走过的历史坐标累计union。

### Held固定表面

在同一显示环境和已确定布局范围内：

```text
nativeOrigin(P) = nativeOrigin0 + (P − P0)
source = S0, size = size0, viewport = V0, capacityOrigin = C0
localPointer = P0 − nativeOrigin0
localBottomTarget = stableDockBottomWorld − nativeOrigin(P).y
localFarTarget = stableCenteredFarWorld − nativeOrigin(P).x
```

现有布局空间公式等价为layoutDip=(worldTarget−monitorOrigin−nativeTranslation)/zoom。弹簧保存相对稳定世界目标的误差，再转换到本帧local，不能继续让local baseBottom/baseFar上的spring直接归零。否则即使Logo正常，吸附底端和远端仍会漂移。

nativeTranslation起点应取Down成功值，再加相对输入差；不能用rawGrip−baseMain重新推初值，否则再次抓住未恢复形变时会跳。UI/指针在屏幕四边的钳制还要统一有效输入基准，避免raw pointer已跨屏而UI仍被留在旧monitor、使反向local位移超过预留量。

held不运行会改变source/size的普通viewport扩张/屏幕裁剪；屏幕布局范围与手势存储包络分开。当前layoutBounds同时参与两种用途，需要拆清语义，不修改真实屏幕/任务栏大小。仅在此状态按用户要求放开HWND底行/越屏限制，源bitmap包含检查始终保留。

source/size固定允许继续partial dirty；保留presentMappingTracker与真正的root/device失效保护，不通过删除保护或每次ForceFullDamage实现。窗口连续移动时，吸附端点的local目标也变化，必须触发所需UI更新，不能仍一律当纯位移而跳过渲染。

### Up/取消交接

从最后完整成功快照取得世界top/bottom/Logo抓手/远端，选择松手后的规范root，再反解初始形状。Floating复用现有Absorb；Docked/Centered分别回到任务栏线/居中远端。纵向可复用ResolveBarBottomDockShiftedShape，横向需要完整成功抓手和远端联合播种。

不再用centerTracker的raw elastic重复播种已经吸收进布局的位移。首张交接只播种不积分，失败从同一成功画面重试；完整成功后退出固定手势表面。收拢仍须包含正在恢复的形状，不能在Up直接裁掉其可见部分；恢复结束继续既有idle收缩。

## 修改范围与复用

预计必要产品文件：Bar.Interaction.cpp（连续native基准及有效输入）；Bar.RenderLoop.cpp（Down预留/held冻结/内部目标/Up交接）；Bar.BottomDock.h（世界误差和成功形状换算）；Bar.WindowGeometry.h（有限固定包络）。固定表面可由已有render owner私有状态持有，优先不新增Main跨线程原子字段或新线程。

复用现有serial/barrier、几何锁、成功快照、dirty tracker、capacity资源和失败恢复。不是两行rawT替换，也不重写整个渲染器。现有无窗口算法与真实RenderLoop测试扩展覆盖同一合同，不新建测试框架。

## 性能判断

- 路径累计长度不会扩大表面，使用绝对P−P0和一次求出的相对包络，不存每轮补偿历史。
- BGRA单份字节=4WH：当前约1300×406约2.01MiB；固定包络的实际尺寸需计算，不能拍脑袋放大成整屏（2880×1920约21.09MiB/份）。真实进程可能还有WARP/GDI/ULW副本。
- 现有backing通常已大于native窗口，应复用、Down不足时补一次。held应无资源重建/分配，source固定也减少映射整窗替换机会；但更大的native表面可能增加复制和合成成本，不能保证性能净提升。
- 度量Down准备延迟、held资源创建次数、full replacement比例、dirty面积、GetDC/ULW耗时及内存峰值，和现有同路线数据比较。

## 必须收口的边界

严格整个手势尺寸/source不变，与任意未知DPI/缩放/布局增大不能无条件同时保证。首个原型建议先验证当前单屏同DPI、固定桌面布局的原症状；这不是对多屏兼容完成的承诺。通用实现须确定外部变化延后到Up，或从成功形状结束/重新准备手势段，不能偷偷扩窗。

raw HWND跨显示器而UI仍被钳在原屏时，预留可能需要virtual desktop位移范围，不能只加40/24DIP。自动隐藏任务栏触发、透明区域命中、跨屏/DPI、布局变化、设备重建以及取消都需单独回归。Down准备和Up仍各有一次几何交接，需单独观察，防止把重复跳变缩成按下/松手各一次。

## 原型验收

1. 准备成功后的整个held段source/size/C0/V0保持常量，mode切换不产生额外native阶跃；重复V往返不增长。
2. native位移符合独立输入差；UI顶边保持原设计，吸附底边/居中远端世界目标正确；真实源像素、命中和光源同帧一致，无脏区外残影。
3. 覆盖准备失败、ULW/ReleaseDC/EndDraw失败、Up失败重试和立即再次Down；成功状态不提前推进、不累计补偿。
4. 当前完整Solution Debug|ARM64、适用既有headless与真实RenderLoop回归通过，再同一36秒V路线无录屏用户观察。日志证明合同成立且肉眼原症状消失，才可记修复；测试参数合法性不代替视觉结论。
5. 其后补四边、长距离、不同抓点、跨屏/DPI和自动隐藏任务栏；临时诊断不进入最终产品改动。

## 本轮状态

此文为评估与建议的实施边界，不是已实现/已验证的修复。已撤销无效整窗候选；上移fixture和本次临时trace将保存到TestResults后从产品源码撤出，保留所有运行证据。旧diagnostic EXE仅存私有TestResults目录。新方案没有运行构建或GUI测试，因为尚未实现。
